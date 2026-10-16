#!/usr/bin/env python3
"""AGPL-3.0-or-later. Actual CChannel::PutAudioData fade/lifecycle integration.
A tiny receive facade controls stock-Put events; real reassembly/Put counters
are independently tested by run_stage02. This test has no native Qt claim.
"""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=Path(os.getenv('POLYIN_CHANNEL_SOURCE',str(root/'src/channel.cpp'))).read_text();a=s.index('EPutDataStat CChannel::PutAudioData');b=s.index('EGetDataStat CChannel::GetData',a);actual=s[a:b]
u=(root/'src/channel.h').read_text();a=u.index('enum EPutDataStat');enum=u[a:u.index('};',a)+2]
code=r'''
#include <vector>
#include <cstdint>
#include <cassert>
#include <iostream>
template<class T>struct CVector:std::vector<T>{using std::vector<T>::vector;};
struct CHostAddress{bool operator==(const CHostAddress&)const{return true;}};
struct Lock{void lock(){}void unlock(){}};
struct Meter{void Reset(){}};
struct Frame{struct Counters{uint64_t stockPuts=0;}stats;bool complete=false,valid=true;
 bool Receive(const uint8_t*,int){if(complete)++stats.stockPuts;return valid;}const Counters&Stats()const{return stats;}};
struct Buffer{bool ok=true;bool Put(const CVector<uint8_t>&,int){return ok;}};
'''+enum+r'''
struct CChannel{
 bool bIsServer=true,bIsIdentified=true;Frame*PolyFrameBuffer=nullptr;Buffer SockBuf;
 Lock MutexSocketBuf;Meter SignalLevelMeter;int iNetwFrameSize=10,iNetwFrameSizeFact=1,iFadeInCnt=0,iFadeInCntMax=100,timeout=1;
 CHostAddress GetAddress()const{return{};}bool IsEnabled()const{return true;}bool IsConnected()const{return timeout>0;}
 void ResetTimeOutCounter(){timeout=10;}
 EPutDataStat PutAudioData(const CVector<uint8_t>&,int,const CHostAddress&);
};
'''+actual+r'''
int main(){CVector<uint8_t>bytes(10);CHostAddress host;CChannel ordinary;ordinary.PutAudioData(bytes,10,host);assert(ordinary.iFadeInCnt==1);
 ordinary.SockBuf.ok=false;ordinary.PutAudioData(bytes,10,host);assert(ordinary.iFadeInCnt==2); // preserve original failed-Put fade
 ordinary.PutAudioData(bytes,9,host);assert(ordinary.iFadeInCnt==2);
 Frame frame;CChannel poly;poly.PolyFrameBuffer=&frame;
 for(int i=0;i<10;++i)poly.PutAudioData(bytes,10,host);assert(poly.iFadeInCnt==0);
 frame.complete=true;poly.PutAudioData(bytes,10,host);assert(poly.iFadeInCnt==1);
 frame.complete=false;for(int i=0;i<10;++i)poly.PutAudioData(bytes,10,host);assert(poly.iFadeInCnt==1);
 frame.valid=false;poly.PutAudioData(bytes,10,host);assert(poly.iFadeInCnt==1);
 frame.valid=true;frame.complete=true;poly.PutAudioData(bytes,10,host);assert(poly.iFadeInCnt==2);
 std::cout<<"PASS actual CChannel fade: one advance per logical stock Put, none for partial/duplicate/rejected fragments; ordinary success/error unchanged\n";
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++11','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
