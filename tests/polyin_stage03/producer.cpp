#include "polyinproducer.h"
#include "polyinsources.h"
#include "polyinwire.h"
#include "stockclientencode.h"
#include <opus_custom.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <vector>
#include <array>
#include <numeric>
using namespace PolyIn;
// Reassemble F01 fragments from the ONE physical frame, with no extra socket,
// receive buffer or encoded-source-specific clock. Verify complete, unique bytes.
struct Rebuilt {
    uint8_t codec=0, channels=0;
    std::vector<uint8_t> bytes;
};
static std::vector<Rebuilt> Collect(const std::vector<std::vector<uint8_t>>& datagrams,
                                    uint32_t frame, uint64_t epoch, int count)
{
    std::vector<Rebuilt> result(count);
    std::vector<std::vector<bool>> seen(count);
    for(const auto& datagram: datagrams) {
        assert(datagram.size()<=MaxDatagram);
        ParsedDatagram parsed;
        assert(ParseDatagram(datagram.data(),datagram.size(),parsed));
        assert(parsed.frame==frame && parsed.generation==epoch);
        for(size_t f=0;f<parsed.count;f++) {
            const auto& part=parsed.fragments[f];
            assert(part.source<count);
            auto& entry=result[part.source];
            if(entry.bytes.empty()) {
                entry.bytes.resize(part.total);
                seen[part.source].resize(part.total,false);
                entry.codec=part.codec;
                entry.channels=part.channels;
            }
            assert(entry.bytes.size()==part.total && entry.codec==part.codec && entry.channels==part.channels);
            for(size_t i=0;i<part.size;i++) {
                assert(!seen[part.source][part.offset+i]);
                seen[part.source][part.offset+i]=true;
                entry.bytes[part.offset+i]=part.data[i];
            }
        }
    }
    for(int k=0;k<count;k++) {
        assert(!result[k].bytes.empty());
        assert(std::all_of(seen[k].begin(),seen[k].end(),[](bool present){return present;}));
    }
    return result;
}
static int16_t Value(int channel,int sample,int tick)
{
    return int16_t(((channel+5)*307+(sample+11)*101+tick*83)%24000-12000);
}
static std::vector<int16_t> Interleaved(int count,int samples,int tick)
{
    std::vector<int16_t> pcm(count*samples);
    for(int i=0;i<samples;i++) for(int c=0;c<count;c++)
        pcm[i*count+c]=Value(c,i,tick);
    return pcm;
}
static int16_t Decode16(const std::vector<uint8_t>& bytes, size_t offset)
{
    return int16_t(uint16_t(bytes[offset]) | (uint16_t(bytes[offset+1])<<8));
}
static void MultichannelTest()
{
    for(int frames : {64,128}) for(int channels : {3,6,20}) {
        const uint8_t codec=frames==64?3:2;
        const int codedBytes=frames==64?22:45;
        SourceProducer raw, opus;
        std::vector<InputSpec> raws, compressed;
        for(int k=0;k<channels;k++) {
            raws.push_back({uint16_t(k),InputRoute::Left,codec,true,frames*2,uint16_t(k),0});
            compressed.push_back({uint16_t(k),InputRoute::Left,codec,false,codedBytes,uint16_t(k),0});
        }
        assert(raw.Configure(frames,channels,raws));
        assert(opus.Configure(frames,channels,compressed));
        assert(raw.Count()==size_t(channels) && opus.Count()==size_t(channels));
        int err=0;
        OpusCustomMode* mode=opus_custom_mode_create(48000,frames,&err);
        assert(mode && err==OPUS_OK);
        std::vector<OpusCustomEncoder*> reference;
        for(int k=0;k<channels;k++) {
            OpusCustomEncoder* enc=opus_custom_encoder_create(mode,1,&err);
            assert(enc && err==OPUS_OK);
            // Precisely the pre-extraction stock CClient CT_OPUS / OPUS64 controls.
            opus_custom_encoder_ctl(enc,OPUS_SET_VBR(0));
            if(frames==64) opus_custom_encoder_ctl(enc,OPUS_SET_PACKET_LOSS_PERC(35));
            opus_custom_encoder_ctl(enc,OPUS_SET_APPLICATION(OPUS_APPLICATION_RESTRICTED_LOWDELAY));
            if(frames==128) opus_custom_encoder_ctl(enc,OPUS_SET_COMPLEXITY(1));
            opus_custom_encoder_ctl(enc,OPUS_SET_BITRATE((48000*codedBytes*8)/frames));
            reference.push_back(enc);
        }
        for(int tick=0;tick<9;tick++) {
            const auto pcm=Interleaved(channels,frames,tick);
            std::vector<std::vector<uint8_t>> datagrams;
            assert(raw.Encode(pcm.data(),pcm.size(),false,tick,105,1200,datagrams));
            const auto rawData=Collect(datagrams,tick,105,channels);
            for(int k=0;k<channels;k++) {
                assert(rawData[k].channels==1 && rawData[k].codec==codec);
                assert(rawData[k].bytes.size()==size_t(frames*2));
                for(int i=0;i<frames;i++) assert(Decode16(rawData[k].bytes,2*i)==Value(k,i,tick));
            }
            assert(opus.Encode(pcm.data(),pcm.size(),false,tick,105,1200,datagrams));
            const auto opusData=Collect(datagrams,tick,105,channels);
            for(int k=0;k<channels;k++) {
                std::vector<int16_t> mono(frames);
                for(int i=0;i<frames;i++) mono[i]=Value(k,i,tick);
                std::vector<uint8_t> expect(codedBytes);
                assert(opus_custom_encode(reference[k],mono.data(),frames,expect.data(),codedBytes)>=0);
                assert(opusData[k].channels==1 && opusData[k].codec==codec);
                assert(opusData[k].bytes==expect); // source-local adaptive encoder parity
            }
        }
        const auto pcm=Interleaved(channels,frames,11);
        std::vector<std::vector<uint8_t>> packets;
        assert(raw.Encode(pcm.data(),pcm.size(),true,555,105,1200,packets));
        for(const auto& result:Collect(packets,555,105,channels))
            assert(std::all_of(result.bytes.begin(),result.bytes.end(),[](uint8_t x){return x==0;}));
        assert(!raw.Encode(pcm.data(),pcm.size()-1,false,555,105,1200,packets));
        assert(packets.empty());
        for(auto* e: reference) opus_custom_encoder_destroy(e);
        opus_custom_mode_destroy(mode);
    }
    // A mono, stereo pair and a 2-input mix all read independent channels
    // from exactly one six-channel callback frame; legacy (0,1) is retained.
    for(int frames : {64,128}) {
        const uint8_t codec=frames==64?3:2;
        SourceProducer p;
        assert(p.Configure(frames,6,{{0,InputRoute::Left,codec,true,frames*2,0,1},
                                    {1,InputRoute::Right,codec,true,frames*2,0,1},
                                    {2,InputRoute::Stereo,codec,true,frames*4,2,4},
                                    {3,InputRoute::MonoMix,codec,true,frames*2,3,5}}));
        const auto pcm=Interleaved(6,frames,0);
        std::vector<std::vector<uint8_t>> packets;
        assert(p.Encode(pcm.data(),pcm.size(),false,110,7,1200,packets));
        const auto a=Collect(packets,110,7,4);
        for(int i=0;i<frames;i++) {
            assert(Decode16(a[0].bytes,2*i)==Value(0,i,0));
            assert(Decode16(a[1].bytes,2*i)==Value(1,i,0));
            assert(Decode16(a[2].bytes,4*i)==Value(2,i,0));
            assert(Decode16(a[2].bytes,4*i+2)==Value(4,i,0));
            assert(Decode16(a[3].bytes,2*i)==(int32_t(Value(3,i,0))+Value(5,i,0))/2);
        }
        assert(a[2].channels==2);
    }
    std::cout<<"F04 real N-capture routes: 3/6/20 channels, 64/128 frames, raw/Opus exact parity, stereo/mix, mute, one packet identity PASS\n";
}
// The actual F03 and F04 components must admit and emit the same source bank.
// This is NOT a Qt capture, server mixer or socket test.
static void CrossComponentContractTest()
{
    SourceSlots owner(8);
    SourceProducer producer;
    assert(owner.AdmitPhysical(0, 1, "")); // valid stock default metadata
    assert(!owner.Reserve(0, 1, 1, {{1, "mic"}}));
    assert(!producer.Configure(64, 2, {{1, InputRoute::Left, 3, true, 128, 0, 0}}));
    assert(producer.Count() == 0);
    const auto reservation = owner.Reserve(0, 1, 1, {{0, ""}, {1, "mic"}});
    assert(reservation && owner.Promote(reservation, 0, 1));
    assert(producer.Configure(64, 2, {{0, InputRoute::Left, 3, true, 128, 0, 1},
                                     {1, InputRoute::Right, 3, true, 128, 0, 1}}));
    std::vector<int16_t> pcm(128, 100);
    std::vector<std::vector<uint8_t>> datagrams;
    assert(producer.Encode(pcm.data(), pcm.size(), false, 4, 1, 1200, datagrams));
    assert(datagrams.size() == 1);
    ParsedDatagram parsed;
    assert(ParseDatagram(datagrams[0].data(), datagrams[0].size(), parsed));
    assert(parsed.count == 2 && parsed.fragments[0].source == 0 && parsed.fragments[1].source == 1);
    SourceSlots::Identity identity;
    for(size_t i=0;i<parsed.count;i++)
        assert(owner.Resolve(0, parsed.generation, parsed.fragments[i].source, identity));
    assert(owner.RetirePhysical(0, 1));
    std::cout << "F03/F04 cross-component source-zero admission and exact emitted bank PASS\n";
}

static void MalformedAndChurnTest()
{
    SourceProducer p, reference;
    const InputSpec a{0,InputRoute::Left,2,false,45,0,1};
    const InputSpec b{1,InputRoute::Left,2,false,45,1,0};
    const InputSpec c{2,InputRoute::Left,2,false,45,2,0};
    // F03/F04 agree: source zero is mandatory for every source bank.
    assert(!p.Configure(64,2,{{1,InputRoute::Left,3,true,128,0,0}}));
    assert(!p.Configure(128,3,{{2,InputRoute::Left,2,false,45,2,0},
                                {1,InputRoute::Left,2,false,45,1,0}}));
    assert(!p.Configure(64,2,{{0,static_cast<InputRoute>(255),3,true,128}}));
    assert(!p.Configure(64,2,{{0,static_cast<InputRoute>(-1),3,true,128}}));
    assert(!p.Configure(64,0,{{0,InputRoute::Left,3,true,128}}));
    assert(!p.Configure(64,MaxCaptureChannels+1,{{0,InputRoute::Left,3,true,128}}));
    assert(!p.Configure(64,2,{{20,InputRoute::Left,3,true,128}}));
    assert(!p.Configure(64,2,{{0,InputRoute::Left,3,true,128,2,1}}));
    assert(!p.Configure(64,2,{{0,InputRoute::Right,3,true,128,0,2}}));
    assert(!p.Configure(64,2,{{0,InputRoute::Stereo,3,true,256,0,2}}));
    assert(!p.Configure(64,2,{{0,InputRoute::MonoMix,3,true,128,2,1}}));
    assert(!p.Configure(64,2,{{0,InputRoute::Left,3,true,128},
                              {0,InputRoute::Right,3,true,128}}));
    assert(!p.Configure(128,3,{{0,InputRoute::Left,3,false,22}}));
    assert(!p.Configure(128,3,{{0,InputRoute::Left,2,true,0}}));
    // No malicious input may upset the existing encoder/history or roster.
    assert(p.Configure(128,3,{a,b}));
    assert(reference.Configure(128,3,{a}));
    for(int tick=0;tick<45;tick++) {
        if(tick==12) assert(p.Configure(128,3,{a,b,c})); // addition retains source 0
        if(tick==24) assert(p.Configure(128,3,{c,a}));   // removal/reorder retains source 0
        if(tick==31) {
            assert(!p.Configure(128,3,{{0,static_cast<InputRoute>(255),2,false,45}}));
            assert(!p.Configure(128,3,{{0,InputRoute::Left,2,false,45,3,0}}));
            assert(!p.Configure(128,3,{{1,InputRoute::Left,2,false,45,1,0}}));
            assert(p.Count()==2);
        }
        const auto pcm=Interleaved(3,128,tick);
        std::vector<std::vector<uint8_t>> pDatagrams,rDatagrams;
        assert(p.Encode(pcm.data(),pcm.size(),false,tick,101,1200,pDatagrams));
        assert(reference.Encode(pcm.data(),pcm.size(),false,tick,101,1200,rDatagrams));
        const auto expected=Collect(rDatagrams,tick,101,1);
        // Collect() requires contiguous IDs; source 0 is always present.
        ParsedDatagram parsed;
        std::vector<uint8_t> kept;
        for(const auto& d:pDatagrams) {
            assert(ParseDatagram(d.data(),d.size(),parsed));
            for(size_t j=0;j<parsed.count;j++) if(parsed.fragments[j].source==0)
                kept.insert(kept.end(),parsed.fragments[j].data,parsed.fragments[j].data+parsed.fragments[j].size);
        }
        assert(kept==expected[0].bytes);
    }
    std::cout<<"F04 invalid route/index/profile rejection, rollback & preserved source encoder history on 2->3->2 churn PASS\n";
}
int main()
{
    CrossComponentContractTest();
    std::vector<int16_t> pcm(256);
    for(int i=0;i<128;i++){pcm[2*i]=int16_t(i*103-4900);pcm[2*i+1]=int16_t(14000-i*89);}
    // Independent source inputs and raw per-source routing, unmodified labels.
    SourceProducer p;
    assert(p.Configure(64,2,{{0,InputRoute::Left,3,true,128},{1,InputRoute::Right,3,true,128}}));
    std::vector<std::vector<uint8_t>> datagrams;
    assert(p.Encode(pcm.data(),128,false,432,99,1200,datagrams));
    assert(datagrams.size()==1);
    ParsedDatagram parsed;
    assert(ParseDatagram(datagrams[0].data(),datagrams[0].size(),parsed));
    assert(parsed.count==2 && parsed.generation==99 && parsed.frame==432);
    for(size_t k=0;k<2;k++) {
        const auto& f=parsed.fragments[k];
        assert(f.source==k && f.codec==3 && f.channels==1 && f.size==128);
        for(int n=0;n<64;n++){
            const auto x=int16_t(uint16_t(f.data[2*n])|(uint16_t(f.data[2*n+1])<<8));
            assert(x==pcm[2*n+k]);
        }
    }
    assert(p.Encode(pcm.data(),128,true,432,99,1200,datagrams));
    assert(ParseDatagram(datagrams[0].data(),datagrams[0].size(),parsed));
    for(size_t k=0;k<parsed.count;k++)
        for(size_t t=0;t<parsed.fragments[k].size;t++) assert(parsed.fragments[k].data[t]==0);
    assert(!p.Encode(pcm.data(),120,false,432,99,1200,datagrams));
    assert(!p.Configure(64,2,{{0,InputRoute::Left,2,false,22}}));
    assert(!p.Configure(64,2,{{0,InputRoute::Left,3,false,22},{0,InputRoute::Right,3,false,22}}));

    int checks=0;
    for(int frames : {64,128}) for(int channels : {1,2}) {
        const int codedBytes=frames==64 ? (channels==1?22:35):(channels==1?45:71);
        const uint8_t codec=frames==64?3:2;
        const auto route=channels==1? InputRoute::Left:InputRoute::Stereo;
        SourceProducer enc;
        assert(enc.Configure(frames,2,{{0,route,codec,false,codedBytes}}));
        // Stock pre-F04 client settings and encode invocation, on its own real
        // vendored Opus mode and encoder, not a fake implementation.
        int err=0;
        OpusCustomMode* mode=opus_custom_mode_create(48000,frames,&err);
        assert(mode && err==OPUS_OK);
        OpusCustomEncoder* stock=opus_custom_encoder_create(mode,channels,&err);
        assert(stock && err==OPUS_OK);
        opus_custom_encoder_ctl(stock,OPUS_SET_VBR(0));
        if(frames==64) opus_custom_encoder_ctl(stock,OPUS_SET_PACKET_LOSS_PERC(35));
        opus_custom_encoder_ctl(stock,OPUS_SET_APPLICATION(OPUS_APPLICATION_RESTRICTED_LOWDELAY));
        if(frames==128) opus_custom_encoder_ctl(stock,OPUS_SET_COMPLEXITY(1));
        opus_custom_encoder_ctl(stock,OPUS_SET_BITRATE((48000*codedBytes*8)/frames));
        std::vector<uint8_t> expected(codedBytes);
        std::vector<int16_t> routed(frames*channels);
        for(int tick=0;tick<40;tick++){
            for(int i=0;i<frames;i++) {
                if(channels==1) routed[i]=pcm[2*i];
                else {routed[2*i]=pcm[2*i];routed[2*i+1]=pcm[2*i+1];}
            }
            assert(opus_custom_encode(stock,routed.data(),frames,expected.data(),codedBytes)>=0);
            assert(enc.Encode(pcm.data(),frames*2,false,tick,99,1200,datagrams));
            assert(datagrams.size()==1);
            assert(ParseDatagram(datagrams[0].data(),datagrams[0].size(),parsed));
            assert(parsed.count==1 && parsed.fragments[0].size==size_t(codedBytes));
            assert(std::equal(expected.begin(),expected.end(),parsed.fragments[0].data));
            ++checks;
        }
        opus_custom_encoder_destroy(stock);
        opus_custom_mode_destroy(mode);
    }
    // Independent encoder state: a source that gets the same input must
    // produce the same outputs regardless of another source's input.
    SourceProducer both,single;
    assert(both.Configure(128,2,{{0,InputRoute::Left,2,false,45},{1,InputRoute::Right,2,false,45}}));
    assert(single.Configure(128,2,{{0,InputRoute::Left,2,false,45}}));
    for(int tick=0;tick<40;tick++){
        std::vector<std::vector<uint8_t>> a,b;
        assert(both.Encode(pcm.data(),256,false,tick,99,1200,a));
        assert(single.Encode(pcm.data(),256,false,tick,99,1200,b));
        ParsedDatagram x,y;
        assert(ParseDatagram(a[0].data(),a[0].size(),x));
        assert(ParseDatagram(b[0].data(),b[0].size(),y));
        assert(x.count==2 && y.count==1);
        assert(x.fragments[0].size==y.fragments[0].size);
        assert(std::equal(x.fragments[0].data,x.fragments[0].data+x.fragments[0].size,y.fragments[0].data));
    }
    // One packing operation per logical frame; fragments are all for the
    // original physical session, not independent transport sockets.
    assert(both.Encode(pcm.data(),256,false,777,99,80,datagrams));
    assert(datagrams.size()>1);
    for(const auto& d:datagrams) {
        assert(d.size()<=80);
        assert(ParseDatagram(d.data(),d.size(),parsed));
        assert(parsed.generation==99 && parsed.frame==777);
    }
    MultichannelTest();
    MalformedAndChurnTest();
    std::cout<<"F04 real vendored Opus/stock configure/encode: "<<checks<<" byte-parity frames, raw/mono/stereo, source isolation, one packetization path PASS\n";
}
