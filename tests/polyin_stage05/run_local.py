#!/usr/bin/env python3
"""Stage 5 reproducible SOURCE-LOCAL tests, no Qt, network, GUI or field claim.
Real production source functions are compiled/extracted by the referenced tests.
The historical Stage3 prove_stock_allocator text signature deliberately does not
run: Stage5 run_stock_channel_list tests the actually changed FindChannel method.
"""
from pathlib import Path
import os,subprocess,sys
root=Path(__file__).resolve().parents[2]
steps=(
 'tests/polyin_stage02/run_wire.py',
 'tests/polyin_stage05/run_channel_fade.py',
 'tests/polyin_stage02/run_stage02.py',
 'tests/polyin_stage05/run_stage03_runtime.py',
 'tests/polyin_stage04/run_channel_get.py',
 'tests/polyin_stage04/run_transport_props.py',
 'tests/polyin_stage04/run_resize_semantics.py',
 'tests/polyin_stage04/run_server_plumbing.py',
 'tests/polyin_stage04/run_stage04.py',
 'tests/polyin_stage05/run_control.py',
 'tests/polyin_stage05/run_protocol_duplicate.py',
 'tests/polyin_stage05/run_stock_channel_list.py',
 'tests/polyin_stage05/run_rt_capture.py',
 'tests/polyin_stage05/run_native_capture.py',
 'tests/polyin_stage05/run_capture_routes.py',
 'tests/polyin_stage05/run_client_capture.py',
 'tests/polyin_stage05/run_refused_amendment.py',
 'tests/polyin_stage05/stock_contract_audit.py',
)
for step in steps:
 print('RUN',sys.executable,step,flush=True)
 p=subprocess.run([sys.executable,str(root/step)],cwd=root,env=os.environ)
 if p.returncode:
  print('FAIL',step,'code',p.returncode,flush=True);sys.exit(p.returncode)
 print('PASS',step,flush=True)
print('PASS SOURCE-LOCAL SUITE; no native Qt build or whole-path qualification was run',flush=True)
