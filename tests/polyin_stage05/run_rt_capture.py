#!/usr/bin/env python3
"""Actual F06 bounded producer against the accepted F04 packing/encoding on real Opus."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
lib=Path(os.getenv('POLYIN_OPUS_BUILD',str(root.parent.parent/'opus-build')))/'libopus.a'
if not lib.is_file(): raise SystemExit(f'Build vendored Opus custom modes first: {lib}')
with tempfile.TemporaryDirectory(prefix='stage05-rt-') as td:
 exe=Path(td)/'test'
 cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'src'),'-I'+str(root/'libs/opus/include'),
      str(root/'src/polyinwire.cpp'),str(root/'src/polyinproducer.cpp'),str(root/'tests/polyin_stage05/rt_capture.cpp'),str(lib),'-lm','-o',str(exe)]
 print('RUN '+' '.join(cmd),flush=True)
 subprocess.run(cmd,check=True)
 subprocess.run([str(exe)],check=True)
