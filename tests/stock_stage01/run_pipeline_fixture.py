#!/usr/bin/env python3
"""Compile and run unmodified extracted real CServer decode and mix methods.

Usage: python3 run_pipeline_fixture.py --stock /path/to/pristine-or-R01/Jamulus
Reuses stock util.h Float2Short/pan and stock buffer.h CConvBuf methods.
The enclosing CServer/CChannel are *test doubles*, so no L05/GUI/network pass.
Real Opus from stock vendored libs/opus is built if necessary; no Qt needed.
"""
import argparse, hashlib, pathlib, re, subprocess, sys, tempfile
p=argparse.ArgumentParser()
p.add_argument('--stock',required=True,type=pathlib.Path)
p.add_argument('--golden',action='store_true',help='check against recorded compiler/platform reference hashes; use pristine/tip comparison for portable parity')
p.add_argument('--expected-sha256',type=str,default=None,help='strict expected fingerprint for this toolchain')
p.add_argument('--build-dir',type=pathlib.Path,default=None)
a=p.parse_args()
root=a.stock.resolve(); here=pathlib.Path(__file__).resolve().parent
server=(root/'src/server.cpp').read_text(); util=(root/'src/util.h').read_text(); buff=(root/'src/buffer.h').read_text()
def extract_method(name):
    m=re.search(r'void\s+CServer::'+name+r'\s*\([^)]*\)\s*\{',server)
    if not m: raise RuntimeError('could not locate stock method '+name)
    depth=0
    for i in range(server.index('{',m.start()),len(server)):
        if server[i]=='{':depth+=1
        elif server[i]=='}':
            depth-=1
            if depth==0:return server[m.start():i+1]
    raise RuntimeError('unbalanced method')
def snippet(src,first,last):return src[src.index(first):src.index(last,src.index(first))]
short=snippet(util,'inline short Float2Short', 'inline int CalcBitRateBitsPerSecFromCodedBytes')
bitrate=snippet(util,'inline int CalcBitRateBitsPerSecFromCodedBytes','QString GetVersionAndNameStr')
# Extract the original functions, but only the declarations relevant to pan.
left=re.search(r'static inline float GetLeftPan\s*\([^\n]*',util).group(0)
right=re.search(r'static inline float GetRightPan\s*\([^\n]*',util).group(0)
conv=snippet(buff,'template<class TData>\nclass CConvBuf','\n};')+'\n};'
sh=here/'stock_pipeline_fixture.cpp'
template=sh.read_text()
out=template.replace('// STOCK_UTIL_EXTRACT',short+'\n'+bitrate+'\nclass MathUtils { public: '+left+'\n'+right+'\n};')
out=out.replace('// STOCK_CONVBUF_EXTRACT',conv)
out=out.replace('// STOCK_METHODS_EXTRACT','\n'.join(extract_method(s) for s in ('DecodeReceiveData','MixEncodeTransmitData')))
with tempfile.TemporaryDirectory(prefix='jamulus-pipeline-') as td:
    scratch=pathlib.Path(td)
    cpp=scratch/'stock_fixture.cpp';cpp.write_text(out)
    build=a.build_dir if a.build_dir else scratch/'opus-build'
    lib=build/'libopus.a'
    if not lib.exists():
        cmd=['cmake','-S',str(root/'libs/opus'),'-B',str(build),'-DOPUS_CUSTOM_MODES=ON', '-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF','-DCMAKE_BUILD_TYPE=Release']
        subprocess.run(cmd,check=True,stdout=sys.stderr,stderr=sys.stderr)
        subprocess.run(['cmake','--build',str(build),'-j','4'],check=True,stdout=sys.stderr,stderr=sys.stderr)
    # Ensure a caller-supplied cached lib is built with OPUS_CUSTOM_MODES.
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wno-unused-parameter','-I'+str(root/'libs/opus/include'),str(cpp),str(lib),'-lm','-o',str(scratch/'stock-pipeline')]
    subprocess.run(cmd,check=True,stdout=sys.stderr,stderr=sys.stderr)
    r=subprocess.run([str(scratch/'stock-pipeline')],capture_output=True,text=True)
    sys.stderr.write(r.stderr)
    if r.returncode: sys.stderr.write(r.stdout);sys.exit(r.returncode)
    sha=hashlib.sha256(r.stdout.encode()).hexdigest()
    print('TRACE_SHA256='+sha,file=sys.stderr)
    if a.golden:
        references={line.split()[0] for line in (here/'pipeline_trace.sha256').read_text().splitlines() if line.strip() and not line.startswith('#')}
        if sha not in references:raise RuntimeError('Unknown reference fingerprint: '+sha+'; check semantic assertions and baseline-versus-R01 parity on this toolchain')
    if a.expected_sha256 and sha!=a.expected_sha256:
        raise RuntimeError('Expected trace SHA '+a.expected_sha256+' but observed '+sha)
    sys.stdout.write(r.stdout)
