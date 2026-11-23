#!/usr/bin/env python3
"""Wybor dzwieku w launcherach QEMU (rununix_virtio_latest.sh, runwindows.ps1) - bez uruchamiania QEMU.

Linux: wykonywane na kopii skryptu w katalogu tymczasowym z atrapa QEMU (lista -audiodev help, wersja,
-device help, symulowane komunikaty stderr i kod wyjscia), atrapami pactl/pw-cli, sztucznym XDG_RUNTIME_DIR
(gniazda Unix) i plikiem kart ALSA. Prawdziwy wirtualny_dysk.img, ISO i uslugi dzwieku hosta nie sa dotykane.
Windows: sprawdzenia statyczne skryptu; gdy jest PowerShell (pwsh), takze -PrintArgv dla kazdego trybu."""
import json, os, shlex, shutil, socket, stat, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UNIX = ROOT / 'rununix_virtio_latest.sh'
WIN = ROOT / 'runwindows.ps1'
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


MOCK_QEMU = r'''#!/usr/bin/env bash
case "$*" in
    "-audiodev help")
        [[ "${MOCK_HELP_FAIL:-0}" == 1 ]] && exit 1
        printf 'Available audio drivers:\r\n'
        for b in $MOCK_BACKENDS; do printf '%s\r\n' "$b"; done
        exit 0 ;;
    "--version") echo "QEMU emulator version 9.9.9 (atrapa)"; exit 0 ;;
    "-device help")
        for d in virtio-blk-pci virtio-net-pci virtio-rng-pci virtio-vga virtio-keyboard-pci virtio-mouse-pci virtio-tablet-pci intel-hda hda-output; do
            echo "name \"$d\", bus PCI"
        done
        exit 0 ;;
esac
if [[ "$1" == "-device" ]]; then echo "  disable-legacy=<OnOffAuto>"; exit 0; fi
printf '%s\n' "$@" > "$MOCK_LOG"
for a in "$@"; do
    case "$a" in wav,id=snd,path=*) p="${a#wav,id=snd,path=}"; p="${p//,,/,}"; printf 'RIFF' > "$p" ;; esac
done
[[ -n "${MOCK_STDOUT:-}" ]] && printf '%s\n' "$MOCK_STDOUT"
[[ -n "${MOCK_STDERR:-}" ]] && printf '%s\n' "$MOCK_STDERR" >&2
exit "${MOCK_EXIT:-0}"
'''
MOCK_PACTL = r'''#!/usr/bin/env bash
[[ "${MOCK_PA_OK:-0}" == 1 ]] || exit 1
case "$1" in
    info) printf 'Server Name: PulseAudio (on PipeWire 1.0.5)\nDefault Sample Specification: s16le 2ch 48000Hz\nDefault Sink: %s\n' "${MOCK_SINK:-alsa_output.pci-0000_00_05.0.analog-stereo}" ;;
    *) echo "0 atrapa" ;;
esac
'''
MOCK_PWCLI = '#!/usr/bin/env bash\n[[ "${MOCK_PW_OK:-0}" == 1 ]]\n'


def write_exec(path, text):
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class Env:
    """Kopia launchera + atrapy w krotkiej sciezce /tmp (limit dlugosci sciezki gniazd Unix)."""
    def __init__(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='bzaud-'))
        self.dir = self.tmp / 'launcher ą ść, x'
        self.dir.mkdir()
        self.script = self.dir / UNIX.name
        shutil.copy2(UNIX, self.script)
        (self.dir / 'BursztynOS.iso').write_bytes(b'')
        (self.dir / 'wirtualny_dysk.img').write_bytes(b'')
        self.bin = self.tmp / 'bin'
        self.bin.mkdir()
        write_exec(self.bin / 'qemu-system-x86_64', MOCK_QEMU)
        write_exec(self.bin / 'pactl', MOCK_PACTL)
        write_exec(self.bin / 'pw-cli', MOCK_PWCLI)
        self.run_dir = self.tmp / 'run'
        (self.run_dir / 'pulse').mkdir(parents=True)
        self.cards = self.tmp / 'cards'
        self.log = self.tmp / 'qemu-argv.txt'
        self.socks = []

    def sock(self, rel):
        s = socket.socket(socket.AF_UNIX)
        s.bind(str(self.run_dir / rel))
        self.socks.append(s)

    def env(self, **extra):
        e = {k: v for k, v in os.environ.items() if k not in ('PULSE_SERVER', 'PIPEWIRE_RUNTIME_DIR', 'PIPEWIRE_REMOTE', 'AUDIO_BACKEND', 'AUDIO_WAV_PATH')}
        e.update(PATH=f'{self.bin}:/usr/bin:/bin', QEMU=str(self.bin / 'qemu-system-x86_64'), XDG_RUNTIME_DIR=str(self.run_dir),
                 BURSZTYN_ASOUND_CARDS=str(self.cards), MOCK_LOG=str(self.log),
                 MOCK_BACKENDS='none alsa dbus jack oss pa pipewire sdl spice wav', QEMU_DISPLAY_ARGS='-display none')
        e.update({k: str(v) for k, v in extra.items()})
        return e

    def run(self, *args, cwd=None, inp='', **extra):
        if self.log.exists():
            self.log.unlink()
        return subprocess.run(['bash', str(self.script), *args], cwd=cwd or self.dir, text=True, capture_output=True,
                              input=inp, env=self.env(**extra), timeout=60)

    def close(self):
        for s in self.socks:
            s.close()
        shutil.rmtree(self.tmp, ignore_errors=True)


def argv_of(p):
    words = shlex.split(p.stdout.strip().splitlines()[-1])
    while words and '=' in words[0] and not words[0].startswith('-') and '/' not in words[0].split('=')[0]:
        words.pop(0)
    return words


def audio_and_devices(argv):
    audiodevs = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == '-audiodev']
    devices = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == '-device']
    return audiodevs, devices


def check_hda(argv, label):
    audiodevs, devices = audio_and_devices(argv)
    check(len(audiodevs) == 1, f'{label}: dokladnie jeden -audiodev: {audiodevs}')
    check(audiodevs[0].split(',')[1] == 'id=snd' if not audiodevs[0].startswith('wav,') else 'id=snd' in audiodevs[0], f'{label}: id=snd')
    check(devices.count('intel-hda') == 1 and devices.count('hda-output,audiodev=snd') == 1, f'{label}: jedno HDA')
    check(devices[0] == 'intel-hda' and devices[1] == 'hda-output,audiodev=snd', f'{label}: HDA przed VirtIO (00:02.0)')
    return audiodevs[0], devices


# ============================================================ Linux
check(subprocess.run(['bash', '-n', str(UNIX)]).returncode == 0, 'bash -n')
text = UNIX.read_text()
check('dsound' not in text.split('case "$HOST_NAME:$AUDIO_REQUEST" in')[1].split('esac')[0].split('*:dsound|*:directsound)')[0], 'Linux nie oferuje DirectSound')
check('pulseaudio --kill' not in text and 'systemctl' not in text and 'apt' not in text.split('\n#')[0], 'launcher nie restartuje/instaluje uslug dzwieku')

e = Env()
try:
    # --- domyslnie ALSA (jak Makefile), topologia PCI bez zmian dla kazdego jawnego trybu
    e.cards.write_text(' 0 [Intel          ]: HDA-Intel - HDA Intel\n')
    base = e.run('--dry-run', '--mode', '1')
    check(base.returncode == 0, base.stderr)
    arg, base_devices = check_hda(argv_of(base), 'domyslny')
    check(arg == 'alsa,id=snd', f'domyslnie ALSA: {arg}')
    check('Wykryte urządzenie Intel HDA' in base.stderr and 'Wersja QEMU:     QEMU emulator version 9.9.9' in base.stderr, 'podsumowanie dzwieku')
    e.sock('pipewire-0'); e.sock('pulse/native')
    for mode, expected in [('pipewire', 'pipewire,id=snd'), ('pa', 'pa,id=snd'), ('pulseaudio', 'pa,id=snd'), ('alsa', 'alsa,id=snd'),
                           ('sdl', 'sdl,id=snd'), ('none', 'none,id=snd'), ('off', 'none,id=snd')]:
        p = e.run('--dry-run', '--mode', '1', '--audio', mode, MOCK_PA_OK=1, MOCK_PW_OK=1)
        check(p.returncode == 0, f'{mode}: {p.stderr}')
        a, devices = check_hda(argv_of(p), mode)
        check(a == expected, f'{mode}: {a}')
        check(devices == base_devices, f'{mode}: topologia PCI bez zmian')
    # AUDIO_BACKEND (dotychczasowy interfejs) nadal dziala; --audio ma pierwszenstwo
    p = e.run('--dry-run', '--mode', '1', AUDIO_BACKEND='none')
    check(check_hda(argv_of(p), 'AUDIO_BACKEND')[0] == 'none,id=snd', 'AUDIO_BACKEND=none')
    p = e.run('--dry-run', '--mode', '1', '--audio', 'sdl', AUDIO_BACKEND='none', MOCK_PA_OK=1, MOCK_PW_OK=1)
    check(check_hda(argv_of(p), 'pierwszenstwo')[0] == 'sdl,id=snd', '--audio > AUDIO_BACKEND')
    # wszystkie tryby uruchomieniowe dzialaja z wyborem dzwieku (istniejace profile)
    for mode in (1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18):
        p = e.run('--dry-run', '--mode', str(mode), '--audio', 'none')
        check(p.returncode == 0 and check_hda(argv_of(p), f'tryb {mode}')[0] == 'none,id=snd', f'tryb {mode}: {p.stderr}')

    # --- wykrywanie i tryb automatyczny: PipeWire > PulseAudio > ALSA; bez cichego "none"
    def auto(**kw):
        p = e.run('--dry-run', '--mode', '1', '--audio', 'auto', **kw)
        return p, (check_hda(argv_of(p), 'auto')[0] if p.returncode == 0 else None)
    p, a = auto(MOCK_PA_OK=1, MOCK_PW_OK=1)
    check(a == 'pipewire,id=snd' and 'automatycznie: PipeWire' in p.stderr, f'auto -> pipewire: {a}')
    p, a = auto(MOCK_PA_OK=1, MOCK_PW_OK=0)
    check(a == 'pa,id=snd', f'auto -> pa gdy PipeWire nie odpowiada: {a}')
    p, a = auto(MOCK_PA_OK=1, MOCK_PW_OK=1, MOCK_BACKENDS='none alsa pa wav')
    check(a == 'pa,id=snd', f'auto pomija backend nieobecny w QEMU: {a}')
    p, a = auto(MOCK_PA_OK=1, MOCK_PW_OK=1, MOCK_SINK='auto_null', MOCK_BACKENDS='none alsa pa wav')
    check(a == 'alsa,id=snd', f'auto: PulseAudio bez urzadzenia wyjsciowego -> ALSA: {a}')
    p, a = auto(MOCK_PA_OK=0, MOCK_PW_OK=0)
    check(a == 'alsa,id=snd', f'auto -> alsa: {a}')
    e.cards.write_text('--- no soundcards ---\n')
    p, a = auto(MOCK_PA_OK=0, MOCK_PW_OK=0)
    check(p.returncode == 3 and a is None and 'nie znaleziono działającego wyjścia' in p.stderr and "Nie przełączam po cichu na 'none'" in p.stderr,
          f'auto bez wyjscia: blad, nie none\n{p.stderr}')
    p, a = auto(MOCK_PA_OK=1, MOCK_PW_OK=1, MOCK_HELP_FAIL=1)
    check(p.returncode == 3 and 'wymaga listy backendów QEMU' in p.stderr, 'auto bez -audiodev help')
    # parser: naglowek i CRLF pomijane (lista z atrapy ma \r\n)
    p = e.run('--audio-diag', MOCK_PA_OK=1, MOCK_PW_OK=1)
    check(p.returncode == 0 and 'Backendy QEMU:   none alsa dbus jack oss pa pipewire sdl spice wav' in p.stdout, f'diagnostyka:\n{p.stdout}')
    check('kto teraz gra' in p.stdout and 'uruchomione procesy QEMU' in p.stdout, 'diagnostyka: strumienie i procesy')
    check(not e.log.exists(), 'diagnostyka nie uruchamia QEMU')

    # --- jawny wybor niedostepny: blad, QEMU nieuruchomione, bez zamiany backendu
    e.cards.write_text(' 0 [Intel          ]: HDA-Intel - HDA Intel\n')
    p = e.run('--mode', '1', '--audio', 'pa', MOCK_PA_OK=0, MOCK_PW_OK=1)
    check(p.returncode == 3 and 'PulseAudio jest niedostępny' in p.stderr and not e.log.exists(), f'pa niedostepny:\n{p.stderr}')
    p = e.run('--mode', '1', '--audio', 'sdl', MOCK_BACKENDS='none alsa wav')
    check(p.returncode == 3 and "nie ma backendu 'sdl'" in p.stderr and not e.log.exists(), 'backend nieobslugiwany')
    p = e.run('--dry-run', '--mode', '1', '--audio', 'pa', MOCK_PA_OK=0)
    check(p.returncode == 0 and 'dry-run - tylko ostrzeżenie' in p.stderr and check_hda(argv_of(p), 'dry pa')[0] == 'pa,id=snd', 'dry-run: ostrzezenie')
    for bad in (['--audio', 'dsound'], ['--audio', 'directsound'], ['--audio', 'coreaudio'], ['--audio', 'xyz'], ['--audio']):
        p = e.run('--dry-run', '--mode', '1', *bad)
        check(p.returncode == 2, f'{bad}: kod 2')
    p = e.run('--dry-run', '--mode', '1', '--audio', 'alsa', '--audio-wav', 'x.wav')
    check(p.returncode == 2 and 'tylko z --audio wav' in p.stderr, '--audio-wav z innym trybem')

    # --- WAV: jeden argument, przecinek podwojony, spacje/UTF-8, brak nadpisywania
    rec = e.dir / 'nagrania' / 'próba ź, 1 [x].wav'
    p = e.run('--dry-run', '--mode', '1', '--audio', 'wav', '--audio-wav', str(rec))
    a, _ = check_hda(argv_of(p), 'wav')
    check(a == 'wav,id=snd,path=' + str(rec).replace(',', ',,'), f'wav: {a}')
    check(f'Ścieżka nagrania: {rec}' in p.stderr and not rec.parent.exists(), 'dry-run nie tworzy katalogu')
    p = e.run('--dry-run', '--mode', '1', '--audio-wav', 'wzgl/plik.wav', cwd=e.tmp)
    check(check_hda(argv_of(p), 'wzgl')[0] == 'wav,id=snd,path=' + str(e.tmp / 'wzgl' / 'plik.wav').replace(',', ',,'),
          'sciezka wzgledna wzgledem katalogu wywolania; --audio-wav bez --audio = wav')
    p = e.run('--dry-run', '--mode', '1', '--audio', 'wav')
    a = check_hda(argv_of(p), 'wav domyslny')[0]
    check('/logi_qemu/nagrania_audio/bursztyn-hda-' in a and a.endswith('.wav'), f'domyslna sciezka: {a}')
    # rzeczywiste "uruchomienie" atrapy: katalog tworzony, plik zgloszony po zakonczeniu
    p = e.run('--mode', '1', '--audio', 'wav', '--audio-wav', str(rec))
    check(p.returncode == 0 and rec.read_bytes() == b'RIFF', f'wav: plik\n{p.stdout}\n{p.stderr}')
    check(f'Nagranie WAV: {rec} (4 B' in p.stdout, 'raport lokalizacji nagrania')
    # istniejacy plik: odmowa bez --audio-overwrite (nieinteraktywnie), tresc nienaruszona
    rec.write_bytes(b'stare nagranie')
    p = e.run('--mode', '1', '--audio', 'wav', '--audio-wav', str(rec))
    check(p.returncode == 3 and 'nie nadpisuję bez --audio-overwrite' in p.stderr and rec.read_bytes() == b'stare nagranie' and not e.log.exists(), 'bez nadpisania')
    p = e.run('--mode', '1', '--audio', 'wav', '--audio-wav', str(rec), '--audio-overwrite')
    check(p.returncode == 0 and rec.read_bytes() == b'RIFF', '--audio-overwrite')
    # katalog jako sciezka: unikalna nazwa domyslna w nim; dowiazanie odrzucane
    p = e.run('--dry-run', '--mode', '1', '--audio', 'wav', '--audio-wav', str(rec.parent))
    check(check_hda(argv_of(p), 'katalog')[0].startswith('wav,id=snd,path=' + str(rec.parent).replace(',', ',,') + '/bursztyn-hda-'), 'katalog')
    link = e.dir / 'link.wav'
    os.symlink(rec, link)
    p = e.run('--mode', '1', '--audio', 'wav', '--audio-wav', str(link))
    check(p.returncode == 3 and 'dowiązaniem' in p.stderr, 'dowiazanie')
    # dwa nagrania domyslne w tej samej sekundzie: unikalne nazwy
    d = e.dir / 'logi_qemu' / 'nagrania_audio'
    p1 = e.run('--mode', '1', '--audio', 'wav'); p2 = e.run('--mode', '1', '--audio', 'wav')
    check(p1.returncode == 0 and p2.returncode == 0 and len(list(d.glob('bursztyn-hda-*.wav'))) == 2, 'unikalne nazwy domyslne')

    # --- klasyfikacja bledow po zakonczeniu QEMU (stderr widoczny, bez falszywego "dziala")
    p = e.run('--mode', '1', '--audio', 'pa', MOCK_PA_OK=1, MOCK_EXIT=1,
              MOCK_STDERR="pulseaudio: pa_context_connect() failed\npulseaudio: Reason: Connection refused\naudio: Could not init `pa' audio driver")
    check(p.returncode == 1 and 'serwer dźwięku (pa) jest niedostępny' in p.stdout and 'pa_context_connect' in p.stderr, f'serwer:\n{p.stdout}')
    check("launcher nie przełącza go samodzielnie" in p.stdout, 'bez automatycznej zmiany')
    p = e.run('--mode', '1', '--audio', 'alsa', MOCK_EXIT=1,
              MOCK_STDERR="alsa: Could not initialize DAC\nalsa: Failed to open `default':\nalsa: Reason: No such device")
    check('brak urządzenia wyjściowego hosta (alsa)' in p.stdout, 'urzadzenie')
    p = e.run('--mode', '1', '--audio', 'sdl', MOCK_PA_OK=1, MOCK_EXIT=1, MOCK_STDERR='sdl: SDL_OpenAudio failed\naudio: Could not init `sdl\' audio driver')
    check("inicjalizacja backendu 'sdl' nie powiodła się" in p.stdout, f'init:\n{p.stdout}')
    p = e.run('--mode', '1', '--audio', 'none', MOCK_EXIT=1,
              MOCK_STDERR="qemu-system-x86_64: -audiodev foo,id=snd: Parameter 'driver' does not accept value 'foo'")
    check('nie jest obsługiwany przez to QEMU' in p.stdout, 'nieobslugiwany (komunikat QEMU)')
    p = e.run('--mode', '1', '--audio', 'alsa', MOCK_EXIT=1, MOCK_STDERR='qemu-system-x86_64: Could not open option rom', MOCK_STDOUT='[HDA] audio init failed (linia goscia)')
    check(p.returncode == 1 and 'przyczyna niezwiązana z wyborem dźwięku' in p.stdout and 'Błąd uruchomienia dźwięku' not in p.stdout,
          f'blad niezwiazany; linia portu szeregowego goscia nie jest bledem dzwieku\n{p.stdout}')
    p = e.run('--mode', '1', '--audio', 'alsa')
    check(p.returncode == 0 and 'to nie potwierdza słyszalnego odtwarzania' in p.stdout, 'sukces bez falszywego potwierdzenia')
    # log COM1 (tryb 3): klasyfikacja z pliku logu
    p = e.run('--mode', '3', '--audio', 'alsa', MOCK_EXIT=1, MOCK_STDERR='alsa: Could not initialize DAC')
    check('brak urządzenia wyjściowego hosta (alsa)' in p.stdout and list((e.dir / 'logi_qemu').glob('*.log')), 'log COM1')

    # --- brak QEMU; tryb nieinteraktywny nie czeka na menu
    p = e.run('--mode', '1', QEMU='/nie/ma/qemu')
    check(p.returncode == 1 and 'Brak programu' in p.stderr, 'brak QEMU')
    p = e.run('--dry-run', '--mode', '1', '--audio', 'auto', QEMU='/nie/ma/qemu')
    check(p.returncode == 3, 'auto bez QEMU')
    p = e.run('--dry-run', '--mode', '1', '--audio', 'alsa', QEMU='/nie/ma/qemu')
    check(p.returncode == 0 and 'nie można sprawdzić' in p.stderr, 'jawny wybor bez QEMU w dry-run: ostrzezenie')
    # menu interaktywne: A -> wybor SDL -> powrot; wyjscie 0
    p = e.run(inp='A\n5\n0\n', MOCK_PA_OK=1, MOCK_PW_OK=1)
    check(p.returncode == 0 and 'Wyjście dźwięku: SDL' in p.stdout and 'obecnie: SDL' in p.stdout, f'menu A\n{p.stdout[-800:]}')
    p = e.run(inp='A\n2\n0\n', MOCK_PA_OK=0, MOCK_PW_OK=0)
    check('PipeWire jest niedostępny' in p.stderr and 'obecnie: ALSA' in p.stdout, 'menu: niedostepna opcja nie zostaje wybrana')
finally:
    e.close()

if shutil.which('shellcheck'):
    r = subprocess.run(['shellcheck', '-S', 'error', str(UNIX)], capture_output=True, text=True)
    check(r.returncode == 0, r.stdout)
    shellcheck = 'shellcheck OK'
else:
    shellcheck = 'shellcheck niedostepny (pominiety)'

# ============================================================ Windows (statycznie)
raw = WIN.read_bytes()
w = raw.decode('utf-8-sig')
check(raw.startswith(b'\xef\xbb\xbf'), 'runwindows.ps1: UTF-8 z BOM (PowerShell 5.1 i polskie znaki)')
check('[ValidateSet("dsound", "sdl", "wav", "none", "")]' in w and '[string]$AudioMode = ""' in w, 'parametr -AudioMode')
check('[string]$AudioWavPath = ""' in w and '[switch]$AudioOverwrite' in w, '-AudioWavPath / -AudioOverwrite')
check('if ([string]::IsNullOrWhiteSpace($AudioWavPath)) { "dsound" } else { "wav" }' in w, 'domyslnie DirectSound')
args_block = w.split('$argsList = @(', 1)[1].split('if ($VirtIoFull) {', 1)[0]
check(args_block.count('"-audiodev"') == 1 and '"-audiodev", $audio.Arg,' in args_block, 'jeden -audiodev z konfiguracji')
check(w.count('"-device", "intel-hda"') == 1 and w.count('"-device", "hda-output,audiodev=snd"') == 1, 'jedno HDA')
check('"-audiodev", "none,id=snd"' not in w, 'brak zakodowanego na stale none')
check('Invoke-NativeCapture -Exe $Qemu -Arguments @("-audiodev", "help")' in w, 'wykrywanie przez -audiodev help wykrytego QEMU')
check("if ($t -match '^[a-z0-9_]+$')" in w, 'parser nazw backendow (bez numerow linii)')
check('nie obsługuje backendu' in w and 'throw ("Błąd uruchomienia dźwięku' in w, 'nieobslugiwany backend: wyjatek')
check(w.count('$script:AudioSelection = ') == 2, 'wybor zmieniany tylko przez parametr i menu (bez cichego fallbacku)')
check('"wav,id=snd,path=$qemuWav"' in w and '$qemuWav = ConvertTo-QemuOptionPath -Path $wav' in w, 'WAV: przecinki podwojone, jeden argument')
check('nie nadpisuję bez -AudioOverwrite' in w and 'ReparsePoint' in w, 'WAV: brak nadpisywania, dowiazania')
check('Write-AudioOutcome -ExitCode $exitCode -Lines $audioLines.ToArray() -Audio $audio' in w, 'raport po zakonczeniu QEMU')
check('to nie potwierdza słyszalnego odtwarzania' in w and "-notmatch '(?i)capture'" in w, 'bez falszywego sukcesu; capture nie jest bledem')
check('"A" { Show-AudioMenu }' in w and 'Wybierz tryb [0-18, A]' in w, 'menu A')
check(w.index('$audio = Resolve-AudioConfig') < w.index('{ Prepare-Disk }'), 'dzwiek sprawdzany przed dyskiem')
check('-ReadOnly:($PrintArgv -or $Audit)' in w, '-PrintArgv/-Audit bez tworzenia katalogow i sond')
for label in ('Wyjście dźwięku', 'Wykryte urządzenie Intel HDA', 'Błąd uruchomienia dźwięku', 'Ścieżka nagrania', 'Brak dźwięku', 'Nagrywanie dźwięku do WAV'):
    check(label in w, f'etykieta: {label}')

pwsh = shutil.which(os.environ.get('PWSH', 'pwsh'))
if pwsh:
    r = subprocess.run([pwsh, '-NoProfile', '-Command', f"$null=[System.Management.Automation.Language.Parser]::ParseFile('{WIN}',[ref]$null,[ref]$e); if($e){{$e;exit 1}}"],
                       capture_output=True, text=True)
    check(r.returncode == 0, 'skladnia PowerShell: ' + r.stdout)
    for mode, expected in (('dsound', 'dsound,id=snd'), ('sdl', 'sdl,id=snd'), ('none', 'none,id=snd')):
        out = subprocess.check_output([pwsh, '-NoProfile', '-File', str(WIN), '-PrintArgv', '-QemuPath', 'qemu-system-x86_64.exe',
                                       '-FirmwarePath', 'C:/QEMU/OVMF.fd', '-AudioMode', mode], cwd=ROOT, text=True)
        argv = json.loads(out)['argv']
        check(argv.count('-audiodev') == 1 and argv[argv.index('-audiodev') + 1] == expected, f'Windows {mode}')
    windows = 'PowerShell: skladnia + -PrintArgv dla dsound/sdl/none'
else:
    windows = 'PowerShell niedostepny - Windows tylko statycznie'

print(f'Launcher audio: {checks} checks PASS (Linux: wykrywanie, auto, jawne backendy, WAV, bledy, menu, '
      f'diagnostyka; {shellcheck}; {windows})')
