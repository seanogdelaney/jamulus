// Actual CChannel::OnNetTranspPropsReceived, EnablePolyInPhysical and GetData
// source bodies linked to the real production stock CNetBufWithStats and F02.
// Small Qt/protocol stand-ins substitute only external Qt wiring.
#include "polyinframe.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <iostream>
#include <stdexcept>
#define emit
#define CT_OPUS 2
#define CT_OPUS64 3
#define NF_WITH_COUNTER 1
#define FADE_IN_NUM_FRAMES 2250
#define FADE_IN_NUM_FRAMES_DBLE_FRAMESIZE 1125
#define SYSTEM_FRAME_SIZE_SAMPLES 64
#define DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES 128
// STOCK_GET_ENUM
struct CNetworkTransportProps {
    int eAudioCodingType=CT_OPUS64;
    int iNumAudioChannels=1;
    int iBlockSizeFact=1;
    int iBaseNetworkPacketSize=9;
    int eFlags=NF_WITH_COUNTER;
};
struct FakeProtocol {int resets=0,unsupported=0;void Reset(){++resets;}void CreateOpusSupportedMes(){++unsupported;}};
struct FakeConvBuf {int resets=0; void Init(int,bool){++resets;}};
struct QMutexLocker {std::mutex* m; explicit QMutexLocker(std::mutex* p):m(p){m->lock();}~QMutexLocker(){m->unlock();}};
class CChannel {
public:
    CNetBufWithStats SockBuf;
    std::unique_ptr<PolyIn::StockFrameBuffer> PolyFrameBuffer;
    CChannel* pPolyInOwner=nullptr;
    int iPolyInFrameCursor=0,iPolyInFrameCount=0;
    std::array<PolyIn::FrameView,2> PolyInFrames{};
    uint16_t iPolyInSource=0;
    int eAudioCompressionType=CT_OPUS64,iNumAudioChannels=1,iPolyInChannels=1,iPolyInEncodedBytes=8,ePolyInCodec=CT_OPUS64;
    std::atomic<int> iConTimeOut=400000;
    int iAudioFrameSizeSamples=64,disconnectedCount=0;
    std::mutex MutexSocketBuf, Mutex, MutexConvBuf;
    FakeProtocol Protocol;
    FakeConvBuf ConvBuf;
    bool bIsServer=true,bUseSequenceNumber=true;
    int iNetwFrameSizeFact=1, iNetwFrameSize=9,iCeltNumCodedBytes=8;
    int iFadeInCntMax=2250, iFadeInCnt=0, iCurSockBufNumFrames=6;
    void ResetNetworkTransportProperties(){}
    void Disconnected(){++disconnectedCount;}
    EGetDataStat GetData(CVector<uint8_t>& bytes,const int size);
    void OnNetTranspPropsReceived(CNetworkTransportProps props);
    bool EnablePolyInPhysical(uint64_t gen);
};
// STOCK_CHANNEL_GET
// STOCK_TRANSPORT_PROPS
// STOCK_ENABLE_POLYIN
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(0)
static std::vector<std::vector<uint8_t>> make(int frame,uint64_t gen,bool missingZero,int codec) {
  std::vector<PolyIn::SourceRecord> records;
  if(!missingZero) records.push_back({0,uint8_t(codec),1,std::vector<uint8_t>(8,uint8_t(frame))});
  records.push_back({1,uint8_t(codec),1,std::vector<uint8_t>(8,uint8_t(frame+33))});
  std::vector<std::vector<uint8_t>> packets;
  CHECK(PolyIn::PackFrame(frame,gen,records,1200,packets));return packets;
}
static void exercise_poly(bool restart,int codec){
  CChannel physical, virtualOne;
  CNetworkTransportProps props;
  props.eAudioCodingType=codec;
  physical.OnNetTranspPropsReceived(props);
  CHECK(physical.EnablePolyInPhysical(707));
  virtualOne.ePolyInCodec=codec;
  virtualOne.pPolyInOwner=&physical;
  virtualOne.iPolyInSource=1;
  CVector<uint8_t> zero(8),one(8);
  int okBefore=0,okAfter=0,virtualAfter=0,missing0After=0;
  int oldCounter=physical.iConTimeOut;
  for(int frame=0;frame<210;++frame){
    if(restart && frame==96){
      // Exact real CChannel properties handler, same codec/format/session.
      physical.OnNetTranspPropsReceived(props);
      CHECK(physical.PolyFrameBuffer->Generation()==707);
      CHECK(physical.ConvBuf.resets==2);
    }
    for(const auto& pkt:make(frame,707,frame%4==0,codec)) CHECK(physical.PolyFrameBuffer->Receive(pkt.data(),pkt.size()));
    physical.iPolyInFrameCount=physical.iPolyInFrameCursor=virtualOne.iPolyInFrameCursor=0;
    const auto base=physical.GetData(zero,8);
    const auto virt=virtualOne.GetData(one,8);
    if(base==GS_BUFFER_OK){if(frame<96)++okBefore;else ++okAfter;}
    if(frame>=96){if(virt==GS_BUFFER_OK)++virtualAfter;
      if(base==GS_BUFFER_UNDERRUN && virt==GS_BUFFER_OK)++missing0After;}
    CHECK(physical.disconnectedCount==0);
    CHECK(physical.iConTimeOut==oldCounter-(frame+1)*physical.iAudioFrameSizeSamples);
  }
  if(restart) CHECK(okBefore>40 && okAfter>30 && virtualAfter>70 && missing0After>8);
  else CHECK(okBefore>40 && okAfter>30 && virtualAfter>70);
  std::cout<<"PASS real transport handler with poly-in codec="<<codec<<" restart="<<restart
           <<" source0 before="<<okBefore<<" after="<<okAfter
           <<" source1 after="<<virtualAfter<<" partial="<<missing0After
           <<" stock gets="<<physical.PolyFrameBuffer->Stats().stockGets<<"\n";
}
static void exercise_legacy(){
  // The same *actual* transport handler must retain ordinary stock Init.
  CChannel ordinary;
  CNetworkTransportProps props;
  ordinary.OnNetTranspPropsReceived(props);
  CHECK(!ordinary.PolyFrameBuffer);
  CVector<uint8_t> input(9),output(8);
  int before=0,after=0;
  for(int i=0;i<210;++i){
    if(i==96)ordinary.OnNetTranspPropsReceived(props);
    for(int k=0;k<8;++k)input[k]=uint8_t(i);
    input[8]=uint8_t(i);
    CHECK(ordinary.SockBuf.Put(input,9));
    auto s=ordinary.GetData(output,8);
    if(s==GS_BUFFER_OK){if(i<96)++before;else ++after;}
  }
  CHECK(before>40 && after>40 && ordinary.ConvBuf.resets==2);
  std::cout<<"PASS actual unchanged ordinary stock handler before="<<before<<" after="<<after<<"\n";
}
int main(){try{exercise_poly(false,CT_OPUS64);exercise_poly(true,CT_OPUS64);
  exercise_poly(false,CT_OPUS);exercise_poly(true,CT_OPUS);exercise_legacy();}
  catch(const std::exception& e){std::cerr<<"FAIL transport props "<<e.what()<<"\n";return 1;}}
