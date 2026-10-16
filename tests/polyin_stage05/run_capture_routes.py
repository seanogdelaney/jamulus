#!/usr/bin/env python3
"""AGPL-3.0-or-later. Bounded route and versioned metadata regression."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
lib=Path(os.environ['POLYIN_OPUS_BUILD'])/'libopus.a'
code=r'''
#include "polyinproducer.h"
#include "polyincontrol.h"
#include <cassert>
#include <iostream>
using namespace PolyIn;
int main(){
 std::vector<CaptureRoute> rows={{"Vox",1,0,-1},{"Keys",2,1,2},{"Guitar",3,3,-1}};
 int captured=0;assert(ValidateCaptureRoutes(rows,16,captured)&&captured==4);
 auto bad=rows;bad[1].iInputChannel1=0;assert(!ValidateCaptureRoutes(bad,16,captured));
 bad=rows;bad[2].iInputChannel1=16;assert(!ValidateCaptureRoutes(bad,16,captured));
 bad=rows;bad[1].iInputChannel2=-2;assert(!ValidateCaptureRoutes(bad,16,captured));
 bad=rows;bad[2].strFaderTag="Vox";assert(!ValidateCaptureRoutes(bad,16,captured));
 bad=rows;bad[0].strFaderTag=std::string(49,'x');assert(!ValidateCaptureRoutes(bad,16,captured));
 assert(!ValidateCaptureRoutes(rows,2,captured));
 std::vector<ControlSource> descriptors={{0,2,2,71,"Vox"},{1,2,2,71,"Keys"},{2,2,1,45,"Guitar"}};
 std::vector<uint8_t> body;assert(EncodeConfig(100,descriptors,body)&&body[0]==1);
 for(size_t i=0;i<descriptors.size();++i)descriptors[i].instrument=int(i)+1;
 assert(EncodeConfig(100,descriptors,body)&&body[0]==2);
 uint64_t epoch=0;std::vector<ControlSource> parsed;
 assert(DecodeConfig(body.data(),body.size(),epoch,parsed)&&epoch==100&&parsed.size()==3);
 for(size_t i=0;i<parsed.size();++i)assert(parsed[i].instrument==int(i)+1);
 for(size_t size=0;size<body.size();++size)assert(!DecodeConfig(body.data(),size,epoch,parsed));
 auto malformed=body;malformed[15]=0;malformed[16]=1;assert(!DecodeConfig(malformed.data(),malformed.size(),epoch,parsed));
 descriptors[1].instrument=256;assert(!EncodeConfig(100,descriptors,body));descriptors[1].instrument=-2;assert(!EncodeConfig(100,descriptors,body));
 for(int samples:{64,128}){
  const int stereoNormal=samples==64?35:71,monoNormal=samples==64?22:45;
  assert(MatchingProfileBytes(samples,true,stereoNormal,false,false)==monoNormal);
  assert(MatchingProfileBytes(samples,false,monoNormal,true,false)==stereoNormal);
  assert(MatchingProfileBytes(samples,true,samples*4,false,true)==samples*2);
  assert(MatchingProfileBytes(samples,true,13,false,false)==0);
  std::vector<InputSpec> specs(3);
  for(int i=0;i<3;++i){auto&s=specs[i];s.source=i;s.codec=samples==64?3:2;s.raw=true;
   s.route=i==2?InputRoute::Left:InputRoute::Stereo;s.channelA=i==0?0:(i==1?1:3);s.channelB=i==1?2:s.channelA;s.encodedBytes=samples*(i==2?2:4);}
  SourceProducer producer;assert(producer.Configure(samples,4,specs));
  std::vector<int16_t> pcm(samples*4);for(int i=0;i<samples;++i)for(int ch=0;ch<4;++ch)pcm[i*4+ch]=int16_t(i+1000*(ch+1));
  std::array<BoundedPacket,MaxSources> packets{};size_t count=0;assert(producer.EncodeBounded(pcm.data(),pcm.size(),false,20,100,MaxDatagram,packets,count));
  size_t seen[3]={};
  for(size_t p=0;p<count;++p){ParsedDatagram data;assert(ParseDatagram(packets[p].data.data(),packets[p].length,data));
   for(size_t f=0;f<data.count;++f){const auto& frag=data.fragments[f];seen[frag.source]+=frag.size;
    for(size_t byte=0;byte<frag.size;++byte){size_t absolute=frag.offset+byte;int valueIndex=int(absolute/2);int frame=valueIndex/frag.channels;int lane=valueIndex%frag.channels;
     int ch=frag.source==0?0:(frag.source==1?lane+1:3);uint16_t value=uint16_t(pcm[frame*4+ch]);
     uint8_t expected=uint8_t(value>>((absolute%2)*8));assert(frag.data[byte]==expected);}}}

  assert(seen[0]==size_t(samples*4)&&seen[1]==size_t(samples*4)&&seen[2]==size_t(samples*2));
 }
 std::vector<ControlSource> maximum;
 for(int i=0;i<20;++i){ControlSource s;s.source=i;s.codec=2;s.channels=1;s.bytes=45;s.name=std::string(64,'a'+i);s.instrument=i;maximum.push_back(s);}
 std::vector<uint8_t> largest;assert(EncodeConfig(999,maximum,largest));assert(largest.size()==1450);
 uint64_t maxEpoch=0;std::vector<ControlSource> maxDecoded;assert(DecodeConfig(largest.data(),largest.size(),maxEpoch,maxDecoded));assert(maxDecoded.size()==20&&maxEpoch==999);
 std::cout<<"PASS route bounds, duplicate/tag validation, v1/v2 icon metadata, every truncated prefix, stock profile reuse and exact mixed mono/stereo RAW64/128 routing\n";
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'src'),'-I'+str(root/'libs/opus/include'),str(p/'test.cpp'),str(root/'src/polyinproducer.cpp'),str(root/'src/polyinwire.cpp'),str(lib),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1'})
