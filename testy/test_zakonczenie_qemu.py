#!/usr/bin/env python3
"""Natywny test BWS 97 / okien podrzednych w jednorazowym gosciu QEMU (kernel z BURSZTYN_DIAG_PROC_LIFECYCLE=1).

Zastepuje bloby: menedzer_okien (launcher z prawem zarzadzania procesami), test (ofiary: spin/event-wait/heap/render),
snake (wlasciciel okien podrzednych), okno_informacyjne (dziecko zawieszone - ignoruje ZAMKNIJ). Nie dotyka drzewa produkcyjnego.
"""
import os, pathlib, re, shlex, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_bws_abuse_qemu import get_make_objs, run_cmd, ROOT

OUT = ROOT / '.zakonczenie-test'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ['x86_64-linux-gnu-g++', '-I.', '-Ijadro/bws', '-Ijadro/gui', '-Ijadro/mm', '-Ijadro/rdzen', '-Ijadro/fs',
             '-Ijadro/arch', '-std=c++17', '-O2', '-g', '-ffreestanding', '-mno-red-zone', '-fno-exceptions', '-fno-rtti',
             '-fno-asynchronous-unwind-tables', '-fno-unwind-tables', '-fstack-protector-strong',
             '-mstack-protector-guard=global', '-mcmodel=large', '-fno-pie']
    roles = {1: 'menedzer_okien', 2: 'test', 3: 'snake', 4: 'okno_informacyjne'}
    for rola, nazwa in roles.items():
        run_cmd([*flags, f'-DZK_ROLA={rola}', '-c', 'narzedzia/fixtures/zakonczenie_programy.cpp', '-o', str(OUT / f'{nazwa}.o')])
        run_cmd(['x86_64-linux-gnu-ld', '--gc-sections', '-T', 'programy/menedzer_okien/menedzer_okien_linker.ld', '-nostdlib',
                 '-no-pie', '--undefined=__stack_chk_guard', '--undefined=__stack_chk_fail', str(OUT / f'{nazwa}.o'),
                 'bursztyn_gui.o', '-o', str(OUT / f'{nazwa}.elf')])
        run_cmd(['objcopy', '-O', 'binary', str(OUT / f'{nazwa}.elf'), str(OUT / f'{nazwa}.bin')])
        run_cmd(['python3', str(ROOT / 'narzedzia/kompresuj_blob.py'), str(OUT / f'{nazwa}.bin'), str(OUT / f'{nazwa}.bin.bz')])
        run_cmd(['ld', '-r', '-b', 'binary', f'{nazwa}.bin.bz', '-o', f'{nazwa}_blob.o'], cwd=OUT)
    # scheduler.o z diagnostyka cyklu zycia (log wolnych ramek przy zwolnieniu slotu)
    dry = subprocess.run(['make', '-n', '-W', 'jadro/bws/scheduler.cpp', 'scheduler.o'], cwd=str(ROOT), stdout=subprocess.PIPE, text=True).stdout
    cmd = next(l for l in dry.splitlines() if l.startswith('x86_64-linux-gnu-g++') and 'scheduler.cpp' in l)
    parts = shlex.split(cmd)
    parts[parts.index('-o') + 1] = str(OUT / 'scheduler_diag.o')
    parts.append('-DBURSZTYN_DIAG_PROC_LIFECYCLE=1')
    parts = [p for p in parts if p != '-fstack-usage']
    run_cmd(parts)
    # Tozsamosc systemowa: zawieszone dziecko ma inny SHA-256 niz produkcyjne okno -> kopia loadera z tablica wygenerowana dla
    # blobow testu (drzewo produkcyjne nie jest zmieniane; weryfikacja tozsamosci pozostaje wlaczona).
    shadow = OUT / 'fs_shadow'
    if shadow.exists(): shutil.rmtree(shadow)
    shutil.copytree(ROOT / 'jadro/fs', shadow)
    run_cmd(['python3', 'narzedzia/generuj_skroty_systemowe.py', str(shadow / 'skroty_systemowe.generated.h'),
             '/programy/ustawienia.cebula/ustawienia.bur=ustawienia.bin',
             '/programy/diagnostyka.cebula/diagnostyka.bur=diagnostyka.bin',
             '/programy/test_dzwieku.cebula/test_dzwieku.bur=test_dzwieku.bin',
             '/programy/aktualizacje.cebula/aktualizacje.bur=aktualizacje.bin',
             '/programy/ekran.cebula/ekran.bur=ekran.bin',
             '/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur=przegladarka_obrazow.bin',
             f'/programy/okno-informacyjne.cebula/okno-informacyjne.bur={OUT / "okno_informacyjne.bin"}'])
    shadow_objs = {}
    for zrodlo, obiekt in (('loader', 'loader.o'), ('podpis_paczki', 'podpis_paczki.o')):
        dry = subprocess.run(['make', '-n', '-W', f'jadro/fs/{zrodlo}.cpp', obiekt], cwd=str(ROOT), stdout=subprocess.PIPE, text=True).stdout
        cmd = next(l for l in dry.splitlines() if l.startswith('x86_64-linux-gnu-g++') and f'{zrodlo}.cpp' in l)
        parts = shlex.split(cmd)
        parts[parts.index('-o') + 1] = str(OUT / f'{zrodlo}_shadow.o')
        parts = [str(shadow / f'{zrodlo}.cpp') if p == f'jadro/fs/{zrodlo}.cpp' else p for p in parts if p != '-fstack-usage']
        run_cmd(parts)
        shadow_objs[obiekt] = str(OUT / f'{zrodlo}_shadow.o')
    zamienniki = {f'{n}_blob.o': str(OUT / f'{n}_blob.o') for n in roles.values()}
    zamienniki.update(shadow_objs)
    zamienniki['scheduler.o'] = str(OUT / 'scheduler_diag.o')
    objects = [zamienniki.get(o, o) for o in get_make_objs()]
    run_cmd(['x86_64-linux-gnu-g++', '-T', 'jadro/arch/linker.ld', '-nostdlib', '-no-pie', '-static', '-Wl,-z,noexecstack',
             '-Wl,-z,max-page-size=0x1000', '-Wl,--build-id=none', '-o', str(OUT / 'kernel.elf'), *objects, '-lgcc'])
    stage = OUT / 'iso'
    if stage.exists(): shutil.rmtree(stage)
    (stage / 'boot/grub').mkdir(parents=True)
    run_cmd(['objcopy', '--strip-all', str(OUT / 'kernel.elf'), str(stage / 'boot/system_operacyjny.bin')])
    (stage / 'boot/grub/grub.cfg').write_text('set timeout=0\nmenuentry "ZK test" {\ninsmod all_video\nset gfxmode=1280x720x32\n'
                                              'set gfxpayload=keep\nmultiboot2 /boot/system_operacyjny.bin\nboot\n}\n')
    iso = OUT / 'test.iso'
    run_cmd(['grub-mkrescue', '-o', str(iso), str(stage)])
    log = OUT / 'serial.log'; log.write_text('')
    vm = subprocess.Popen(['qemu-system-x86_64', '-machine', 'pc,hpet=on', '-cpu', 'max', '-accel', 'tcg', '-m', '2048',
        '-bios', '/usr/share/ovmf/OVMF.fd', '-display', 'none', '-vga', 'none', '-device', 'virtio-vga,disable-legacy=on',
        '-drive', f'file={iso},format=raw,if=none,id=boot,readonly=on',
        '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1', '-device', 'virtio-rng-pci,disable-legacy=on',
        '-serial', f'file:{log}', '-no-reboot'], stdout=subprocess.DEVNULL, stderr=(OUT / 'qemu.log').open('w'))
    try:
        deadline = time.monotonic() + float(os.environ.get('BURSZTYN_QEMU_TIMEOUT', '600'))
        while time.monotonic() < deadline:
            text = log.read_text(errors='replace')
            if '[ZK] KONIEC' in text or vm.poll() is not None: break
            time.sleep(.5)
        time.sleep(1)
        text = log.read_text(errors='replace')
    finally:
        vm.terminate()
        try: vm.wait(timeout=5)
        except subprocess.TimeoutExpired: vm.kill(); vm.wait()
    zk = [l for l in text.splitlines() if l.startswith('[ZK]')]
    print('\n'.join(zk))
    assert '[ZK] KONIEC' in text, 'scenariusz nie dobiegl konca:\n' + text[-3000:]
    assert not re.search(r'PANIC|KERNEL PANIC|\[PANIC', text, re.I), 'panika jadra'
    assert ' FAIL' not in '\n'.join(zk), 'scenariusze zakonczone porazka'
    m = re.search(r'\[ZK\] KONIEC pass=(\d+) fail=(\d+)', text)
    assert m and int(m[2]) == 0 and int(m[1]) >= 14, m and m.group(0)
    assert 'wymuszone-zakonczenie-dziecka' in text, 'brak eskalacji zawieszonego dziecka'
    ramki = [int(x) for x in re.findall(r'\[PROC-CLEANUP\] pid=\d+ wolne_ramki=(\d+)', text)]
    assert len(ramki) >= 40, len(ramki)
    # pomiar wycieku: ramki po zwolnieniu tego samego rodzaju procesu nie moga systematycznie spadac
    start, koniec = ramki[len(ramki) // 3], ramki[-1]
    print(f'[ZK-HOST] zwolnien={len(ramki)} wolne_ramki: poczatek={ramki[0]} (1/3)={start} koniec={koniec} min={min(ramki)} max={max(ramki)}')
    assert start - koniec <= 64, f'mozliwy wyciek ramek: {start} -> {koniec}'
    print('ZAKONCZENIE PROCESOW natywnie: PASS')


if __name__ == '__main__': main()
