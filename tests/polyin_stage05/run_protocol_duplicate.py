#!/usr/bin/env python3
"""Compile actual producer duplicate gate/cache; Qt dispatcher/assembly are separate native tests.
Only message dispatch/ACK transport are replaced by counters. No receiver protocol is simulated.
"""
from pathlib import Path
import re,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];source=(root/'src/protocol.cpp').read_text();header=(root/'src/protocol.h').read_text()
start=source.index('void CProtocol::ParseMessageBody (');a=source.index('{',start);depth=1;b=a+1
while depth:
 depth+=(source[b]=='{')-(source[b]=='}');b+=1
body=source[a+1:b-1];condition_start=body.index('if ( ( iOldRecID == iRecID )');condition_end=body.index('\n    {',condition_start);condition=body[condition_start:condition_end].strip()[3:].strip();assert condition.startswith('(')
prefix=body[:condition_start];tail_start=list(re.finditer(r'\s+iOldRecID\s*=\s*iRecID;',body))[-1].start();tail=body[tail_start:];tail=tail[:tail.rindex('\n        }\n    }')]
m=re.search(r'std::array<uint8_t,\s*(\d+)> oldPolyInControlBody',header);capacity=int(m[1]) if m else 1450
fixture=r'''
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <new>
static bool monitor=false;static int allocations=0;
void*operator new(size_t n){if(monitor)++allocations;void*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}void operator delete(void*p)noexcept{std::free(p);}
template<class T>struct CVector:std::vector<T>{using std::vector<T>::vector;int Size()const{return int(this->size());}};
constexpr int PROTMESSID_POLYIN_CONFIG=37,PROTMESSID_POLYIN_RESPONSE=38,PROTMESSID_SPECIAL_SPLIT_MESSAGE=2001;
struct Gate{int iOldRecID=-1,iOldRecCnt=0,oldPolyInControlSize=-1;std::array<uint8_t,CAPACITY>oldPolyInControlBody{};
bool feed(const CVector<uint8_t>&vecbyMesBodyData,int iRecCounter,int iRecID){
PREFIX
const bool duplicate=CONDITION;
if(!duplicate){TAIL}
return !duplicate;}};
int main(){Gate g;CVector<uint8_t>a(10,0),b(10,0);a[0]=b[0]=1;a[1]=100;b[1]=101;
assert(g.feed(a,7,37));assert(!g.feed(a,7,37));assert(g.feed(b,7,37));assert(!g.feed(b,7,37));assert(g.feed(a,7,37));
assert(g.feed(a,7,38));assert(!g.feed(a,7,38));assert(g.feed(b,7,38));
CVector<uint8_t>first(20,0),last(20,0);first[0]=last[0]=37;first[8]=1;last[8]=2;
assert(g.feed(first,8,2001));assert(!g.feed(first,8,2001));assert(g.feed(last,8,2001));assert(!g.feed(last,8,2001));
// Legacy split containers and ordinary controls preserve stock ID/counter rules,
// including different payloads at an identical ID/counter.
first[0]=last[0]=10;assert(g.feed(first,9,2001));assert(!g.feed(last,9,2001));assert(g.feed(a,10,13));assert(!g.feed(b,10,13));
for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(9),size_t(CAPACITY),size_t(CAPACITY+1)}){CVector<uint8_t>x(n,0),y(n,0);if(n)y[n-1]=1;g.feed(x,1,37);g.feed(y,1,37);g.feed(x,1,2001);g.feed(y,1,2001);}
CVector<uint8_t>x(CAPACITY,0),y(CAPACITY,0);y.back()=1;monitor=true;for(int i=0;i<10000;++i)assert(g.feed(i%2?x:y,42,37));monitor=false;assert(allocations==0);
std::printf("PASS actual producer duplicate gate: fresh payload same counter, exact retransmit, split containers, responses, malformed sizes; legacy rule unchanged; zero cache/gate allocations\n");}
'''.replace('CAPACITY',str(capacity)).replace('PREFIX',prefix).replace('CONDITION',condition).replace('TAIL',tail)
with tempfile.TemporaryDirectory(prefix='stage05-protocol-duplicate-') as td:
 p=Path(td);(p/'fixture.cpp').write_text(fixture);cmd=['clang++','-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'fixture.cpp'),'-o',str(p/'test')];print('BUILD',' '.join(cmd),flush=True);subprocess.run(cmd,check=True);subprocess.run([str(p/'test')],check=True)
