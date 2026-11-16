#!/usr/bin/env python3
"""Niezalezny puls hosta: wykrywa zatrzymania calej maszyny (VM) niezaleznie od QEMU/jadra.
Co 0.25 s zapisuje przerwy > prog (domyslnie 1.5 s) miedzy kolejnymi obudzeniami: czas monotoniczny, czas scienny i
/proc/uptime. Jesli przerwy wystepuja bez uruchomionego QEMU, dlugie pauzy gosciu NIE sa winą jadra."""
import sys, time
prog = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5
czas = float(sys.argv[3]) if len(sys.argv) > 3 else 1800
wyj = open(sys.argv[1], 'a', buffering=1)
t0 = prev = time.monotonic(); r_prev = time.time(); n = 0; maks = 0.0
wyj.write(f'# start realtime={r_prev:.3f} prog={prog}s czas={czas}s\n')
while time.monotonic() - t0 < czas:
    time.sleep(0.25)
    now = time.monotonic(); r = time.time(); dt = now - prev; maks = max(maks, dt)
    if dt > prog:
        n += 1
        wyj.write(f'przerwa {dt:.1f}s monotonic_t={now - t0:.1f}s realtime={r:.3f} dr={r - r_prev:.1f}s loadavg={open("/proc/loadavg").read().split()[0]}\n')
    prev, r_prev = now, r
wyj.write(f'# koniec przerw={n} maks={maks:.1f}s\n')
