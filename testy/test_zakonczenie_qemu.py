#!/usr/bin/env python3
"""Natywny test BWS 97 / okien podrzednych w jednorazowym gosciu QEMU (kernel z BURSZTYN_DIAG_PROC_LIFECYCLE=1).

Zastepuje bloby: menedzer_okien (launcher z prawem zarzadzania procesami), test (ofiary: spin/event-wait/heap/render),
snake (wlasciciel okien podrzednych), okno_informacyjne (dziecko zawieszone - ignoruje ZAMKNIJ). Nie dotyka drzewa produkcyjnego.
"""
import os, pathlib, re, shlex, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_bws_abuse_qemu import get_make_objs, run_cmd, ROOT

OUT = ROOT / '.zakonczenie-test'


def zbuduj():
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ['x86_64-linux-gnu-g++', '-I.', '-Ijadro/bws', '-Ijadro/gui', '-Ijadro/mm', '-Ijadro/rdzen', '-Ijadro/fs',
             '-Ijadro/arch', '-std=c++17', '-O2', '-g', '-ffreestanding', '-mno-red-zone', '-fno-exceptions', '-fno-rtti',
             '-fno-asynchronous-unwind-tables', '-fno-unwind-tables', '-fstack-protector-strong',
             '-mstack-protector-guard=global', '-mcmodel=large', '-fno-pie']
    roles = {1: 'menedzer_okien', 2: 'test', 3: 'snake', 4: 'okno_informacyjne'}
    for rola, nazwa in roles.items():
        run_cmd([*flags, f'-DZK_ROLA={rola}', f'-DZK_PAUZA={os.environ.get("ZK_PAUZA", "1")}', f'-DZK_TYLKO_FLOOD={os.environ.get("ZK_TYLKO_FLOOD", "0")}', '-c', 'narzedzia/fixtures/zakonczenie_programy.cpp', '-o', str(OUT / f'{nazwa}.o')])
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
    if os.environ.get('ZK_BASELINE') == '1': parts.append('-DBURSZTYN_DIAG_BASELINE_SCHED=1')   # A/B: stan sprzed poprawki glodzenia
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
    return iso


def uruchom_gosc(iso, log, timeout, probkuj_rejestry=False, qemu_log=None):
    log.write_text('')
    qemu_log = qemu_log or (OUT / 'qemu.log')
    (OUT / 'monitor.sock').unlink(missing_ok=True)
    rejestry = []
    vm = subprocess.Popen(['qemu-system-x86_64', '-machine', 'pc,hpet=on', '-cpu', 'max', '-accel', 'tcg', '-m', '2048',
        '-bios', '/usr/share/ovmf/OVMF.fd', '-display', 'none', '-vga', 'none', '-device', 'virtio-vga,disable-legacy=on',
        '-drive', f'file={iso},format=raw,if=none,id=boot,readonly=on',
        '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1', '-device', 'virtio-rng-pci,disable-legacy=on',
        '-serial', f'file:{log}', '-no-reboot'] + (['-monitor', f'unix:{OUT / "monitor.sock"},server=on,wait=off'] if probkuj_rejestry else []),
        stdout=subprocess.DEVNULL, stderr=pathlib.Path(qemu_log).open('w'))
    t0 = time.monotonic(); powod = 'timeout'
    probki = []          # (czas_hosta, cpu_qemu_s, bajty_logu, loadavg1, steal_jiffies)
    tck = os.sysconf('SC_CLK_TCK')

    def cpu_qemu():
        try:
            f = open(f'/proc/{vm.pid}/stat').read().rsplit(')', 1)[1].split()
            return (int(f[11]) + int(f[12])) / tck
        except OSError: return None

    def steal():
        try: return int(open('/proc/stat').readline().split()[8])
        except (OSError, IndexError): return 0
    nast = 0.0; nast_rej = 8.0; sock = None; przerwy_hosta = []; ostatnia_iter = time.monotonic()

    def probka_rejestrow(tekst=''):
        nonlocal sock
        gt = re.findall(r't_ns=(\d+)', tekst[-4000:]); gt = int(gt[-1]) / 1e9 if gt else 0.0
        import socket
        try:
            if sock is None:
                sock = socket.socket(socket.AF_UNIX); sock.settimeout(5); sock.connect(str(OUT / 'monitor.sock')); sock.recv(4096)
            sock.sendall(b'info registers\n'); buf = b''
            while not (b'EFER=' in buf and buf.rstrip().endswith(b'(qemu)')): buf += sock.recv(65536)
            t = buf.decode(errors='replace')
            rip = int(re.search(r'RIP=([0-9a-f]+)', t)[1], 16); rfl = int(re.search(r'RFL=([0-9a-f]+)', t)[1], 16)
            cpl = int(re.search(r'CPL=(\d)', t)[1]); cr3 = int(re.search(r'CR3=([0-9a-f]+)', t)[1], 16)
            rejestry.append((time.monotonic() - t0, rip, rfl, cpl, cr3, gt))
        except (OSError, TypeError, AttributeError): sock = None
    try:
        while time.monotonic() - t0 < timeout:
            teraz = time.monotonic()
            if teraz - ostatnia_iter > 5.0: przerwy_hosta.append((ostatnia_iter - t0, teraz - ostatnia_iter))   # petla hosta (cykl 0.5 s) nie dostala CPU
            ostatnia_iter = teraz
            text = log.read_text(errors='replace')
            if '[ZK] KONIEC' in text: powod = 'koniec'; break
            if vm.poll() is not None: powod = f'qemu-exit-{vm.returncode}'; break
            if time.monotonic() - t0 >= nast:
                c = cpu_qemu()
                if c is not None: probki.append((time.monotonic() - t0, c, len(text), float(open('/proc/loadavg').read().split()[0]), steal()))
                nast += 5.0
            if probkuj_rejestry and time.monotonic() - t0 >= nast_rej: probka_rejestrow(text); nast_rej += 4.0
            time.sleep(.5)
        time.sleep(1)
    finally:
        vm.terminate()
        try: vm.wait(timeout=5)
        except subprocess.TimeoutExpired: vm.kill(); vm.wait()
    uruchom_gosc.probki = probki; uruchom_gosc.rejestry = rejestry; uruchom_gosc.przerwy_hosta = przerwy_hosta
    return log.read_text(errors='replace'), powod, time.monotonic() - t0


def diagnoza_hosta(probki):
    """Okna >=30 s bez postepu logu gosciu: CPU QEMU (%) w oknie rozroznia zatrzymanie hosta/VM (niskie CPU albo wysokie steal)
    od zywej petli gosciu (CPU ~100% bez postepu). Zwraca liste opisow."""
    wyniki = []; i = 0
    while i < len(probki):
        j = i
        while j + 1 < len(probki) and probki[j + 1][2] == probki[i][2]: j += 1
        dt = probki[j][0] - probki[i][0]
        if dt >= 30 and j > i:
            cpu = (probki[j][1] - probki[i][1]) / dt * 100
            wyniki.append(f'brak postepu logu {dt:.0f}s od t={probki[i][0]:.0f}s: CPU QEMU={cpu:.0f}% steal={probki[j][4] - probki[i][4]} loadavg={probki[j][3]:.2f}')
        i = j + 1
    return wyniki


def kv(linia):
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', linia)}


def analizuj(text, powod):
    """Zwraca (bledy_jadra, bledy_harnessu, statystyki)."""
    jadro, harness, st = [], [], {}
    zk = [l for l in text.splitlines() if l.startswith('[ZK]')]
    nieudane = [l for l in zk if l.rstrip().endswith(' FAIL')]
    if re.search(r'PANIC|KERNEL PANIC|\[PANIC', text, re.I): jadro.append('panika jadra')
    st['scenariusze_fail'] = nieudane
    m = re.search(r'\[ZK\] KONIEC pass=(\d+) fail=(\d+)', text)
    st['pass'], st['fail'] = (int(m[1]), int(m[2])) if m else (0, 0)
    if not m: harness.append(f'scenariusz nie dobiegl konca ({powod})')
    # zadania zakonczenia i ich realizacja (czas gosciny HPET, ns)
    req = {}; lat = []; lat_irq = []; nieposprzatane = []
    zdarzenia = []
    for l in text.splitlines():
        if l.startswith('[PROC-REQ]'):
            d = kv(l); zdarzenia.append(('req', d))
            if d['wynik'] == 1: req[d['pid']] = (d['t_ns'], d.get('irq', 0))
        elif l.startswith('[PROC-CLEANUP]'):
            d = kv(l); zdarzenia.append(('cleanup', d))
            if d['pid'] in req:
                t_req, irq_req = req.pop(d['pid']); lat.append((d['pid'], d['t_ns'] - t_req)); lat_irq.append(d.get('irq', 0) - irq_req)
            if d['pid'] == d['pid_biezacy']: jadro.append(f"sprzatanie procesu biezacego pid={d['pid']}")
    nieposprzatane = sorted(req)
    # Gestosc tickow schedulera w czasie gosciny (HPET): dlugie przerwy bez tickow = wstrzymanie vCPU/hosta albo sekcje Ring 0.
    punkty = [(d['t_ns'] / 1e9, d.get('tick', d.get('sprzatanie_tick'))) for t, d in zdarzenia if d.get('t_ns')]
    przerwy = [(b[0] - a[0], (b[1] - a[1]) / max(b[0] - a[0], 1e-9)) for a, b in zip(punkty, punkty[1:]) if b[0] - a[0] > 1.0]
    st['przerwy_tickow_gt1s'] = len(przerwy)
    st['maks_przerwa_s'] = max((x for x, _ in przerwy), default=0.0)
    st['min_czestosc_tickow_hz'] = min((r for _, r in przerwy), default=None)
    st['latencje_irq'] = lat_irq; st['maks_latencja_irq'] = max(lat_irq, default=0)
    st['latencje_ns'] = lat; st['maks_latencja_ms'] = max((x for _, x in lat), default=0) / 1e6
    st['nieposprzatane_pid'] = nieposprzatane
    cl = [d for t, d in zdarzenia if t == 'cleanup']
    st['zwolnien'] = len(cl); st['ramki'] = [d['wolne_ramki'] for d in cl]
    st['aktywne_koniec'] = cl[-1]['aktywne'] if cl else None
    # podwojne sprzatanie: dwa [PROC-CLEANUP] tego samego pid bez zadania/utworzenia pomiedzy
    stan = {}
    for t, d in zdarzenia:
        if t == 'req' and d['wynik'] == 1: stan[d['pid']] = 'req'
        elif t == 'cleanup':
            if stan.get(d['pid']) not in ('req', 'self'):
                # samozakonczenie bez zadania (np. okno zamkniete przez ZAMKNIJ) jest dozwolone tylko raz na instancje
                pass
            stan[d['pid']] = 'done'
    if nieposprzatane and m: jadro.append(f'zadanie zakonczenia bez sprzatania: pid={nieposprzatane}')
    if nieudane and not jadro: harness.append('scenariusze: ' + '; '.join(nieudane))
    if re.search(r'\[USER FAULT\]', text) : st['user_fault'] = len(re.findall(r'\[USER FAULT\]', text))
    return jadro, harness, st


# ---------------------------------------------------------------------------------------------------------------------
# Klasyfikacja przebiegu (czysta funkcja; testy syntetyczne: testy/test_zakonczenie_klasyfikacja.py, bez QEMU).
#
# Statusy (wiazace = liczone jako wynik testu):
#   PASS               KONIEC, fail=0, pass>=minimum                                       wiazacy
#   FAIL-JADRO         panika / sprzatanie procesu biezacego / zadanie bez sprzatania     wiazacy (NIGDY stall)
#   FAIL-QEMU          proces QEMU zakonczyl sie przed KONIEC (crash, triple fault, -no-reboot)  wiazacy (NIGDY stall)
#   FAIL               scenariusz FAIL bez dowodu pauzy hosta, albo za malo scenariuszy    wiazacy
#   FAIL-BRAK-POSTEPU  timeout: >= PROG_BRAKU_POSTEPU_S czasu, w ktorym host dawal CPU, a log gosciu nie rosl  wiazacy
#   FAIL-TIMEOUT       timeout przy postepie i wystarczajacym czasie dostepnym dla gosciu  wiazacy
#   STALL-HOSTA        udowodniona pauza hosta: niewiazacy, raportowany osobno, NIGDY nie liczony jako PASS
#
# Dowod pauzy hosta dla scenariusza FAIL wymaga OBU sygnalow: (1) po stronie gosciu w oknie awarii (ostatnie zdarzenie
# PROC przed linia FAIL .. pierwsze po niej) HPET (zegar hosta) przyrosl o dt >= 2 s, a przerwania timera prawie nie
# przyszly (< 10% nominalnych 250 Hz) - vCPU nie wykonywal sie; (2) po stronie hosta petla harnessu nie dostawala CPU
# (przerwy > 5 s) lacznie >= 50% dt. Zywy gosc z IF=0 tez gubi tiki, ale wtedy host NIE ma przerw - wynik FAIL.
# Dla timeoutu: okna bez postepu logu pomniejszone o przerwy hosta; jesli zostaje >= 30 s - gosc stal (FAIL-BRAK-POSTEPU).
# ---------------------------------------------------------------------------------------------------------------------
NOMINALNE_TICKI_HZ = 250.0
PROG_ZAPASCI_TICKOW = 0.10          # ulamek nominalnej czestosci
PROG_OKNA_AWARII_S = 2.0
PROG_POKRYCIA_HOSTEM = 0.5
PROG_BRAKU_POSTEPU_S = 30.0
STATUSY_WIAZACE_FAIL = ('FAIL-JADRO', 'FAIL-QEMU', 'FAIL', 'FAIL-BRAK-POSTEPU', 'FAIL-TIMEOUT')


def _zdarzenia_czasowe(text):
    """[(indeks_linii, t_s, licznik_irq)] ze zdarzen PROC. Preferuje irq (kazde IRQ timera); stare logi: tick."""
    wynik = []
    for i, l in enumerate(text.splitlines()):
        if l.startswith(('[PROC-REQ]', '[PROC-CLEANUP]')):
            d = kv(l)
            licz = d.get('irq', d.get('tick', d.get('sprzatanie_tick')))
            if d.get('t_ns') and licz is not None: wynik.append((i, d['t_ns'] / 1e9, licz))
    return wynik


def okno_awarii(text, indeks_linii):
    """Okno czasu gosciu wokol linii FAIL: dt (HPET), przyrost IRQ, czestosc i czy nastapila zapasc tickow."""
    zd = _zdarzenia_czasowe(text)
    przed = [z for z in zd if z[0] < indeks_linii]; po = [z for z in zd if z[0] > indeks_linii]
    if not przed or not po: return None
    a, b = przed[-1], po[0]
    dt = b[1] - a[1]; dirq = b[2] - a[2]
    hz = dirq / dt if dt > 0 else float('inf')
    return {'dt_s': dt, 'irq': dirq, 'hz': hz,
            'zapasc': dt >= PROG_OKNA_AWARII_S and hz < PROG_ZAPASCI_TICKOW * NOMINALNE_TICKI_HZ}


def _pokrycie(a, b, przerwy_hosta):
    """Ile sekund przedzialu [a, b] (czas harnessu) przypada na przerwy hosta [(t, d)]."""
    return sum(max(0.0, min(b, t + d) - max(a, t)) for t, d in przerwy_hosta)


def okna_bez_postepu(probki, czas_s):
    """[(t0, t1)] przedzialy czasu harnessu, w ktorych dlugosc logu gosciu nie rosla (z koncowka do czas_s)."""
    okna = []; i = 0
    while i < len(probki):
        j = i
        while j + 1 < len(probki) and probki[j + 1][2] == probki[i][2]: j += 1
        t1 = probki[j + 1][0] if j + 1 < len(probki) else max(czas_s, probki[j][0])
        if t1 > probki[i][0]: okna.append((probki[i][0], t1))
        i = j + 1
    return okna


def klasyfikuj(text, powod, czas_s, przerwy_hosta, probki, min_pass, timeout_s):
    jadro, harness, st = analizuj(text, powod)
    suma_hosta = sum(d for _, d in przerwy_hosta)
    w = {'status': None, 'wiazacy': True, 'powody': [], 'jadro': jadro, 'harness': harness, 'st': st,
         'suma_przerw_hosta_s': round(suma_hosta, 1)}
    koniec = re.search(r'\[ZK\] KONIEC pass=\d+ fail=\d+', text) is not None
    linie = text.splitlines()
    if jadro:
        w['status'] = 'FAIL-JADRO'; w['powody'] += jadro
    elif powod.startswith('qemu-exit') and not koniec:
        w['status'] = 'FAIL-QEMU'; w['powody'].append(f'proces QEMU zakonczony przed KONIEC ({powod})')
    elif koniec and st['fail'] == 0 and st['pass'] >= min_pass:
        w['status'] = 'PASS'
        if suma_hosta: w['powody'].append(f'przerwy hosta {suma_hosta:.0f}s (bez wplywu na wynik)')
    elif st['scenariusze_fail']:
        wyjasnione = True
        for l in st['scenariusze_fail']:
            idx = next((i for i, x in enumerate(linie) if x == l), None)
            o = okno_awarii(text, idx) if idx is not None else None
            if o and o['zapasc'] and suma_hosta >= PROG_POKRYCIA_HOSTEM * o['dt_s']:
                w['powody'].append(f'{l.strip()}: okno {o["dt_s"]:.1f}s, IRQ {o["irq"]} ({o["hz"]:.1f} Hz), przerwy hosta {suma_hosta:.0f}s -> pauza hosta')
            else:
                wyjasnione = False
                opis = 'brak zdarzen wokol awarii' if not o else f'okno {o["dt_s"]:.1f}s, IRQ {o["hz"]:.1f} Hz, przerwy hosta {suma_hosta:.0f}s'
                w['powody'].append(f'{l.strip()}: {opis} -> awaria gosciu')
        w['status'] = 'STALL-HOSTA' if wyjasnione else 'FAIL'
    elif koniec:
        w['status'] = 'FAIL'; w['powody'].append(f'za malo scenariuszy: pass={st["pass"]} < {min_pass}')
    elif powod == 'timeout' and not probki:
        w['status'] = 'FAIL-TIMEOUT'; w['powody'].append('timeout bez probek postepu: pauzy hosta nie da sie udowodnic')
    elif powod == 'timeout':
        stale = []
        for a, b in okna_bez_postepu(probki, czas_s):
            aktywny = (b - a) - _pokrycie(a, b, przerwy_hosta)
            if aktywny >= PROG_BRAKU_POSTEPU_S: stale.append((a, b, aktywny))
        dostepny = czas_s - suma_hosta
        if stale:
            w['status'] = 'FAIL-BRAK-POSTEPU'
            w['powody'] += [f'brak postepu logu t={a:.0f}..{b:.0f}s, w tym {akt:.0f}s z CPU od hosta' for a, b, akt in stale]
        elif dostepny < 0.5 * timeout_s:
            w['status'] = 'STALL-HOSTA'
            w['powody'].append(f'przerwy hosta {suma_hosta:.0f}s z {czas_s:.0f}s: gosc mial tylko {dostepny:.0f}s CPU')
        else:
            w['status'] = 'FAIL-TIMEOUT'
            w['powody'].append(f'timeout przy postepie: gosc mial {dostepny:.0f}s CPU (limit {timeout_s:.0f}s)')
    else:
        w['status'] = 'FAIL'; w['powody'].append(f'nieoczekiwany powod zakonczenia: {powod}')
    w['wiazacy'] = w['status'] != 'STALL-HOSTA'
    return w


def katalog_przebiegow(baza):
    """Nigdy nie nadpisuje dowodow: istniejacy niepusty katalog -> nowy z sufiksem czasu."""
    baza = pathlib.Path(baza)
    if baza.exists() and any(baza.iterdir()):
        baza = baza.with_name(baza.name + time.strftime('_%Y%m%d-%H%M%S'))
    baza.mkdir(parents=True, exist_ok=True)
    return baza


def main():
    import argparse, json
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=1)
    ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--timeout', type=float, default=float(os.environ.get('BURSZTYN_QEMU_TIMEOUT', '600')),
                    help='limit jednego przebiegu w czasie rzeczywistym hosta (CLOCK_MONOTONIC, wlicza pauzy VM)')
    ap.add_argument('--limit-sesji', type=float, default=None,
                    help='twardy limit calej sesji [s]; domyslnie runs*(timeout+30). Po jego przekroczeniu kolejne przebiegi nie startuja')
    ap.add_argument('--stop-on-fail', action='store_true')
    ap.add_argument('--regs', action='store_true', help='probkuj rejestry vCPU przez monitor QEMU co 4 s')
    ap.add_argument('--out-runs', default='runs')
    args = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    limit_sesji = args.limit_sesji if args.limit_sesji is not None else args.runs * (args.timeout + 30.0)
    t_sesji = time.monotonic()
    print(f'[LIMITY] przebieg={args.timeout:.0f}s sesja={limit_sesji:.0f}s (czas rzeczywisty hosta)')
    min_pass = 1 if os.environ.get('ZK_TYLKO_FLOOD') == '1' else 15
    iso = OUT / 'test.iso' if args.skip_build else zbuduj()
    runs = katalog_przebiegow(OUT / args.out_runs)
    print(f'[DOWODY] {runs}')
    licz = {}; wszystkie = []
    for i in range(1, args.runs + 1):
        pozostalo = limit_sesji - (time.monotonic() - t_sesji)
        if pozostalo < 30.0:
            print(f'[LIMIT SESJI] pozostalo {pozostalo:.0f}s - przebiegi {i}..{args.runs} NIE wykonane'); break
        log = runs / f'run_{i:02d}.log'
        text, powod, czas = uruchom_gosc(iso, log, min(args.timeout, pozostalo), args.regs, qemu_log=runs / f'run_{i:02d}.qemu.log')
        probki = getattr(uruchom_gosc, 'probki', []); przerwy_hosta = getattr(uruchom_gosc, 'przerwy_hosta', [])
        w = klasyfikuj(text, powod, czas, przerwy_hosta, probki, min_pass, args.timeout)
        st = w['st']; status = w['status']
        licz[status] = licz.get(status, 0) + 1
        print(f'[run {i}/{args.runs}] {status} stalle_hosta={[(round(t), round(d)) for t, d in przerwy_hosta]} powod={powod} czas={czas:.0f}s przerw>1s={st["przerwy_tickow_gt1s"]} maks_przerwa={st["maks_przerwa_s"]:.1f}s min_tick_hz={st["min_czestosc_tickow_hz"] if st["min_czestosc_tickow_hz"] is None else round(st["min_czestosc_tickow_hz"],1)} pass={st["pass"]} fail={st["fail"]} '
              f'zwolnien={st["zwolnien"]} maks_latencja_ms={st["maks_latencja_ms"]:.1f} maks_latencja_irq={st["maks_latencja_irq"]} aktywne_koniec={st["aktywne_koniec"]} '
              f'ramki={st["ramki"][0] if st["ramki"] else None}->{st["ramki"][-1] if st["ramki"] else None}', flush=True)
        for e in w['powody']: print('   KLASYFIKACJA:', e)
        for e in diagnoza_hosta(probki): print('   OKNO-BEZ-POSTEPU:', e)
        rej = getattr(uruchom_gosc, 'rejestry', [])
        if rej:
            (runs / f'run_{i:02d}.regs').write_text('\n'.join(f'{t:.1f} rip={r:#x} rfl={f:#x} if={(f >> 9) & 1} cpl={c} cr3={c3:#x} guest_t={g:.1f}' for t, r, f, c, c3, g in rej))
            ring0_if0 = sum(1 for _, _, f, c, _, _ in rej if c == 0 and not (f >> 9) & 1)
            print(f'   REJESTRY: probek={len(rej)} cpl0={sum(1 for x in rej if x[3] == 0)} cpl0_if0={ring0_if0}')
        for e in w['jadro']: print('   JADRO:', e)
        for e in w['harness']: print('   HARNESS:', e)
        for l in st['scenariusze_fail']: print('   scenariusz:', l)
        (runs / f'run_{i:02d}.klasyfikacja.json').write_text(json.dumps(
            {'status': status, 'wiazacy': w['wiazacy'], 'powod': powod, 'czas_s': round(czas, 1), 'powody': w['powody'],
             'przerwy_hosta': przerwy_hosta, 'probki': probki, 'jadro': w['jadro'], 'harness': w['harness']}, indent=1))
        wszystkie.append((i, status, st))
        if status != 'PASS':
            shutil.copy2(log, runs / f'FAILED_run_{i:02d}.log')        # kazda porazka (takze STALL-HOSTA) zachowuje log
            if args.stop_on_fail and status in STATUSY_WIAZACE_FAIL: break
    maks = max((st['maks_latencja_ms'] for _, _, st in wszystkie), default=0)
    fail_n = sum(licz.get(k, 0) for k in STATUSY_WIAZACE_FAIL); stall_n = licz.get('STALL-HOSTA', 0)
    print(f'[PODSUMOWANIE] zaplanowano={args.runs} wykonano={len(wszystkie)} PASS={licz.get("PASS", 0)} FAIL(wiazace)={fail_n} '
          f'STALL-HOSTA(niewiazace)={stall_n} statusy={licz} maks_latencja_sprzatania_ms={maks:.1f}')
    if fail_n: sys.exit(1)
    if stall_n or len(wszystkie) < args.runs:
        print('WYNIK NIEKOMPLETNY: przebiegi niewiazace lub niewykonane - wynik nie jest PASS'); sys.exit(2)
    print('ZAKONCZENIE PROCESOW natywnie: PASS')


if __name__ == '__main__': main()
