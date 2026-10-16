// Exercise the genuine production F06 control-body codec and F03 stock ID
// allocator with the same generation semantics used by the server handler.
#include "polyincontrol.h"
#include "polyinsources.h"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <set>
using namespace PolyIn;
int main() {
    const std::vector<ControlSource> inputs = {
        {0,3,1,22,"Irish fiddle"}, {1,3,1,22,"Vocal"}, {2,3,2,35,"Violin"} };
    std::vector<uint8_t> wire;
    assert(EncodeConfig(123456,inputs,wire));
    uint64_t epoch=0;
    std::vector<ControlSource> out;
    assert(DecodeConfig(wire.data(),wire.size(),epoch,out));
    assert(epoch==123456 && out.size()==3 && out[1].name=="Vocal");
    auto corrupted=wire;
    corrupted[9]=21; assert(!DecodeConfig(corrupted.data(),corrupted.size(),epoch,out));
    corrupted=wire;corrupted[15]=0; // invalid empty first source name
    assert(!DecodeConfig(corrupted.data(),corrupted.size(),epoch,out));
    corrupted=wire;corrupted.push_back(0);assert(!DecodeConfig(corrupted.data(),corrupted.size(),epoch,out));
    assert(!DecodeConfig(wire.data(),9,epoch,out));
    for(size_t i=0;i<wire.size();++i){auto cut=wire;cut.resize(i);assert(!DecodeConfig(cut.data(),cut.size(),epoch,out));}
    auto r=Response(123456,true); bool accepted=false;
    assert(DecodeResponse(r.data(),r.size(),epoch,accepted)&&accepted&&epoch==123456);
    r[9]=2;assert(!DecodeResponse(r.data(),r.size(),epoch,accepted));
    SourceSlots slots(5);
    assert(slots.AdmitPhysical(0,1,""));
    assert(slots.AdmitPhysical(1,1,"")); // ordinary stock client never stolen
    const std::vector<SourceSlots::Definition> defs={{0,"Singer"},{1,"Guitar"},{2,"Bass"}};
    const auto ticket=slots.Reserve(0,1,123456,defs); assert(ticket);
    assert(slots.Promote(ticket,0,1));
    SourceSlots::Identity src0,src1,src2;
    assert(slots.Resolve(0,123456,0,src0)&&src0.channel==0);
    assert(slots.Resolve(0,123456,1,src1)&&src1.channel!=1);
    assert(slots.Resolve(0,123456,2,src2)&&src2.channel!=1&&src2.channel!=src1.channel);
    auto ids=slots.OrdinaryChannels();
    assert(ids.size()==4);
    for(size_t i=1;i<ids.size();++i) assert(ids[i-1].channel<ids[i].channel);
    for(const auto& identity:ids) assert(identity.tag.size()<=size_t(SourceSlots::TagBytes));
    assert(!slots.RetirePhysical(0,1)); // stale disconnect cannot free epoch
    assert(!slots.Resolve(0,1,1,src1));
    auto next=slots.Reserve(0,123456,123457,{{0,"Singer"},{1,"Guitar"}});assert(next);
    assert(slots.Promote(next,0,123456));
    assert(!slots.Resolve(0,123456,1,src1));
    assert(slots.RetirePhysical(0,123457));
    assert(!slots.IsTaken(0));assert(slots.IsTaken(1));
    assert(slots.RetirePhysical(1,1));
    std::cout << "F06 production control-body bounds and epoch/stock-channel ownership PASS\n";
}
