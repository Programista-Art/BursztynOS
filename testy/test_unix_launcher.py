#!/usr/bin/env python3
"""rununix_virtio_latest.sh: skladnia, dry-run wszystkich trybow, zgodnosc z `make runuefi-argv`,
bezpieczenstwo dysku i obejscie IBus tylko dla procesu QEMU. Bez uruchamiania QEMU; przypadki brzegowe
w izolowanym katalogu tymczasowym (wirtualny_dysk.img uzytkownika nie jest dotykany)."""
import json, os, shlex, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
from qemu_launcher_audit import normalize, topology  # noqa: E402

SCRIPT = ROOT / 'rununix_virtio_latest.sh'
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


def dry(mode, cwd=ROOT, script=SCRIPT, env=None, ok=True):
    p = subprocess.run(['bash', str(script), '--dry-run', '--mode', str(mode)], cwd=cwd, text=True,
                       capture_output=True, env=dict(os.environ, **(env or {})), timeout=60)
    if ok:
        check(p.returncode == 0, f'tryb {mode}: kod {p.returncode}\n{p.stderr}')
    return p


def argv_of(p):
    line = p.stdout.strip().splitlines()[-1]
    words = shlex.split(line)
    env = {}
    while words and '=' in words[0] and not words[0].startswith('-') and '/' not in words[0].split('=')[0]:
        k, v = words.pop(0).split('=', 1)
        env[k] = v
    return env, words


check(subprocess.run(['bash', '-n', str(SCRIPT)]).returncode == 0, 'bash -n')
check(os.access(SCRIPT, os.X_OK), 'brak prawa wykonania')
check(SCRIPT.read_text().startswith('#!/usr/bin/env bash\n'), 'shebang bash')

# Wszystkie tryby uruchamiajace QEMU daja argv; tryby VirtIO maja tablet jako ostatnie urzadzenie.
for mode in (1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18):
    env, argv = argv_of(dry(mode))
    check(argv[0] == 'qemu-system-x86_64', f'tryb {mode}: binarka')
    check(env.get('IBUS_ADDRESS', '').startswith('unix:path=/nonexistent/'), f'tryb {mode}: IBus tylko dla QEMU')
    devs = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == '-device']
    if mode in (16, 17, 18):
        check('e1000,netdev=n1' in devs and not any(d.startswith('virtio-') for d in devs), f'tryb {mode}: legacy')
    else:
        check(devs[-1] == 'virtio-tablet-pci' and devs.count('virtio-mouse-pci') == 1, f'tryb {mode}: tablet ostatni')
    check(('-cdrom' in argv) == (mode not in (14, 18)), f'tryb {mode}: ISO')
    if '-cdrom' in argv:
        check(argv[argv.index('-boot') + 1] == 'order=d,menu=on', f'tryb {mode}: ISO ma pierwszenstwo')
    else:
        check(argv[argv.index('-boot') + 1] == 'order=c', f'tryb {mode}: start z dysku')

# Tryb 2 = make runuefi: identyczne opcje, wartosci, kolejnosc i BDF poza roznicami hosta.
_, uefi = argv_of(dry(2))
make = json.loads(subprocess.check_output(['make', '-s', 'runuefi-argv'], cwd=ROOT, text=True))['argv']


def canonical(argv):
    a = normalize(argv)
    out, i = [], 1
    while i < len(a):
        opt, val = a[i], a[i + 1]
        if opt not in ('-display', '-serial', '-name'):
            out += [opt, val]
        i += 2
    return out


check(canonical(uefi) == canonical(make), 'roznica wzgledem make runuefi-argv:\n%s\n%s' % (canonical(uefi), canonical(make)))
check(topology(uefi) == topology(make), 'topologia PCI')

with tempfile.TemporaryDirectory(prefix='launcher ą ść, test ') as tmp:
    t = Path(tmp)
    s = t / SCRIPT.name
    shutil.copy2(SCRIPT, s)
    (t / 'BursztynOS.iso').write_bytes(b'')
    # Sciezka ze spacja, polskimi znakami i przecinkiem: file= z podwojonym przecinkiem, -cdrom surowo.
    p = dry(2, cwd='/', script=s)
    _, argv = argv_of(p)
    drive = next(a for a in argv if a.startswith('id=disk,file='))
    check(drive == 'id=disk,file=' + str(t / 'wirtualny_dysk.img').replace(',', ',,') + ',format=raw,if=none', drive)
    check(argv[argv.index('-cdrom') + 1] == str(t / 'BursztynOS.iso'), 'cdrom')
    check(not (t / 'wirtualny_dysk.img').exists(), 'dry-run nie tworzy dysku')
    check('zwykly start utworzylby pusty dysk' in p.stderr, 'informacja o braku dysku')
    # Dysk jako dowiazanie do urzadzenia / zerwane dowiazanie: odmowa, nic nie tworzone.
    os.symlink('/dev/null', t / 'wirtualny_dysk.img')
    p = dry(2, script=s, ok=False)
    check(p.returncode == 1 and 'nie jest zwyklym plikiem' in p.stderr, 'urzadzenie jako dysk')
    os.unlink(t / 'wirtualny_dysk.img')
    os.symlink(t / 'brak' / 'x.img', t / 'wirtualny_dysk.img')
    p = dry(14, script=s, ok=False)
    check(p.returncode == 1 and 'zerwanym dowiazaniem' in p.stderr and not (t / 'brak').exists(), 'zerwane dowiazanie')
    os.unlink(t / 'wirtualny_dysk.img')
    # Usuwanie dysku: dry-run nic nie usuwa; bez potwierdzenia USUN dysk zostaje.
    disk = t / 'wirtualny_dysk.img'
    disk.write_bytes(b'dane')
    dry(13, script=s)
    check(disk.read_bytes() == b'dane', 'dry-run 13')
    p = subprocess.run(['bash', str(s), '--mode', '13'], input='usun\n', text=True, capture_output=True, timeout=30)
    check(p.returncode == 1 and disk.read_bytes() == b'dane' and 'Anulowano' in p.stdout, 'brak potwierdzenia')
    # Brak ISO: blad bez cichego fallbacku.
    (t / 'BursztynOS.iso').unlink()
    p = dry(2, script=s, ok=False)
    check(p.returncode == 1 and 'Nie znaleziono: obrazu ISO' in p.stderr, 'brak ISO')
    # Nieprawidlowe opcje: kod 2.
    for args in (['--mode', '99'], ['--nieznana']):
        p = subprocess.run(['bash', str(s)] + args, text=True, capture_output=True, timeout=30)
        check(p.returncode == 2, f'opcje {args}')

# Obejscie IBus mozna wylaczyc; nie jest eksportowane globalnie (tylko prefiks polecenia QEMU).
env, _ = argv_of(dry(2, env={'QEMU_SDL_BEZ_IME': '0', 'QEMU_DISPLAY_ARGS': '-display sdl'}))
check('IBUS_ADDRESS' not in env, 'QEMU_SDL_BEZ_IME=0')
text = SCRIPT.read_text()
check('export IBUS_ADDRESS' not in text and 'IBUS_ADDRESS="$IBUS_ADDRESS_BEZ_IME" "$QEMU" "$@"' in text, 'tylko proces QEMU')
check('rm -f "$DISK"' not in text and 'rm -f -- "$DISK"' in text and text.count('rm -f -- "$DISK"') == 1, 'jedno usuwanie, po potwierdzeniu')
print(f'Unix launcher: {checks} checks PASS (bash -n, 15 trybow dry-run, parzystosc z make runuefi-argv, '
      'sciezki Unicode/przecinek, bezpieczenstwo dysku, IBus tylko dla QEMU)')
