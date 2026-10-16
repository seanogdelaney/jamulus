#!/usr/bin/env python3
"""Compile and execute actual CoreAudio/ASIO capture-loop snippets on host with SDK facades.
This is a source-extracted component test, NOT native platform/hardware qualification.
"""
import pathlib, re, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
a = (ROOT/'src/sound/asio/sound.cpp').read_text()
c = (ROOT/'src/sound/coreaudio-mac/sound.cpp').read_text()
b = (ROOT/'src/sound/soundbase.h').read_text()
def between(s, x, y):
    assert s.count(x) == 1 and y in s.split(x, 1)[1], (x,y)
    return s.split(x,1)[1].split(y,1)[0]
asio = between(a,'        // CAPTURE -------------------------------------------------------------', '        // call processing callback function')
core = between(c, '        // No allocation, extra IOProc, resampling or clock in the callback.\n', '        // call processing callback function')
start = b.index('static void GetSelCHAndAddCH')
br = b.index('{',start)
depth = 0
for k in range(br,len(b)):
    depth += (b[k]=='{') - (b[k]=='}')
    if depth == 0: break
helper=b[start:k+1]
cases=sorted(set(re.findall(r'case (ASIOST\w+):', asio)))
assert len(cases) >= 13
assert 'Sound.PolyInCaptureFrame()' in (ROOT/'src/client.cpp').read_text()
common = r'''
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>
using UInt32=unsigned;using Float32=float;
constexpr int INVALID_INDEX=-1, NUM_IN_OUT_CHANNELS=2;
constexpr float _MAXSHORT=32767.0f;
template<typename T> struct CVector:std::vector<T>{using std::vector<T>::vector;
  void Init(int n){this->assign(n,T{});} void Reset(T v){std::fill(this->begin(),this->end(),v);}
  int Size()const{return int(this->size());}};
static int16_t Float2Short(float v){return int16_t(std::clamp(v,-32768.f,32767.f));}
static int16_t Flip16Bits(int16_t v){uint16_t x=uint16_t(v);return int16_t((x>>8)|(x<<8));}
static int32_t Flip32Bits(int32_t v){uint32_t x=uint32_t(v);return int32_t((x>>24)|((x>>8)&0xff00)|((x<<8)&0xff0000)|(x<<24));}
static int64_t Flip64Bits(int64_t v){uint64_t x=uint64_t(v);
 x=((x&0xffffffffULL)<<32)|(x>>32);
 x=((x&0x0000ffff0000ffffULL)<<16)|((x&0xffff0000ffff0000ULL)>>16);
 x=((x&0x00ff00ff00ff00ffULL)<<8)|((x&0xff00ff00ff00ff00ULL)>>8);return int64_t(x);}
'''
as_test=common+'enum ASIOType{'+','.join(cases)+'};\n'+r'''
struct ASIOInfo{ASIOType type;}; struct ASIOBuf{void* buffers[2];};
struct ASIOFake{int polyInCaptureChannels=2,lNumInChan=4,iASIOBufferSizeMono=3;
  int vSelectedInputChannels[2]={3,0};ASIOInfo channelInfosInput[4]{};
  ASIOBuf bufferInfos[4]{};CVector<int16_t> polyInCaptureFrame,vecsMultChanAudioSndCrd;};
'''+helper+r'''
void exercise(ASIOFake* pSound,long index){int iCurSample;
 int& iASIOBufferSizeMono=pSound->iASIOBufferSizeMono;
 CVector<int16_t>& vecsMultChanAudioSndCrd=pSound->vecsMultChanAudioSndCrd;
 {'''+asio+r'''}}
int main(){
 ASIOFake s;int16_t ch0[3]={100,101,102},ch3[3]={-400,-401,-402};
 int32_t ch1[3]={201<<16,202<<16,203<<16};float ch2[3]={.25f,.5f,-.25f};
 void* bufs[]={ch0,ch1,ch2,ch3};
 ASIOType ty[]={ASIOSTInt16LSB,ASIOSTInt32LSB,ASIOSTFloat32LSB,ASIOSTInt16LSB};
 for(int i=0;i<4;++i){s.channelInfosInput[i].type=ty[i];s.bufferInfos[i].buffers[0]=bufs[i];}
 s.vecsMultChanAudioSndCrd.Init(6);s.polyInCaptureFrame.Init(12);
 s.polyInCaptureChannels=4;exercise(&s,0);
 const int16_t e[12]={100,201,8191,-400,101,202,16383,-401,102,203,-8191,-402};
 for(int i=0;i<12;++i)if(s.polyInCaptureFrame[i]!=e[i]){
 std::cerr<<"ASIO order/fmt mismatch "<<i<<" got "<<s.polyInCaptureFrame[i]<<" expected "<<e[i]<<"\n";return 1;}
 for(int i=0;i<3;++i)if(s.vecsMultChanAudioSndCrd[2*i]!=ch3[i]||s.vecsMultChanAudioSndCrd[2*i+1]!=ch0[i])return 2;
 s.polyInCaptureChannels=2;s.polyInCaptureFrame.Reset(777);s.vecsMultChanAudioSndCrd.Reset(0);
 exercise(&s,0);
 for(int i=0;i<3;++i)if(s.vecsMultChanAudioSndCrd[2*i]!=ch3[i]||s.vecsMultChanAudioSndCrd[2*i+1]!=ch0[i])return 3;
 for(auto x:s.polyInCaptureFrame)if(x!=777)return 4;
 std::cout<<"ASIO multi-format, physical ordering, stock parity PASS\n";}
'''
ca_test=common+r'''
struct AudioBuffer{UInt32 mNumberChannels,mDataByteSize;void* mData;};
struct AudioBufferList{UInt32 mNumberBuffers;AudioBuffer mBuffers[8];};
struct CoreFake{int polyInCaptureChannels=4;CVector<int16_t> polyInCaptureFrame;};
void exercise(CoreFake* pSound,AudioBufferList* inInputData,CVector<int>& vecNumInBufChan,int iCoreAudioBufferSizeMono){
'''+core+r'''}
int main(){(void)INVALID_INDEX;(void)NUM_IN_OUT_CHANNELS;CoreFake s;s.polyInCaptureFrame.Init(12);
 float a[3]={.25f,.5f,-.25f};float b[9]={.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f,.9f};
 AudioBufferList v{};v.mNumberBuffers=2;v.mBuffers[0]={1,sizeof(a),a};v.mBuffers[1]={3,sizeof(b),b};
 CVector<int> lanes={1,3};exercise(&s,&v,lanes,3);
 float expected[]={.25f,.1f,.2f,.3f,.5f,.4f,.5f,.6f,-.25f,.7f,.8f,.9f};
 for(int i=0;i<12;++i)if(s.polyInCaptureFrame[i]!=Float2Short(expected[i]*_MAXSHORT))return 1;
 v.mBuffers[1].mDataByteSize-=4;exercise(&s,&v,lanes,3);
 for(auto x:s.polyInCaptureFrame)if(x!=0)return 2;
 s.polyInCaptureChannels=3;s.polyInCaptureFrame.Init(9);
 float p[3]={.1f,.2f,.3f},q[3]={.4f,.5f,.6f},r[3]={.7f,.8f,.9f};
 v.mNumberBuffers=3;v.mBuffers[0]={1,sizeof(p),p};v.mBuffers[1]={1,sizeof(q),q};v.mBuffers[2]={1,sizeof(r),r};
 lanes={1,1,1};exercise(&s,&v,lanes,3);
 for(int i=0;i<3;++i){if(s.polyInCaptureFrame[i*3]!=Float2Short(p[i]*_MAXSHORT))return 3;
 if(s.polyInCaptureFrame[i*3+1]!=Float2Short(q[i]*_MAXSHORT))return 4;
 if(s.polyInCaptureFrame[i*3+2]!=Float2Short(r[i]*_MAXSHORT))return 5;}
 std::cout<<"CoreAudio interleaved, noninterleaved, fail-closed PASS\n";}
'''
with tempfile.TemporaryDirectory(prefix='polyin-native-') as dir:
    for name,s in [('asio',as_test),('coreaudio',ca_test)]:
        source=pathlib.Path(dir)/(name+'.cpp');binary=pathlib.Path(dir)/name
        source.write_text(s)
        subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
                        '-Wno-unused-function','-fsanitize=undefined','-fno-omit-frame-pointer',
                        str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print('SOURCE_EXTRACTED_CAPTURE_PASS; NATIVE_MAC_WINDOWS_NOT_RUN')
