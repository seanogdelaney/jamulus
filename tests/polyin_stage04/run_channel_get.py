#!/usr/bin/env python3
"""Actual CChannel::GetData body + production F01/F02 + production stock buffer.cpp.
Qt protocol/signal/mutex types are small inert test host substitutes, not a Qt server.
"""
from pathlib import Path
import re,subprocess,tempfile,sys,os
root=Path(__file__).resolve().parents[2];src=root/'src';here=Path(__file__).resolve().parent
def extract(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
def function(s,name):
 pattern=r'EGetDataStat\s+CChannel::'+name+r'\s*\([^)]*\)\s*\{'
 m=re.search(pattern,s);assert m,name;pos=s.index('{',m.start());dep=0
 for k in range(pos,len(s)):
  if s[k]=='{':dep+=1
  elif s[k]=='}':
   dep-=1
   if dep==0:return s[m.start():k+1]
 raise RuntimeError('unbalanced')
actual=function((src/'channel.cpp').read_text(),'GetData')
util=(src/'util.h').read_text()
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
global_h=(src/'global.h').read_text();bounds=[x for x in global_h.splitlines() if x.startswith(('#define MIN_NET_BUF_SIZE_NUM_BL','#define MAX_NET_BUF_SIZE_NUM_BL'))]
assert len(bounds)==2
shim+='\n'.join(bounds)+'\n'
header=(src/'buffer.h').read_text().replace('#include "util.h"\n#include "global.h"','#include "stock_vector_shim.h"')
template=(here/'channel_get_fixture.cpp').read_text()
enum=extract(util,'enum EGetDataStat','};')+'};'
code=template.replace('// STOCK_CHANNEL_GET',actual).replace('// STOCK_GET_ENUM',enum)
with tempfile.TemporaryDirectory(prefix='stage04-actual-channel-') as td:
 p=Path(td)
 for fn in ('buffer.cpp','polyinframe.h','polyinframe.cpp','polyinwire.h','polyinwire.cpp'): (p/fn).write_bytes((src/fn).read_bytes())
 (p/'buffer.h').write_text(header);(p/'stock_vector_shim.h').write_text(shim);(p/'fixture.cpp').write_text(code)
 cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',str(p/'buffer.cpp'),str(p/'polyinwire.cpp'),str(p/'polyinframe.cpp'),str(p/'fixture.cpp'),'-I'+str(p),'-pthread','-o',str(p/'test')]
 print('BUILD '+' '.join(cmd));subprocess.run(cmd,check=True)
 test=subprocess.run([str(p/'test')],capture_output=True,text=True)
 print(test.stdout,end='');print(test.stderr,end='',file=sys.stderr)
 sys.exit(test.returncode)
