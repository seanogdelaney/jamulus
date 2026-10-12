#include "../src/netreceive.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace NetReceive;
    assert(SequenceDelta(uint8_t(255),uint8_t(0)) == -1);
    assert(SequenceDelta(uint8_t(0),uint8_t(255)) == 1);
    assert(SequenceDelta(uint8_t(128),uint8_t(0)) == -128);
    assert(SequenceDelta(uint32_t(0),uint32_t(0xffffffff)) == 1);
    assert(SequenceDelta(uint32_t(0xffffffff),uint32_t(0)) == -1);
    auto a = PlaceArrival(-2, 3);
    assert(a.backward == 2 && a.offset == 0 && a.forward == 0);
    a = PlaceArrival(6, 3);
    assert(a.forward == 4 && a.offset == 2 && a.backward == 0);
    a = PlaceArrival(2, 3);
    assert(a.forward == 0 && a.offset == 2);
    assert(ForwardTargetCursor(101,110,2) == 109);
    assert(ForwardTargetCursor(0xfffffffeu,1u,2) == 0u);
    assert(ForwardTargetCursor(111,110,2) == 111);
    std::cout << "netreceive: PASS\n";
}
