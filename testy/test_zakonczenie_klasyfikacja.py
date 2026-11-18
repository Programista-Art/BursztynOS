#!/usr/bin/env python3
"""Syntetyczne testy klasyfikacji przebiegow harnessu BWS 97 (testy/test_zakonczenie_qemu.py) - BEZ uruchamiania QEMU.

Sprawdza, ze: awaria jadra i wyjscie QEMU nigdy nie sa pauza hosta; pauza hosta wymaga dowodu po obu stronach;
brak postepu gosciu przy dostepnym CPU jest porazka; timeout bez probek jest porazka. Dodatkowo klasyfikuje dwa
prawdziwe logi z docs/zakonczenie-procesow-2026-11-16/serie (tylko odczyt), jesli sa dostepne.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import test_zakonczenie_qemu as h  # noqa: E402  (import nie uruchamia QEMU ani budowania)

ROOT = pathlib.Path(__file__).resolve().parent.parent
bledy = 0
sprawdzen = 0


def sprawdz(warunek, opis):
    global bledy, sprawdzen
    sprawdzen += 1
    if not warunek:
        bledy += 1
        print('BLAD:', opis)


def zdarzenie(pid, t_s, irq, rodzaj='REQ'):
    if rodzaj == 'REQ':
        return f'[PROC-REQ] pid={pid} t_ns={int(t_s * 1e9)} wynik=1 z_timera=0 stan=6 pid_biezacy=1 tick={irq} irq={irq} przelaczen=1 odroczonych=0'
    return (f'[PROC-CLEANUP] pid={pid} t_ns={int(t_s * 1e9)} wolne_ramki=469891 aktywne=1 cr3_biezacy=1 pid_biezacy=0 '
            f'sprzatanie_tick={irq} irq={irq} przelaczen=1 odroczonych=0')


def log_scenariuszy(n_pass, fail_linie=(), koniec=True, przerwa_przy_fail=None):
    """Log gosciu: pary REQ/CLEANUP co 0.2 s przy 250 Hz. przerwa_przy_fail=(dt_s, irq): okno wokol linii FAIL."""
    linie, t, irq = [], 1.0, 250
    for k in range(n_pass):
        linie += [zdarzenie(2, t, irq), zdarzenie(2, t + 0.01, irq + 2, 'CLEANUP'), f'[ZK] s{k + 1} test PASS']
        t += 0.2; irq += 50
    for f in fail_linie:
        linie.append(zdarzenie(3, t, irq))
        linie.append(f)
        dt, dirq = przerwa_przy_fail or (0.2, 50)
        t += dt; irq += dirq
        linie.append(zdarzenie(3, t, irq, 'CLEANUP'))
    if koniec:
        linie.append(f'[ZK] KONIEC pass={n_pass} fail={len(fail_linie)}')
    return '\n'.join(linie) + '\n'


def probki_rosnace(czas_s, co=5.0, przerwy=(), zamrozenie_od=None):
    """Probki (t, cpu, dlugosc_logu, load, steal): log rosnie co probke; brak probek w trakcie przerw hosta;
    od zamrozenie_od dlugosc stala (gosc przestal logowac)."""
    pr, t, n = [], 0.0, 100
    while t <= czas_s:
        if not any(a <= t < a + d for a, d in przerwy):
            if zamrozenie_od is None or t < zamrozenie_od: n += 10
            pr.append((t, t, n, 0.1, 0))
        t += co
    return pr


K = lambda *a, **k: h.klasyfikuj(*a, **k)['status']  # noqa: E731


def main():
    S12 = '[ZK] s12 12 cykli wlasciciel+dziecko FAIL'
    # 1. PASS (takze z przerwami hosta - nie wplywaja na wynik)
    sprawdz(K(log_scenariuszy(15), 'koniec', 60, [], [], 15, 600) == 'PASS', 'czysty PASS')
    sprawdz(K(log_scenariuszy(15), 'koniec', 160, [(10, 90)], [], 15, 600) == 'PASS', 'PASS mimo przerw hosta')
    # 2. Awaria jadra NIGDY nie jest pauza hosta
    panika = log_scenariuszy(5, koniec=False) + '[PANIC] Wyjatek procesora #GP\n'
    sprawdz(K(panika, 'timeout', 600, [(10, 300)], probki_rosnace(600, przerwy=[(10, 300)]), 15, 600) == 'FAIL-JADRO',
            'panika z przerwami hosta -> FAIL-JADRO')
    sprawdz(K(panika, 'koniec', 60, [], [], 15, 600) == 'FAIL-JADRO', 'panika -> FAIL-JADRO')
    bez_sprzatania = log_scenariuszy(15).replace('[ZK] KONIEC', zdarzenie(7, 9.0, 3000) + '\n[ZK] KONIEC')
    sprawdz(K(bez_sprzatania, 'koniec', 60, [(5, 50)], [], 15, 600) == 'FAIL-JADRO', 'zadanie bez sprzatania -> FAIL-JADRO')
    # 3. Wyjscie/crash QEMU pozostaje porazka (rowniez przy przerwach hosta)
    for kod in ('qemu-exit-0', 'qemu-exit-1', 'qemu-exit--11'):
        sprawdz(K(log_scenariuszy(4, koniec=False), kod, 40, [(5, 30)], probki_rosnace(40), 15, 600) == 'FAIL-QEMU',
                f'{kod} -> FAIL-QEMU')
    # 4. Scenariusz FAIL: pauza hosta tylko z dowodem po obu stronach
    zapasc = log_scenariuszy(11, [S12], przerwa_przy_fail=(62.0, 26))          # jak v2_oddanie/run_01: 62 s HPET, 26 IRQ
    sprawdz(K(zapasc, 'koniec', 120, [(30, 60)], probki_rosnace(120), 15, 600) == 'STALL-HOSTA', 'zapasc IRQ + przerwa hosta')
    sprawdz(K(zapasc, 'koniec', 120, [], probki_rosnace(120), 15, 600) == 'FAIL', 'zapasc IRQ bez przerwy hosta (IF=0 w gosciu)')
    sprawdz(K(zapasc, 'koniec', 120, [(30, 8)], probki_rosnace(120), 15, 600) == 'FAIL', 'przerwa hosta za krotka wzgledem okna')
    zwykly = log_scenariuszy(11, [S12], przerwa_przy_fail=(4.0, 1000))         # 250 Hz: gosc dzialal - prawdziwy FAIL
    sprawdz(K(zwykly, 'koniec', 120, [(30, 60)], probki_rosnace(120), 15, 600) == 'FAIL', 'FAIL przy normalnych IRQ mimo przerw hosta')
    dwa = log_scenariuszy(10, [S12, '[ZK] s3 kill heap FAIL'], przerwa_przy_fail=(62.0, 26))
    sprawdz(K(dwa, 'koniec', 200, [(30, 140)], probki_rosnace(200), 15, 600) == 'STALL-HOSTA', 'oba FAIL w oknach pauzy')
    mieszany = zapasc.replace('[ZK] KONIEC', zdarzenie(4, 200.0, 99000) + '\n[ZK] s9 x FAIL\n' + zdarzenie(4, 200.2, 99050, 'CLEANUP') + '\n[ZK] KONIEC')
    sprawdz(K(mieszany, 'koniec', 300, [(30, 60)], probki_rosnace(300), 15, 600) == 'FAIL', 'jeden FAIL bez dowodu -> FAIL')
    sprawdz(K(log_scenariuszy(5), 'koniec', 30, [], [], 15, 600) == 'FAIL', 'KONIEC z za mala liczba scenariuszy')
    # 5. Timeout: brak postepu gosciu vs pauza hosta
    stoi = log_scenariuszy(6, koniec=False)
    sprawdz(K(stoi, 'timeout', 400, [], probki_rosnace(400, zamrozenie_od=20), 15, 400) == 'FAIL-BRAK-POSTEPU',
            'log stoi, host daje CPU -> FAIL-BRAK-POSTEPU')
    przerwy = [(10, 150), (170, 200)]
    sprawdz(K(stoi, 'timeout', 400, przerwy, probki_rosnace(400, przerwy=przerwy), 15, 400) == 'STALL-HOSTA',
            'przerwy hosta zjadly czas, log rosnie miedzy nimi -> STALL-HOSTA')
    krotsze = [(30, 100), (200, 100)]                                         # 200 s bez przerw, log stoi od t=25
    sprawdz(K(stoi, 'timeout', 400, krotsze, probki_rosnace(400, przerwy=krotsze, zamrozenie_od=25), 15, 400) == 'FAIL-BRAK-POSTEPU',
            'zawieszony gosc NIE jest ukryty przez przerwy hosta')
    sprawdz(K(stoi, 'timeout', 400, [], probki_rosnace(400), 15, 400) == 'FAIL-TIMEOUT', 'wolny postep -> FAIL-TIMEOUT')
    sprawdz(K(stoi, 'timeout', 400, [(0, 390)], [], 15, 400) == 'FAIL-TIMEOUT', 'brak probek -> nie da sie udowodnic pauzy')
    sprawdz(K(stoi, 'nieznany', 10, [], [], 15, 400) == 'FAIL', 'nieoczekiwany powod -> FAIL')
    w = h.klasyfikuj(stoi, 'timeout', 400, przerwy, probki_rosnace(400, przerwy=przerwy), 15, 400)
    sprawdz(not w['wiazacy'] and w['powody'], 'STALL-HOSTA niewiazacy i z uzasadnieniem')
    sprawdz(all(s.startswith('FAIL') for s in h.STATUSY_WIAZACE_FAIL), 'lista statusow wiazacych')
    # 6. Pomocnicze
    sprawdz(abs(h._pokrycie(0, 100, [(50, 100), (-10, 20)]) - 60) < 1e-9, 'pokrycie przedzialu przerwami hosta')
    sprawdz(h.okna_bez_postepu([(0, 0, 1, 0, 0), (5, 0, 1, 0, 0), (10, 0, 2, 0, 0)], 20) == [(0, 10), (10, 20)], 'okna bez postepu')

    # 7. Prawdziwe logi (tylko odczyt; pomijane, gdy brak plikow)
    serie = ROOT / 'docs/zakonczenie-procesow-2026-11-16/serie'        # wersjonowana kopia dowodow
    if not serie.exists(): serie = ROOT / '.zakonczenie-test/serie'
    v2 = serie / 'v2_oddanie/run_01.log'                    # FAILED_run_01.log to bajtowy duplikat
    if v2.exists():
        t = v2.read_text(errors='replace')
        sprawdz(K(t, 'koniec', 112, [(40, 60)], probki_rosnace(112), 15, 600) == 'STALL-HOSTA', 'v2_oddanie run_01 + przerwa hosta 60 s')
        sprawdz(K(t, 'koniec', 112, [], probki_rosnace(112), 15, 600) == 'FAIL', 'v2_oddanie run_01 bez dowodu z hosta -> FAIL')
    b1 = serie / 'b1_fix/run_01.log'
    if b1.exists():
        t = b1.read_text(errors='replace')
        p = [(9, 59), (70, 5), (75, 88), (164, 22), (192, 25), (218, 69), (288, 108)]   # z b1_fix.out
        sprawdz(K(t, 'timeout', 402, p, probki_rosnace(402, przerwy=p), 15, 400) == 'STALL-HOSTA', 'b1_fix run_01 -> STALL-HOSTA')
        # Granica rozroznialnosci: przy 376 s pauz z 402 s gosc mial ~26 s CPU (< PROG_BRAKU_POSTEPU_S), wiec
        # zawieszenia nie da sie udowodnic - wynik niewiazacy (nigdy PASS), a nie falszywy FAIL.
        sprawdz(K(t, 'timeout', 402, p, probki_rosnace(402, przerwy=p, zamrozenie_od=20), 15, 400) == 'STALL-HOSTA',
                'b1_fix: zawieszenie nieudowadnialne przy ~26 s CPU gosciu -> niewiazacy')
        p3 = p[:3]                                                                       # gosc mial ~250 s CPU
        sprawdz(K(t, 'timeout', 402, p3, probki_rosnace(402, przerwy=p3, zamrozenie_od=20), 15, 400) == 'FAIL-BRAK-POSTEPU',
                'b1_fix z zawieszeniem i wystarczajacym CPU -> FAIL-BRAK-POSTEPU')

    if bledy:
        print(f'FAIL klasyfikacja harnessu: {bledy}/{sprawdzen} bledow')
        return 1
    print(f'PASS klasyfikacja harnessu BWS 97: {sprawdzen} sprawdzen (bez QEMU)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
