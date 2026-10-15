#!/usr/bin/env python3
"""Compile source-extracted ACTUAL stock CServer decode/mixer with real Opus.
Not a substitute for full Qt OnTimer or CChannel implementation test.
"""
import pathlib, re, subprocess, tempfile, hashlib, sys, os
root=pathlib.Path(__file__).resolve().parents[2]
here=pathlib.Path(__file__).resolve().parent
server=(root/'src/server.cpp').read_text()
util=(root/'src/util.h').read_text()
buff=(root/'src/buffer.h').read_text()
def extract(name):
 m=re.search(r'void\s+CServer::'+name+r'\s*\([^)]*\)\s*\{',server)
 assert m, name
 pos=server.index('{',m.start());dep=0
 for i in range(pos,len(server)):
  if server[i]=='{':dep+=1
  elif server[i]=='}':
   dep-=1
   if dep==0:return server[m.start():i+1]
 raise RuntimeError('unbalanced')
def snippet(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
short=snippet(util,'inline short Float2Short','inline int CalcBitRateBitsPerSecFromCodedBytes')
bitrate=snippet(util,'inline int CalcBitRateBitsPerSecFromCodedBytes','QString GetVersionAndNameStr')
left=re.search(r'static inline float GetLeftPan\s*\([^\n]*',util).group(0)
right=re.search(r'static inline float GetRightPan\s*\([^\n]*',util).group(0)
conv=snippet(buff,'template<class TData>\nclass CConvBuf','\n};')+'\n};'
template=(here/'stage04_fixture.cpp').read_text()
code=template.replace('// STOCK_UTIL_EXTRACT',short+'\n'+bitrate+'\nclass MathUtils { public: '+left+'\n'+right+'\n};')
code=code.replace('// STOCK_CONVBUF_EXTRACT',conv)
code=code.replace('// STOCK_METHODS_EXTRACT','\n'.join(extract(s) for s in ('DecodeReceiveData','MixEncodeTransmitData')))
checks={
 'original delay history':'const CVector<int16_t>& vecsData2 = vecvecsData2[j];' in extract('MixEncodeTransmitData'),
 'delay history in live OnTimer':'for ( int i = 0; i < iNumMixSources; i++ )' in server,
 'real caller physical only':'MixEncodeTransmitData ( iChanCnt, iNumMixSources )' in server,
 'real stock output codec':'vecChannels[iCurChanID].PrepAndSendPacket ( &Socket, vecvecbyCodedData[iChanCnt], iCeltNumCodedBytes );' in extract('MixEncodeTransmitData'),
 'additional sources original decode':'DecodeReceiveDataBlocks ( this, iNumClients, iNumMixSources - 1, iNumMixSources );' in server,
 'physical timeout stock method':'iConTimeOut -= iAudioFrameSizeSamples;' in (root/'src/channel.cpp').read_text(),
 'single physical stock window':'PolyFrameBuffer->Get ( view )' in (root/'src/channel.cpp').read_text()
}
for name, ok in checks.items():
 print('SOURCE_AUDIT',name, 'PASS' if ok else 'FAIL',file=sys.stderr)
 if not ok:sys.exit(1)
with tempfile.TemporaryDirectory() as td:
 path=pathlib.Path(td);cpp=path/'stage04.cpp';cpp.write_text(code)
 # Repeatably built real vendored custom Opus, sharing cache between invocations.
 libdir=pathlib.Path(os.environ.get('POLYIN_OPUS_BUILD','/tmp/polyin-stage04-opus'))
 lib=libdir/'libopus.a'
 if not lib.exists():
  subprocess.run(['cmake','-S',str(root/'libs/opus'),'-B',str(libdir),'-DOPUS_CUSTOM_MODES=ON','-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF','-DCMAKE_BUILD_TYPE=Release'],check=True,stdout=subprocess.DEVNULL)
  subprocess.run(['cmake','--build',str(libdir),'-j','4'],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Wno-unused-parameter', '-I'+str(root/'libs/opus/include'),str(cpp),str(lib),'-lm','-o',str(path/'test')],check=True)
 r=subprocess.run([str(path/'test')],capture_output=True,text=True)
 print(r.stdout,end='')
 print(r.stderr,end='',file=sys.stderr)
 print('TRACE_SHA256='+hashlib.sha256(r.stdout.encode()).hexdigest(),file=sys.stderr)
 sys.exit(r.returncode)
