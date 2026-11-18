#!/usr/bin/env python3
"""Disposable mixed v1/v2 guest: execute converted PIE repeatedly in the real loader."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import time
import sys
import socket
import os
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "testy"))

from test_bws_abuse_qemu import get_make_objs, run_cmd, ROOT
from symbolicate_bur_v2_crash import symbolicate_line


def main():
    out = ROOT / '.aslr-test/guest'
    out.mkdir(parents=True, exist_ok=True)
    flags = ['x86_64-linux-gnu-g++', '-I.', '-Ijadro/bws', '-Ijadro/gui', '-Ijadro/mm', '-Ijadro/rdzen', '-Ijadro/fs', '-Ijadro/arch', '-std=c++17', '-O2', '-g', '-ffreestanding',
             '-mno-red-zone', '-fno-exceptions', '-fno-rtti', '-fno-asynchronous-unwind-tables',
             '-fno-unwind-tables', '-fstack-protector-strong', '-mstack-protector-guard=global']
    run_cmd([*flags, '-fPIE', '-fvisibility=hidden', '-c', 'narzedzia/bur_v2_probe.cpp',
             '-o', str(out/'probe.o')])
    run_cmd(['x86_64-linux-gnu-ld', '-shared', '-Bsymbolic', '--no-undefined', '-z', 'text',
             '--gc-sections', '-T', 'bur_v2_linker.ld', str(out/'probe.o'), '-o', str(out/'probe.elf')])
    run_cmd(['python3', 'narzedzia/elftoburv2.py', str(out/'probe.elf'), str(out/'test.bin')])
    # Legacy WM launch ABI, running simultaneously with v2 children.
    source = (ROOT/'narzedzia/fixtures/bur_v1_launcher_prefix.inc').read_text()
    source += '#include "jadro/bws/bws_procesy.h"\nextern "C" __attribute__((noreturn)) void _start() {'
    start = source.index('extern "C" __attribute__((noreturn)) void _start() {')
    source = source[:start] + '''extern "C" __attribute__((noreturn)) void _start() {
      bws_wywolaj(BWS_URUCHOM_PROGRAM,(uint64_t)"/programy/fpu-a.cebula/fpu-a.bur",0,0,0);
      unsigned iteration=0;
      static const char* modes[]={"g","t","r","h","s","c"};
      for (;;) {
        BwsProcesInfo entries[BWS_PROCESY_MAKS]{};
        auto count=bws_wywolaj(BWS_PROCESY_LISTA,(uint64_t)entries,BWS_PROCESY_MAKS,0,0);
        if (!count) { bws_wywolaj(BWS_WYPISZ,(uint64_t)"[BUR2 PROCESS LIST DENIED]",0,0,0); for(;;)asm volatile("pause"); }
        bool running=false;
        for(unsigned i=0;i<count;++i) {
          const char* name=entries[i].nazwa;
          if(name[0]=='t'&&name[1]=='e'&&name[2]=='s'&&name[3]=='t'&&name[4]=='.'&&name[5]=='b'&&name[6]=='u'&&name[7]=='r'&&name[8]==0)running=true;
        }
        const char* mode=iteration<6 ? modes[iteration] : "";
        if(!running && bws_wywolaj(BWS_URUCHOM_PROGRAM,(uint64_t)"/programy/test.cebula/test.bur",(mode[0] ? (uint64_t)mode : 0),0,0)) ++iteration;
        auto until=bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS,0,0,0,0)+500000000ULL;
        while(bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS,0,0,0,0)<until) asm volatile("pause");
      }
    }
'''
    (out/'launcher.cpp').write_text(source)
    run_cmd([*flags, '-mcmodel=large', '-fno-pie', '-c', str(out/'launcher.cpp'), '-o', str(out/'launcher.o')])
    run_cmd(['x86_64-linux-gnu-ld', '--gc-sections', '-T', 'programy/menedzer_okien/menedzer_okien_linker.ld',
             '-nostdlib', '-no-pie', '--undefined=__stack_chk_guard', '--undefined=__stack_chk_fail',
             str(out/'launcher.o'), 'bursztyn_gui.o', '-o', str(out/'launcher.elf')])
    run_cmd(['objcopy', '-O', 'binary', str(out/'launcher.elf'), str(out/'menedzer_okien.bin')])
    shutil.copy2(ROOT/'narzedzia/fixtures/bur_v1_legacy/fpu_probe.bin',out/'fpu_probe.bin')
    for app in ('test', 'menedzer_okien','fpu_probe'):
        # Jadro oczekuje blobow SKOMPRESOWANYCH (_binary_*_bin_bz_*), jak w produkcyjnym Makefile.
        run_cmd(['python3', str(ROOT/'narzedzia/kompresuj_blob.py'), str(out/(app+'.bin')), str(out/(app+'.bin.bz'))])
        run_cmd(['ld','-r','-b','binary',app+'.bin.bz','-o',app+'_blob.o'], cwd=out)
    objects = [str(out/o) if o in ('test_blob.o','menedzer_okien_blob.o','fpu_probe_blob.o') else o
               for o in get_make_objs()]
    run_cmd(['x86_64-linux-gnu-g++','-T','jadro/arch/linker.ld','-nostdlib','-no-pie','-static',
             '-Wl,-z,noexecstack','-Wl,-z,max-page-size=0x1000','-Wl,--build-id=none',
             '-o',str(out/'kernel.elf'),*objects,'-lgcc'])
    stage = out/'iso'
    if stage.exists(): shutil.rmtree(stage)
    (stage/'boot/grub').mkdir(parents=True)
    run_cmd(['objcopy','--strip-all',str(out/'kernel.elf'),str(stage/'boot/system_operacyjny.bin')])
    (stage/'boot/grub/grub.cfg').write_text('set timeout=0\nmenuentry "BUR v2 guest test" {\ninsmod all_video\nset gfxmode=1280x720x32\nset gfxpayload=keep\nmultiboot2 /boot/system_operacyjny.bin\nboot\n}\n')
    iso=out/'test.iso'
    run_cmd(['grub-mkrescue','-o',str(iso),str(stage)])
    log=out/'serial.log'; log.write_text('')
    monitor=out/'monitor.sock'
    monitor.unlink(missing_ok=True)
    vm=subprocess.Popen(['qemu-system-x86_64','-machine','pc,hpet=on','-cpu','max',
        '-accel','tcg','-m','2048','-bios','/usr/share/ovmf/OVMF.fd','-display','none',
        '-vga','none','-device','virtio-vga,disable-legacy=on',
        '-drive',f'file={iso},format=raw,if=none,id=boot,readonly=on',
        '-device','virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1',
        '-device','virtio-rng-pci,disable-legacy=on','-serial',f'file:{log}','-monitor',f'unix:{monitor},server=on,wait=off','-no-reboot'],
        stdout=subprocess.DEVNULL,stderr=(out/'qemu.log').open('w'))
    try:
        deadline=time.monotonic()+float(os.environ.get("BURSZTYN_QEMU_TIMEOUT", "300"))
        while time.monotonic()<deadline:
            text=log.read_text(errors='replace')
            if text.count('[BUR2 EXEC PASS]')>=26 and '[FPU PROBE] A PASS' in text: break
            if vm.poll() is not None: break
            time.sleep(.25)
        text=log.read_text(errors='replace')
        rows=re.findall(r'\[BUR2 LAYOUT\] code=(0x[0-9a-f]+) stack=(0x[0-9a-f]+) heap=(0x[0-9a-f]+) guard=(0x[0-9a-f]+) shm=(0x[0-9a-f]+)',text)
        assert len(rows)>=26, text[-5000:]
        assert '[FPU PROBE] A PASS' in text, text[-5000:]
        assert '[BUR2 NEGATIVE FAILED]' not in text
        for mode in 'gtrhsc': assert '[BUR2 NEGATIVE] '+mode in text
        assert text.count('[USER FAULT]') >= 5
        assert '[STACKPROTECTORVIOLATION]' in text
        assert '[BUR2 FAIL]' not in text
        symbolicated = [
            r for line in text.splitlines()
            if (r := symbolicate_line(line, bur_path=out/'test.bin')) is not None
        ]
        assert len(symbolicated) >= 5, symbolicated
        code_faults = [
            r for r in symbolicated
            if r['bur_version'] == 2 and r['symbol'] == '_start' and 'bur_v2_probe.cpp' in (r['location'] or '')
        ]
        assert len(code_faults) >= 3, symbolicated
        for column in range(5):
            assert len({r[column] for r in rows})>1, ('no variation',column)
            assert all(0 < int(r[column],16) < (1<<47) for r in rows)
        with socket.socket(socket.AF_UNIX) as control:
            control.settimeout(10)
            control.connect(str(monitor))
            control.recv(4096)
            control.sendall(b'info registers\n')
            registers=b''
            # Odpowiedz monitora = echo polecenia + rejestry + prompt: czekamy na rejestry, nie na pierwszy prompt.
            while not (b'CR4=' in registers and b'EFER=' in registers and registers.rstrip().endswith(b'(qemu)')):
                registers+=control.recv(65536)
        (out/'registers.txt').write_bytes(registers)
        cr4=int(re.search(rb'CR4=([0-9a-fA-F]+)',registers)[1],16)
        efer=int(re.search(rb'EFER=([0-9a-fA-F]+)',registers)[1],16)
        assert cr4 & (1<<20) and cr4 & (1<<21), ('SMEP/SMAP disabled',hex(cr4))
        assert efer & (1<<11), ('NX disabled',hex(efer))
        result=dict(cr4=hex(cr4),efer=hex(efer),smep_smap_nx_enabled=True,executions=len(rows),unique_per_column=[len({r[c] for r in rows}) for c in range(5)],
                    columns=['code','stack','heap','canary_address','shared_mapping'],layouts=rows,
                    symbolicated_faults=symbolicated,
                    legacy_fpu_fixture_sha256=hashlib.sha256((out/'fpu_probe.bin').read_bytes()).hexdigest(),
                    canary_code_rodata_write_denied=True,heap_stack_execution_denied=True,ssp_contained=True,
                    kernel_sha256=hashlib.sha256((stage/'boot/system_operacyjny.bin').read_bytes()).hexdigest())
        (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        print('BUR v2 guest execution PASS:',result['unique_per_column'],flush=True)
    finally:
        vm.terminate()
        try: vm.wait(timeout=5)
        except subprocess.TimeoutExpired: vm.kill();vm.wait()


if __name__=='__main__': main()
