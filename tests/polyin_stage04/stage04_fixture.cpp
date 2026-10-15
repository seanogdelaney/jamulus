// Copyright (c) 2026. Test-only adapter; production CServer method bodies are
// extracted *verbatim* from the pinned Jamulus src/server.cpp at build time.
// This is NOT an instance of the real Qt CServer/CChannel/Socket lifecycle.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "opus_custom.h"
#include "opus_defines.h"
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
using EGetDataStat = int;
template<typename T> class CVector : public std::vector<T> {
public:
    using std::vector<T>::vector;
    void Init(int n) { this->assign(n,T{}); }
    void Reset(T x) { std::fill(this->begin(),this->end(),x); }
};
// STOCK_UTIL_EXTRACT
// STOCK_CONVBUF_EXTRACT
struct FakeSocket {};
struct FakeChannel {
    int num=1, compr=CT_RAW, bytes=128;
    float fade=1.f;
    std::vector<float> gains=std::vector<float>(8,1.f), pans=std::vector<float>(8,.5f);
    struct Incoming { int stat; std::vector<uint8_t> bytes; };
    std::vector<Incoming> incoming;
    size_t inpos=0;
    std::vector<std::vector<uint8_t>> sent;
    int GetNumAudioChannels() const {return num;}
    int GetAudioCompressionType() const {return compr;}
    int GetCeltNumCodedBytes() const {return bytes;}
    float GetFadeInGain() const {return fade;}
    void GetGainsAndPannings(const CVector<int>& ids,int n,CVector<float>& g,CVector<float>& p) {
        for(int i=0;i<n;++i) { g[i]=gains[ids[i]]; p[i]=pans[ids[i]]; }
    }
    EGetDataStat GetData(CVector<uint8_t>& dst,int expected) {
        if(inpos==incoming.size()) return GS_BUFFER_UNDERRUN;
        const auto& x=incoming.at(inpos++);
        if(x.stat==GS_BUFFER_OK) { if((int)x.bytes.size()!=expected) throw std::runtime_error("incoming size");
           std::copy(x.bytes.begin(),x.bytes.end(),dst.begin()); }
        return x.stat;
    }
    void PrepAndSendPacket(FakeSocket*,const CVector<uint8_t>& bytes,int n) {
        sent.emplace_back(bytes.begin(),bytes.begin()+n);
    }
};
struct CServer {
    bool bUseDoubleSystemFrameSize=false,bDelayPan=false,bChannelIsNowDisconnected=false;
    int iServerFrameSizeSamples=64;
    FakeChannel vecChannels[8];
    FakeSocket Socket;
    CVector<int> vecChanIDsCurConChan;
    CVector<int> vecNumAudioChannels, vecAudioComprType,vecNumFrameSizeConvBlocks;
    CVector<bool> vecUseDoubleSysFraSizeConvBuf;
    CVector<CVector<int16_t>> vecvecsData,vecvecsData2,vecvecsSendData;
    CVector<CVector<uint8_t>> vecvecbyCodedData;
    CVector<CVector<float>> vecvecfGains,vecvecfPannings,vecvecfIntermediateProcBuf;
    CConvBuf<int16_t> DoubleFrameSizeConvBufIn[8],DoubleFrameSizeConvBufOut[8];
    OpusCustomDecoder* OpusDecoderMono[8]{},*OpusDecoderStereo[8]{},*Opus64DecoderMono[8]{},*Opus64DecoderStereo[8]{};
    OpusCustomEncoder* OpusEncoderMono[8]{},*OpusEncoderStereo[8]{},*Opus64EncoderMono[8]{},*Opus64EncoderStereo[8]{};
    CServer() {
        vecChanIDsCurConChan.Init(8);
        vecNumAudioChannels.Init(8); vecAudioComprType.Init(8);vecNumFrameSizeConvBlocks.Init(8);
        vecUseDoubleSysFraSizeConvBuf.Init(8);
        vecvecsData.resize(8);vecvecsData2.resize(8);vecvecsSendData.resize(8);
        vecvecbyCodedData.resize(8);vecvecfGains.resize(8);vecvecfPannings.resize(8);vecvecfIntermediateProcBuf.resize(8);
        for(int i=0;i<8;i++) {
            vecChanIDsCurConChan[i]=i;
            vecvecsData[i].Init(256);vecvecsData2[i].Init(256);vecvecsSendData[i].Init(256);
            vecvecbyCodedData[i].Init(512);vecvecfGains[i].Init(8);vecvecfPannings[i].Init(8);vecvecfIntermediateProcBuf[i].Init(256);
            DoubleFrameSizeConvBufIn[i].Init(256);DoubleFrameSizeConvBufOut[i].Init(256);
        }
    }
    void ClientDisconnected(int){};
    void FreeChannel(int){};
    void DecodeReceiveData(int,int);
    void MixEncodeTransmitData(int,int);
};
// STOCK_METHODS_EXTRACT
static void must(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
static std::vector<uint8_t> pcm(int count, int base, int step=0) {
    std::vector<uint8_t> b(count*2);
    for(int i=0;i<count;++i) { int16_t x=(int16_t)(base+i*step); std::memcpy(b.data()+i*2,&x,2); }
    return b;
}
static void setup(CServer& s,int n,int frame) {
    s.bUseDoubleSystemFrameSize=(frame==128);s.iServerFrameSizeSamples=frame;
    for(int i=0;i<n;++i) {s.vecChannels[i].num=1;s.vecChannels[i].compr=(frame==128 ? CT_OPUS : CT_OPUS64);
        s.vecChannels[i].bytes=frame*2;s.vecChannels[i].fade=1.; }
}
static int checksum(const CVector<int16_t>& v,int n) {
    int ret=0;for(int i=0;i<n;i++)ret=(ret*65599u+(uint16_t)v[i])&0x7fffffff;return ret;
}
static void dump(const char* name,CServer& s,int n,int frames=1) {
    for(int i=0;i<n;++i) {
        const auto& c=s.vecChannels[i];
        std::cout<<name<<",target="<<i<<",pcmhash="<<checksum(s.vecvecsSendData[i],s.iServerFrameSizeSamples*c.num)
                 <<",packets="<<c.sent.size();
        for(size_t k=0;k<c.sent.size();++k) {
            int hash=0;for(auto x:c.sent[k])hash=(hash*65599u+x)&0x7fffffff;
            std::cout<<",packet"<<k<<"="<<hash<<":"<<c.sent[k].size();
        }
        std::cout<<"\n";
    }
}
static void raw_mix_64() {
    CServer s;setup(s,3,64);
    // Receiver 0 mono: 1000 + 2000*.5 + (-900)*.5 => 1550;
    // Receiver 1 stereo, with independent left/right pans.
    s.vecChannels[0].gains[0]=1.f;s.vecChannels[0].gains[1]=.5f;s.vecChannels[0].gains[2]=.5f;
    s.vecChannels[1].num=2;s.vecChannels[1].bytes=256;
    s.vecChannels[1].pans[0]=0.f;s.vecChannels[1].pans[1]=.5f;s.vecChannels[1].pans[2]=1.f;
    s.vecChannels[2].fade=.5f;
    s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(64,1000)});
    std::vector<uint8_t> two=pcm(128,2000); // stereo repeating
    s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,two});
    s.vecChannels[2].incoming.push_back({GS_BUFFER_OK,pcm(64,-900)});
    for(int i=0;i<3;++i)s.DecodeReceiveData(i,3);
    for(int i=0;i<3;++i)s.MixEncodeTransmitData(i,3);
    must(s.vecvecsSendData[0][0]==1775,"raw mono gain/fade mix"); // 1000 + 1000 - 225 = 1775
    must(s.vecvecsSendData[1][0]==3000,"stereo left pan"); // 1000 + 2000
    must(s.vecvecsSendData[1][1]==1550,"stereo right pan"); // 2000 - 450
    must(s.vecChannels[0].sent.size()==1 && s.vecChannels[0].sent[0].size()==128,"raw mono packet");
    must(s.vecChannels[1].sent.size()==1 && s.vecChannels[1].sent[0].size()==256,"raw stereo packet");
    must(std::memcmp(s.vecChannels[0].sent[0].data(),s.vecvecsSendData[0].data(),128)==0,"raw actual packet payload");
    dump("L02-L04-raw64",s,3);
}
static void raw_mix_128_clip() {
    CServer s;setup(s,2,128);
    s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(128,30000)});
    s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(128,30000)});
    for(int i=0;i<2;++i)s.DecodeReceiveData(i,2);
    for(int i=0;i<2;++i)s.MixEncodeTransmitData(i,2);
    must(s.vecvecsSendData[0][0]==32767,"clip to max short");
    must(s.vecChannels[0].sent.size()==1 && s.vecChannels[0].sent[0].size()==256,"128 packet");
    dump("L02-L04-raw128-clip",s,2);
}
static void raw_loss() {
    CServer s;setup(s,1,64);
    s.vecChannels[0].incoming.push_back({GS_BUFFER_UNDERRUN,{}});
    s.DecodeReceiveData(0,1);s.MixEncodeTransmitData(0,1);
    must(s.vecvecsSendData[0][0]==0,"raw missing -> silence");
    dump("L02-raw-loss",s,1);
}
static void history_test(bool delay,int channels,int frame) {
    CServer s;setup(s,2,frame);s.bDelayPan=delay;
    s.vecChannels[0].num=2;s.vecChannels[0].bytes=frame*4;
    s.vecChannels[0].pans[1]=0.f; // L/R asymmetric when delay enabled
    s.vecChannels[1].num=channels;s.vecChannels[1].bytes=frame*2*channels;
    for(int t=0;t<3;t++) {
        // Tick zero acts as history. Later frames trigger boundary cross.
        s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(2*frame,0)});
        s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(frame*channels,1000+t*100)});
    }
    for(int t=0;t<3;t++) {
        for(int i=0;i<2;i++)s.DecodeReceiveData(i,2);
        s.MixEncodeTransmitData(0,2);
        std::cout<<"L03-delay="<<delay<<",source_ch="<<channels<<",frame="<<frame<<",tick="<<t
                 <<",l0="<<s.vecvecsSendData[0][0]<<",r0="<<s.vecvecsSendData[0][1]
                 <<",l63="<<s.vecvecsSendData[0][2*(frame-1)]<<",r63="<<s.vecvecsSendData[0][2*frame-1]<<"\n";
        if(delay && t==1) {
            // pan 0 => 0 left delay; 62 right delay. First right samples
            // therefore refer to previous frame rather than current frame.
            must(s.vecvecsSendData[0][0]==1100,"current frame left");
            must(s.vecvecsSendData[0][1]==1000,"previous frame right");
            must(s.vecvecsSendData[0][2*frame-1]==1100,"current frame right tail");
        }
        if(!delay && t==1) {
            must(s.vecvecsSendData[0][0]==1100,"normal pan left");
            must(s.vecvecsSendData[0][1]==0,"normal pan right muted");
        }
        for(int i=0;i<2;i++)for(int j=0;j<2*frame;++j)s.vecvecsData2[i][j]=s.vecvecsData[i][j];
    }
}

static std::vector<uint8_t> coded(OpusCustomEncoder* encoder,int frame,int nch,int value,int nbytes) {
    std::vector<int16_t> samples(frame*nch);
    for(size_t i=0;i<samples.size();++i) samples[i]=(int16_t)(value+(int(i)*11)%301);
    std::vector<uint8_t> bytes(nbytes);
    int ret=opus_custom_encode(encoder,samples.data(),frame,bytes.data(),nbytes);
    must(ret==nbytes,"client opus encode expected bytes");
    return bytes;
}
static void opus_roundtrip(int frame,int nch,bool plc) {
    // Exercise stock OpusCustom encoders/decoders, frame conversion and PLC.
    int status=0;
    auto* mode=opus_custom_mode_create(48000,frame,&status);
    must(status==OPUS_OK && mode,"custom mode");
    auto* source=opus_custom_encoder_create(mode,nch,&status);
    must(status==OPUS_OK && source,"source encoder");
    auto* dec=opus_custom_decoder_create(mode,nch,&status);
    must(status==OPUS_OK && dec,"server decoder");
    auto* enc=opus_custom_encoder_create(mode,nch,&status);
    must(status==OPUS_OK && enc,"server encoder");
    const int bytes=40;
    CServer s;
    // Matching stock server frames, plus 64/128 cadence-conversion variants.
    const int serverFrame=frame==128?64:128;
    setup(s,1,serverFrame);
    auto& channel=s.vecChannels[0];
    channel.num=nch;channel.compr=(frame==128?CT_OPUS:CT_OPUS64);channel.bytes=bytes;
    if(frame==128) {
        if(nch==1) {s.OpusDecoderMono[0]=dec;s.OpusEncoderMono[0]=enc;}
        else {s.OpusDecoderStereo[0]=dec;s.OpusEncoderStereo[0]=enc;}
    } else {
        if(nch==1) {s.Opus64DecoderMono[0]=dec;s.Opus64EncoderMono[0]=enc;}
        else {s.Opus64DecoderStereo[0]=dec;s.Opus64EncoderStereo[0]=enc;}
    }
    const int ticks=12;
    for(int t=0;t<ticks;t++) {
        const int blocks = frame==64?2:1;
        for(int k=0;k<blocks;k++) {
            auto b=coded(source,frame,nch,2000+t*80+k*40,bytes);
            channel.incoming.push_back({plc && t==1 && k==0 ? GS_BUFFER_UNDERRUN : GS_BUFFER_OK,std::move(b)});
        }
    }
    int nonzero=0;
    for(int t=0;t<ticks;t++) {
        s.DecodeReceiveData(0,1);s.MixEncodeTransmitData(0,1);
        const int first=s.vecvecsData[0][0];
        nonzero+=(first!=0);
        std::cout << "opus,client_frame=" << frame << ",server_frame=" << serverFrame
                  << ",channels=" << nch << ",plc=" << plc << ",tick=" << t
                  << ",pcm0=" << first << ",mix0=" << s.vecvecsSendData[0][0]
                  << ",packets=" << channel.sent.size() << "\n";
    }
    must(nonzero>=2,"opus decoded multiple real frames");
    must(channel.sent.size()==(size_t)(frame==128?ticks/2:ticks*2),"opus cadence and packets");
    unsigned hash=0; for(const auto& packet:channel.sent) {must(packet.size()==bytes,"opus packet size"); for(uint8_t x:packet)hash=hash*65599u+x;}
    std::cout<<"opus-encoded,client_frame="<<frame<<",channels="<<nch<<",plc="<<plc<<",bytehash="<<hash<<"\n";
    if(plc) must(channel.inpos==(size_t)(frame==128?ticks/2-1:ticks*2),"packet cadence during PLC");
    opus_custom_encoder_destroy(enc);
    opus_custom_decoder_destroy(dec);
    opus_custom_encoder_destroy(source);
    opus_custom_mode_destroy(mode);
}


// F05 mixer exercise, original extracted CServer::DecodeReceiveData and
// CServer::MixEncodeTransmitData run as production bodies, not copied DSP.
// Fake CChannel injects source records: Qt sockbuf admission tested separately.
static void logical_sources(bool delay,int frame,int virtChannels) {
    CServer s; setup(s,3,frame);
    s.bDelayPan=delay;
    // 0 and 1 are physically connected; index 2 is a logical source of 0.
    s.vecChannels[0].num=2;s.vecChannels[0].bytes=4*frame;
    s.vecChannels[1].num=1;s.vecChannels[1].bytes=2*frame;
    s.vecChannels[2].num=virtChannels;s.vecChannels[2].bytes=2*frame*virtChannels;
    s.vecChannels[0].pans[2]=0.f;
    s.vecChannels[1].pans[2]=1.f;
    s.vecChannels[0].gains[0]=0.f; // self is excluded by stock fader
    s.vecChannels[0].gains[1]=0.f;
    s.vecChannels[0].gains[2]=1.f;
    s.vecChannels[1].gains[0]=0.f;
    s.vecChannels[1].gains[1]=0.f;
    s.vecChannels[1].gains[2]=.5f;
    for(int tick=0;tick<4;++tick) {
        s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(2*frame,0)});
        s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(frame,0)});
        // missing source on tick 2 gives source-only PLC/silence;
        // physical source 0 and other client's decode remain valid.
        s.vecChannels[2].incoming.push_back({tick==2?GS_BUFFER_UNDERRUN:GS_BUFFER_OK,
            pcm(frame*virtChannels,1000+100*tick)});
    }
    for(int tick=0;tick<4;++tick) {
        for(int id=0;id<3;++id) s.DecodeReceiveData(id,3);
        const int n0=s.vecChannels[0].sent.size(),n1=s.vecChannels[1].sent.size();
        // Original mixer is called exactly once per physical recipient.
        s.MixEncodeTransmitData(0,3);
        s.MixEncodeTransmitData(1,3);
        must((int)s.vecChannels[0].sent.size()==n0+1,"single return recipient 0");
        must((int)s.vecChannels[1].sent.size()==n1+1,"single return recipient 1");
        must(s.vecChannels[2].sent.empty(),"no extra return for virtual source");
        if(!delay && tick==1) {
            must(s.vecvecsSendData[0][0]==1100,"virtual gain recipient 0 left");
            must(s.vecvecsSendData[0][1]==0,"virtual pan recipient 0 right");
            must(s.vecvecsSendData[1][0]==550,"independent recipient fader gain");
        }
        if(delay && tick==1) {
            // Pan==0 holds R channel back 62 samples for either mono/stereo;
            // its first sample comes from the prior tick's distinct history.
            must(s.vecvecsSendData[0][0]==1100,"delay L current virtual source");
            must(s.vecvecsSendData[0][1]==1000,"delay R previous virtual source");
        }
        if(!delay && tick==2) {
            must(s.vecvecsSendData[0][0]==0,"missing virtual record PLC raw silence");
            must(s.vecChannels[0].inpos==3,"physical decoded despite missing virtual");
        }
        // Original OnTimer copy extends history to all logical slots.
        for(int id=0;id<3;++id)for(int j=0;j<2*frame;++j)s.vecvecsData2[id][j]=s.vecvecsData[id][j];
        std::cout<<"F05-sources="<<3<<",physical=2,delay="<<delay<<",frame="<<frame
                 <<",virtualchannels="<<virtChannels<<",tick="<<tick
                 <<",out0="<<s.vecvecsSendData[0][0]<<",out1="<<s.vecvecsSendData[1][0]<<"\n";
    }
}

static void multi_source_opus_plc() {
    // Source zero (physical) and another physical client continue decoding
    // while only the virtual source has a missing admitted record.
    int error=0;
    OpusCustomMode* mode=opus_custom_mode_create(48000,64,&error);
    must(mode&&error==OPUS_OK,"Opus64 source mode");
    auto* sourceEncoder=opus_custom_encoder_create(mode,1,&error);
    auto* sourceDecoder=opus_custom_decoder_create(mode,1,&error);
    must(sourceEncoder && sourceDecoder && error==OPUS_OK,"Opus source states");
    CServer s;setup(s,3,64);
    s.vecChannels[2].num=1;
    s.vecChannels[2].compr=CT_OPUS64;
    s.vecChannels[2].bytes=40;
    s.Opus64DecoderMono[2]=sourceDecoder;
    s.vecChannels[0].gains[0]=0;
    s.vecChannels[0].gains[1]=0;
    s.vecChannels[0].gains[2]=1;
    for(int tick=0;tick<6;++tick) {
        s.vecChannels[0].incoming.push_back({GS_BUFFER_OK,pcm(64,400+tick*10)});
        s.vecChannels[1].incoming.push_back({GS_BUFFER_OK,pcm(64,-100)});
        auto compressed=coded(sourceEncoder,64,1,1800+100*tick,40);
        s.vecChannels[2].incoming.push_back({tick==3?GS_BUFFER_UNDERRUN:GS_BUFFER_OK,std::move(compressed)});
    }
    for(int tick=0;tick<6;++tick) {
        for(int id=0;id<3;++id) s.DecodeReceiveData(id,3);
        must(s.vecvecsData[0][0]==400+tick*10,"other physical survives virtual loss");
        if(tick==3)must(s.vecvecsData[2][0]!=0,"virtual Opus PLC, not forced silence");
        s.MixEncodeTransmitData(0,3);
        s.MixEncodeTransmitData(1,3);
        must(s.vecChannels[0].sent.size()==size_t(tick+1),"one return for physical recipient A");
        must(s.vecChannels[1].sent.size()==size_t(tick+1),"one return for physical recipient B");
        must(s.vecChannels[2].sent.empty(),"no virtual socket send on PLC");
    }
    std::cout<<"F05 independent original Opus64 virtual PLC + raw physical continuous PASS\n";
    opus_custom_encoder_destroy(sourceEncoder);
    opus_custom_decoder_destroy(sourceDecoder);
    opus_custom_mode_destroy(mode);
}
int main() {
  try { multi_source_opus_plc();for (auto frame:{64,128}) for (auto ch:{1,2}) for (auto delay:{false,true}) logical_sources(delay,frame,ch); raw_mix_64();raw_mix_128_clip();raw_loss();for(int frame : {64,128}) {history_test(false,1,frame);history_test(true,1,frame);history_test(false,2,frame);history_test(true,2,frame);}
      opus_roundtrip(128,1,false);opus_roundtrip(128,2,true);
      opus_roundtrip(64,1,false);opus_roundtrip(64,2,true);
      std::cerr<<"PASS stock server-method raw/PCM/packet/delay-pan fixture; NOT a Qt lifecycle test\n";return 0; }
  catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
