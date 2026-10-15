#!/usr/bin/env python3
"""Stage3 component regressions: real SourceSlots + real vendored Opus producer.
No Qt server or network-session integration is implied by this harness.
"""
import pathlib,re,subprocess,tempfile,sys
root=pathlib.Path(__file__).resolve().parents[2]
def run(cmd):
 print('RUN '+' '.join(map(str,cmd)),flush=True)
 subprocess.run(list(map(str,cmd)),check=True)
source=(root/'src/client.cpp').read_text()
assert '#include "stockclientencode.h"' in source
assert 'StockClientEncodeFrame ( CurOpusEncoder' in source
assert 'StockClientEncoderDefaults ( OpusEncoderMono' in source
assert 'StockClientEncoderBitrate ( CurOpusEncoder' in source
baseline=subprocess.check_output(['git','-C',str(root),'rev-list','--max-parents=0','HEAD'],text=True).strip().splitlines()[0]
raw=subprocess.check_output(['git','-C',str(root),'show',baseline+':src/client.cpp'],text=True)
# Verify only stock-call extraction, no new encode implementation or send path.
assert 'opus_custom_encode ( CurOpusEncoder, &vecsStereoSndCrd[j]' in raw
assert 'Channel.PrepAndSendPacket ( &Socket, vecCeltData, iCeltNumCodedBytes );' in raw
assert 'Channel.PrepAndSendPacket ( &Socket, vecCeltData, iCeltNumCodedBytes );' in source
h=(root/'src/client.h').read_text()
# Exact stock output profile sizes must match optional producer's bounded table.
names=('MONO_LOW_QUALITY','MONO_NORMAL_QUALITY','MONO_HIGH_QUALITY',
       'STEREO_LOW_QUALITY','STEREO_NORMAL_QUALITY','STEREO_HIGH_QUALITY')
values=[]
for db in (False,True):
 for s in names:
  n='OPUS_NUM_BYTES_'+s+('_DBLE_FRAMESIZE' if db else '')
  val=re.search(r'^#define\s+'+n+r'\s+(\d+)\s*$',h,re.M)
  assert val,n
  values.append(int(val.group(1)))
assert values==[12,22,36,24,35,73,25,45,82,47,71,165],values
stockGlobals=(root/'src/global.h').read_text()
for name,val in [('MAX_NUM_CHANNELS','150'),('MAX_LEN_FADER_TAG','16'),
                 ('SYSTEM_SAMPLE_RATE_HZ','48000')]:
 assert re.search(r'^#define\s+'+name+r'\s+'+val+r'\b',stockGlobals,re.M),name
assert '    src/polyinproducer.cpp \\' in (root/'Jamulus.pro').read_text()
assert '    src/polyinsources.cpp \\' in (root/'Jamulus.pro').read_text()
with tempfile.TemporaryDirectory(prefix='jamulus-stage03-') as d:
 args=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'src')]
 san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
 exe=pathlib.Path(d)/'ownership'
 run(args+san+[root/'src/polyinsources.cpp',root/'tests/polyin_stage03/ownership.cpp','-o',exe])
 run([exe])
 build=root.parent.parent/'opus-build'
 lib=build/'libopus.a'
 if not lib.exists():
  run(['cmake','-S',root/'libs/opus','-B',build,'-DOPUS_CUSTOM_MODES=ON',
       '-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF','-DCMAKE_BUILD_TYPE=Release'])
  run(['cmake','--build',build,'-j4'])
 exe=pathlib.Path(d)/'producer'
 run(args+san+['-I'+str(root/'libs/opus/include'),root/'src/polyinwire.cpp',
     root/'src/polyinsources.cpp',root/'src/polyinproducer.cpp',root/'tests/polyin_stage03/producer.cpp',lib,'-lm','-o',exe])
 run([exe])
run([sys.executable,root/'tests/polyin_stage03/prove_stock_allocator.py'])
print('P05 F03 F04 SOURCE/CAPACITY/ACTUAL STOCK PRODUCER GATES + DESIGN ALLOCATOR PASS')
