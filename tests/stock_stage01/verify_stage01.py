#!/usr/bin/env python3
"""Stage-1 acceptance component replay, from immutable master and handoff ZIP.

One-shot command:
 python3 verify_stage01.py --master jamulus-polyin-immutable-master-v4.zip \\
   --handoff stage-01-output.zip --output stage01-fixture-evidence

Creates two clean pristine-stock extractions, patches only the second, builds
stock's vendored Opus once, and compares *actual extracted stock CServer method*
traces using one identical test-only environment. Full Qt lifecycle is NOT_RUN.
"""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

p=argparse.ArgumentParser()
p.add_argument('--master',type=pathlib.Path,required=True)
p.add_argument('--handoff',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args()
master=a.master.resolve();handoff=a.handoff.resolve();out=a.output.resolve()
out.mkdir(parents=True,exist_ok=True)
here=pathlib.Path(__file__).resolve().parent

def sha(data):return hashlib.sha256(data).hexdigest()
def run(cmd,log,cwd=None,stdoutfile=None):
    with (out/log).open('w') as f:
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        f.write(r.stdout.decode('utf-8','replace'))
    if r.returncode: raise RuntimeError(f'{log}: exit {r.returncode}; see {out/log}')
    if stdoutfile:
        # Runner traces are stdout-only, and diagnostics belong to separate logs.
        (out/stdoutfile).write_bytes(r.stdout)
    return r.stdout

with tempfile.TemporaryDirectory(prefix='jamulus-stage01-fixtures-') as td:
    work=pathlib.Path(td)
    with zipfile.ZipFile(master) as z:
        with z.open(next(n for n in z.namelist() if n.endswith('/original/inputs/jamulus-main(3).zip'))) as f:blob=f.read()
    want='f56e200fabd3bc52e5799cd76a5cc864b31c0dd7e659eff6936c96112ed85363'
    assert sha(blob)==want, 'bad authoritative baseline ZIP SHA'
    (out/'BASELINE_SHA256.txt').write_text(want+'\n')
    for n in ('pristine','r01'):
        target=work/n;target.mkdir()
        with zipfile.ZipFile(__import__('io').BytesIO(blob)) as z:z.extractall(target)
        assert (target/'jamulus-main/src/server.cpp').exists(), f'bad source layout {n}'
    with zipfile.ZipFile(handoff) as z:z.extractall(work/'handoff')
    stage=work/'handoff';pristine=work/'pristine/jamulus-main';patched=work/'r01/jamulus-main'
    run(['bash',stage/'apply-patches.sh',patched],'01-apply.log')
    runner=here/'run_pipeline_fixture.py'
    libdir=work/'opus-build'
    def fixture(stock,label):
        cmd=['python3',runner,'--stock',stock,'--build-dir',libdir]
        with (out/(label+'-stderr.log')).open('w') as err:
            with (out/(label+'-trace.txt')).open('wb') as f:
                r=subprocess.run(list(map(str,cmd)),stdout=f,stderr=err)
        if r.returncode: raise RuntimeError(f'{label} fixture FAILED, see stderr')
        b=(out/(label+'-trace.txt')).read_bytes()
        (out/(label+'-sha256.txt')).write_text(sha(b)+'\n')
        return b
    original=fixture(pristine,'02-stock')
    rtip=fixture(patched,'03-r01')
    if original!=rtip:raise RuntimeError('FAIL baseline versus R01 server pipeline trace mismatch')
    # Also execute L01 pinned buffer harness (may be more compiler-sensitive).
    l01=patched/'tests/stock_stage01/run_buffer_real_stock.py'
    with (out/'04-L01-stderr.log').open('w') as e, (out/'04-L01-trace.csv').open('wb') as f:
        r=subprocess.run(['python3',str(l01),'--golden'],cwd=patched,stdout=f,stderr=e)
    if r.returncode:raise RuntimeError('FAIL L01 golden (see 04-L01-stderr.log)')
    run(['python3',patched/'tests/stock_stage01/inspect_stock_contracts.py'],'05-static-inspection.log',cwd=patched)
    # Method byte-equivalence is a useful additional guard independent of trace.
    assert (patched/'src/server.cpp').read_bytes()==(pristine/'src/server.cpp').read_bytes()
    assert (patched/'src/buffer.h').read_bytes()==(pristine/'src/buffer.h').read_bytes()
    (out/'RESULT.txt').write_text(
        'PASS pinned baseline SHA-256 and strict R01 git-am replay\n'
        'PASS R01 L01 receive buffer real-stock golden\n'
        'PASS L02-L04 isolated real-stock CServer method + vendored real Opus smoke/assertions\n'
        'PASS bytewise stock vs R01 isolated server pipeline trace parity\n'
        'PASS server.cpp/buffer.h production source identity\n'
        'NOT_RUN whole CServer Qt/Socket/channel lifecycle\n'
        'NOT_RUN L05 recorder, meters, real timeouts/reconnect, actual legacy client\n'
        'NOT_RUN OF-01 historical captured backlog incident\n'
        'READY_FOR_REVIEW Stage 1 baseline only; deferred integration must be tracked at owning stages\n'
    )
    print((out/'RESULT.txt').read_text(),end='')
    print('Component trace SHA-256:',sha(original))
    print('Evidence:',out)
