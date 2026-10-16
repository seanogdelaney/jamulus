#!/usr/bin/env python3
"""Compile actual modified CServer stock-ID admission, free, list functions.
Qt and socket boundaries are replaced by a deterministic facade: not a Qt test.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/server.cpp').read_text()
def method(start):
    a=s.index(start); pos=s.index('{',a);depth=0
    for end in range(pos,len(s)):
        if s[end]=='{':depth+=1
        elif s[end]=='}':
            depth-=1
            if not depth: return s[a:end+1]
    raise ValueError(start)
methods='\n'.join(method(sig) for sig in (
 'int CServer::FindChannel (','void CServer::InitChannel (',
 'void CServer::FreeChannel (','CVector<CChannelInfo> CServer::CreateChannelList()'))
source=r'''
#include "polyinsources.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
constexpr int MAX_NUM_CHANNELS=150;
constexpr int INVALID_CHANNEL_ID=MAX_NUM_CHANNELS+1;
struct QString : std::string { using std::string::string;
  QString(const std::string& s):std::string(s){}
  static QString fromStdString(const std::string& s){return QString(s);}
};
struct CChannelCoreInfo{QString strName;};
struct CChannelInfo {
 int iChanID;CChannelCoreInfo info;
 CChannelInfo(int id,CChannelCoreInfo i):iChanID(id),info(i){}
};
template<typename T> struct CVector : std::vector<T> {
 explicit CVector(int){} void Add(const T& t){this->push_back(t);}
};
struct CHostAddress{int id=-1;int Compare(const CHostAddress& rhs) const {
 return (id>rhs.id)-(id<rhs.id);
}};
struct QMutex{};
struct QMutexLocker{explicit QMutexLocker(QMutex*){}};
struct Warning{template<class T> Warning& operator<<(const T&){return *this;}};
Warning qWarning(){return {};}
struct Channel{
 CHostAddress addr;bool connected=false,physical=false;Channel* owner=nullptr;
 CChannelCoreInfo info;QString tag;
 const CHostAddress& GetAddress()const{return addr;}
 void SetAddress(const CHostAddress& a){addr=a;}
 void ResetInfo(){info={};}
 void SetGain(int,float){}void SetPan(int,float){}
 bool IsConnected()const{return connected;}
 bool IsPolyInPhysical()const{return physical;}
 bool IsPolyInVirtual()const{return owner!=nullptr;}
 Channel* PolyInPhysicalOwner()const{return owner;}
 CChannelCoreInfo GetChanInfo()const{return info;}
 QString GetName()const{return owner ? tag:info.strName;}
};
struct CServer {
 explicit CServer(int cap):iMaxNumChannels(cap),polyInSlots(cap){
  for(int i=0;i<cap;i++) vecChannelOrder[i]=i;
 }
 int FindChannel(const CHostAddress&,bool=false);
 void InitChannel(int,const CHostAddress&);
 void FreeChannel(int);
 CVector<CChannelInfo> CreateChannelList();
 int iMaxNumChannels=0,iCurNumChannels=0;
 Channel vecChannels[MAX_NUM_CHANNELS];int vecChannelOrder[MAX_NUM_CHANNELS]={};
 QMutex MutexChanOrder;
 PolyIn::SourceSlots polyInSlots;
 std::array<uint64_t,MAX_NUM_CHANNELS> polyInEpoch{};
};
// PRODUCTION_SERVER_METHODS
int main(){
 CServer svr(6);
 const int owner=svr.FindChannel({10},true);
 const int guest=svr.FindChannel({20},true);
 assert(owner==0 && guest==1);
 svr.vecChannels[owner].connected=true;svr.vecChannels[guest].connected=true;
 svr.vecChannels[owner].info.strName="Alice";
 svr.vecChannels[guest].info.strName="Bob";
 auto t=svr.polyInSlots.Reserve(owner,1,100,{{0,"Left"},{1,"Right"}});assert(t);
 assert(svr.polyInSlots.Promote(t,owner,1));svr.polyInEpoch[owner]=100;
 svr.vecChannels[owner].physical=true;
 PolyIn::SourceSlots::Identity identity;
 assert(svr.polyInSlots.Resolve(owner,100,1,identity));
 int virt=identity.channel;assert(virt==2);
 svr.vecChannels[virt].owner=&svr.vecChannels[owner];
 svr.vecChannels[virt].tag=QString::fromStdString(identity.tag);
 auto channels=svr.CreateChannelList();assert(channels.size()==3);
 assert(channels[0].iChanID==0 && channels[0].info.strName=="Left [1]");
 assert(channels[1].iChanID==1 && channels[1].info.strName=="Bob");
 assert(channels[2].iChanID==2 && channels[2].info.strName=="Right [2]");
 const int next=svr.FindChannel({30},true);assert(next==3);
 svr.vecChannels[next].connected=true;
 channels=svr.CreateChannelList();assert(channels.size()==4);
 for(size_t i=1;i<channels.size();++i)assert(channels[i-1].iChanID<channels[i].iChanID);
 // Stock physical teardown returns only its ID, not a virtual fader slot.
 svr.vecChannels[guest].connected=false;svr.FreeChannel(guest);
 assert(!svr.polyInSlots.IsTaken(guest)&&svr.polyInSlots.IsTaken(virt));
 assert(svr.FindChannel({25},true)==guest);
 assert(!svr.polyInSlots.RetirePhysical(owner,1));
 // Fresh epoch may replace the source while preserving the physical ID.
 t=svr.polyInSlots.Reserve(owner,100,101,{{0,"Left"},{1,"Right"}});
 assert(t&&svr.polyInSlots.Promote(t,owner,100));
 assert(!svr.polyInSlots.RetirePhysical(owner,100));
 svr.polyInEpoch[owner]=101;
 svr.vecChannels[owner].connected=false;svr.FreeChannel(owner);
 assert(!svr.polyInSlots.IsTaken(owner)&&!svr.polyInSlots.IsTaken(virt));
 std::cout << "F06/F07 extracted production FindChannel/FreeChannel/CreateChannelList: sorted ordinary IDs, virtual tags, stock clients, reuse, epoch PASS\n";
}
'''.replace('// PRODUCTION_SERVER_METHODS',methods)
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'fixture.cpp').write_text(source)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',
         '-I'+str(root/'src'),str(p/'fixture.cpp'),str(root/'src/polyinsources.cpp'),'-o',str(p/'fixture')]
    print('BUILD',' '.join(cmd),flush=True)
    subprocess.run(cmd,check=True)
    subprocess.run([str(p/'fixture')],check=True)
