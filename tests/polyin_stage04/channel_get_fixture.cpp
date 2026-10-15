// Qt-free test host wrapping actual CChannel::GetData source body.
#include "polyinframe.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <iostream>
#include <stdexcept>
#define emit
// STOCK_GET_ENUM
struct FakeProtocol {int resets=0;void Reset(){++resets;}};
class CChannel {
public:
    CNetBufWithStats SockBuf;
    std::unique_ptr<PolyIn::StockFrameBuffer> PolyFrameBuffer;
    CChannel* pPolyInOwner=nullptr;
    int iPolyInFrameCursor=0,iPolyInFrameCount=0;
    std::array<PolyIn::FrameView,2> PolyInFrames{};
    uint16_t iPolyInSource=0;
    int eAudioCompressionType=3,iNumAudioChannels=1,iPolyInChannels=1,iPolyInEncodedBytes=8,ePolyInCodec=3;
    std::atomic<int> iConTimeOut=9999;
    int iAudioFrameSizeSamples=64, disconnectedCount=0;
    std::mutex MutexSocketBuf;
    FakeProtocol Protocol;
    void ResetNetworkTransportProperties(){}
    void Disconnected(){++disconnectedCount;}
    EGetDataStat GetData(CVector<uint8_t>& bytes,const int size);
};
// STOCK_CHANNEL_GET
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(0)
static std::vector<std::vector<uint8_t>> make(int frame,uint64_t gen,bool missingZero) {
  std::vector<PolyIn::SourceRecord> records;
  if(!missingZero) records.push_back({0,3,1,std::vector<uint8_t>(8,uint8_t(30+frame))});
  records.push_back({1,3,1,std::vector<uint8_t>(8,uint8_t(90+frame))});
  std::vector<std::vector<uint8_t>> packets;
  CHECK(PolyIn::PackFrame(frame,gen,records,1200,packets));return packets;
}
int main() {
 try {
  CChannel physical,virtualOne,virtualTwo;
  physical.SockBuf.SetUseDoubleSystemFrameSize(false);
  physical.PolyFrameBuffer=std::make_unique<PolyIn::StockFrameBuffer>(physical.SockBuf);
  physical.PolyFrameBuffer->Init(4343,6);
  virtualOne.pPolyInOwner=&physical;virtualOne.iPolyInSource=1;
  virtualTwo.pPolyInOwner=&physical;virtualTwo.iPolyInSource=2;
  // Admit many frames into actual production F02 + stock window.
  for(int frame=0;frame<6;++frame)for(const auto& packet:make(frame,4343,frame%3==0))
    CHECK(physical.PolyFrameBuffer->Receive(packet.data(),packet.size()));
  CVector<uint8_t> zero(8),one(8),two(8);
  int present=0,missingZero=0,missingTwo=0;
  for(int tick=0;tick<95;++tick) {
    for(const auto& packet:make(tick+6,4343,(tick+6)%3==0))
      CHECK(physical.PolyFrameBuffer->Receive(packet.data(),packet.size()));
    physical.iPolyInFrameCount=0;physical.iPolyInFrameCursor=0;
    virtualOne.iPolyInFrameCursor=virtualTwo.iPolyInFrameCursor=0;
    int before=physical.iConTimeOut;
    auto base=physical.GetData(zero,8);
    auto a=virtualOne.GetData(one,8);
    auto b=virtualTwo.GetData(two,8);
    CHECK(physical.iConTimeOut==before-64); // one physical timeout decrement, not three
    if(a==GS_BUFFER_OK) {
      ++present;
      CHECK(b==GS_BUFFER_UNDERRUN);
      if(base==GS_BUFFER_UNDERRUN) ++missingZero;
    }
    if(b==GS_BUFFER_UNDERRUN)++missingTwo;
  }
  std::cerr<<"DEBUG present="<<present<<" missing0="<<missingZero<<" missing2="<<missingTwo<<"\n";
  CHECK(present>20 && missingZero>4 && missingTwo==95);
  CHECK(physical.PolyFrameBuffer->Stats().stockGets==95); // one stock read/tick
  CHECK(virtualOne.iConTimeOut==9999 && virtualTwo.iConTimeOut==9999);
  CHECK(physical.disconnectedCount==0);
  std::cout<<"PASS actual CChannel::GetData + production stock buffer/F02: virtual present="<<present
    <<" missing source0="<<missingZero<<" absent source2="<<missingTwo
    <<" physical gets="<<physical.PolyFrameBuffer->Stats().stockGets<<"\n";
  // Real legacy branch (same source body) still calls stock.Get and timeout.
  CChannel legacy;
  legacy.SockBuf.SetUseDoubleSystemFrameSize(false);
  legacy.SockBuf.Init(8,6,true);
  legacy.iConTimeOut=64;
  CHECK(legacy.GetData(zero,8)==GS_CHAN_NOW_DISCONNECTED);
  CHECK(legacy.disconnectedCount==1 && legacy.Protocol.resets==1);
  CHECK(legacy.GetData(zero,8)==GS_CHAN_NOT_CONNECTED);
  std::cout<<"PASS stock timeout/disconnect envelope retained\n";
 } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
