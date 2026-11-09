#!/usr/bin/env python3
"""Both diagnostic modes: actual transport, synthetic queues, no emulator."""
import os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/acer-boot-qemu-audit-2026-10-11'
expected=None
for mode in ['production','diagnostic']:
 p=subprocess.run(['make','-s','test-virtio-pci','BURSZTYNDIAGVIRTIOINPUTINIT=0','BURSZTYNDIAGVIRTIOINPUTQEMU='+str(int(mode=='diagnostic'))],cwd=ROOT,
     text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
     env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (OUT/('virtio-'+mode+'-tests.txt')).write_text(p.stdout)
 if p.returncode:raise SystemExit(p.stdout[-6000:])
 assert 'PASS=167, FAIL=0' in p.stdout
 effects=[line for line in p.stdout.splitlines() if line.startswith('VIRTIO STATE')]
 assert len(effects)==6
 if expected is None:expected=effects
 else:assert effects==expected,'VirtIO functional transitions differ with diagnostics enabled'
 print(f'VirtIO {mode}: 167 named tests PASS, {len(effects)} lifecycle/report records',flush=True)
result='VirtIO diagnostic functional parity: 6 ordered records identical; 334 named cases PASS across 2 builds. ASan/UBSan PASS.\n'+'\n'.join(expected)+'\n'
(OUT/'virtio-diagnostic-parity.txt').write_text(result)
print(result)
