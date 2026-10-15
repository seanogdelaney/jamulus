#!/usr/bin/env python3
"""Design-only feasibility proof for minimal insertion in real stock FindChannel.

Compiles exact pinned stock FindChannel/InitChannel/FreeChannel method bodies,
including their address-sorted physical prefix and ascending numeric free tail,
and a second variant with ONLY a free-tail IsTaken skip. Not real Qt wiring.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
server = (root / 'src/server.cpp').read_text()

def method(signature, end_signature):
    a = server.find(signature)
    b = server.find(end_signature, a + len(signature))
    assert a >= 0 and b > a, (signature, end_signature)
    return server[a:b]

find = method('int CServer::FindChannel (', 'void CServer::InitChannel (')
init = method('void CServer::InitChannel (', 'void CServer::FreeChannel (')
free = method('void CServer::FreeChannel (', 'void CServer::DumpChannels (')
original = '''    i          = iCurNumChannels++; // save index of free channel and increment count
    iNewChanID = vecChannelOrder[i];'''
assert find.count(original) == 1, 'stock allocation statement changed; re-review the source-specific insertion'
assert 'QMutexLocker locker ( &MutexChanOrder );' in find
assert 'QMutexLocker locker ( &MutexChanOrder );' in free
assert 'vecChannelOrder[i + 1] < iCurChanID' in free
assert 'InitChannel ( iNewChanID, CheckAddr );' in find

replacement = '''    // Minimal Stage5 proposed insertion under stock MutexChanOrder:
    // select the first UNRESERVED ID in stock's ascending free suffix.
    // Shift only free IDs to preserve their numeric sorting. The remaining
    // stock address-sorted physical prefix insertion is unchanged.
    int selected = iCurNumChannels;
    while ( selected < iMaxNumChannels && IsTaken ( vecChannelOrder[selected] ) ) ++selected;
    if ( selected == iMaxNumChannels ) return INVALID_CHANNEL_ID;
    iNewChanID = vecChannelOrder[selected];
    while ( selected > iCurNumChannels )
    {
        vecChannelOrder[selected] = vecChannelOrder[selected - 1];
        --selected;
    }
    i = iCurNumChannels++;'''

header = r'''
#include "polyinsources.h"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <vector>
#include <utility>
constexpr int INVALID_CHANNEL_ID = -1;
struct CHostAddress {
    int value = -1;
    int Compare(const CHostAddress& other) const { return (value > other.value) - (value < other.value); }
};
struct Channel {
    CHostAddress address;
    const CHostAddress& GetAddress() const { return address; }
    void SetAddress(const CHostAddress& a) { address = a; }
    void ResetInfo() {} // no metadata before physical admission
    void SetGain(int, double) {}
    void SetPan(int, double) {}
};
struct QMutexLocker { explicit QMutexLocker(int*) {} }; // SERIAL simulation only; Stage5 must lock
struct Silent { template <class T> Silent& operator<<(const T&) { return *this; } };
Silent qWarning() { return {}; }
'''
class_template = r'''
namespace NAMESPACE {
class CServer {
public:
    explicit CServer(int cap) : iMaxNumChannels(cap), vecChannels(cap), vecChannelOrder(cap), slots(cap) {
        for (int i=0;i<cap;i++) vecChannelOrder[i]=i;
    }
    int FindChannel(const CHostAddress&, const bool = false);
    void InitChannel(int, const CHostAddress&);
    void FreeChannel(int);
    int iCurNumChannels=0, iMaxNumChannels, MutexChanOrder=0;
    std::vector<Channel> vecChannels;
    std::vector<int> vecChannelOrder;
    PolyIn::SourceSlots slots;
    bool IsTaken(int id) const { return BLOCK; }
    void AssertInvariants() const {
        std::vector<int> ids = vecChannelOrder;
        std::sort(ids.begin(), ids.end());
        for(int i=0;i<iMaxNumChannels;i++) assert(ids[i]==i);
        for(int i=1;i<iCurNumChannels;i++)
            assert(vecChannels[vecChannelOrder[i-1]].GetAddress().value <
                   vecChannels[vecChannelOrder[i]].GetAddress().value);
        for(int i=iCurNumChannels+1;i<iMaxNumChannels;i++)
            assert(vecChannelOrder[i-1]<vecChannelOrder[i]);
    }
};
METHODS
}
'''
# Stock::IsTaken is a no-op, matching the unmodified method's allocation policy.
stock = class_template.replace('NAMESPACE', 'Stock').replace('BLOCK', '(void(id), false)').replace('METHODS', find + init + free)
candidate = class_template.replace('NAMESPACE', 'Candidate').replace('BLOCK', 'slots.IsTaken(id)').replace('METHODS', find.replace(original, replacement) + init + free)
exercise = r'''
int main() {
    // No reservations: candidate allocator selects EXACT same stock IDs and
    // retains the two stock ordering invariants over arrival/deletion/churn.
    Stock::CServer unmodified(7);
    Candidate::CServer extended(7);
    auto arrive=[&](int a){
        const int original = unmodified.FindChannel({a},true);
        const int proposed = extended.FindChannel({a},true);
        assert(original==proposed);
        unmodified.AssertInvariants(); extended.AssertInvariants();
        assert(unmodified.vecChannelOrder==extended.vecChannelOrder);
        return original;
    };
    assert(arrive(100)==0);
    assert(arrive(20)==1);
    assert(arrive(130)==2);
    assert(arrive(55)==3);
    assert(unmodified.FindChannel({55})==extended.FindChannel({55}));
    unmodified.FreeChannel(1); extended.FreeChannel(1);
    assert(arrive(30)==1);
    unmodified.FreeChannel(0); extended.FreeChannel(0);
    assert(arrive(90)==0);
    assert(arrive(50)==4);
    assert(arrive(10)==5);
    assert(arrive(200)==6);
    assert(arrive(300)==INVALID_CHANNEL_ID);
    assert(extended.FindChannel({300})==INVALID_CHANNEL_ID);

    // With virtuals: physical channel 0 doubles as source zero, 1 and 2 are
    // held ordinary source identities. The stock free suffix must skip them.
    Candidate::CServer mixed(6);
    const int host0=mixed.FindChannel({10},true);
    assert(host0==0);
    assert(mixed.slots.AdmitPhysical(host0,100,"")); // stock initially unnamed
    auto ticket=mixed.slots.Reserve(host0,100,100,{{0,""},{1,"mic"},{2,"guitar"}});
    assert(ticket);
    assert(mixed.slots.IsTaken(1) && mixed.slots.IsTaken(2)); // pending reservation
    const int host1=mixed.FindChannel({20},true); // SKIP 1 and 2
    assert(host1==3 && mixed.slots.AdmitPhysical(host1,200,"Other"));
    mixed.AssertInvariants();
    assert(mixed.slots.Promote(ticket,host0,100));
    assert(mixed.FindChannel({20})==host1);
    assert(mixed.FindChannel({10})==host0);
    const int host2=mixed.FindChannel({30},true);
    const int host3=mixed.FindChannel({40},true);
    assert(host2==4 && host3==5);
    assert(mixed.slots.AdmitPhysical(host2,300,"Third"));
    assert(mixed.slots.AdmitPhysical(host3,400,"Fourth"));
    assert(mixed.FindChannel({50},true)==INVALID_CHANNEL_ID);
    assert(mixed.iCurNumChannels==4); // 4 physical + 2 virtual = capacity 6
    mixed.AssertInvariants();
    // Remove one physical session and recycle its EXACT free ID, excluding
    // the two still-active virtual channels.
    assert(mixed.slots.RetirePhysical(host1,200));
    mixed.FreeChannel(host1);
    mixed.AssertInvariants();
    assert(mixed.FindChannel({50},true)==host1);
    assert(mixed.slots.AdmitPhysical(host1,500,""));
    mixed.AssertInvariants();
    // Retiring sources makes 1,2 available to stock without moving the
    // physical address-sorted prefix or bypassing the normal FreeChannel.
    ticket=mixed.slots.Reserve(host0,100,100,{{0,""}});
    assert(ticket && mixed.slots.Promote(ticket,host0,100));
    const int host4=mixed.FindChannel({60},true);
    assert(host4==1 && mixed.slots.AdmitPhysical(host4,600,""));
    mixed.AssertInvariants();
    const int host5=mixed.FindChannel({70},true);
    assert(host5==2 && mixed.slots.AdmitPhysical(host5,700,""));
    mixed.AssertInvariants();
    assert(mixed.FindChannel({80},true)==INVALID_CHANNEL_ID);
    // A full disconnect retires the physical and every virtual ID before
    // stock's existing FreeChannel reclaims the physical index.
    assert(mixed.slots.RetirePhysical(host0,100));
    mixed.FreeChannel(host0);
    mixed.AssertInvariants();
    assert(mixed.FindChannel({10},true)==host0);
    std::cout << "STOCK source-extracted FindChannel/InitChannel/FreeChannel + MINIMAL free-tail skip: "
                 "baseline parity, held virtual collision avoidance, exhaustion, free/reuse, sorted prefix and suffix PASS\n";
}
'''
code = header + stock + candidate + exercise
with tempfile.TemporaryDirectory(prefix='jamulus-stock-allocator-') as d:
    cpp = Path(d) / 'proof.cpp'
    exe = Path(d) / 'proof'
    cpp.write_text(code)
    cmd = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
           '-I'+str(root / 'src'), str(cpp), str(root / 'src/polyinsources.cpp'), '-o', str(exe)]
    print('STOCK real method extraction src/server.cpp: FindChannel, InitChannel, FreeChannel')
    print('PROPOSED ONLY: replace two allocation statements with skip/shift; not production wired')
    print(' '.join(cmd), flush=True)
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
