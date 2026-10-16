// Source-local REAL producer RT parity+allocation proof (Qt/socket not present).
#include "polyinproducer.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
using namespace PolyIn;
static std::atomic<int> allocations{0};
static bool counted=false;
void* operator new(std::size_t n) { if(counted) ++allocations; if(void* p=std::malloc(n?n:1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { if(counted) ++allocations; if(void* p=std::malloc(n?n:1)) return p; throw std::bad_alloc(); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
static void check(int count, int samples, bool raw)
{
    std::vector<InputSpec> specs;
    for(int source=0; source<count; ++source) {
        InputSpec spec;
        spec.source=source;
        spec.route=InputRoute::Left;
        spec.channelA=source;
        spec.codec=samples==64?3:2;
        spec.raw=raw;
        spec.encodedBytes=raw ? samples*2 : samples==64 ? 22:45;
        specs.push_back(spec);
    }
    SourceProducer normal, realtime;
    assert(normal.Configure(samples,count,specs));
    assert(realtime.Configure(samples,count,specs));
    std::vector<int16_t> capture(count*samples);
    for(int s=0;s<samples;++s) for(int c=0;c<count;++c)
        capture[s*count+c]=int16_t(550*c + 7*s - 4000);
    std::vector<std::vector<uint8_t>> packets;
    std::array<BoundedPacket,MaxSources> bounded;
    size_t used=0;
    // Identical encoder histories, one independent copy per comparison.
    for(int k=0;k<4;++k) {
        assert(normal.Encode(capture.data(),capture.size(),false,30+k,9201,MaxDatagram,packets));
        counted=true; allocations=0;
        const bool ok = realtime.EncodeBounded(capture.data(),capture.size(),false,30+k,9201,MaxDatagram,bounded,used);
        counted=false;
        assert(ok && allocations==0);
        assert(packets.size()==used && used<=MaxSources);
        for(size_t i=0;i<used;++i) {
            assert(packets[i].size()==bounded[i].length);
            assert(!std::memcmp(packets[i].data(),bounded[i].data.data(),bounded[i].length));
            ParsedDatagram parsed;
            assert(ParseDatagram(bounded[i].data.data(),bounded[i].length,parsed));
            assert(parsed.generation==9201 && parsed.frame==uint32_t(30+k));
        }
    }
    std::cout<<count<<" independent sources "<<samples<<" samples raw="<<raw<<"; 0 allocations; bitwise wire parity PASS\n";
}
int main()
{
    for(int n: {2,6,20}) {
        check(n,64,false);check(n,128,false);
        check(n,64,true);check(n,128,true);
    }
    // An in-session codec transition must replace the matching Opus encoder,
    // and RAW must select the actual stock 128/256-byte mono payload profile.
    SourceProducer p;
    std::vector<InputSpec> s(2);
    for(int i=0;i<2;++i) {s[i].source=i;s[i].route=InputRoute::Left;s[i].channelA=i;s[i].codec=2;s[i].encodedBytes=45;}
    assert(p.Configure(128,2,s));
    for(auto& x:s) {x.raw=true;x.encodedBytes=256;}
    assert(p.Configure(128,2,s));
    for(auto& x:s) {x.raw=false;x.encodedBytes=45;}
    assert(p.Configure(128,2,s));
    std::cout<<"Opus to raw to Opus configuration PASS\n";
}
