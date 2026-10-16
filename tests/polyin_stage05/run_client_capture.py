#!/usr/bin/env python3
"""Compile exact F06 CClient Prepare/Reconfigure/Refresh methods with test-only
Qt device facades. Real SourceProducer/Opus included; NOT native Qt validation."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
lib=Path(os.getenv('POLYIN_OPUS_BUILD',str(root.parent.parent/'opus-build')))/'libopus.a'
if not lib.is_file(): raise SystemExit(f'No vendored Opus build: {lib}')
src=(root/'src/client.cpp').read_text()
def extract(signature):
 st=src.index(signature); lb=src.index('{',st); depth=0
 for i in range(lb,len(src)):
  if src[i]=='{': depth+=1
  elif src[i]=='}':
   depth-=1
   if not depth: return src[st:i+1]
 raise ValueError(signature)
# Minimal test-only stand-ins for Qt-owned boundaries, not stock application tests.
prefix=r'''
#include "polyinproducer.h"
#include "polyincontrol.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
using namespace PolyIn;
enum { CC_MONO=1, CT_OPUS=2, CT_OPUS64=3 };
struct QByteArray {
 std::string s;
 QByteArray(const char*p):s(p){} QByteArray(std::string x):s(std::move(x)){}
 QByteArray left(int n)const{return QByteArray(s.substr(0,n));}
 std::string toStdString()const{return s;}
 static QByteArray number(int i){return QByteArray(std::to_string(i));}
 QByteArray operator+(const QByteArray&rhs)const{return QByteArray(s+rhs.s);}
};
struct QString {std::string s="abc";bool isEmpty()const{return s.empty();}QByteArray toUtf8()const{return QByteArray(s);} };
struct FakeInfo {QString strName;};
struct FakeDate {static uint64_t currentMSecsSinceEpoch(){return 12345;}};
using QDateTime=FakeDate;
std::ostream& qWarning(){return std::cerr;}
template<typename T>struct CVector:std::vector<T>{using std::vector<T>::vector;void Init(int size){this->resize(size);}int Size()const{return int(this->size());}};
struct MockSound {
 bool running=false,enabled=true;
 int channels=2,stops=0,starts=0,rejectCount=-1;
 bool ConfigurePolyInCapture(int n){if(!enabled || n>20 || n==rejectCount || running)return false;channels=n;return true;}
 bool IsRunning()const{return running;}
 void Stop(){++stops;running=false;}
 void Start(){++starts;running=true;}
};
struct CClient {
 bool bPolyInRequested=true,bRawAudioIsSupported=false,bSndCrdConversionBufferRequired=false;
 std::atomic<bool> bPolyInActive{false},bPolyInMediaHold{false};
 bool bPolyInInitialPending=false,bPolyInAmendmentPending=false,bPolyInFormatTransitionPending=false,bPolyInWithdrawalPending=false,bPolyInServerAdmitted=false;
 int polyInAdmittedCodec=-1,polyInAdmittedSamples=0,polyInAdmittedBytes=0,polyInAdmittedSources=0,polyInAdmittedFrameFactor=0;
 bool polyInAdmittedRaw=false;
 uint64_t polyInRetiredProfileTag=0;
 uint64_t PolyInProfileTag() const;
 int polyInPriorSources=0;uint64_t polyInPriorEpoch=0;
 int eAudioChannelConf=CC_MONO,eAudioCompressionType=CT_OPUS,iCeltNumCodedBytes=45;
 int iOPUSFrameSizeSamples=128,iSndCrdFrameSizeFactor=1,iMonoBlockSizeSam=128;
 int iSndCardMonoBlockSizeSamConvBuff=128,polyInDesiredSources=2,polyInCaptureChannels=2;
 void* CurOpusEncoder=reinterpret_cast<void*>(1);
 uint64_t polyInEpoch=0;
 SourceProducer polyInProducer, polyInPreviousProducer;
 std::vector<ControlSource> polyInDescriptors;
 CVector<int16_t> polyInRawInterleaved,polyInConvertedCapture;
 std::array<CVector<uint8_t>,MaxSources> polyInSendBuffers;
 struct Buffer {void Init(int){};} polyInConversionIn;
 MockSound Sound;
 FakeInfo ChannelInfo;
 int sends=0;
 void SendPolyInConfig(){++sends;}
 bool PreparePolyInProducer();
 bool ReconfigurePolyInSources(int count);
 void RefreshPolyInAfterSoundInit();
 void BeginPolyInWithdrawal();
 void RememberPolyInProfile();
 void OnPolyInResponse(CVector<uint8_t> body);
};
'''
main=r'''
int main(){
 CClient client;
 assert(client.PreparePolyInProducer());
 assert(client.polyInProducer.Count()==2);
 assert(client.polyInEpoch!=0);
 auto epoch=client.polyInEpoch;
 client.bPolyInActive.store(true); // prior initial acceptance in this isolated harness
 client.Sound.Start();
 for(int n : {6,20,2}) {
   assert(client.ReconfigurePolyInSources(n));
   assert(client.bPolyInAmendmentPending && client.bPolyInMediaHold && !client.bPolyInActive);
   auto accepted = Response(client.polyInEpoch,true);
   client.OnPolyInResponse(CVector<uint8_t>(accepted.begin(),accepted.end()));
   assert(client.bPolyInActive && !client.bPolyInAmendmentPending && !client.bPolyInMediaHold);
   assert(client.Sound.IsRunning());
   assert(client.polyInProducer.Count()==size_t(n));
   assert(client.polyInEpoch>epoch);
   assert(client.polyInDescriptors.size()==size_t(n));
   for(int i=0;i<n;++i) { assert(client.polyInDescriptors[i].source==i); assert(client.polyInDescriptors[i].name.find(std::to_string(i+1))!=std::string::npos); }
   epoch=client.polyInEpoch;
 }
 assert(client.Sound.stops==3 && client.Sound.starts==4 && client.sends==3);
 client.Sound.Stop();
 client.CurOpusEncoder=nullptr;
 client.bRawAudioIsSupported=true;
 client.iCeltNumCodedBytes=256;
 client.polyInDesiredSources=20;
 assert(client.PreparePolyInProducer());
 assert(client.polyInProducer.Count()==20);
 std::vector<int16_t> pcm(20*128);
 std::array<BoundedPacket,MaxSources> packets;size_t num=0;
 assert(client.polyInProducer.EncodeBounded(pcm.data(),pcm.size(),false,17,client.polyInEpoch,MaxDatagram,packets,num));
 assert(num>0);
 for(const auto& s:client.polyInDescriptors){assert(s.bytes==256 && s.codec==2);}
 client.iOPUSFrameSizeSamples=64;client.iCeltNumCodedBytes=128;client.eAudioCompressionType=CT_OPUS64;
 assert(client.PreparePolyInProducer());
 assert(client.polyInProducer.Count()==20);
 assert(client.polyInProducer.EncodeBounded(pcm.data(),20*64,false,18,client.polyInEpoch,MaxDatagram,packets,num));
 for(const auto& s:client.polyInDescriptors){assert(s.bytes==128 && s.codec==3);}
 const auto admitted = client.polyInEpoch;
 client.Sound.rejectCount=6;
 client.bPolyInActive.store(true);
 assert(!client.ReconfigurePolyInSources(6));
 assert(client.polyInDesiredSources==20);
 assert(client.polyInEpoch==admitted && client.bPolyInActive && !client.bPolyInMediaHold);
 std::cout << "ACTUAL CClient Prepare/Reconfigure/Refresh source-extracted: 2->6->20->2, RAW128/256, epochs, local rollback PASS (mock Qt/JACK)\\n";
}
'''
body=prefix+extract('static uint64_t NextPolyInEpoch()')+'\n'+extract('uint64_t CClient::PolyInProfileTag() const')+'\n'+extract('void CClient::RememberPolyInProfile')+'\n'+extract('void CClient::BeginPolyInWithdrawal')+'\n'+ '\n'.join(extract('bool CClient::'+s) for s in ['PreparePolyInProducer','ReconfigurePolyInSources'])+'\n'+extract('void CClient::RefreshPolyInAfterSoundInit')+'\n'+extract('void CClient::OnPolyInResponse')+'\n'+main
with tempfile.TemporaryDirectory(prefix='stage05-client-capture-') as td:
 cpp=Path(td)/'fixture.cpp';cpp.write_text(body)
 exe=Path(td)/'fixture'
 cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'src'),'-I'+str(root/'libs/opus/include'),
      str(root/'src/polyinwire.cpp'),str(root/'src/polyinproducer.cpp'),str(cpp),str(lib),'-lm','-o',str(exe)]
 print('RUN '+' '.join(cmd),flush=True)
 subprocess.run(cmd,check=True)
 subprocess.run([str(exe)],check=True)
