#!/usr/bin/env python3
"""Compile actual F06 control implementation and F03 allocator, independent of Qt."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer',
         '-I'+str(root/'src'),str(root/'src/polyinsources.cpp'),
         str(root/'tests/polyin_stage05/control_smoke.cpp'),'-o',tmp+'/control']
    print('BUILD', ' '.join(cmd),flush=True)
    subprocess.run(cmd,check=True)
    subprocess.run([tmp+'/control'],check=True)
