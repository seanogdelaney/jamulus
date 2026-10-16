// F05 source-extracted production OnTimer, ConfigurePolyInAudio, FreeChannel,
// GetGainsAndPannings, SetGain/SetPan, metering, decoder and mixer.
// The host mocks Qt dispatcher, timer, socket, recorder and network transport;
// it is not a running Qt application and does NOT qualify live clients.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <future>
#include <atomic>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "opus_custom.h"
#include "opus_defines.h"
#include "polyinsources.h"
#define SYSTEM_SAMPLE_RATE_HZ 48000
#define SYSTEM_FRAME_SIZE_SAMPLES 64
#define DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES 128
#define MAX_DELAY_PANNING_SAMPLES 64
#define _MINSHORT -32768
#define _MAXSHORT 32767
#define Q_UNUSED(x) ((void)(x));
#define emit
#define CT_OPUS 1
#define CT_OPUS64 2
#define CT_RAW 3
#define GS_BUFFER_OK 0
#define GS_BUFFER_UNDERRUN 1
#define GS_CHAN_NOW_DISCONNECTED 2
#define MAX_NUM_CHANNELS 8
#define INVALID_CHANNEL_ID -1
#define CHANNEL_LEVEL_UPDATE_INTERVAL 200
using EGetDataStat=int;
using EAudComprType=int;
struct QString : std::string {
    using std::string::string;
    QString()=default;
    QString(const std::string& s):std::string(s){}
    static QString fromStdString(const std::string& s){return QString(s);}
};
struct QMutexLocker { std::unique_lock<std::mutex> guard; explicit QMutexLocker(std::mutex* p):guard(*p){} void unlock(){guard.unlock();} };
std::ostream& qWarning(){return std::cerr;}
template<typename T> class CVector : public std::vector<T> {
public:
    using std::vector<T>::vector;
    void Init(int n) { this->assign(n,T{}); }
    void Reset(T x) { std::fill(this->begin(),this->end(),x); }
};
// STOCK_UTIL_EXTRACT
// STOCK_CONVBUF_EXTRACT
struct FakeSocket {};
struct CChannel {
    int id=0, num=1, compr=CT_OPUS64, bytes=128;
    bool connected=false, physical=false;
    CChannel* owner=nullptr;
    uint16_t source=0;
    QString name="";
    int address=0;
    int meterResetCount=0;
    std::mutex* serverMutex=nullptr;
    int mixLockObserved=-1;
    float fade=1.f;
    CVector<float> vecfGains=CVector<float>(8,1.f), vecfPannings=CVector<float>(8,.5f);
    std::mutex Mutex;
    struct Incoming { int stat; std::vector<uint8_t> bytes; };
    std::vector<Incoming> incoming;
    size_t inpos=0;
    std::vector<std::vector<uint8_t>> sent;
    int GetNumAudioChannels() const {return num;}
    int GetAudioCompressionType() const {return compr;}
    int GetCeltNumCodedBytes() const {return bytes;}
    float GetFadeInGain() const {return owner?owner->GetFadeInGain():fade;}
    bool IsConnected() const{return connected;}
    bool IsPolyInPhysical() const{return physical;}
    bool IsPolyInVirtual() const{return owner!=nullptr;}
    bool IsPolyInVirtualOf(const CChannel* p) const{return owner==p;}
    CChannel* PolyInPhysicalOwner() const{return owner;}
    int PolyInOwnerAddress() const{return owner?owner->address:address;}
    int GetAddress()const{return address;}
    QString GetName()const{return name;}
    bool EnablePolyInPhysical(uint64_t gen){if(!gen||!connected)return false; physical=true;return true;}
    void EnablePolyInVirtual(CChannel* p,uint16_t s,const QString& tag, EAudComprType codec,int ch,int coded,bool newIdentity){if(newIdentity)++meterResetCount;owner=p;source=s;name=tag;compr=codec;num=ch;bytes=coded;}
    void DisablePolyIn(){owner=nullptr;physical=false;}
    void BeginPolyInTick(){}
    void UpdateSocketBufferSize(){}
    double UpdateAndGetLevelForMeterdB(const CVector<int16_t>&,int,bool){return 10+id;}
    void MuteStateHasChanged(int,bool){}
    void SetGain(const int,float);
    void SetPan(const int,float);
    void GetGainsAndPannings(const CVector<int>&,const int,CVector<float>&,CVector<float>&);
    EGetDataStat GetData(CVector<uint8_t>& dst,int expected) {
        if(inpos==incoming.size())return GS_BUFFER_UNDERRUN;
        const auto& x=incoming.at(inpos++);
        if(x.stat==GS_CHAN_NOW_DISCONNECTED){connected=false;return x.stat;}
        if(x.stat==GS_BUFFER_OK){if((int)x.bytes.size()!=expected)throw std::runtime_error("incoming size for id="+std::to_string(id)+" expected="+std::to_string(expected)+" actual="+std::to_string(x.bytes.size()));
           std::copy(x.bytes.begin(),x.bytes.end(),dst.begin());}
        return x.stat;
    }
    void PrepAndSendPacket(FakeSocket*,const CVector<uint8_t>& bytes,int n){
        if(serverMutex){bool available=serverMutex->try_lock();mixLockObserved=available?0:1;if(available)serverMutex->unlock();}
        sent.emplace_back(bytes.begin(),bytes.begin()+n);
    }
};
struct FakeFuture {std::future<void> job; void wait(){job.get();}};
struct FakePool {template<typename F,typename...A>FakeFuture enqueue(F f,A... args){return {std::async(std::launch::async,f,args...)};}};
struct FakeController {bool recording=true; bool GetRecordingEnabled()const{return recording;}};
struct FakeProtocol {
    std::vector<std::vector<uint16_t>> levels;
    std::vector<int> levelTargets;
    void CreateCLChannelLevelListMes(int address,const CVector<uint16_t>& v,int count) {
        levelTargets.push_back(address);
        levels.emplace_back(v.begin(),v.begin()+count);
    }
};
struct CServer {
    using PolyInAudioSource = struct {uint16_t source;int channel;EAudComprType codec;int channels,encodedBytes;};
    bool bUseDoubleSystemFrameSize=false,bDelayPan=false,bHasPolyInAudio=false,bUseMultithreading=false;
    std::atomic<bool> bChannelIsNowDisconnected{false};
    int iServerFrameSizeSamples=64, iMaxNumChannels=8,iMaxNumThreads=2, iFrameCount=201;
    int iCurNumChannels=2;
    int vecChannelOrder[8]{0,4,1,2,3,5,6,7};
    CChannel vecChannels[8];
    FakeSocket Socket;
    OpusCustomMode* testMode128=nullptr;
    OpusCustomMode* testMode64=nullptr;
    std::mutex Mutex,MutexChanOrder;
    FakePool pool;FakePool* pThreadPool=&pool;
    std::vector<FakeFuture> Futures;
    FakeController JamController;
    FakeProtocol ConnLessProtocol;
    CVector<int> vecChanIDsCurConChan;
    CVector<float> vecfFadeInGains;
    CVector<int> vecNumAudioChannels, vecAudioComprType,vecNumFrameSizeConvBlocks;
    CVector<bool> vecUseDoubleSysFraSizeConvBuf;
    CVector<CVector<int16_t>> vecvecsData,vecvecsData2,vecvecsSendData;
    CVector<int> vecPrevDelayPanIDs;
    CVector<CVector<int16_t>> vecDelayPanRemapScratch;
    int iPrevDelayPanSources=0;
    bool bPrevDelayPanHadVirtual=false;
    struct PolyInHistoryIdentity { int owner=-1; uint64_t generation=0; uint16_t source=0; };
    std::array<PolyInHistoryIdentity,MAX_NUM_CHANNELS> polyInHistoryIdentity{};
    // Stage 5 adds real-ID admission to physical FreeChannel and OnTimer.
    // This Stage 4 audio fixture substitutes a distinct F03 owner; those
    // methods must still compile against the real production source text.
    PolyIn::SourceSlots polyInSlots{MAX_NUM_CHANNELS};
    std::array<uint64_t,MAX_NUM_CHANNELS> polyInEpoch{};
    CVector<CVector<uint8_t>> vecvecbyCodedData;
    CVector<CVector<float>> vecvecfGains,vecvecfPannings,vecvecfIntermediateProcBuf;
    CVector<uint16_t> vecChannelLevels,vecSortedPolyInLevels;
    CConvBuf<int16_t> DoubleFrameSizeConvBufIn[8],DoubleFrameSizeConvBufOut[8];
    OpusCustomDecoder* OpusDecoderMono[8]{},*OpusDecoderStereo[8]{},*Opus64DecoderMono[8]{},*Opus64DecoderStereo[8]{};
    OpusCustomEncoder* OpusEncoderMono[8]{},*OpusEncoderStereo[8]{},*Opus64EncoderMono[8]{},*Opus64EncoderStereo[8]{};
    std::vector<int> recordedIDs;
    std::vector<int> recordedAddress;
    std::vector<std::string> recordedName;
    int rosterBroadcasts=0, stopped=0, disconnected=0;
    CServer() {
        vecfFadeInGains.Init(8);vecChanIDsCurConChan.Init(8);vecPrevDelayPanIDs.Init(8);vecDelayPanRemapScratch.resize(8);
        vecChannelLevels.Init(8);vecSortedPolyInLevels.Init(8);
        vecNumAudioChannels.Init(8); vecAudioComprType.Init(8);vecNumFrameSizeConvBlocks.Init(8);
        vecUseDoubleSysFraSizeConvBuf.Init(8);
        vecvecsData.resize(8);vecvecsData2.resize(8);vecvecsSendData.resize(8);
        vecvecbyCodedData.resize(8);vecvecfGains.resize(8);vecvecfPannings.resize(8);vecvecfIntermediateProcBuf.resize(8);
        for(int i=0;i<8;i++) {
            vecChannels[i].id=i;vecChannels[i].address=i;vecChannels[i].name=QString("src")+std::to_string(i);
            vecChannels[i].serverMutex=&Mutex;
            vecvecsData[i].Init(256);vecvecsData2[i].Init(256);vecvecsSendData[i].Init(256);
            vecDelayPanRemapScratch[i].Init(256);
            vecvecbyCodedData[i].Init(512);vecvecfGains[i].Init(8);vecvecfPannings[i].Init(8);vecvecfIntermediateProcBuf[i].Init(256);
            DoubleFrameSizeConvBufIn[i].Init(256);DoubleFrameSizeConvBufOut[i].Init(256);
        }
        int err=0;
        testMode128=opus_custom_mode_create(48000,128,&err);
        testMode64=opus_custom_mode_create(48000,64,&err);
        if(!testMode128 || !testMode64)throw std::runtime_error("opus modes");
        for(int i=0;i<8;++i){
            OpusDecoderMono[i]=opus_custom_decoder_create(testMode128,1,&err);
            OpusDecoderStereo[i]=opus_custom_decoder_create(testMode128,2,&err);
            Opus64DecoderMono[i]=opus_custom_decoder_create(testMode64,1,&err);
            Opus64DecoderStereo[i]=opus_custom_decoder_create(testMode64,2,&err);
            OpusEncoderMono[i]=opus_custom_encoder_create(testMode128,1,&err);
            OpusEncoderStereo[i]=opus_custom_encoder_create(testMode128,2,&err);
            Opus64EncoderMono[i]=opus_custom_encoder_create(testMode64,1,&err);
            Opus64EncoderStereo[i]=opus_custom_encoder_create(testMode64,2,&err);
        }
        vecChannels[0].connected=true;vecChannels[4].connected=true;
    }
    ~CServer(){for(int i=0;i<8;++i){
        opus_custom_decoder_destroy(OpusDecoderMono[i]);opus_custom_decoder_destroy(OpusDecoderStereo[i]);
        opus_custom_decoder_destroy(Opus64DecoderMono[i]);opus_custom_decoder_destroy(Opus64DecoderStereo[i]);
        opus_custom_encoder_destroy(OpusEncoderMono[i]);opus_custom_encoder_destroy(OpusEncoderStereo[i]);
        opus_custom_encoder_destroy(Opus64EncoderMono[i]);opus_custom_encoder_destroy(Opus64EncoderStereo[i]);
    }opus_custom_mode_destroy(testMode128);opus_custom_mode_destroy(testMode64);}
    void ClientDisconnected(int){++disconnected;}
    void Stop(){++stopped;}
    void CreateAndSendChanListForAllConChannels(){++rosterBroadcasts;}
    void AudioFrame(int id,const QString& name,int address,int,const CVector<int16_t>&){
        recordedIDs.push_back(id);recordedName.push_back(name);recordedAddress.push_back(address);
    }
    void DecodeReceiveData(int,int);
    void MixEncodeTransmitData(int,int);
    static void DecodeReceiveDataBlocks(CServer*,int,int,int);
    static void MixEncodeTransmitDataBlocks(CServer*,int,int,int);
    void OnTimer();
    bool CreateLevelsForAllConChannels(const int);
    void FreeChannel(const int);
    void DisablePolyInAudio(int);
    bool ConfigurePolyInAudio(const PolyIn::SourceSlots&,int,uint64_t,const std::vector<PolyInAudioSource>&);
};
// STOCK_METHODS_EXTRACT
static void must(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static std::vector<uint8_t> pcm(int frames,int v){std::vector<uint8_t> b(frames*2);for(int i=0;i<frames;++i){int16_t x=v;std::memcpy(b.data()+i*2,&x,2);}return b;}
static void sendFrame(CServer& s,bool disconnectOwner=false){
    s.iFrameCount=201;
    s.vecChannels[0].incoming.push_back({disconnectOwner?GS_CHAN_NOW_DISCONNECTED:GS_BUFFER_OK,pcm(64,0)});
    s.vecChannels[4].incoming.push_back({GS_BUFFER_OK,pcm(s.vecChannels[4].num*64,0)});
    if(!disconnectOwner){
        s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(64,3000)});
        s.vecChannels[2].incoming.push_back({GS_BUFFER_OK,pcm(64,-2000)});
    }
    s.OnTimer();
}
static void test(bool delay){
    CServer s;s.bDelayPan=delay;
    PolyIn::SourceSlots identities(8);
    must(identities.AdmitPhysical(0,55,"owner"),"admit");
    auto ticket=identities.Reserve(0,55,55,{{0,"p"},{1,"mic"},{2,"guitar"}});
    must(ticket!=0 && identities.Promote(ticket,0,55),"promote");
    PolyIn::SourceSlots::Identity a,b;
    must(identities.Resolve(0,55,1,a)&&identities.Resolve(0,55,2,b),"resolve");
    must(a.channel==1&&b.channel==2,"must have distinct stock virtual IDs");
    must(s.ConfigurePolyInAudio(identities,0,55,{{0,0,CT_OPUS64,1,128},{1,1,CT_OPUS64,1,128},{2,2,CT_OPUS64,1,128}}),"real ConfigurePolyInAudio failed");
    must(s.vecChannels[1].IsPolyInVirtualOf(&s.vecChannels[0]),"configure not hooked");
    // Exercise actual stock channel fader functions, not FakeChannel gains arrays.
    s.vecChannels[4].SetGain(1,0.5f);s.vecChannels[4].SetGain(2,0.25f);
    s.vecChannels[4].SetPan(1,0.f);s.vecChannels[4].SetPan(2,1.f);
    s.vecChannels[4].num=2;s.vecChannels[4].bytes=256;
    sendFrame(s);
    must(s.vecChannels[0].sent.size()==1 && s.vecChannels[4].sent.size()==1,"one physical return each");
    must(s.vecChannels[4].mixLockObserved==1,"poly-in mixer not protected from mid-frame source reconfigure");
    must(s.vecChannels[1].sent.empty()&&s.vecChannels[2].sent.empty(),"virtual must not send");
    must(s.ConnLessProtocol.levels.size()==2,"meters sent once to each physical");
    for(auto& levels:s.ConnLessProtocol.levels) must(levels==std::vector<uint16_t>({10,11,12,14}),"levels must be stock ID ascending");
    must(s.recordedIDs==std::vector<int>({0,4,1,2}),"recorder physical-first plus virtual");
    must(s.recordedAddress==std::vector<int>({0,4,0,0}),"virtual recorder must use owner physical address");
    must(s.recordedName[2].find("[2]")!=std::string::npos &&
         s.recordedName[3].find("[3]")!=std::string::npos,"ordinary virtual tags must reach recorder");
    auto first=s.vecChannels[4].sent.at(0);
    s.vecChannels[4].SetGain(1,0.1f);s.vecChannels[4].SetGain(2,1.f);
    s.vecChannels[4].SetPan(1,1.f);s.vecChannels[4].SetPan(2,0.f);
    sendFrame(s);
    must(s.vecChannels[4].sent.size()==2,"one return after fader change");
    must(first!=s.vecChannels[4].sent.at(1),"actual faders failed to alter stock mixer output");
    // Exact production OnTimer and FreeChannel inside original physical decode.
    // Distinctive virtual nonzero PCM existed on previous tick. It must NOT be
    // mixed, recorded or listed after owner times out within the next tick.
    s.ConnLessProtocol.levels.clear();s.ConnLessProtocol.levelTargets.clear();
    s.recordedIDs.clear();s.recordedAddress.clear();s.recordedName.clear();
    sendFrame(s,true);
    must(s.disconnected==1&&s.rosterBroadcasts==1,"stock disconnect and roster broadcast");
    must(!s.vecChannels[1].IsPolyInVirtual()&&!s.vecChannels[2].IsPolyInVirtual(),"owner retire must clear virtuals");
    must(s.vecChannels[4].sent.size()==3,"recipient must get one normal packet post-timeout");
    must(s.ConnLessProtocol.levels.size()==2,"meter notifications for exactly 2 physical recipients");
    for(auto& levels:s.ConnLessProtocol.levels)must(levels==std::vector<uint16_t>({10,14}),"stale virtual in meters");
    must(s.recordedIDs==std::vector<int>({0,4}),"stale virtual in recorder");
    const auto& packet=s.vecChannels[4].sent.at(2);
    // No source 1 or 2; physical inputs are silence. Non-zero indicates stale PCM.
    for(uint8_t c:packet)must(c==0,"stale virtual PCM entered recipient after owner timeout");
    std::cout<<"PASS real ConfigurePolyInAudio/OnTimer/FreeChannel/SetGain/SetPan/GetGainsAndPannings/levels/recorder+stock DSP delaypan="<<delay
             <<" recipient_packets="<<s.vecChannels[4].sent.size()<<" virtual_ids="<<a.channel<<","<<b.channel<<"\n";
}
static void testSurvivingOwner(bool delay,bool useMT=false){
    CServer s; s.bDelayPan=delay; s.bUseMultithreading=useMT;
    s.vecChannels[6].connected=true;
    s.iCurNumChannels=3;
    s.vecChannelOrder[0]=0;s.vecChannelOrder[1]=4;s.vecChannelOrder[2]=6;
    s.vecChannels[4].num=1;s.vecChannels[4].bytes=128;
    s.vecChannels[6].num=2;s.vecChannels[6].bytes=256;
    PolyIn::SourceSlots identity(8);
    must(identity.AdmitPhysical(0,55,"one") && identity.AdmitPhysical(4,66,"two"),"admit both");
    auto t0=identity.Reserve(0,55,55,{{0,"one"},{1,"one-mic"},{2,"one-gtr"}});
    must(t0 && identity.Promote(t0,0,55),"promote one");
    auto t4=identity.Reserve(4,66,66,{{0,"two"},{1,"two-mic"}});
    must(t4 && identity.Promote(t4,4,66),"promote two");
    PolyIn::SourceSlots::Identity survivor;
    must(identity.Resolve(4,66,1,survivor)&&survivor.channel==3,"survivor ID3");
    must(s.ConfigurePolyInAudio(identity,0,55,{{0,0,CT_OPUS64,1,128},{1,1,CT_OPUS64,1,128},{2,2,CT_OPUS64,1,128}}),"configure first owner");
    must(s.ConfigurePolyInAudio(identity,4,66,{{0,4,CT_OPUS64,1,128},{1,3,CT_OPUS64,1,128}}),"configure second owner");
    s.vecChannels[6].SetGain(3,0.5f);s.vecChannels[6].SetPan(3,0.0f);
    auto tick=[&](bool lose0){
        s.iFrameCount=201;
        s.vecChannels[0].incoming.push_back({lose0?GS_CHAN_NOW_DISCONNECTED:GS_BUFFER_OK,pcm(64,0)});
        s.vecChannels[4].incoming.push_back({GS_BUFFER_OK,pcm(64,0)});
        s.vecChannels[6].incoming.push_back({GS_BUFFER_OK,pcm(128,0)});
        if(!lose0){
            s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(64,7000)});
            s.vecChannels[2].incoming.push_back({GS_BUFFER_OK,pcm(64,-2000)});
        }
        s.vecChannels[3].incoming.push_back({GS_BUFFER_OK,pcm(64,1500)});
        s.OnTimer();
    };
    tick(false);tick(false);
    auto packetBefore=s.vecChannels[6].sent.back();
    must(s.vecChannels[3].sent.empty(),"surviving virtual cannot send");
    s.ConnLessProtocol.levels.clear();s.recordedIDs.clear();
    tick(true);
    must(!s.vecChannels[1].IsPolyInVirtual()&&!s.vecChannels[2].IsPolyInVirtual(),"retire first owner");
    must(s.vecChannels[3].IsPolyInVirtualOf(&s.vecChannels[4]),"second owner must survive");
    must(s.vecChannels[6].sent.size()==3&&s.vecChannels[4].sent.size()==3,"one return per physical tick");
    must(s.vecChannels[3].sent.empty(),"virtual cannot return");
    for(auto& v:s.ConnLessProtocol.levels)must(v==std::vector<uint16_t>({10,13,14,16}),"survivor meter ordering");
    must(s.recordedIDs==std::vector<int>({0,4,6,3}),"survivor recorder ordering");
    must(s.vecChannels[6].sent.back()!=std::vector<uint8_t>(256,0),"survivor disappeared from recipient mix");
    // Both surviving owner and recipient source0 were silence; new output must
    // derive solely from the remaining physical owner's virtual source.
    must(s.vecChannels[6].sent.back()!=packetBefore,"retired sources still in output");
    std::cout<<"PASS surviving second owner + compacted fader/history/meter/record delaypan="<<delay<<"\n";
}
// New source-history regression executes the real stock decoder/mixer and
// production OnTimer, with the physical-transport boundary stubbed. A silent
// client added or removed must not change another source's --delaypan history.
static std::vector<std::vector<uint8_t>> historySequence(bool stable,bool add,
                                                            bool delay,int frame,bool lower=false) {
    CServer s;
    s.bDelayPan=delay;
    s.bUseDoubleSystemFrameSize=(frame==128);
    s.iServerFrameSizeSamples=frame;
    s.vecChannels[4].num=2;
    s.vecChannels[4].compr=(frame==128?CT_OPUS:CT_OPUS64);
    s.vecChannels[4].bytes=frame*4;
    s.vecChannels[0].compr=s.vecChannels[1].compr=(frame==128?CT_OPUS:CT_OPUS64);
    s.vecChannels[0].bytes=s.vecChannels[1].bytes=frame*2;
    s.vecChannels[6].bytes=frame*2;s.vecChannels[6].compr=(frame==128?CT_OPUS:CT_OPUS64);
    PolyIn::SourceSlots ids(8);
    must(ids.AdmitPhysical(0,55,"owner"),"history admit");
    if(lower) must(ids.AdmitPhysical(1,91,"prior physical"),"history occupied lower ID");
    auto t=ids.Reserve(0,55,55,{{0,"p"},{1,"mic"}});
    must(t && ids.Promote(t,0,55),"history promote");
    const int virtualID=lower?2:1;
    if(lower) s.vecChannels[virtualID].bytes=frame*2;
    if(lower) s.vecChannels[virtualID].compr=(frame==128?CT_OPUS:CT_OPUS64);
    if(lower) s.vecChannels[1].bytes=frame*2;
    if(lower) s.vecChannels[1].compr=(frame==128?CT_OPUS:CT_OPUS64);
    must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,s.vecChannels[0].compr,1,frame*2},
                                          {1,virtualID,s.vecChannels[virtualID].compr,1,frame*2}}),"history configure");
    s.vecChannels[4].SetPan(virtualID,1.f);
    s.vecChannels[4].SetGain(virtualID,1.f);
    // Add silent client 6 only to the stable scenario on tick0, and to the
    // change scenario on tick1. Leave tests reverse this sequence.
    auto tick=[&](int time){
        bool present=stable ? (add || time<1) : (add ? time>=1 : time<1);
        const int visitor=lower?1:6;
        s.vecChannels[visitor].connected=present;
        s.iCurNumChannels=present ? 3 : 2;
        s.vecChannelOrder[0]=0;s.vecChannelOrder[1]=4;
        if(present) s.vecChannelOrder[2]=visitor;
        s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(frame,0)});
        s.vecChannels[4].incoming.push_back({GS_BUFFER_OK,pcm(frame*2,0)});
        s.vecChannels[virtualID].incoming.push_back({GS_BUFFER_OK,pcm(frame,time==0 ? 10000 : 0)});
        if(present) s.vecChannels[visitor].incoming.push_back({GS_BUFFER_OK,pcm(frame,0)});
        s.OnTimer();
    };
    for(int n=0;n<3;++n)tick(n);
    return s.vecChannels[4].sent;
}
static void testHistoryJoinLeave(bool delay,int frame){
    for(bool lower:{false,true}) for(bool join:{true,false}){
        const auto stable=historySequence(true,join,delay,frame,lower);
        const auto changing=historySequence(false,join,delay,frame,lower);
        must(stable.size()==3 && changing.size()==3,"history three ticks");
        for(int tick=0;tick<3;++tick)
            if(stable[tick]!=changing[tick]) {
                int differences=0;
                for(size_t byte=0;byte<stable[tick].size();++byte)
                    differences+=(stable[tick][byte]!=changing[tick][byte]);
                std::cerr<<"HISTORY_"<<(join?"JOIN":"LEAVE")<<" delay="<<delay
                         <<" frame="<<frame<<" tick="<<tick<<" different="<<differences<<"\n";
                throw std::runtime_error("virtual delaypan history is positional, not identity-keyed");
            }
        std::cout<<"PASS history "<<(join?"JOIN":"LEAVE")<<" visitor="<<(lower?"BELOW":"ABOVE")<<" delaypan="<<delay
                 <<" frame="<<frame<<" stock decoder/mixer packets identical\n";
    }
}

static std::vector<std::vector<uint8_t>> stockHistorySequence(bool stable,bool delay,int frame){
    CServer s;s.bDelayPan=delay;
    s.iServerFrameSizeSamples=frame;s.bUseDoubleSystemFrameSize=(frame==128);
    s.vecChannels[1].connected=true;
    s.vecChannels[0].num=1;s.vecChannels[0].bytes=frame*2;
    s.vecChannels[1].num=1;s.vecChannels[1].bytes=frame*2;
    s.vecChannels[4].num=2;s.vecChannels[4].bytes=frame*4;
    s.vecChannels[6].num=1;s.vecChannels[6].bytes=frame*2;
    for(int id:{0,1,4,6})s.vecChannels[id].compr=(frame==128?CT_OPUS:CT_OPUS64);
    s.vecChannels[4].SetPan(1,1.f);
    for(int tick=0;tick<3;++tick){
        const bool present=stable || tick>=1;
        s.vecChannels[6].connected=present;
        s.iCurNumChannels=present?4:3;
        s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(frame,0)});
        s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(frame,tick==0?10000:0)});
        s.vecChannels[4].incoming.push_back({GS_BUFFER_OK,pcm(frame*2,0)});
        if(present)s.vecChannels[6].incoming.push_back({GS_BUFFER_OK,pcm(frame,0)});
        s.OnTimer();
    }
    return s.vecChannels[4].sent;
}
static void testStockHistoryJoin(bool delay,int frame){
    const auto stable=stockHistorySequence(true,delay,frame);
    const auto joining=stockHistorySequence(false,delay,frame);
    must(stable.size()==3 && joining==stable,"stock physical history join changed output");
    std::cout<<"PASS unchanged stock physical-source delaypan="<<delay<<" frame="<<frame<<" three packets identical\n";
}
static void testVirtualSourceChurn(){
    CServer s;s.bDelayPan=true;s.vecChannels[4].num=2;s.vecChannels[4].bytes=256;
    PolyIn::SourceSlots ids(8);
    must(ids.AdmitPhysical(0,55,"owner"),"churn admit");
    auto t=ids.Reserve(0,55,55,{{0,"p"},{1,"mic"}});
    must(t && ids.Promote(t,0,55),"churn promote1");
    must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,CT_OPUS64,1,128},{1,1,CT_OPUS64,1,128}}),"churn cfg1");
    s.vecChannels[4].SetPan(1,1.f);
    auto tick=[&](int sample){
        s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(64,0)});
        s.vecChannels[4].incoming.push_back({GS_BUFFER_OK,pcm(128,0)});
        s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(64,sample)});
        s.OnTimer();
    };
    tick(10000);
    must(s.iPrevDelayPanSources==3&&s.vecPrevDelayPanIDs[2]==1,"churn snapshot before new generation");
    // An idempotent reconfiguration of the same identity keeps its history.
    must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,CT_OPUS64,1,128},{1,1,CT_OPUS64,1,128}}),"churn same cfg");
    must(s.vecPrevDelayPanIDs[2]==1,"idempotent config invalidated history");
    // A new generation reuses the ordinary ID for the continuing source.
    auto t2=ids.Reserve(0,55,66,{{0,"p"},{1,"new microphone"}});
    must(t2 && ids.Promote(t2,0,55),"churn promote2");
    PolyIn::SourceSlots::Identity x;
    must(ids.Resolve(0,66,1,x)&&x.channel==1,"churn same ID reassigned");
    must(s.ConfigurePolyInAudio(ids,0,66,{{0,0,CT_OPUS64,1,128},{1,1,CT_OPUS64,1,128}}),"churn cfg2");
    must(s.vecPrevDelayPanIDs[2]==INVALID_CHANNEL_ID,"new virtual inherited retired ID history");
    tick(0);
    must(s.vecChannels[4].sent.size()==2,"churn recipient return count");
    for(uint8_t v:s.vecChannels[4].sent[1]) must(v==0,"new virtual source inherited old delayed PCM");
    std::cout<<"PASS virtual source generation/index churn does not inherit prior delay history\n";
}


// Top-down derived regression: ordinary ID reuse is a new logical ownership
// event, unlike an idempotent config. Exercises real production Configure,
// SetGain/Pan, DecodeReceiveData, mixer, and their per-slot Opus decoders.
static void testIdentityRebinding(bool delay,int frame){
 CServer s;
 s.bDelayPan=delay;
 s.bUseDoubleSystemFrameSize=(frame==128);s.iServerFrameSizeSamples=frame;
 const int codec=frame==128?CT_OPUS:CT_OPUS64;
 s.vecChannels[0].compr=codec;s.vecChannels[0].bytes=frame*2;
 s.vecChannels[4].compr=codec;s.vecChannels[4].num=2;s.vecChannels[4].bytes=frame*4;
 s.vecChannels[6].connected=true;s.vecChannels[6].compr=codec;s.vecChannels[6].num=2;s.vecChannels[6].bytes=frame*4;
 s.iCurNumChannels=3;s.vecChannelOrder[0]=0;s.vecChannelOrder[1]=4;s.vecChannelOrder[2]=6;
 PolyIn::SourceSlots ids(3);
 must(ids.AdmitPhysical(0,55,"owner")&&ids.AdmitPhysical(2,99,"occupied ID"),"admit");
 auto ticket=ids.Reserve(0,55,55,{{0,"base"},{1,"microphone"}});
 must(ticket&&ids.Promote(ticket,0,55),"initial promote");
 must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,codec,1,frame*2},{1,1,codec,1,frame*2}}),"configure mic");
 must(s.vecChannels[1].meterResetCount==1,"first virtual meter not reset");
 for(int recipient:{4,6}){
   s.vecChannels[recipient].SetGain(1,0.25f);
   s.vecChannels[recipient].SetPan(1,0.95f);
 }
 s.vecChannels[1].SetGain(4,0.45f);s.vecChannels[1].SetPan(4,0.1f);
 must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,codec,1,frame*2},{1,1,codec,1,frame*2}}),"idempotent");
 must(s.vecChannels[1].meterResetCount==1,"idempotent configuration reset meter");
 for(int recipient:{4,6}){
   must(s.vecChannels[recipient].vecfGains[1]==0.25f,"idempotent lost fader gain");
   must(s.vecChannels[recipient].vecfPannings[1]==0.95f,"idempotent lost fader pan");
 }
 auto tick=[&](int sample){
   s.iFrameCount=201;
   s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(frame,0)});
   s.vecChannels[4].incoming.push_back({GS_BUFFER_OK,pcm(frame*2,0)});
   s.vecChannels[6].incoming.push_back({GS_BUFFER_OK,pcm(frame*2,0)});
   s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(frame,sample)});
   s.OnTimer();
 };
 tick(11000);
 must(s.vecChannels[4].sent.size()==1,"source mix not sent");
 const auto previous=s.vecChannels[4].sent.back();
 ticket=ids.Reserve(0,55,55,{{0,"base"},{2,"guitar"}});
 must(ticket&&ids.Promote(ticket,0,55),"replace source under same generation");
 PolyIn::SourceSlots::Identity resolved;
 must(ids.Resolve(0,55,2,resolved)&&resolved.channel==1,"must reuse same ordinary ID");
 // Prime the previous identity's conversion state to detect cross-source PCM.
 CVector<int16_t> stale(256);stale.Reset(9000);
 s.DoubleFrameSizeConvBufIn[1].PutAll(stale);
 must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,codec,1,frame*2},{2,1,codec,1,frame*2}}),"configure replacement");
 CVector<int16_t> afterRebind(64);
 must(s.DoubleFrameSizeConvBufIn[1].Get(afterRebind,64),"fresh conversion buffer Get");
 for(auto x:afterRebind)must(x==0,"new source inherited old conversion-buffer PCM");
 for(int recipient:{4,6}){
   must(s.vecChannels[recipient].vecfGains[1]==1.f,"different source retained old gain");
   must(s.vecChannels[recipient].vecfPannings[1]==0.5f,"different source retained old pan");
 }
 must(s.vecChannels[1].vecfGains[4]==1.f&&s.vecChannels[1].vecfPannings[4]==0.5f,"stock InitChannel two-sided reset");
 must(s.vecChannels[1].meterResetCount==2,"new logical ID failed meter reset");
 if(delay)must(s.vecPrevDelayPanIDs[3]==INVALID_CHANNEL_ID,"stale source identity in delay history");
 tick(4000);
 must(s.vecChannels[4].sent.size()==2 && s.vecChannels[6].sent.size()==2,"one return per physical after reuse");
 must(s.vecChannels[4].sent.back()!=previous,"reset mix failed to use new identity and gain");
 // A continuing source must keep faders, even if source format changes.
 s.vecChannels[4].SetGain(1,0.4f);s.vecChannels[4].SetPan(1,0.9f);
 must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,codec,1,frame*2},{2,1,codec,2,frame*4}}),"format change");
 must(s.vecChannels[4].vecfGains[1]==0.4f && s.vecChannels[4].vecfPannings[1]==0.9f,"format update lost source identity faders");
 must(s.vecChannels[1].meterResetCount==2,"format-only update reset identity meter");
 if(delay)must(s.vecPrevDelayPanIDs[3]==INVALID_CHANNEL_ID,"format update kept incompatible delay history");
 // A genuine generation change is another fresh identity even for source 2.
 ticket=ids.Reserve(0,55,66,{{0,"base"},{2,"new guitar"}});
 must(ticket&&ids.Promote(ticket,0,55),"generation promote");
 must(s.ConfigurePolyInAudio(ids,0,66,{{0,0,codec,1,frame*2},{2,1,codec,2,frame*4}}),"new generation");
 must(s.vecChannels[4].vecfGains[1]==1.f && s.vecChannels[4].vecfPannings[1]==0.5f,"generation reused fader");
 must(s.vecChannels[1].meterResetCount==3,"new generation meter leak");
 std::cout<<"PASS SOURCE_IDENTITY_REBIND frame="<<frame<<" delaypan="<<delay
          <<" same-epoch ID reuse gain+pan both-way; idempotent; generation; format; actual mixed PCM\n";
}
static void testOpusResetOnReuse(){
 CServer s;
 PolyIn::SourceSlots ids(3);
 must(ids.AdmitPhysical(0,55,"owner")&&ids.AdmitPhysical(2,99,"occupied"),"opus admit");
 auto ticket=ids.Reserve(0,55,55,{{0,"base"},{1,"mic"}});
 must(ticket&&ids.Promote(ticket,0,55),"opus first promote");
 must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,CT_OPUS64,1,128},{1,1,CT_OPUS64,1,42}}),"opus configure");
 // Drive actual production Opus decoder, then observe PLC from its old state.
 CVector<int16_t> signal(64);for(int i=0;i<64;++i)signal[i]=int16_t((i%13-6)*2800);
 std::array<uint8_t,42> coded{};
 opus_custom_encoder_ctl(s.Opus64EncoderMono[1],OPUS_SET_BITRATE(60000));
 must(opus_custom_encode(s.Opus64EncoderMono[1],signal.data(),64,coded.data(),42)>=0,"opus encode");
 std::array<int16_t,64> decoded{};
 must(opus_custom_decode(s.Opus64DecoderMono[1],coded.data(),42,decoded.data(),64)>=0,"opus decode");
 must(opus_custom_decode(s.Opus64DecoderMono[1],nullptr,42,decoded.data(),64)>=0,"old opus PLC");
 bool active=false;for(auto v:decoded)active|=v!=0;
 must(active,"fixture failed to prime actual Opus PLC state");
 ticket=ids.Reserve(0,55,55,{{0,"base"},{2,"new guitar"}});
 must(ticket&&ids.Promote(ticket,0,55),"opus replacement");
 must(s.ConfigurePolyInAudio(ids,0,55,{{0,0,CT_OPUS64,1,128},{2,1,CT_OPUS64,1,42}}),"opus new identity");
 decoded.fill(1234);
 must(opus_custom_decode(s.Opus64DecoderMono[1],nullptr,42,decoded.data(),64)>=0,"new source PLC");
 int error=0;
 OpusCustomDecoder* fresh=opus_custom_decoder_create(s.testMode64,1,&error);
 must(fresh && error==0,"reference fresh decoder");
 std::array<int16_t,64> reference{};
 must(opus_custom_decode(fresh,nullptr,42,reference.data(),64)>=0,"reference fresh PLC");
 opus_custom_decoder_destroy(fresh);
 must(decoded==reference,"new source's PLC differs from fresh Opus decoder");
 std::cout<<"PASS OPUS_PLC_NEW_SOURCE_RESET real Opus decoder, missing first record after reused stock ID\n";
}
int main(){try{testOpusResetOnReuse();for(int frames:{64,128})for(bool delay:{false,true})testIdentityRebinding(delay,frames);test(false);test(true);testSurvivingOwner(false);testSurvivingOwner(true);testSurvivingOwner(false,true);testSurvivingOwner(true,true);for(int frame:{64,128})for(bool delay:{false,true}){testHistoryJoinLeave(delay,frame);testStockHistoryJoin(delay,frame);}testVirtualSourceChurn();}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
