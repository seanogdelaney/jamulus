#!/usr/bin/env python3
"""Run the unchanged production src/buffer.cpp algorithms with Qt-free vector shim.

Exactly src/buffer.cpp implementation code is compiled. Qt-heavy includes of
src/buffer.h are replaced in a generated throw-away header, while stock
CMovingAv and CErrorRate templates are copied verbatim from src/util.h.
This is real stock receive code, NOT a Jamulus Qt/audio full-path test.
"""
import argparse
import hashlib
import pathlib
import subprocess
import tempfile
import sys

p = argparse.ArgumentParser()
p.add_argument('--golden', action='store_true', help='compare trace SHA to committed baseline')
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
src = root/'src'
util = (src/'util.h').read_text()
header = (src/'buffer.h').read_text()
code = (src/'buffer.cpp').read_text()

def between(s, first, last):
    return s[s.index(first):s.index(last, s.index(first))]

moving = between(util, 'template<class TData>\nclass CMovingAv', '/******************************************************************************\\\n* GUI Utilities')
err = between(util, 'class CErrorRate', '// Generic hash functor for enum classes')
math = between(util, 'class MathUtils', '    // calculate pan gains:') + '};\n'
vector = '''template<class T> class CVector : public std::vector<T> {
public:
 CVector() = default;
 explicit CVector(int n) { Init(n); }
 CVector(int n, T value) { Init(n,value); }
 void Init(int n) { this->clear(); this->resize(n); }
 void Init(int n, T x) { Init(n); Reset(x); }
 void Reset(T x) { std::fill(this->begin(),this->end(),x); }
 void Enlarge(int n) { this->resize(this->size()+n); }
 void Add(const T& x) { this->push_back(x); }
 int Size() const { return (int)this->size(); }
};
'''
shim = '''#pragma once\n#include <vector>\n#include <algorithm>\n#include <cstdint>\n#include <cmath>\n#include <cstring>\n#include <cstdio>\n#include <cstdlib>\n''' + vector + '\n' + moving + '\n' + err + '\n' + math + '\n'
header = header.replace('#include "util.h"\n#include "global.h"', '#include "stock_vector_shim.h"')
runner = (pathlib.Path(__file__).with_name('stock_buffer_trace.cpp')).read_text()
with tempfile.TemporaryDirectory(prefix='jamulus-real-stock-buffer-') as td:
    tmp=pathlib.Path(td)
    (tmp/'stock_vector_shim.h').write_text(shim)
    (tmp/'buffer.h').write_text(header)
    (tmp/'buffer.cpp').write_text(code)
    (tmp/'stock_buffer_trace.cpp').write_text(runner)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-pedantic',str(tmp/'buffer.cpp'),str(tmp/'stock_buffer_trace.cpp'),'-o',str(tmp/'stock-buffer-test')]
    print('BUILD: '+' '.join(cmd),file=sys.stderr)
    subprocess.run(cmd,check=True)
    result=subprocess.run([str(tmp/'stock-buffer-test')],capture_output=True,text=True,check=True)
    digest=hashlib.sha256(result.stdout.encode()).hexdigest()
    print(result.stderr,end='',file=sys.stderr)
    print('TRACE_SHA256='+digest,file=sys.stderr)
    if a.golden:
        expected=pathlib.Path(__file__).with_name('stock_buffer_trace.sha256').read_text().strip()
        if digest!=expected:
            print('FAIL baseline real-stock buffer trace mismatch',file=sys.stderr)
            print(result.stdout[:1000],file=sys.stderr)
            sys.exit(1)
    print(result.stdout,end='')
