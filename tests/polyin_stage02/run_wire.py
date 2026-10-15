#!/usr/bin/env python3
"""F01 can compile and run without stock buffering or Qt."""
import pathlib,subprocess,tempfile,re
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='jamulus-wire-f01-') as tmp:
    # Compile against the ORIGINAL enum declaration, not a copied numeric stand-in.
    stock=(root/'src/util.h').read_text()
    m=re.search(r'enum EAudComprType\s*\{[^}]*\};',stock,re.S)
    assert m, 'Actual stock EAudComprType not found'
    stock_enum=pathlib.Path(tmp)/'stock_codec_enum.h'
    stock_enum.write_text(m.group(0)+'\n')
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic',
         str(root/'src/polyinwire.cpp'),str(root/'tests/polyin_stage02/wire_smoke.cpp'),
         '-I'+str(root/'src'),'-include',str(stock_enum),'-o',tmp+'/wire']
    print('BUILD '+' '.join(cmd),flush=True)
    subprocess.run(cmd,check=True)
    subprocess.run([tmp+'/wire'],check=True)
