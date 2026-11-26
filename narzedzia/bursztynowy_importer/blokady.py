"""Wykrywanie, czy obraz jest uzywany (QEMU, VirtualBox, inny importer) i wylacznosc zapisu.

Metody (wszystkie tylko do odczytu, zadna nie zmienia obrazu):
  * plik blokady <obraz>.importer-blokada (O_EXCL) - jeden importer naraz; martwa blokada tego samego hosta
    (proces nie istnieje) jest rozpoznawana,
  * Linux: blokada fcntl na calym pliku (QEMU blokuje obrazy blokadami OFD, ktore koliduja z blokadami POSIX),
    deskryptory /proc/<pid>/fd wskazujace obraz, wiersze polecen procesow qemu*/VBox*/VirtualBox*,
  * Windows: otwarcie pliku bez wspoldzielenia (CreateFileW, share=0) - odmowa = plik otwarty przez inny proces;
    wiersze polecen procesow QEMU/VirtualBox (wmic/PowerShell, jesli dostepne).
"""
import os
import socket
import subprocess
import sys

NAZWY_MASZYN = ('qemu', 'virtualbox', 'vboxheadless', 'vboxsvc', 'virtualboxvm')


class ObrazUzywany(Exception):
    pass


def _linux_procesy_z_obrazem(sciezka):
    cel = os.path.realpath(sciezka)
    nazwa = os.path.basename(cel)
    wynik = []
    for pid in os.listdir('/proc'):
        if not pid.isdigit() or int(pid) == os.getpid():
            continue
        try:
            with open('/proc/%s/cmdline' % pid, 'rb') as f:
                cmd = f.read().replace(b'\0', b' ').decode('utf-8', 'replace')
        except OSError:
            continue
        exe = cmd.split(' ', 1)[0].lower()
        if any(n in os.path.basename(exe) for n in NAZWY_MASZYN) and nazwa in cmd:
            wynik.append((int(pid), cmd[:160]))
            continue
        try:
            for fd in os.listdir('/proc/%s/fd' % pid):
                try:
                    if os.path.realpath('/proc/%s/fd/%s' % (pid, fd)) == cel:
                        wynik.append((int(pid), cmd[:160]))
                        break
                except OSError:
                    continue
        except OSError:
            continue
    return wynik


def _windows_otwarty_gdzie_indziej(sciezka):
    import ctypes
    from ctypes import wintypes
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    GENERIC_READ, OPEN_EXISTING = 0x80000000, 3
    h = k32.CreateFileW(os.path.abspath(sciezka), GENERIC_READ, 0, None, OPEN_EXISTING, 0, None)
    if h == wintypes.HANDLE(-1).value or h is None:
        return ctypes.get_last_error() == 32       # ERROR_SHARING_VIOLATION
    k32.CloseHandle(h)
    return False


def _windows_procesy_z_obrazem(sciezka):
    nazwa = os.path.basename(sciezka).lower()
    wynik = []
    polecenia = [['powershell', '-NoProfile', '-Command',
                  'Get-CimInstance Win32_Process | ForEach-Object { "$($_.ProcessId)`t$($_.Name)`t$($_.CommandLine)" }']]
    for cmd in polecenia:
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        for linia in out.splitlines():
            czesci = linia.split('\t', 2)
            if len(czesci) == 3 and any(n in czesci[1].lower() for n in NAZWY_MASZYN) and nazwa in czesci[2].lower():
                wynik.append((int(czesci[0]) if czesci[0].isdigit() else 0, czesci[2][:160]))
        break
    return wynik


def sprawdz_czy_uzywany(sciezka):
    """Zglasza ObrazUzywany z opisem po polsku, gdy obraz jest (lub moze byc) uzywany."""
    if sys.platform.startswith('linux'):
        procesy = _linux_procesy_z_obrazem(sciezka)
        if procesy:
            raise ObrazUzywany('Obraz jest otwarty przez inny proces (np. QEMU/VirtualBox): ' +
                               '; '.join('PID %d: %s' % p for p in procesy[:3]) +
                               '. Zamknij całkowicie maszynę wirtualną i spróbuj ponownie.')
    elif os.name == 'nt':
        if _windows_otwarty_gdzie_indziej(sciezka):
            raise ObrazUzywany('Obraz jest otwarty przez inny program (np. QEMU lub VirtualBox). '
                               'Zamknij całkowicie maszynę wirtualną i spróbuj ponownie.')
        procesy = _windows_procesy_z_obrazem(sciezka)
        if procesy:
            raise ObrazUzywany('Działa maszyna wirtualna używająca tego obrazu: ' + procesy[0][1])


def _zywy(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except (PermissionError, OSError):
        return True


class Blokada:
    """Wylacznosc: plik blokady + (POSIX) blokada fcntl na oryginale przez caly czas transakcji."""

    def __init__(self, sciezka):
        self.sciezka = sciezka
        self.plik_blokady = sciezka + '.importer-blokada'
        self._fd = None
        self._fd_obrazu = None

    def __enter__(self):
        tresc = '%d %s\n' % (os.getpid(), socket.gethostname())
        for proba in range(2):
            try:
                self._fd = os.open(self.plik_blokady, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
                os.write(self._fd, tresc.encode())
                break
            except FileExistsError:
                try:
                    with open(self.plik_blokady) as f:
                        pid_s, host = (f.read().split() + ['', ''])[:2]
                except OSError:
                    pid_s, host = '', ''
                if proba == 0 and host == socket.gethostname() and pid_s.isdigit() and not _zywy(int(pid_s)):
                    os.unlink(self.plik_blokady)          # martwa blokada tego hosta
                    continue
                raise ObrazUzywany('Inny Bursztynowy Importer Plików pracuje z tym obrazem (plik blokady %s). '
                                   'Jeśli żaden importer nie działa, usuń ten plik ręcznie.' % self.plik_blokady)
        if os.name == 'posix':
            import fcntl
            # Blokada wylaczna na calym pliku: koliduje z blokadami OFD, ktore QEMU zaklada na uzywanym obrazie,
            # i dopoki jest trzymana, QEMU nie uruchomi sie z tym obrazem. Deskryptor sluzy tylko do blokady
            # (O_RDWR jest wymagane przez POSIX dla blokady wylacznej; nic nie jest zapisywane).
            try:
                self._fd_obrazu = os.open(self.sciezka, os.O_RDWR)
                tryb = fcntl.LOCK_EX
            except PermissionError:
                self._fd_obrazu = os.open(self.sciezka, os.O_RDONLY)
                tryb = fcntl.LOCK_SH
            try:
                fcntl.lockf(self._fd_obrazu, tryb | fcntl.LOCK_NB)
            except OSError:
                self.__exit__()
                raise ObrazUzywany('Obraz jest zablokowany przez inny proces (QEMU blokuje używane obrazy). '
                                   'Zamknij całkowicie maszynę wirtualną.')
        return self

    def __exit__(self, *a):
        if self._fd_obrazu is not None:
            os.close(self._fd_obrazu)
            self._fd_obrazu = None
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None
            try:
                os.unlink(self.plik_blokady)
            except OSError:
                pass
