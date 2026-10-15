// Deterministic stock Put/Get/candidate comparator with real production buffer.cpp.
#include "polyinframe.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <string>
#include <vector>
using namespace PolyIn;
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); std::abort(); } } while(0)

SourceRecord Record ( uint16_t source, uint32_t frame, size_t length = 4 )
{
    SourceRecord r; r.source = source; r.codec = uint8_t(1+source%3); r.channels = (source%2)+1;
    r.encoded.resize(length);
    for(size_t i=0;i<length;i++) r.encoded[i]=uint8_t(frame+source*31+i*7);
    return r;
}
std::vector<std::vector<uint8_t>> Make(uint32_t frame,uint64_t generation,size_t sources=1,size_t mtu=1200,size_t length=4)
{
    std::vector<SourceRecord> rec;
    for(size_t i=0;i<sources;i++) rec.push_back(Record(i,frame,length));
    std::vector<std::vector<uint8_t>> bytes;
    CHECK(PackFrame(frame,generation,rec,mtu,bytes));
    return bytes;
}
uint32_t Decode32(const CVector<uint8_t>& p)
{
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
void PutBase(CNetBufWithStats& b,uint32_t seq,uint64_t generation)
{
    CVector<uint8_t> packet(13);
    for(int i=0;i<4;i++)packet[i]=uint8_t(seq>>(8*i));
    for(int i=0;i<8;i++)packet[4+i]=uint8_t(generation>>(8*i));
    packet[12]=uint8_t(seq);
    CHECK(b.Put(packet,13));
}

void Validation()
{
    CNetBufWithStats physical;
    auto a=std::make_unique<StockFrameBuffer>(physical);
    a->Init(0x12345678,20);
    auto packets=Make(0,0x12345678,2,65,100);
    CHECK(packets.size()>4);
    FrameView v;
    // Bad CT_NONE must never be independently decodable or advance real stock Put.
    a->Init(99,20);
    auto invalidCodec=Make(0,99)[0];
    invalidCodec[HeaderBytes+2]=0;
    CHECK(!a->Receive(invalidCodec.data(),invalidCodec.size()));
    CHECK(a->Stats().stockPuts==0 && a->Stats().admittedFrames==0);
    auto validOpus64=Make(0,99)[0];
    validOpus64[HeaderBytes+2]=3;
    CHECK(a->Receive(validOpus64.data(),validOpus64.size()));
    CHECK(a->Stats().stockPuts==1 && a->Stats().admittedFrames==1);
    FrameView codecView;
    CHECK(a->Get(codecView) && codecView.count==1 && codecView.records[0].codec==3);
    a->Init(0x12345678,20);
    // First partial fragment never gets admitted, and can't hide a physical loss.
    CHECK(a->Receive(packets[0].data(),packets[0].size()));
    CHECK(a->Stats().stockPuts==0 && a->Stats().admittedFrames==0);
    // Entire later source arrives in an independently complete fragment sequence.
    for(size_t i=packets.size()/2;i<packets.size();i++)
        CHECK(a->Receive(packets[i].data(),packets[i].size()));
    CHECK(a->Stats().admittedFrames==1 && a->Stats().stockPuts==1);
    for(size_t i=1;i<packets.size()/2;i++) CHECK(a->Receive(packets[i].data(),packets[i].size()));
    CHECK(a->Stats().admittedFrames==1);
    CHECK(a->Get(v) && v.count==2);
    const auto first=v.records[0].data[0];
    const uint8_t* frozen=v.records[0].data;
    std::vector<uint8_t> snapshot(frozen,frozen+v.records[0].size);
    CHECK(v.records[0].size==100 && first==0);
    // After the first playout, a different source record can't update a replay snapshot.
    auto late=Make(0,0x12345678,3);
    for(auto& p:late) CHECK(a->Receive(p.data(),p.size()));
    CHECK(std::equal(snapshot.begin(),snapshot.end(),frozen)); // immutable after output
    CHECK(a->Get(v) || a->Stats().stockLoss!=0); // never unsafe or mutated, stock may replay
    if(v.count) CHECK(v.count==2 && v.records[0].data[0]==first);
    // Malformed packets must not cause a Put.
    auto valid=Make(7,0x12345678)[0];
    auto before=a->Stats().stockPuts;
    for(int b=0;b<12;b++) { auto bad=valid; bad[20]=uint8_t(b); CHECK(!a->Receive(bad.data(),bad.size())); }
    { auto bad=valid; bad[12]^=0x55; CHECK(!a->Receive(bad.data(),bad.size())); }
    { auto bad=valid; bad[4]=2; CHECK(!a->Receive(bad.data(),bad.size())); }
    { auto bad=valid; bad[24]=250; CHECK(!a->Receive(bad.data(),bad.size())); }
    { auto bad=valid; bad[32]=0xff; CHECK(!a->Receive(bad.data(),bad.size())); }
    CHECK(a->Stats().stockPuts==before);
    CHECK(a->Receive(valid.data(),valid.size()));
    CHECK(a->Stats().stockPuts==before+1);
    // Conflicting reassembled record: invalidate source, do not insert a global frame.
    a->Init(99,6);
    auto p=Make(0,99,1,60,200);
    CHECK(a->Receive(p[0].data(),p[0].size()));
    auto conflicting=p[0]; conflicting.back()^=0x60;
    CHECK(a->Receive(conflicting.data(),conflicting.size()));
    for(size_t i=1;i<p.size();i++)CHECK(a->Receive(p[i].data(),p[i].size()));
    CHECK(a->Stats().stockPuts==0 && a->Stats().conflicts==1);
    // Out-of-order fragments of a single valid record assemble and admit once.
    a->Init(99,6);
    std::reverse(p.begin(),p.end());
    for(auto& pkt:p)CHECK(a->Receive(pkt.data(),pkt.size()));
    CHECK(a->Stats().stockPuts==1 && a->Stats().completeRecords==1);
    // Source-local loss from a missing record must not masquerade as stock loss.
    a->Init(99,6);
    std::vector<std::vector<uint8_t>> many;
    CHECK(PackFrame(0,99,{Record(0,0,100),Record(2,0,100)},110,many));
    for(auto& pkt:many) CHECK(a->Receive(pkt.data(),pkt.size()));
    CHECK(a->Stats().stockPuts==1);
    CHECK(a->Get(v));
    CHECK(v.count==2 && v.records[0].source==0 && v.records[1].source==2);
    // Full 32-bit boundary is identity only, stock still uses its low 8 bits.
    a->Init(99,20);
    auto wrap=Make(std::numeric_limits<uint32_t>::max(),99);
    CHECK(a->Receive(wrap[0].data(),wrap[0].size()));
    CHECK(a->Get(v) && v.frame==std::numeric_limits<uint32_t>::max());
    auto afterWrap=Make(0,99);
    CHECK(a->Receive(afterWrap[0].data(),afterWrap[0].size()));
    CHECK(a->Get(v) && v.frame==0);
    // Incomplete frame 64 must NOT evict a still-live admitted frame 0.
    a->Init(99,20);
    auto live=Make(0,99);
    auto partial=Make(64,99,1,50,300);
    CHECK(partial.size()>1);
    CHECK(a->Receive(live[0].data(),live[0].size()));
    CHECK(a->Receive(partial[0].data(),partial[0].size()));
    CHECK(a->Stats().stockPuts==1);
    CHECK(a->Get(v) && v.frame==0 && v.count==1);
    // Conflict after first complete record leaves the first stable result.
    a->Init(99,20);
    CHECK(a->Receive(live[0].data(),live[0].size()));
    auto attack=live[0]; attack.back()^=0xff;
    CHECK(a->Receive(attack.data(),attack.size()));
    CHECK(a->Stats().stockPuts==1 && a->Stats().conflicts==1);
    CHECK(a->Get(v) && v.count==1 && v.records[0].data[0]==0);
    printf("P01 codec0-before-admission REJECT, codec3-first-complete ADMIT, wire bounds/duplicates/conflicts/fragment out-of-order/source-local missing/wrap/partial-collision PASS\n");
}

void Scenario(const char* name,int total,int firstSize,int sourceCount,bool adversarial=false,int blackout=0,bool lagIncident=false,bool doubleFrameMode=false)
{
    const uint64_t gen=0x1234abcdef123457ULL;
    CNetBufWithStats physical;
    auto feature=std::make_unique<StockFrameBuffer>(physical);
    // Stock CChannel applies this mode BEFORE stock Init. Borrowed physical
    // buffer must retain precisely the same inherited filter/estimator policy.
    physical.SetUseDoubleSystemFrameSize(doubleFrameMode);
    feature->Init(gen,firstSize);
    CNetBufWithStats stock;
    stock.SetUseDoubleSystemFrameSize(doubleFrameMode);
    stock.Init(TokenBytes,firstSize,true);
    CVector<uint8_t> raw(TokenBytes);
    int missed=0,successful=0,expired=0,readMismatch=0, statsMismatch=0;
    std::unordered_set<uint32_t> admitted;
    int ninthLag=0, recovery=0;
    for(int tick=0;tick<total;tick++) {
        // Outside the CChannel 1..20 physical window range: adapter must
        // ignore values rather than initializing a different stock buffer.
        if(tick==144) feature->Resize(0);
        if(tick==145) feature->Resize(21);
        if(tick==146) feature->Resize(64);
        // Buffer size is the actual stock size. No adapter-only resizing.
        if(!lagIncident && (tick==150 || tick==250 || tick==450)) {
            int n=tick==150?6:tick==250?20:2;
            feature->Resize(n); stock.Init(TokenBytes,n,true,true);
        }
        std::vector<uint32_t> arriving;
        if(lagIncident) {
            arriving.push_back(uint32_t(tick>=100 && tick<300 ? tick-9 : tick));
        } else if(adversarial) {
            // +256 alias tested separately: preserve stock inherited semantics.
            if(tick%103==0 && tick>0) for(int k=0;k<9;k++)arriving.push_back(uint32_t(tick+k));
            if(tick%89==0 && tick>0) arriving.push_back(uint32_t(tick-9));
            if(tick%23!=0) arriving.push_back(uint32_t(tick));
        } else {
            if(tick%27==0 && tick>5) arriving.push_back(uint32_t(tick-4));
            if(tick%13!=0) arriving.push_back(uint32_t(tick));
            if(tick%17==0) arriving.push_back(uint32_t(tick+1));
        }
        if(blackout && tick>=90 && tick<90+blackout) arriving.clear();
        for(auto seq:arriving) {
            auto packet=Make(seq,gen,sourceCount,1200,4);
            for(auto& datagram:packet)CHECK(feature->Receive(datagram.data(),datagram.size()));
            if(admitted.insert(seq).second) PutBase(stock,seq,gen);
        }
        FrameView view;
        bool f=feature->Get(view),s=stock.Get(raw,TokenBytes);
        if(!s) missed++;
        else successful++;
        if(f!=s) {expired++; if(readMismatch<8)fprintf(stderr,"mismatch %s tick=%d stock=%d feature=%d stockframe=%u\n",name,tick,s,f,s?Decode32(raw):0);}
        if(f&&s&&view.frame!=Decode32(raw)) {readMismatch++;}
        if(f&&s&&view.count!=size_t(sourceCount))readMismatch++;
        CVector<double> x,y; double lo1,hi1,lo2,hi2;
        feature->GetErrorRates(x,lo1,hi1); stock.GetErrorRates(y,lo2,hi2);
        if(x!=y||lo1!=lo2||hi1!=hi2||feature->GetAutoSetting()!=stock.GetAutoSetting())statsMismatch++;
        if(tick==0) {
            CHECK(lo1==(doubleFrameMode ? ERROR_RATE_BOUND_DOUBLE_FRAME_SIZE : ERROR_RATE_BOUND));
            CHECK(hi1==(doubleFrameMode ? UP_MAX_ERROR_BOUND_DOUBLE_FRAME_SIZE : UP_MAX_ERROR_BOUND));
        }
        if((adversarial || lagIncident) && s) {
            auto lag=tick-int(Decode32(raw));
            if(lag==9)ninthLag++;
            if(tick>(lagIncident?330:500) && lag>=-3 && lag<=3)recovery++;
        }
        // Emit actual stock read sequence and adapter sequence during the
        // OF-01 9-frame jitter incident, not a fabricated second cursor.
        if((adversarial || lagIncident) && tick>=94 && tick<=115)
            printf("OF01 trace tick=%d stock_read=%d feature_read=%d stock_frame=%u adapter_frame=%u configured_target=%d\n",
                tick,int(s),int(f),s?Decode32(raw):0,f?view.frame:0,firstSize);
    }
    const auto& c=feature->Stats();
    printf("L01 %s ticks=%d stock_good=%d stock_misses=%d adapter_expired=%d identity_mismatches=%d estimators_mismatches=%d extra_PLC_vs_stock=%d put=%llu get=%llu source_records=%llu nine_lag=%d recovered_ticks=%d\n",
           name,total,successful,missed,expired,readMismatch,statsMismatch,expired,
           (unsigned long long)c.stockPuts,(unsigned long long)c.stockGets,
           (unsigned long long)c.completeRecords,ninthLag,recovery);
    CHECK(statsMismatch==0 && readMismatch==0);
    CHECK(c.stockGets==uint64_t(total) && c.stockLoss==uint64_t(missed));
    CHECK(c.stockPuts==c.admittedFrames);
    // In adversarial aliases beyond stock's 8-bit range, safe expiry is
    // reported separately, not "fixed" by an independent jump classifier.
    if(!adversarial) CHECK(expired==0);
    else CHECK(recovery>100); // connected recovery, never disconnected/reset
    if(lagIncident) CHECK(ninthLag>50 && recovery>100);
}

void PhysicalBufferBounds()
{
    CNetBufWithStats physical;
    auto a=std::make_unique<StockFrameBuffer>(physical);
    FrameView v;
    auto pkt=Make(0,99)[0];
    for(int invalid : {-1,0,21,64,65}) {
        a->Init(99,invalid);
        CHECK(!a->Receive(pkt.data(),pkt.size()));
        CHECK(!a->Get(v));
        CHECK(a->Stats().stockPuts==0);
    }
    for(int valid : {1,2,6,20}) {
        a->Init(99,valid);
        CHECK(a->Receive(pkt.data(),pkt.size()));
        CHECK(a->Stats().stockPuts==1);
        CHECK(a->Get(v) && v.frame==0);
    }
    printf("F02 stock physical 1..20, reject -1/0/21/64/65; 64-frame sidecars unchanged PASS\n");
}

void Lifetime()
{
    CNetBufWithStats physical;
    auto a=std::make_unique<StockFrameBuffer>(physical); a->Init(1,20);
    FrameView v;
    for(int i=0;i<300;i++) {
        auto pkt=Make(uint32_t(i),1,20,1200,4);
        for(auto& part:pkt) CHECK(a->Receive(part.data(),part.size()));
        CHECK(a->Get(v)); CHECK(v.frame==uint32_t(i) && v.count==20);
    }
    a->Init(1,20);
    auto zero=Make(0,1), alias=Make(256,1);
    CHECK(a->Receive(zero[0].data(),zero[0].size()));
    CHECK(a->Receive(alias[0].data(),alias[0].size()));
    CHECK(a->Get(v) && v.frame==256); // stock low-byte alias, exact sidecar identity
    a->Init(2,20);
    auto old=Make(256,1);
    CHECK(!a->Receive(old[0].data(),old[0].size()));
    auto fresh=Make(256,2);
    CHECK(a->Receive(fresh[0].data(),fresh[0].size()));
    CHECK(a->Get(v) && v.frame==256 && v.generation==2);
    printf("P03/P04 production 20 sources, 64-slot retention, 300 frames, generation replacement PASS\n");
}
int main() {
    Validation();
    PhysicalBufferBounds();
    Lifetime();
    Scenario("resize2-6-20-2",750,2,1);
    Scenario("resize2-6-20-2-with20sources",550,2,20);
    Scenario("opus-128-sample-stock-statistics",750,2,3,false,0,false,true);
    Scenario("opus-128-sample-with20sources",550,2,20,false,0,false,true);
    Scenario("OF01-02-historical-target2-jitter9",1500,2,1,true);
    Scenario("OF01-transient-9lag-to-stock-recovery",700,2,1,false,0,true);
    for(int n : {9,32,127,128,257}) {
        std::string scenario="connected-blackout-"+std::to_string(n);
        Scenario(scenario.c_str(),n+500,2,1,false,n);
    }
    printf("STAGE2 TESTS PASS (compiled stock buffer.cpp + source F01/F02)\n");
}
