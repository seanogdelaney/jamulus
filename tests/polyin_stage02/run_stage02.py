#!/usr/bin/env python3
"""Compile actual pinned production buffer.cpp and F01/F02 with test-only Qt-free stock headers.
Only CVector/MathUtils/CErrorRate header support is shimmed, as in accepted Stage 01.
Neither buffer.cpp nor polyinframe.cpp is copied/reimplemented in Python.
"""
import pathlib, subprocess, tempfile, sys, argparse, os
root=pathlib.Path(__file__).resolve().parents[2]
src=root/'src'
tests=root/'tests/stock_stage01'
p=argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
p.add_argument('--log', default='')
a=p.parse_args()
def between(s,start,end): return s[s.index(start):s.index(end,s.index(start))]
util=(src/'util.h').read_text()
vector='''template<class T> class CVector : public std::vector<T> {
public:
 CVector() = default;
 explicit CVector(int n) { Init(n); }
 CVector(int n,T value) { Init(n,value); }
 void Init(int n) { this->clear(); this->resize(n); }
 void Init(int n,T x) { Init(n); Reset(x); }
 void Reset(T x) { std::fill(this->begin(),this->end(),x); }
 void Enlarge(int n) { this->resize(this->size()+n); }
 void Add(const T& x) { this->push_back(x); }
 int Size() const { return (int)this->size(); }
};'''
shim='''#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
'''+vector+'\n'+between(util,'template<class TData>\nclass CMovingAv','/******************************************************************************\\\n* GUI Utilities')+'\n'+between(util,'class CErrorRate','// Generic hash functor for enum classes')+'\n'+between(util,'class MathUtils','    // calculate pan gains:')+'};\n'
# Compile the actual stock CChannel bounds, not duplicated adapter test constants.
global_h=(src/'global.h').read_text()
bounds=[line for line in global_h.splitlines() if line.startswith('#define MIN_NET_BUF_SIZE_NUM_BL') or line.startswith('#define MAX_NET_BUF_SIZE_NUM_BL')]
assert len(bounds)==2, 'Actual stock channel bounds missing'
shim+='\n'+'\n'.join(bounds)+'\n'
header=(src/'buffer.h').read_text().replace('#include "util.h"\n#include "global.h"','#include "stock_vector_shim.h"')
with tempfile.TemporaryDirectory(prefix='jamulus-polyin-stage2-') as td:
    path=pathlib.Path(td)
    for file in ('buffer.cpp','polyinwire.h','polyinwire.cpp','polyinframe.h','polyinframe.cpp'):
        (path/file).write_bytes((src/file).read_bytes())
    (path/'buffer.h').write_text(header)
    (path/'stock_vector_shim.h').write_text(shim)
    args=['g++','-std=c++17','-O1' if a.sanitize else '-O2','-g','-Wall','-Wextra','-Werror','-pedantic']
    if a.sanitize: args+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    args+=[str(path/'buffer.cpp'),str(path/'polyinwire.cpp'),str(path/'polyinframe.cpp'),str(root/'tests/polyin_stage02/stage02.cpp'),'-I'+str(path),'-o',str(path/'stage02-test')]
    print('BUILD '+' '.join(args),flush=True)
    subprocess.run(args,check=True)
    env=os.environ.copy()
    if a.sanitize: env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
    proc=subprocess.run([str(path/'stage02-test')],text=True,capture_output=True,env=env)
    print(proc.stdout,end=''); print(proc.stderr,end='',file=sys.stderr)
    print('RETURN',proc.returncode)
    if a.log: pathlib.Path(a.log).write_text('BUILD '+' '.join(args)+'\n'+proc.stdout+'\nSTDERR\n'+proc.stderr+'\nRETURN '+str(proc.returncode)+'\n')
    sys.exit(proc.returncode)
