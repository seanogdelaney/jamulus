#!/usr/bin/env python3
"""Source-extract real production timer/plumbing DSP & CChannel controls.
The Qt/socket/recorder dispatcher is substituted; coverage is *not* a full Qt run.
"""
from pathlib import Path
import re,subprocess,tempfile,sys,os,argparse
p=argparse.ArgumentParser();p.add_argument('--pre-fix',action='store_true',help='reintroduce old post-physical stale-source bug in memory');p.add_argument('--sanitize',action='store_true');p.add_argument('--pre-history-fix',action='store_true',help='remove cross-tick source-history remap (negative control)');p.add_argument('--pre-fader-fix',action='store_true');p.add_argument('--pre-opus-fix',action='store_true');p.add_argument('--pre-timer-lock',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];src=root/'src';here=Path(__file__).resolve().parent
server=(src/'server.cpp').read_text()
if a.pre_fader_fix:
 assert server.count('if ( changedIdentity )')==1
 server=server.replace('if ( changedIdentity )','if ( !vecChannels[s.channel].IsPolyInVirtual() )')
if a.pre_opus_fix:
 for decoder in ('OpusDecoderMono','OpusDecoderStereo','Opus64DecoderMono','Opus64DecoderStereo'):
  statement=f'            opus_custom_decoder_ctl ( {decoder}[s.channel], OPUS_RESET_STATE );'
  assert statement in server;server=server.replace(statement,'')
if a.pre_timer_lock:
 assert server.count('if ( !bPolyInAtStart ) locker.unlock();')==1
 server=server.replace('if ( !bPolyInAtStart ) locker.unlock();','locker.unlock();')
if a.pre_history_fix:
 start=server.index('        // Stock --delaypan reads vecvecsData2')
 end=server.index('        // Physical source-zero has consumed the stock window.',start)
 server=server[:start]+server[end:]
if a.pre_fix:
 start=server.index('        // A physical GetData may have timed out')
 end=server.index('        // Physical source-zero has consumed the stock window.',start)
 server=server[:start]+server[end:]
 server=server.replace('if ( iNumMixSources > iNumClients )\n            DecodeReceiveDataBlocks', 'if ( bHasPolyInAudio )\n            DecodeReceiveDataBlocks',1)
util=(src/'util.h').read_text();buff=(src/'buffer.h').read_text();chan=(src/'channel.cpp').read_text()
def method(s,clazz,name,result):
 m=re.search(rf'{result}\s+{clazz}::{name}\s*\([^)]*\)\s*\{{',s);assert m,(clazz,name)
 pos=s.index('{',m.start());depth=0
 for i in range(pos,len(s)):
  if s[i]=='{':depth+=1
  elif s[i]=='}':
   depth-=1
   if depth==0:return s[m.start():i+1]
 raise ValueError(name)
def snippet(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
short=snippet(util,'inline short Float2Short','inline int CalcBitRateBitsPerSecFromCodedBytes')
bitrate=snippet(util,'inline int CalcBitRateBitsPerSecFromCodedBytes','QString GetVersionAndNameStr')
left=re.search(r'static inline float GetLeftPan\s*\([^\n]*',util).group(0)
right=re.search(r'static inline float GetRightPan\s*\([^\n]*',util).group(0)
conv=snippet(buff,'template<class TData>\nclass CConvBuf','\n};')+'\n};'
control=[method(chan,'CChannel','SetGain','void'),method(chan,'CChannel','SetPan','void'),method(chan,'CChannel','GetGainsAndPannings','void')]
methods=[method(server,'CServer',name,result) for name,result in [
 ('DecodeReceiveData','void'),('MixEncodeTransmitData','void'),('DecodeReceiveDataBlocks','void'),
 ('MixEncodeTransmitDataBlocks','void'),('OnTimer','void'),('CreateLevelsForAllConChannels','bool'),
 ('FreeChannel','void'),('ConfigurePolyInAudio','bool'),('DisablePolyInAudio','void')]]
code=(here/'server_plumbing_fixture.cpp').read_text()
code=code.replace('// STOCK_UTIL_EXTRACT',short+'\n'+bitrate+'\nclass MathUtils { public: '+left+'\n'+right+'\n template<class T> static bool InRange(T v,T l,T u) {return v>=l&&v<u;}\n};')
code=code.replace('// STOCK_CONVBUF_EXTRACT',conv)
code=code.replace('// STOCK_METHODS_EXTRACT','\n'.join(control+methods))
with tempfile.TemporaryDirectory(prefix='stage04-server-plumbing-') as td:
 pth=Path(td);(pth/'fixture.cpp').write_text(code)
 libdir=Path(os.environ.get('POLYIN_OPUS_BUILD','/mnt/data/opus-build'));lib=libdir/'libopus.a'
 if not lib.exists():
  subprocess.run(['cmake','-S',str(root/'libs/opus'),'-B',str(libdir),'-DOPUS_CUSTOM_MODES=ON','-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF','-DCMAKE_BUILD_TYPE=Release'],check=True,stdout=subprocess.DEVNULL)
  subprocess.run(['cmake','--build',str(libdir),'-j','4'],check=True,stdout=subprocess.DEVNULL)
 cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wno-unused-parameter','-I'+str(root/'libs/opus/include'),'-I'+str(src),str(pth/'fixture.cpp'),str(src/'polyinsources.cpp'),str(lib),'-pthread','-lm','-o',str(pth/'test')]
 if a.sanitize:cmd[1:1]=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 print('BUILD '+' '.join(cmd),flush=True)
 subprocess.run(cmd,check=True)
 test=subprocess.run([str(pth/'test')],capture_output=True,text=True)
 print(test.stdout,end='');print(test.stderr,end='',file=sys.stderr)
 sys.exit(test.returncode)
