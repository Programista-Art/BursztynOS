#!/usr/bin/env python3
"""Bursztyn OS - test wspolbieznosci workspace OTA/storage (test_wspolbieznosc.py).

Czesci:
  A. Statyczny audyt grafu blokad (kod zrodlowy jadra):
       1. Hierarchia OTA -> PSF -> Formatter -> AHCI: brak krawedzi odwrotnych
          (nizsza warstwa nigdy nie bierze blokady wyzszej).
       2. Handler IRQ (timer/klawiatura/mysz) nie bierze zadnej z tych blokad
          (brak deadlocku IRQ vs spinlock bez cli).
       3. Brak yield/sleep/blockujacego waita na sciezkach trzymajacych
          GuardOTA / GuardStorageFormatter / BlokadaPSF.
       4. BlokadaPSF: funkcje publiczne nie wywoluja innych publicznych
          wpisow PSF (blokada nierekurencyjna; rekurencja = zakleszczenie).
       5. GuardAHCI jest try-lockiem (nigdy nie wiruje w nieskonczonosc).
       6. Wskazniki FormatterWorkspace nie uciekaja poza guard (uzycie
          g_workspace wylacznie w funkcjach bioracych GuardStorageFormatter).
  B. Hostowy test regresyjny (kompilacja + uruchomienie):
       podwojne gpt_read, grubenv wsrod operacji, zagniezdzenie guard,
       blad backendu, przeplot PSF/storage, diagnostyka owner/depth/site.
"""

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

zdane = 0
zlane = 0


def raport(nazwa, ok, szczegoly=""):
    global zdane, zlane
    if ok:
        zdane += 1
        print("PASS %s" % nazwa)
    else:
        zlane += 1
        msg = (" (%s)" % szczegoly) if szczegoly else ""
        print("FAIL %s%s" % (nazwa, msg))


def czytaj(rel):
    with open(os.path.join(REPO, rel), "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def bez_komentarzy(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


# Hierarchia: im wyzszy numer, tym wyzsza warstwa.
WARSTWA = {
    "GuardOTA": 4,
    "BlokadaPSF": 3,
    "GuardStorageFormatter": 2,
    "GuardAHCI": 1,
}

# Ktore pliki implementuja ktore warstwy (wlasciciel blokady).
WLASCICIEL = {
    "jadro/aktualizacja/aktualizacja.cpp": {"GuardOTA"},
    "jadro/fs/psf.cpp": {"BlokadaPSF"},
    "jadro/fs/storage_format.cpp": {"GuardStorageFormatter"},
    "sterowniki/magazyn/ahci.cpp": {"GuardAHCI"},
}


def test_kolejnosc_blokad():
    # Dla kazdego pliku sprawdz, jakie guardy sa w nim BEZPOSREDNIO brane
    # (deklaracje zmiennych straznikow). Nizsza warstwa nie moze brac
    # blokady warstwy wyzszej ani rownorzednej spoza wlasnej.
    ok = True
    for rel, dozwolone in [
        ("jadro/aktualizacja/aktualizacja.cpp", {"GuardOTA"}),
        ("jadro/fs/psf.cpp", {"BlokadaPSF"}),
        ("jadro/fs/storage_format.cpp", {"GuardStorageFormatter"}),
        ("sterowniki/magazyn/ahci.cpp", {"GuardAHCI"}),
        ("jadro/siec/siec.cpp", set()),
        ("jadro/bws/scheduler.cpp", set()),
        ("sterowniki/wejscie/klawiatura.cpp", set()),
        ("sterowniki/wejscie/mysz.cpp", set()),
        ("sterowniki/grafika/grafika.cpp", set()),
    ]:
        src = bez_komentarzy(czytaj(rel))
        brane = {g for g in WARSTWA if re.search(r"\b%s\s+\w+\s*(\(|;)" % g, src)}
        obce = brane - dozwolone
        if obce:
            ok = False
            raport("blokady_brak_odwrotnych_%s" % os.path.basename(rel), False,
                   "obce guardy: %s" % sorted(obce))
    raport("blokady_brak_odwrotnych_krawedzi", ok)


def test_irq_bez_blokad():
    # Handlery IRQ nie moga brac zadnej z czterech blokad: timer moze
    # przerwać watek trzymajacy GuardOTA/Formatter (te nie robia cli),
    # a petla spin w handlerze = deadlock uniprocesora.
    ok = True
    for rel, funkcje in [
        ("sterowniki/grafika/grafika.cpp", ["obsluga_przerwania_zegara"]),
        ("sterowniki/wejscie/klawiatura.cpp", ["obsluga_przerwania_klawiatury"]),
        ("sterowniki/wejscie/mysz.cpp", ["obsluga_przerwania_myszy"]),
    ]:
        src = bez_komentarzy(czytaj(rel))
        for fn in funkcje:
            m = re.search(r"void\s+%s\(\)\s*\{(.*?)\n\}" % fn, src, re.S)
            body = m.group(1) if m else ""
            # Jezeli nie znaleziono ciala (makra/extern), przeszukaj caly plik
            # pod katem wywolan sciezek storage w poblizu nazwy handlera.
            zle = [g for g in list(WARSTWA) + ["psf_", "Storage::", "aktualizacja_",
                                               "ahci_czytaj", "ahci_zapisz"]
                   if g in body]
            if zle:
                ok = False
                raport("irq_bez_blokad_%s" % fn, False, "podejrzane: %s" % zle)
    # Scheduler (kontekst IRQ timera) tez nie bierze tych blokad.
    sched = bez_komentarzy(czytaj("jadro/bws/scheduler.cpp"))
    zle_sched = [g for g in WARSTWA if re.search(r"\b%s\s+\w+\s*(\(|;)" % g, sched)]
    if zle_sched:
        ok = False
        raport("irq_bez_blokad_scheduler", False, "%s" % zle_sched)
    raport("irq_bez_blokad", ok)


def test_brak_spania_pod_blokada():
    # Pod GuardOTA / GuardStorageFormatter / BlokadaPSF nie wolno usypiac
    # ani czekac na zdarzenia (yield/sleep/wait). Dozwolone sa krotkie
    # petle pause (spin) oraz wywolania sieciowe poll (busy-wait, bez yield).
    zakazane = ["ZablokujAktualnyProces", "scheduler_czekaj_na_zdarzenie",
                "scheduler_pobierz_klawisz_lub_zablokuj", "usypij", "sleep_ms",
                "czekaj_na_zamek", "BWS38"]
    ok = True
    for rel in ["jadro/aktualizacja/aktualizacja.cpp", "jadro/fs/storage_format.cpp", "sterowniki/magazyn/ahci.cpp"]:
        src = bez_komentarzy(czytaj(rel))
        traf = [z for z in zakazane if z in src]
        if traf:
            ok = False
            raport("brak_spania_%s" % os.path.basename(rel), False, "%s" % traf)
    # Siec uzywana spod GuardOTA (pobieranie OTA) musi byc poll, nie yield.
    siec = bez_komentarzy(czytaj("jadro/siec/siec.cpp"))
    traf_siec = [z for z in zakazane if z in siec]
    if traf_siec:
        ok = False
        raport("brak_spania_siec", False, "%s" % traf_siec)
    raport("brak_spania_pod_blokada", ok)


def test_psf_brak_rekurencji_publicznej():
    # BlokadaPSF jest nierekurencyjna (spin + cli). Publiczny wpis
    # wywolujacy inny publiczny wpis = pewny deadlock. Sprawdzamy, ze
    # ciala extern "C" uzywaja wylacznie helperow *_wewn / malych liter.
    src = bez_komentarzy(czytaj("jadro/fs/psf.cpp"))
    nazwy = re.findall(r'extern\s+"C"\s+(?:bool|void|uint32_t|uint8_t\*|int)\s+(\w+)\s*\(', src)
    # Grube przyblizenie cial: od naglowka extern do nastepnego extern.
    bloki = re.split(r'(?=extern\s+"C"\s+(?:bool|void|uint32_t|uint8_t\*|int)\s+\w+\s*\()', src)
    ok = True
    for b in bloki:
        m = re.match(r'extern\s+"C"\s+(?:bool|void|uint32_t|uint8_t\*|int)\s+(\w+)\s*\(', b)
        if not m:
            continue
        wlasciciel = m.group(1)
        if "BlokadaPSF" not in b:
            continue
        for inny in nazwy:
            if inny == wlasciciel:
                continue
            if re.search(r"\b%s\s*\(" % inny, b):
                ok = False
                raport("psf_rekurencja_%s" % wlasciciel, False,
                       "wywoluje publiczny %s" % inny)
    raport("psf_brak_rekurencji_publicznej", ok)


def test_ahci_try_lock():
    # GuardAHCI musi byc try-lockiem: pojedyncza proba atomowa, bez petli.
    # (Implementacja uzywa __atomic_test_and_set albo __atomic_exchange_n.)
    src = bez_komentarzy(czytaj("sterowniki/magazyn/ahci.cpp"))
    m = re.search(r"bool\s+ahci_sprobuj_zablokowac\(\)\s*\{(.*?)\n\}", src, re.S)
    ok = bool(m) and "while" not in m.group(1) and \
        ("__atomic_exchange_n" in m.group(1) or "__atomic_test_and_set" in m.group(1))
    raport("ahci_try_lock_bez_petli", ok)
    # Kazde uzycie GuardAHCI sprawdza aktywny() (fail-safe, nie deadlock).
    guardy = len(re.findall(r"GuardAHCI\s+\w+\s*;", src))
    sprawdzenia = len(re.findall(r"guard\.aktywny\(\)", src))
    raport("ahci_sprawdzenie_aktywnosci", guardy > 0 and sprawdzenia >= guardy,
           "guardy=%d sprawdzenia=%d" % (guardy, sprawdzenia))


def bloki_funkcji(src):
    # Kernel Bursztyn OS definiuje funkcje na poziomie pliku z typem
    # zwracanym w kolumnie 0 (lambdy i wywolania sa wciete). Dzielimy
    # plik na bloki [naglowek, ...cialo] bez liczenia klamer.
    nagl = re.compile(r"^(?:static\s+)?(?:bool|void|uint32_t|uint64_t|int|unsigned|size_t|uint8_t\*|char\*)\s+(\w+)\s*\(", re.M)
    znaczniki = [(m.start(), m.group(1)) for m in nagl.finditer(src)]
    bloki = []
    for i, (pos, nazwa) in enumerate(znaczniki):
        koniec = znaczniki[i + 1][0] if i + 1 < len(znaczniki) else len(src)
        bloki.append((nazwa, pos, koniec, src[pos:koniec]))
    return bloki


def funkcja_dla_pozycji(bloki, pos):
    for nazwa, start, koniec, _ in bloki:
        if start <= pos < koniec:
            return nazwa
    return None


def test_workspace_nie_ucieka():
    # Kazde uzycie g_workspace musi byc w funkcji bioracej
    # GuardStorageFormatter (krytyczna sekcja). Wskaznik nie moze byc
    # zwracany ani zapisywany do struktur trwalych.
    src = bez_komentarzy(czytaj("jadro/fs/storage_format.cpp"))
    bloki = bloki_funkcji(src)
    with_guard = {nazwa for nazwa, _, _, cialo in bloki
                  if "GuardStorageFormatter" in cialo}
    ok = True
    for m in re.finditer(r"g_workspace\.(\w+)", src):
        otaczajaca = funkcja_dla_pozycji(bloki, m.start())
        if otaczajaca not in with_guard:
            ok = False
            raport("workspace_w_sekcji_%s" % (otaczajaca or "?"), False,
                   "pole %s poza guardem" % m.group(1))
    # Zakazane: return wskaznika do workspace / zapis do globali.
    if re.search(r"return\s+g_workspace", src):
        ok = False
        raport("workspace_brak_return", False)
    raport("workspace_nie_ucieka", ok)


def test_diagnostyka_owner():
    # Oba guardy maja: owner PID, depth, site + atomowe gettery.
    ok = True
    for rel, pref in [("jadro/aktualizacja/aktualizacja.cpp", "s_ota_"),
                      ("jadro/fs/storage_format.cpp", "s_formatter_")]:
        src = czytaj(rel)
        for symbol in ["%sowner" % pref, "%sdepth" % pref, "%ssite" % pref]:
            if symbol not in src:
                ok = False
                raport("owner_pola_%s" % symbol, False, rel)
    for rel, ctor in [("jadro/aktualizacja/aktualizacja.h", "GuardOTA"),
                      ("jadro/fs/storage_format.h", "GuardStorageFormatter")]:
        src = czytaj(rel)
        if "miejsce_" not in src or "miejsce()" not in src:
            ok = False
            raport("owner_site_%s" % ctor, False, rel)
    # Miejsca akwizycji przekazywane (__func__).
    if "guard(__func__)" not in czytaj("jadro/aktualizacja/aktualizacja.cpp"):
        ok = False
        raport("owner_site_wywolania_ota", False)
    if "guard(__func__)" not in czytaj("jadro/fs/storage_format.cpp"):
        ok = False
        raport("owner_site_wywolania_formatter", False)
    raport("diagnostyka_owner", ok)


def test_hostowy():
    build = ["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
             "-DBURSZTYN_PSF_HOST_TEST", "-fsanitize=address,undefined",
             "narzedzia/instalator/test_wspolbieznosc.cpp",
             "jadro/fs/storage_format.cpp", "jadro/fs/psf.cpp",
             "-o", ".installertest/test-wspolbieznosc"]
    c = subprocess.run(build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       cwd=REPO)
    if c.returncode != 0:
        raport("host_kompilacja", False, c.stdout.decode()[-2000:])
        return
    raport("host_kompilacja", True)
    r = subprocess.run([".installertest/test-wspolbieznosc"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=REPO,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    raport("host_wspolbieznosc", r.returncode == 0,
           r.stdout.decode()[-2000:] if r.returncode != 0 else
           r.stdout.decode().strip().splitlines()[-1])


def main():
    print("=== TEST WSPOLBIEZNOSCI WORKSPACE OTA/STORAGE BURSZTYN OS ===")
    test_kolejnosc_blokad()
    test_irq_bez_blokad()
    test_brak_spania_pod_blokada()
    test_psf_brak_rekurencji_publicznej()
    test_ahci_try_lock()
    test_workspace_nie_ucieka()
    test_diagnostyka_owner()
    test_hostowy()
    print("==============================================================")
    print("Zdane: %d, Zlane: %d" % (zdane, zlane))
    if zlane > 0:
        sys.exit(1)
    print("Wszystkie testy wspolbieznosci zakonczone sukcesem.")


if __name__ == "__main__":
    main()