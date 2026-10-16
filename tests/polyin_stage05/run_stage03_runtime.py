#!/usr/bin/env python3
"""Rerun actual F03/F04 ownership + vendored Opus tests on Stage5 tip.
The Stage3-era prove_stock_allocator.py exact-statement invariant is obsolete
once F06 reserves physical IDs: run_stock_channel_list.py now tests ACTUAL F06
FindChannel/FreeChannel instead. No production/stock fakes in these two tests.
"""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
lib=Path(os.getenv('POLYIN_OPUS_BUILD',str(root.parent.parent/'opus-build')))/'libopus.a'
if not lib.exists():
    raise SystemExit('Missing real vendored Opus custom-mode library: '+str(lib)+'; see validation/test_commands.md')
with tempfile.TemporaryDirectory(prefix='stage05-stage03-runtime-') as tmp:
    args=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'src')]
    def run(cmd):
        print('RUN '+' '.join(map(str,cmd)),flush=True);subprocess.run(list(map(str,cmd)),check=True)
    exe=Path(tmp)/'ownership'
    run(args+[root/'src/polyinsources.cpp',root/'tests/polyin_stage03/ownership.cpp','-o',exe]);run([exe])
    exe=Path(tmp)/'producer'
    run(args+['-I'+str(root/'libs/opus/include'),root/'src/polyinwire.cpp',root/'src/polyinsources.cpp',
              root/'src/polyinproducer.cpp',root/'tests/polyin_stage03/producer.cpp',lib,'-lm','-o',exe]);run([exe])
print('PASS actual F03 allocator ownership and F04 real Opus producer; standalone adapted server allocation: run_stock_channel_list.py')
