#!/usr/bin/env python3
"""Real source-extracted production OnNetTranspPropsReceived/GetData and F02/stock.
A format-preserving transport-properties reannouncement must not switch the
one borrowed stock receive buffer from token framing back to ordinary PCM size.
Qt mutex/protocol classes are test host adapters, not Qt runtime validation.
"""
from pathlib import Path
import re,subprocess,tempfile,sys,argparse
p=argparse.ArgumentParser();p.add_argument('--pre-fix',action='store_true',help='negative control using original rejected F05 handler')
a=p.parse_args()
root=Path(__file__).resolve().parents[2];src=root/'src';here=Path(__file__).resolve().parent
text=(src/'channel.cpp').read_text()
if a.pre_fix:
 # Negative-control experiment: exactly restore the stock-only Init branch
 # from the independently rejected F05, without modifying the worktree.
 amended='if ( PolyFrameBuffer ) PolyFrameBuffer->Init ( PolyFrameBuffer->Generation(), iCurSockBufNumFrames );\n                else SockBuf.Init ( iCeltNumCodedBytes, iCurSockBufNumFrames, bUseSequenceNumber );'
 stock='SockBuf.Init ( iCeltNumCodedBytes, iCurSockBufNumFrames, bUseSequenceNumber );'
 assert text.count(amended)==1
 text=text.replace(amended,stock)
def fn(text,name,returntype):
 pat=rf'{returntype}\s+CChannel::{name}\s*\([^)]*\)\s*\{{'
 match=re.search(pat,text);assert match,name
 depth=0
 for j in range(text.index('{',match.start()),len(text)):
  if text[j]=='{':depth+=1
  elif text[j]=='}':
   depth-=1
   if depth==0:return text[match.start():j+1]
 raise AssertionError(name)
util=(src/'util.h').read_text()
def extract(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
vector='''template<class T> class CVector : public std::vector<T> {
public: CVector()=default; explicit CVector(int n){Init(n);}
void Init(int n){this->clear();this->resize(n);} void Init(int n,T x){Init(n);Reset(x);}
void Reset(T x){std::fill(this->begin(),this->end(),x);} void Enlarge(int n){this->resize(this->size()+n);}
void Add(const T& x){this->push_back(x);} int Size()const{return (int)this->size();}
};'''
shim='''#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
'''+vector+'\n'+extract(util,'template<class TData>\nclass CMovingAv','/******************************************************************************\\\n* GUI Utilities')+'\n'+extract(util,'class CErrorRate','// Generic hash functor for enum classes')+'\n'+extract(util,'class MathUtils','    // calculate pan gains:')+'};\n'
global_h=(src/'global.h').read_text();bounds=[x for x in global_h.splitlines() if x.startswith(('#define MIN_NET_BUF_SIZE_NUM_BL','#define MAX_NET_BUF_SIZE_NUM_BL'))];assert len(bounds)==2
shim+='\n'.join(bounds)+'\n'
header=(src/'buffer.h').read_text().replace('#include "util.h"\n#include "global.h"','#include "stock_vector_shim.h"')
template=(here/'transport_props_fixture.cpp').read_text()
code=(template.replace('// STOCK_GET_ENUM',extract(util,'enum EGetDataStat','};')+'};')
 .replace('// STOCK_CHANNEL_GET',fn(text,'GetData','EGetDataStat'))
 .replace('// STOCK_TRANSPORT_PROPS',fn(text,'OnNetTranspPropsReceived','void'))
 .replace('// STOCK_ENABLE_POLYIN',fn(text,'EnablePolyInPhysical','bool')))
with tempfile.TemporaryDirectory(prefix='stage04-actual-transport-') as td:
 work=Path(td)
 for f in ('buffer.cpp','polyinframe.h','polyinframe.cpp','polyinwire.h','polyinwire.cpp'): (work/f).write_bytes((src/f).read_bytes())
 (work/'buffer.h').write_text(header);(work/'stock_vector_shim.h').write_text(shim);(work/'fixture.cpp').write_text(code)
 cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',str(work/'buffer.cpp'),str(work/'polyinwire.cpp'),str(work/'polyinframe.cpp'),str(work/'fixture.cpp'),'-I'+str(work),'-pthread','-o',str(work/'test')]
 print('BUILD '+' '.join(cmd),flush=True);subprocess.run(cmd,check=True)
 test=subprocess.run([str(work/'test')],capture_output=True,text=True)
 print(test.stdout,end='');print(test.stderr,end='',file=sys.stderr)
 sys.exit(test.returncode)
