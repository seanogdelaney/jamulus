#include "polyinsources.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace PolyIn;
using D=SourceSlots::Definition;
using I=SourceSlots::Identity;
int main()
{
    // Stock InitChannel calls ResetInfo before receiving any user metadata.
    SourceSlots unnamed(3);
    assert(unnamed.AdmitPhysical(0, 5, ""));
    I initial;
    assert(unnamed.Resolve(0, 5, 0, initial) && initial.tag.empty());
    auto emptyNames = unnamed.Reserve(0, 5, 5, {{0, ""}, {1, ""}});
    assert(emptyNames && unnamed.Promote(emptyNames, 0, 5));
    assert(unnamed.Resolve(0, 5, 1, initial) && initial.tag == " [2]");
    assert(unnamed.RetirePhysical(0, 5));
    SourceSlots map(7);
    assert(map.AdmitPhysical(2, 11, "First"));
    assert(map.AdmitPhysical(4, 51, "Second"));
    assert(!map.AdmitPhysical(2, 11, "duplicate"));
    assert(map.IsTaken(2) && map.IsTaken(4));
    auto ticket=map.Reserve(2,11,11,{{0,"First"},{1,"L"},{2,"R"}});
    assert(ticket);
    I i; assert(map.Resolve(2,11,0,i) && i.channel==2);
    assert(!map.Resolve(2,11,1,i)); // no premature visibility
    assert(!map.Rollback(ticket,2,999));
    assert(map.Promote(ticket,2,11));
    assert(map.Resolve(2,11,1,i) && i.tag=="L [2]"); int a=i.channel;
    assert(map.Resolve(2,11,2,i)); int b=i.channel;
    assert(a != b && a != 2 && b != 2 && a != 4 && b != 4);
    assert(!map.AdmitPhysical(a, 20,"Collision"));
    assert(map.IsTaken(a));
    auto list=map.OrdinaryChannels();
    for(size_t k=1;k<list.size();++k) assert(list[k-1].channel<list[k].channel);
    assert(list.size()==4);
    // Stable source ID, churn and rollback.
    ticket=map.Reserve(2,11,11,{{0,"First"},{1,"L"},{3,"New"}});
    assert(ticket && map.Resolve(2,11,2,i));
    assert(map.Rollback(ticket,2,11));
    assert(map.Resolve(2,11,2,i) && i.channel==b);
    ticket=map.Reserve(2,11,11,{{0,"First"},{1,"L"},{3,"New"}});
    assert(ticket && map.Promote(ticket,2,11));
    assert(!map.Resolve(2,11,2,i));
    assert(map.Resolve(2,11,1,i) && i.channel==a);
    // Genuine replacement: old-epoch lookup, controls and teardown are rejected.
    ticket=map.Reserve(2,11,99,{{0,"Fresh"},{1,"Fresh L"}});
    assert(ticket && map.Promote(ticket,2,11));
    assert(!map.Resolve(2,11,1,i));
    assert(map.Resolve(2,99,1,i) && i.channel==a);
    assert(!map.RetirePhysical(2,11));
    assert(!map.Reserve(2,11,111,{{0,"Stale"}}));
    assert(map.RetirePhysical(2,99));
    assert(!map.ResolveChannel(a,i));
    assert(!map.IsTaken(a));
    assert(map.Resolve(4,51,0,i) && i.channel==4);
    assert(map.AdmitPhysical(2,101,"Return"));
    assert(!map.Resolve(2,99,0,i));
    assert(map.Resolve(2,101,0,i));
    // Independent physical sessions may have concurrent outstanding source
    // reservations; they must not steal each other's held channel slots.
    SourceSlots concurrent(8);
    assert(concurrent.AdmitPhysical(0,301,"Alice"));
    assert(concurrent.AdmitPhysical(4,401,"Bob"));
    auto aTicket=concurrent.Reserve(0,301,301,{{0,"Alice"},{1,"mic"}});
    auto bTicket=concurrent.Reserve(4,401,401,{{0,"Bob"},{1,"gtr"}});
    assert(aTicket && bTicket && aTicket != bTicket);
    assert(concurrent.Promote(bTicket,4,401));
    assert(concurrent.Rollback(aTicket,0,301));
    assert(concurrent.Resolve(4,401,1,i));
    assert(!concurrent.Resolve(0,301,1,i));
    assert(concurrent.RetirePhysical(4,401));
    assert(concurrent.RetirePhysical(0,301));
    // All-capacity semantics; independent stock channel allocation must
    // consult IsTaken(), including held tickets.
    SourceSlots full(4);
    assert(full.AdmitPhysical(0,1,"A"));
    assert(full.AdmitPhysical(3,2,"B"));
    ticket=full.Reserve(0,1,1,{{0,"A"},{1,"1"},{2,"2"}});
    assert(ticket && full.IsTaken(1) && full.IsTaken(2));
    assert(!full.AdmitPhysical(1,7,"C"));
    assert(full.Promote(ticket,0,1));
    assert(!full.Reserve(0,1,1,{{0,"A"},{1,"1"},{2,"2"},{3,"3"}}));
    assert(full.RetirePhysical(3,2));
    ticket=full.Reserve(0,1,1,{{0,"A"},{1,"1"},{2,"2"},{3,"3"}});
    assert(ticket && full.Promote(ticket,0,1));
    assert(full.OrdinaryChannels().size()==4);
    // Arbitrary repeated atomic epoch replacements; old identities never survive.
    for(uint64_t epoch=2;epoch<150;epoch++){
        ticket=full.Reserve(0,epoch-1,epoch,{{0,"Main"},{1,"One"}});
        assert(ticket && full.Promote(ticket,0,epoch-1));
        assert(!full.Resolve(0,epoch-1,1,i));
        assert(full.Resolve(0,epoch,1,i));
    }
    // Actual stock 150-ID space / 20-source wire bound, not a test-sized map.
    SourceSlots production(150);
    for(int c=0;c<130;c++) assert(production.AdmitPhysical(c,uint64_t(c+1),"Legacy"));
    std::vector<D> twenty;
    for(uint16_t source=0;source<20;source++) twenty.push_back({source,"Multi"});
    auto capacityTicket=production.Reserve(0,1,1,twenty);
    assert(capacityTicket && production.Promote(capacityTicket,0,1));
    auto channels=production.OrdinaryChannels();
    assert(channels.size()==149);
    for(size_t j=1;j<channels.size();j++) assert(channels[j-1].channel<channels[j].channel);
    for(uint16_t source=0;source<20;source++) {
        assert(production.Resolve(0,1,source,i));
        assert(i.channel>=0 && i.channel<150 && i.tag.size()<=16);
    }
    assert(!production.AdmitPhysical(130,999,"Would overwrite virtual"));
    assert(production.RetirePhysical(0,1));
    assert(production.AdmitPhysical(130,999,"Now free"));
    assert(SourceSlots::Tagged("1234567890123456789",9)=="12345678901 [10]");
    assert(SourceSlots::Tagged("abcdef",1)=="abcdef [2]");
    std::cout<<"F03 actual SourceSlots: ownership/collision/sorting/rollback/churn/epoch/148 replacements PASS\n";
}
