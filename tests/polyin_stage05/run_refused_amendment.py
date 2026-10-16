#!/usr/bin/env python3
"""C4 control-state regression: real CClient/Server config+reply methods and
real SourceSlots admission with test-only Qt/audio/socket facades.
Not a native Qt/JACK or live network qualification test.
"""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
lib = Path(os.getenv('POLYIN_OPUS_BUILD', str(root.parent.parent/'opus-build'))) / 'libopus.a'
if not lib.is_file():
    raise SystemExit(f'Missing real vendored Opus build: {lib}')

def extract(path, signature):
    src = (root / 'src' / path).read_text()
    at = src.index(signature)
    lb = src.index('{', at)
    depth = 0
    for i in range(lb, len(src)):
        if src[i] == '{': depth += 1
        elif src[i] == '}':
            depth -= 1
            if not depth: return src[at:i + 1]
    raise AssertionError(signature)

# The full real production method bodies are inserted unchanged into this
# intentionally small substitute for Qt's message and sound-device surfaces.
prefix = r'''
#include "polyincontrol.h"
#include "polyinproducer.h"
#include "polyinsources.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using namespace PolyIn;
enum EAudComprType { CT_OPUS=2, CT_OPUS64=3 };
enum { CC_MONO=1, CC_STEREO=2, CS_DISCONNECTED=0, CS_CONNECTED=1, FRAME_SIZE_FACTOR_PREFERRED=1, FRAME_SIZE_FACTOR_DEFAULT=2, FRAME_SIZE_FACTOR_SAFE=4 };
template<typename T> struct CVector:std::vector<T> {
    using std::vector<T>::vector;
    int Size() const {return int(this->size());}
    void Init(int n) {this->resize(n);}
    void Add(const T& value) {this->push_back(value);}
};
struct QByteArray {
    std::string s;
    QByteArray(const char* p):s(p){}
    QByteArray(std::string x):s(std::move(x)){}
    QByteArray left(int n)const{return QByteArray(s.substr(0,n));}
    std::string toStdString()const{return s;}
    static QByteArray number(int n){return QByteArray(std::to_string(n));}
    QByteArray operator+(const QByteArray& r)const{return QByteArray(s+r.s);}
};
struct QString {std::string s="A"; bool isEmpty()const{return s.empty();} QByteArray toUtf8()const{return QByteArray(s);}};
struct ChannelInfoMock {QString strName;};
struct QDateTime {static uint64_t currentMSecsSinceEpoch(){return 12345;}};
std::ostream& qWarning(){return std::cerr;}
struct MockSound {
    bool running=false,enabled=true;
    int maxChannels=20, channels=2, stops=0, starts=0;
    bool ConfigurePolyInCapture(int n) {
        if(!enabled || n>maxChannels || running)return false;
        channels=n; return true;
    }
    bool IsRunning()const{return running;}
    void Stop(){assert(running);++stops;running=false;}
    void Start(){assert(!running);++starts;running=true;}
};
struct ChannelMock {
    std::vector<uint8_t> lastConfig;
    int sends=0;
    void CreatePolyInConfig(const CVector<uint8_t>& body){++sends;lastConfig.assign(body.begin(),body.end());}
};
struct CClient {
    int polyInDesiredSources=2,polyInCaptureChannels=2,eAudioChannelConf=CC_MONO;
    int eAudioCompressionType=CT_OPUS,iCeltNumCodedBytes=45;
    int iSndCrdPrefFrameSizeFactor=2; bool bEnableOPUS64=false,requestRaw=false;
    int iOPUSFrameSizeSamples=128,iSndCrdFrameSizeFactor=1,iMonoBlockSizeSam=128;
    int iSndCardMonoBlockSizeSamConvBuff=128,eConnectionState=CS_CONNECTED;
    bool bPolyInRequested=true,bRawAudioIsSupported=false,bSndCrdConversionBufferRequired=false;
    std::atomic<bool> bPolyInActive{false},bPolyInMediaHold{false};
    bool bPolyInInitialPending=false,bPolyInAmendmentPending=false,bPolyInFormatTransitionPending=false,
        bPolyInWithdrawalPending=false,bPolyInServerAdmitted=false;
    int polyInAdmittedCodec=-1,polyInAdmittedSamples=0,polyInAdmittedBytes=0,polyInAdmittedSources=0,polyInAdmittedFrameFactor=0;
    bool polyInAdmittedRaw=false;
    uint64_t polyInRetiredProfileTag=0;
    uint64_t PolyInProfileTag() const;
    int polyInPriorSources=0;
    uint64_t polyInPriorEpoch=0,polyInEpoch=0;
    void* CurOpusEncoder=reinterpret_cast<void*>(1);
    SourceProducer polyInProducer, polyInPreviousProducer;
    std::vector<ControlSource> polyInDescriptors;
    CVector<int16_t> polyInRawInterleaved,polyInConvertedCapture;
    struct Buffer{void Init(int){};} polyInConversionIn;
    std::array<CVector<uint8_t>,MaxSources> polyInSendBuffers;
    MockSound Sound;
    ChannelInfoMock ChannelInfo;
    ChannelMock Channel;
    bool PreparePolyInProducer();
    bool ReconfigurePolyInSources(int count);
    void OnPolyInResponse(CVector<uint8_t> body);
    void SendPolyInConfig();
    void BeginPolyInWithdrawal();
    void RememberPolyInProfile();
    void RefreshPolyInAfterSoundInit();
    void SetSndCrdPrefFrameSizeFactor(int);
    void SetEnableOPUS64(bool);
    void OnRawAudioSupported();
    void Init() {
        // Test-only sound/codec facade: the extracted setter still calls this
        // method, which emulates stock's 64/128 profile choice.
        iOPUSFrameSizeSamples = (bEnableOPUS64 && iSndCrdPrefFrameSizeFactor==1)?64:128;
        eAudioCompressionType = iOPUSFrameSizeSamples==64?CT_OPUS64:CT_OPUS;
        iMonoBlockSizeSam = iOPUSFrameSizeSamples;
        iCeltNumCodedBytes = (bRawAudioIsSupported && requestRaw)?iOPUSFrameSizeSamples*2:(iOPUSFrameSizeSamples==64?22:45);
        CurOpusEncoder = (bRawAudioIsSupported && requestRaw)?nullptr:reinterpret_cast<void*>(1);
    }
};
struct CChannelInfo {int iChanID;};
static bool audioLocked=false;
struct MutexMock {};
struct QMutexLocker {
    explicit QMutexLocker(MutexMock*) {assert(!audioLocked);audioLocked=true;}
    ~QMutexLocker() {audioLocked=false;}
};
struct ServerChannel {
    bool connected=false;
    ServerChannel* owner=nullptr;
    std::vector<std::vector<int>> publishedLists;
    CVector<uint8_t> response;
    bool polyActive=false; int codec=CT_OPUS,bytes=45;
    struct MockName{std::string toStdString()const{return "stock";}};
    struct MockInfo{MockName strName;};
    MockInfo GetChanInfo()const{return {};};
    bool IsPolyInPhysical()const{return polyActive;}
    bool IsConnected()const{return connected;}
    bool IsPolyInVirtualOf(const ServerChannel* p)const{return owner==p;}
    void CreateConClientListMes(const CVector<CChannelInfo>& list) {
        assert(!audioLocked);
        std::vector<int> ids;
        for(const auto& info:list)ids.push_back(info.iChanID);
        publishedLists.push_back(ids);
    }
    EAudComprType GetAudioCompressionType()const{return EAudComprType(codec);}
    int GetNumAudioChannels()const{return 1;}
    int GetCeltNumCodedBytes()const{return bytes;}
    void CreatePolyInResponse(CVector<uint8_t> body){response=std::move(body);}
};
struct CServer {
    struct PolyInAudioSource {
        uint16_t source; int channel; EAudComprType codec; int channels, encodedBytes;
    };
    int iMaxNumChannels=8, lists=0, disables=0, configures=0;
    std::array<ServerChannel,8> vecChannels{};
    std::array<uint64_t,8> polyInEpoch{};
    SourceSlots polyInSlots;
    MutexMock Mutex;
    explicit CServer(int capacity=8):iMaxNumChannels(capacity),polyInSlots(capacity){
        vecChannels[0].connected=true;
        assert(polyInSlots.AdmitPhysical(0,1,"A"));
    }
    void DisablePolyInAudio(int physical) {
        ++disables;vecChannels[physical].polyActive=false;
        for(auto& channel:vecChannels)if(channel.owner==&vecChannels[physical])channel.owner=nullptr;
    }
    bool ConfigurePolyInAudio(const SourceSlots& slots,int p,uint64_t generation,
                              const std::vector<PolyInAudioSource>& audio) {
        ++configures;
        if(audio.size()<2)return false;
        for(const auto& s:audio){ SourceSlots::Identity found;
            if(!slots.Resolve(p,generation,s.source,found) || found.channel!=s.channel)return false;
        }
        vecChannels[p].polyActive=true;
        for(const auto& s:audio)if(s.channel!=p)vecChannels[s.channel].owner=&vecChannels[p];
        return true;
    }
    CVector<CChannelInfo> CreateChannelList() {
        CVector<CChannelInfo> result;
        for(int i=0;i<iMaxNumChannels;++i)
            if(vecChannels[i].connected || vecChannels[i].owner)result.Add({i});
        return result;
    }
    void CreateAndSendChanListForAllConChannels(){
        ++lists;auto list=CreateChannelList();
        for(auto& channel:vecChannels)if(channel.connected)channel.CreateConClientListMes(list);
    }
    void HandlePolyInConfig(int physical,CVector<uint8_t> body);
};
static CVector<uint8_t> bytes(const std::vector<uint8_t>& v) {
    return CVector<uint8_t>(v.begin(),v.end());
}
static void roundtrip(CClient& client,CServer& server) {
    assert(!client.Channel.lastConfig.empty());
    server.HandlePolyInConfig(0,bytes(client.Channel.lastConfig));
    assert(!server.vecChannels[0].response.empty());
    client.OnPolyInResponse(server.vecChannels[0].response);
}
'''
main = r'''
int main(){
    {
        CClient subject, reference;
        assert(subject.PreparePolyInProducer() && reference.PreparePolyInProducer());
        reference.polyInEpoch=subject.polyInEpoch;
        subject.bPolyInServerAdmitted=true;subject.bPolyInActive=true;subject.RememberPolyInProfile();subject.Sound.Start();
        auto sameEncodedFrame=[&](int frame){
            std::vector<int16_t> pcm(256);for(size_t i=0;i<pcm.size();++i)pcm[i]=int16_t(frame*31+int(i)*7-2000);
            std::array<BoundedPacket,MaxSources> a{},b{};size_t na=0,nb=0;
            assert(subject.polyInProducer.EncodeBounded(pcm.data(),pcm.size(),false,frame,subject.polyInEpoch,MaxDatagram,a,na));
            assert(reference.polyInProducer.EncodeBounded(pcm.data(),pcm.size(),false,frame,reference.polyInEpoch,MaxDatagram,b,nb));
            assert(na==nb);for(size_t i=0;i<na;++i)assert(a[i].length==b[i].length&&std::equal(a[i].data.begin(),a[i].data.begin()+a[i].length,b[i].data.begin()));
        };
        for(int frame=0;frame<12;++frame)sameEncodedFrame(frame);
        assert(subject.ReconfigurePolyInSources(6));
        subject.OnPolyInResponse(bytes(Response(subject.polyInEpoch,false)));
        for(int frame=12;frame<24;++frame)sameEncodedFrame(frame);
        subject.Sound.maxChannels=2;
        assert(!subject.ReconfigurePolyInSources(6));
        for(int frame=24;frame<36;++frame)sameEncodedFrame(frame);
        std::cout<<"PASS refused local/server amendment preserves exact accepted Opus encoder state and wire bytes\n";
    }

    // Reused virtual IDs must first leave the stock list. A stock receiver
    // then restores its fader controls when the same named source rejoins.
    {
        CServer replacementServer;
        CClient replacementClient;
        assert(replacementClient.PreparePolyInProducer());
        replacementClient.bPolyInInitialPending=true;
        replacementClient.SendPolyInConfig();
        roundtrip(replacementClient,replacementServer);
        auto& published=replacementServer.vecChannels[0].publishedLists;
        assert(published.size()==1 && published.back()==std::vector<int>({0,1}));
        replacementClient.bPolyInServerAdmitted=false;
        replacementClient.bPolyInActive=false;
        assert(replacementClient.PreparePolyInProducer());
        replacementClient.bPolyInInitialPending=true;
        replacementClient.SendPolyInConfig();
        roundtrip(replacementClient,replacementServer);
        assert(published.size()==3);
        assert(published[1]==std::vector<int>({0}));
        assert(published[2]==std::vector<int>({0,1}));
        std::cout<<"PASS reused virtual ID: stock leave/rejoin lists in order outside audio mutex\n";
    }
    CServer server;
    CClient client;
    assert(client.PreparePolyInProducer());
    client.Sound.Start();
    client.bPolyInInitialPending=true; // Start() explicitly opens initial opt-in.
    client.SendPolyInConfig();
    roundtrip(client,server);
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    const uint64_t epoch0=client.polyInEpoch;
    assert(epoch0==server.polyInEpoch[0]);
    SourceSlots::Identity original;
    assert(server.polyInSlots.Resolve(0,epoch0,1,original));
    assert(client.bPolyInServerAdmitted);
    const int sendsAfterAdmission=client.Channel.sends;
    client.SendPolyInConfig(); // property callback after accepted initial offer
    assert(client.Channel.sends==sendsAfterAdmission && !client.bPolyInInitialPending);

    // 8 ordinary channel IDs cannot accept 20 sources. Server keeps epoch0.
    assert(client.ReconfigurePolyInSources(20));
    const uint64_t refusedEpoch=client.polyInEpoch;
    assert(refusedEpoch>epoch0);
    assert(client.bPolyInAmendmentPending && client.bPolyInMediaHold && !client.bPolyInActive);
    assert(!client.ReconfigurePolyInSources(6)); // no crossed outstanding amendment
    server.HandlePolyInConfig(0,bytes(client.Channel.lastConfig));
    uint64_t gotEpoch=0; bool accepted=true;
    assert(DecodeResponse(server.vecChannels[0].response.data(),server.vecChannels[0].response.Size(),gotEpoch,accepted));
    assert(gotEpoch==refusedEpoch && !accepted);
    assert(server.polyInEpoch[0]==epoch0);
    assert(server.polyInSlots.Resolve(0,epoch0,1,original));
    client.OnPolyInResponse(server.vecChannels[0].response);
    assert(!client.bPolyInAmendmentPending && !client.bPolyInMediaHold && client.bPolyInActive);
    assert(client.polyInDesiredSources==2 && client.polyInProducer.Count()==2);
    assert(client.polyInEpoch==epoch0); // *exact* still-admitted old epoch
    assert(client.Sound.IsRunning());
    assert(server.polyInSlots.Resolve(0,client.polyInEpoch,1,original));
    // Stale positive and negative replies for refused epoch must not change it.
    client.OnPolyInResponse(bytes(Response(refusedEpoch,true)));
    client.OnPolyInResponse(bytes(Response(refusedEpoch,false)));
    assert(client.bPolyInActive && client.polyInEpoch==epoch0);

    // Successful admission after refusal still works. A late old refusal is ignored.
    assert(client.ReconfigurePolyInSources(6));
    const uint64_t epoch6=client.polyInEpoch;
    roundtrip(client,server);
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    assert(server.polyInEpoch[0]==epoch6 && epoch6>refusedEpoch);
    assert(client.polyInProducer.Count()==6);
    client.OnPolyInResponse(bytes(Response(refusedEpoch,false)));
    client.OnPolyInResponse(bytes(Response(epoch0,false)));
    assert(client.bPolyInActive && client.polyInEpoch==epoch6);

    // Repeat refusal with a six-source admitted session and retain its IDs.
    assert(client.ReconfigurePolyInSources(20));
    roundtrip(client,server);
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    assert(client.polyInEpoch==epoch6 && client.polyInDesiredSources==6);
    assert(server.polyInEpoch[0]==epoch6);
    assert(server.polyInSlots.Resolve(0,epoch6,5,original));

    // Local device capture denial must also restore the old stream without
    // transmitting a new config or admitting an incompatible epoch.
    const auto previousConfig=client.Channel.lastConfig;
    client.Sound.maxChannels=6;
    assert(!client.ReconfigurePolyInSources(20));
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    assert(client.polyInEpoch==epoch6 && client.polyInDesiredSources==6);
    assert(client.Channel.lastConfig==previousConfig);

    // First-time opt-in denied: no prior PLIN session, ordinary stock fallback.
    CClient initial;
    CServer one(1);
    assert(initial.PreparePolyInProducer());
    initial.Sound.Start(); // same physical capture lifecycle as production Start()
    initial.bPolyInInitialPending=true; // Start() explicitly opens initial opt-in.
    initial.SendPolyInConfig();
    roundtrip(initial,one);
    assert(!initial.bPolyInActive && initial.bPolyInMediaHold && initial.bPolyInWithdrawalPending);
    roundtrip(initial,one); // explicit stock withdrawal, even for rejected initial opt-in
    assert(!initial.bPolyInActive && !initial.bPolyInMediaHold && !initial.bPolyInWithdrawalPending);
    const int initialRetiredSends=initial.Channel.sends;
    initial.Sound.Stop();initial.RefreshPolyInAfterSoundInit();initial.Sound.Start();
    assert(initial.Channel.sends==initialRetiredSends && !initial.bPolyInInitialPending);
    initial.SendPolyInConfig(); // duplicate property callback on stock state
    assert(initial.Channel.sends==initialRetiredSends && !initial.bPolyInInitialPending);

    // OF-05: unchanged-profile sound restart preserves epoch and stock jitter.
    const auto beforeSameProfile=client.Channel.lastConfig;
    client.Sound.Stop();
    client.RefreshPolyInAfterSoundInit();
    client.Sound.Start();
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    assert(client.polyInEpoch==epoch6 && client.Channel.lastConfig==beforeSameProfile);

    // C5: a changed-quality Init while the server is still PLIN must not send STOCK.
    client.Sound.Stop();
    client.iCeltNumCodedBytes=82;
    client.RefreshPolyInAfterSoundInit();
    client.Sound.Start();
    assert(!client.bPolyInActive && client.bPolyInMediaHold && client.bPolyInFormatTransitionPending);
    const auto pendingRefresh=client.polyInEpoch;
    assert(pendingRefresh>epoch6);
    // Existing PLIN is unchanged while request is in flight.
    assert(server.polyInEpoch[0]==epoch6 && server.vecChannels[0].polyActive);
    // Accepted changed-quality refresh resumes PLIN with new epoch.
    server.vecChannels[0].bytes=82;
    roundtrip(client,server);
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    assert(!client.bPolyInFormatTransitionPending && server.polyInEpoch[0]==pendingRefresh);
    const auto refreshed=client.polyInEpoch;

    // C6: actual frame-size setter must enter negotiation; codec mismatch
    // triggers a refusal, then zero-source withdrawal, with media held until ACK.
    client.SetEnableOPUS64(true);
    assert(client.iOPUSFrameSizeSamples==128); // factor=2 keeps stock Opus128
    client.SetSndCrdPrefFrameSizeFactor(FRAME_SIZE_FACTOR_PREFERRED);
    assert(client.iOPUSFrameSizeSamples==64 && client.eAudioCompressionType==CT_OPUS64);
    assert(!client.bPolyInActive && client.bPolyInMediaHold);
    const auto proposed64=client.polyInEpoch;
    server.vecChannels[0].bytes=82; // old server quality still in force
    server.HandlePolyInConfig(0,bytes(client.Channel.lastConfig));
    uint64_t ignoredEpoch=0; bool wasAccepted=true;
    assert(DecodeResponse(server.vecChannels[0].response.data(),server.vecChannels[0].response.Size(),ignoredEpoch,wasAccepted));
    assert(!wasAccepted && ignoredEpoch==proposed64);
    client.OnPolyInResponse(server.vecChannels[0].response);
    assert(client.bPolyInWithdrawalPending && client.bPolyInMediaHold && !client.bPolyInActive);
    assert(server.vecChannels[0].polyActive && server.polyInEpoch[0]==refreshed);
    const auto withdrawalEpoch=client.polyInEpoch;
    assert(withdrawalEpoch>proposed64);
    roundtrip(client,server);
    assert(!client.bPolyInWithdrawalPending && !client.bPolyInMediaHold && !client.bPolyInActive);
    assert(!server.vecChannels[0].polyActive && server.polyInEpoch[0]==withdrawalEpoch);
    // Reliable duplicate of the zero-source request is idempotent.
    server.HandlePolyInConfig(0,bytes(client.Channel.lastConfig));
    uint64_t duplicateEpoch=0; bool duplicateAccepted=false;
    assert(DecodeResponse(server.vecChannels[0].response.data(),server.vecChannels[0].response.Size(),duplicateEpoch,duplicateAccepted));
    assert(duplicateAccepted && duplicateEpoch==withdrawalEpoch);
    assert(!server.vecChannels[0].polyActive && server.polyInEpoch[0]==withdrawalEpoch);
    client.OnPolyInResponse(bytes(Response(withdrawalEpoch,true)));
    assert(!client.bPolyInActive && !client.bPolyInMediaHold && !client.bPolyInWithdrawalPending);
    assert(!server.polyInSlots.Resolve(0,refreshed,1,original));
    assert(!server.polyInSlots.IsTaken(1));
    client.OnPolyInResponse(bytes(Response(proposed64,true))); // late amendment
    assert(!client.bPolyInActive && !client.bPolyInMediaHold);

    // C7 (reviewer reproduction 1): unsolicited stock property callbacks
    // cannot re-offer an already withdrawn producer at the retired epoch.
    // The old outgoing payload is the zero-source withdrawal and must not
    // be replaced by a nonempty CONFIG or open a new initial transaction.
    assert(client.polyInRetiredProfileTag==client.PolyInProfileTag());
    // An automatic unchanged-profile device restart after completed withdrawal
    // must remain stock; it is NOT an explicit renewed opt-in.
    client.Sound.Stop(); client.RefreshPolyInAfterSoundInit(); client.Sound.Start();
    assert(!client.bPolyInActive && !client.bPolyInInitialPending && !client.bPolyInMediaHold);
    assert(!server.vecChannels[0].polyActive);
    const auto retiredConfig=client.Channel.lastConfig;
    const int retiredSends=client.Channel.sends;
    std::vector<ControlSource> retiredDescriptors;
    uint64_t decodedRetiredEpoch=0;
    assert(DecodeConfig(retiredConfig.data(),retiredConfig.size(),decodedRetiredEpoch,retiredDescriptors));
    assert(decodedRetiredEpoch==withdrawalEpoch && retiredDescriptors.empty());
    client.SendPolyInConfig(); // NetwTranspPropsSent and OnNewConnection callback target
    assert(client.Channel.lastConfig==retiredConfig && client.Channel.sends==retiredSends &&
           !client.bPolyInInitialPending);
    assert(!client.bPolyInActive && !client.bPolyInMediaHold && !client.bPolyInServerAdmitted);
    // A duplicate withdrawal ACK must remain inert even after callback.
    client.OnPolyInResponse(bytes(Response(withdrawalEpoch,true)));
    assert(!client.bPolyInActive && !client.bPolyInInitialPending);
    assert(!server.vecChannels[0].polyActive);

    // Server hard fence: even a malformed/replaying old producer which sends
    // nonempty CONFIG under the retired epoch is refused despite *matching*
    // codec/size. No virtual ID reallocation or reactivation is permitted.
    server.vecChannels[0].codec=CT_OPUS64;
    server.vecChannels[0].bytes=22;
    std::vector<uint8_t> illegalReoffer;
    assert(EncodeConfig(withdrawalEpoch,client.polyInDescriptors,illegalReoffer));
    server.HandlePolyInConfig(0,bytes(illegalReoffer));
    uint64_t rejectedRetiredEpoch=0; bool rejectedRetiredAccepted=true;
    assert(DecodeResponse(server.vecChannels[0].response.data(),server.vecChannels[0].response.Size(),
                          rejectedRetiredEpoch,rejectedRetiredAccepted));
    assert(rejectedRetiredEpoch==withdrawalEpoch && !rejectedRetiredAccepted);
    assert(server.polyInEpoch[0]==withdrawalEpoch && !server.vecChannels[0].polyActive);
    assert(!server.polyInSlots.IsTaken(1));

    // Admit RAW/64 as a new opt-in after acknowledged withdrawal; sender and
    // transport both use the same profile. No stock frames during admission.
    client.bRawAudioIsSupported=true;
    client.iCeltNumCodedBytes=128;
    client.CurOpusEncoder=nullptr;
    client.Sound.Stop(); client.RefreshPolyInAfterSoundInit(); client.Sound.Start();
    // C7 (reviewer reproduction 2): a new explicit request must have a fresh
    // epoch. Its pending state must not interpret a duplicate positive reply
    // for the *previous zero-source withdrawal* as acceptance of the new offer.
    const auto freshEpoch=client.polyInEpoch;
    assert(freshEpoch>withdrawalEpoch && client.bPolyInInitialPending);
    assert(!client.bPolyInActive && !server.vecChannels[0].polyActive);
    client.OnPolyInResponse(bytes(Response(withdrawalEpoch,true)));
    assert(client.bPolyInInitialPending && !client.bPolyInActive);
    assert(!server.vecChannels[0].polyActive);
    server.vecChannels[0].codec=CT_OPUS64;
    server.vecChannels[0].bytes=128;
    roundtrip(client,server);
    assert(client.bPolyInActive && client.bPolyInServerAdmitted && server.vecChannels[0].polyActive);
    const auto rawEpoch=client.polyInEpoch;

    // C5: local unsupported stereo cannot fall back to stock until the server
    // has explicitly retired PLIN, even when Prepare() emits no new encoder.
    client.Sound.Stop();
    client.eAudioChannelConf=CC_STEREO;
    client.RefreshPolyInAfterSoundInit();
    client.Sound.Start();
    assert(client.bPolyInWithdrawalPending && !client.bPolyInActive && client.bPolyInMediaHold);
    assert(server.polyInEpoch[0]==rawEpoch && server.vecChannels[0].polyActive);
    roundtrip(client,server);
    assert(!client.bPolyInMediaHold && !server.vecChannels[0].polyActive);

    // C5: format accept while server stock transport is changed too.
    client.Sound.Stop();client.eAudioChannelConf=CC_MONO;
    client.bRawAudioIsSupported=false;
    client.iCeltNumCodedBytes=22;
    client.CurOpusEncoder=reinterpret_cast<void*>(1);
    client.RefreshPolyInAfterSoundInit();client.Sound.Start();
    server.vecChannels[0].bytes=22;
    roundtrip(client,server);
    assert(client.bPolyInActive && client.bPolyInServerAdmitted);
    client.SetSndCrdPrefFrameSizeFactor(FRAME_SIZE_FACTOR_DEFAULT);
    assert(client.bPolyInMediaHold && !client.bPolyInActive);
    server.vecChannels[0].codec=CT_OPUS;
    server.vecChannels[0].bytes=45;
    roundtrip(client,server);
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    const auto beforeRawArrival=client.polyInEpoch;
    // Exercise the actual stock OnRawAudioSupported entry point (not a copy).
    client.requestRaw=true;
    client.OnRawAudioSupported();
    assert(client.bRawAudioIsSupported && client.iCeltNumCodedBytes==256);
    assert(!client.bPolyInActive && client.bPolyInMediaHold && client.bPolyInFormatTransitionPending);
    assert(server.vecChannels[0].polyActive && server.polyInEpoch[0]==beforeRawArrival);
    server.vecChannels[0].bytes=256;
    roundtrip(client,server);
    assert(client.bPolyInActive && !client.bPolyInMediaHold);
    assert(server.polyInEpoch[0]>beforeRawArrival);

    std::cout<<"PASS production C4/C5/C6/C7 methods: rejected count restore, ordinary Init, accepted refresh, 64/128 changes, codec refusal and acknowledged withdrawal, unsupported local mode, RAW/64, stale reply fencing; mock Qt/JACK boundary\n";
}
'''
methods = [extract('client.cpp', 'static uint64_t NextPolyInEpoch()')]
methods += [extract('client.cpp', 'uint64_t CClient::PolyInProfileTag() const')]
methods += [extract('client.cpp', s) for s in (
    'bool CClient::PreparePolyInProducer()',
    'bool CClient::ReconfigurePolyInSources ( int count )',
    'void CClient::OnPolyInResponse ( CVector<uint8_t> body )',
    'void CClient::BeginPolyInWithdrawal()',
    'void CClient::RefreshPolyInAfterSoundInit()',
    'void CClient::RememberPolyInProfile()',
    'void CClient::SetSndCrdPrefFrameSizeFactor ( const int iNewFactor )',
    'void CClient::SetEnableOPUS64 ( const bool eNEnableOPUS64 )',
    'void CClient::OnRawAudioSupported()',
    'void CClient::SendPolyInConfig()')]
methods += [extract('server.cpp', 'void CServer::HandlePolyInConfig ( int physical, CVector<uint8_t> body )')]
callback = (root / 'src/client.cpp').read_text()
assert 'else if ( !bPolyInMediaHold.load() && !bPolyInActive.load() )' in callback, 'Audio callback must not fall back to stock while an accepted PLIN capture is absent'
body = prefix + '\n\n'.join(methods) + '\n' + main
with tempfile.TemporaryDirectory(prefix='stage05-refused-amendment-') as d:
    fixture=Path(d)/'fixture.cpp'
    fixture.write_text(body)
    exe=Path(d)/'fixture'
    cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
         '-I'+str(root/'src'),'-I'+str(root/'libs/opus/include'),
         str(root/'src/polyinwire.cpp'),str(root/'src/polyinproducer.cpp'),
         str(root/'src/polyinsources.cpp'),str(fixture),str(lib),'-lm','-o',str(exe)]
    print('BUILD', ' '.join(cmd),flush=True)
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
