#!/usr/bin/env python3
"""Compile actual input/controller paths with each diagnostic mode; compare effects."""
import argparse
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.pointingtest/coexistence-validation/parity'
FLAGS = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-I.',
         '-Ijadro/arch', '-Ijadro/mm', '-Ijadro/fs', '-Ijadro/bws', '-Ijadro/gui',
         '-Ijadro/siec', '-Ijadro/aktualizacja', '-Ijadro/diag', '-Ijadro/rdzen',
         '-Isterowniki/pci', '-Isterowniki/virtio', '-Isterowniki/magazyn', '-Isterowniki/siec',
         '-Isterowniki/wejscie', '-Isterowniki/czas', '-Isterowniki/platforma', '-Isterowniki/grafika',
         '-Isterowniki', '-fno-omit-frame-pointer',
         '-fsanitize=address,undefined', '-no-pie', '-ffunction-sections', '-fdata-sections',
         '-DBURSZTYN_VIRTIO_HOST_TEST', '-DBURSZTYN_BOOT_TIMING_HOST_TEST']
MODES = {'boot-timing': ['BURSZTYNDIAGBOOTXHCITIMING'], 'production': [], 'physical': ['BURSZTYNDIAGXHCIPHYSICALMOUSE'],
         'coexistence': ['BURSZTYNDIAGINPUTCOEXISTENCE'],
         'all-input-diagnostics': ['BURSZTYNDIAGXHCIPHYSICALMOUSE', 'BURSZTYNDIAGINPUTCOEXISTENCE',
             'BURSZTYNDIAGXHCIPOINTING', 'BURSZTYNDIAGUSBHID', 'BURSZTYNDIAGPS2',
             'BURSZTYN_DEBUG_XHCI', 'BURSZTYN_DEBUG_GUI_PERF']}
COMMON = ['sterowniki/wejscie/input_device.cpp', 'sterowniki/usb/hid_pointer.cpp']
CASES = {'incremental': (['testy/test_xhci_incremental.cpp', 'jadro/arch/idt.cpp', 'sterowniki/pci/pci.cpp', 'sterowniki/usb/xhci_ring.cpp'] + COMMON, ['-DBURSZTYN_XHCI_HOST_TEST', '-DBURSZTYN_IDT_HOST_TEST']), 'input': (['testy/test_input_coexistence.cpp'] + COMMON, []),
         'boot': (['testy/test_xhci_boot_coexistence.cpp', 'jadro/arch/idt.cpp', 'sterowniki/pci/pci.cpp',
                   'sterowniki/usb/xhci_ring.cpp'] + COMMON,
                  ['-DBURSZTYN_XHCI_HOST_TEST', '-DBURSZTYN_IDT_HOST_TEST'])}
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

def run(name, sources, defines):
    binary = OUT / name
    subprocess.run(['g++', *FLAGS, *defines, *sources, '-Wl,--gc-sections', '-o', str(binary)], cwd=ROOT, check=True)
    output = subprocess.check_output([str(binary)], cwd=ROOT, env=ENV, text=True)
    (OUT / (name + '.txt')).write_text(output)
    for line in output.splitlines():
        if 'PASS' in line or line.startswith(('BOOT ', 'EMPTY ', 'TIMEOUT ', 'PHASE ')):
            print(name + ': ' + line, flush=True)
    return [line for line in output.splitlines() if line.startswith(('GUI ', 'STATE ', 'WRITE ', 'TRB ', 'REPORT ', 'EMPTY ', 'TIMEOUT '))]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--case', action='append', choices=tuple(CASES),
                        help='Run only these suites; default runs all suites.')
    args=parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    comparisons=0
    for case, (sources, extra) in CASES.items():
        if args.case and case not in args.case:
            continue
        expected=None
        for mode, flags in MODES.items():
            effects=run(case+'-'+mode, sources, extra+['-D'+f+'=1' for f in flags])
            if expected is None:
                expected=effects
            else:
                assert effects==expected, f'{case}: functional effects differ for {mode}'
                comparisons+=1
                print(f'Parity {case}/{mode}: {len(effects)} ordered functional records identical', flush=True)
    if args.baseline:
        run('boot-baseline', CASES['boot'][0], CASES['boot'][1]+['-DBURSZTYN_XHCI_BASELINE',
             '-DBURSZTYN_XHCI_SOURCE="'+str(args.baseline.resolve())+'"'])
    print(f'Diagnostic functional parity: {comparisons} comparisons PASS', flush=True)

if __name__=='__main__':
    main()
