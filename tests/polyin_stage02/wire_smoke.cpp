// F01 has no Qt or buffering dependency: independently compile the wire parser.
#include "polyinwire.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
using namespace PolyIn;
// EAudComprType is extracted unchanged from actual stock util.h by run_wire.py.
static_assert(CT_NONE == 0 && CT_CELT == 1 && CT_OPUS == 2 && CT_OPUS64 == 3,
              "poly-in wire codec IDs must match the production stock enum");
int main() {
    // Positive and negative codec coverage in BOTH pack and parse paths.
    for (int codec=0; codec<=4; ++codec) {
        SourceRecord r; r.source=0; r.channels=1; r.codec=uint8_t(codec);
        r.encoded={1,2,3,4};
        std::vector<std::vector<uint8_t>> datagrams;
        const bool allowed = codec==CT_CELT || codec==CT_OPUS || codec==CT_OPUS64;
        assert(PackFrame(1,7,{r},1200,datagrams)==allowed);
        if (allowed) {
            assert(datagrams.size()==1);
            ParsedDatagram parsed;
            assert(ParseDatagram(datagrams[0].data(),datagrams[0].size(),parsed));
            assert(parsed.count==1 && parsed.fragments[0].codec==codec);
        }
        // Parser must independently reject bytes produced by other senders.
        r.codec=CT_OPUS;
        assert(PackFrame(1,7,{r},1200,datagrams));
        datagrams[0][HeaderBytes+2]=uint8_t(codec);
        ParsedDatagram parsed;
        assert(ParseDatagram(datagrams[0].data(),datagrams[0].size(),parsed)==allowed);
    }

    for (int sources : {1,2,6,20}) for (size_t mtu : {size_t(55),size_t(1200)}) {
        std::vector<SourceRecord> records;
        for(int i=0;i<sources;i++) {
            SourceRecord r; r.source=i; r.channels=(i%2)+1; r.codec=1+i%3;
            r.encoded.assign(200,uint8_t(i+17)); records.push_back(r);
        }
        std::vector<std::vector<uint8_t>> pkts;
        assert(PackFrame(0xfffffff0u,0x123456789abcdef0ULL,records,mtu,pkts));
        size_t fragments=0;
        for (const auto& p:pkts) {
            ParsedDatagram d;
            assert(ParseDatagram(p.data(),p.size(),d));
            assert(d.frame==0xfffffff0u && d.generation==0x123456789abcdef0ULL);
            assert(p.size()<=mtu && d.count>0);
            fragments+=d.count;
            for(size_t i=0;i<d.count;i++) {
                auto& f=d.fragments[i];
                assert(f.source<(unsigned)sources && f.total==200 && f.offset+f.size<=200);
                for(size_t j=0;j<f.size;j++)assert(f.data[j]==uint8_t(f.source+17));
            }
            auto corrupted=p; corrupted[4]=99; assert(!ParseDatagram(corrupted.data(),corrupted.size(),d));
            corrupted=p; corrupted[22]=1; assert(!ParseDatagram(corrupted.data(),corrupted.size(),d));
            corrupted=p; corrupted.back()^=0xff; assert(ParseDatagram(corrupted.data(),corrupted.size(),d)); // encoded bytes intentionally opaque
        }
        assert(fragments>=(size_t)sources);
    }
    printf("F01 stock EAudComprType 1/2/3 accepted, 0/4 rejected in pack AND parse; bounds 1/2/6/20 PASS\n");
}
