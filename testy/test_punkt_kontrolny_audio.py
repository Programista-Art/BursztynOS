#!/usr/bin/env python3
"""Regresja: dlugie, niewywlaszczalne petle jadra wykonuja prace odroczona audio (punkt_kontrolny_audio.h).

Bez punktu kontrolnego mikser HDA/AC97 pomija okresy, gdy inna aplikacja wykonuje dlugi syscall (duza alokacja
Przegladarki Obrazow, odczyt pliku z dysku przez Eksplorator) - slyszalne przerwy w muzyce Grajka.

Pilnuje tez kontraktu bezpieczenstwa z audytu (2026-11-26):
 - punkt w KAZDEJ iteracji (bez licznika "co N", ktory zerowal sie dla kazdego zadania dysku),
 - walidacja kontekstu w kolejnosci: praca -> IF=1 -> brak zagniezdzenia -> wolna blokada miksera,
 - VirtIO-BLK nie liczy czasu pracy audio do limitu urzadzenia (blk_punkt_audio / przedluz_termin),
 - workery audio nie wolaja alokatora, sterty procesu, dysku, VFS ani schedulera (brak sciezki rekursji),
 - atrapy testowe nie moga trafic do jadra.
"""
import os
import re
import sys

bledy = []


def wymagaj(warunek, opis):
    if not warunek:
        bledy.append(opis)


def tresc(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def cialo_funkcji(zrodlo, naglowek):
    """Cialo funkcji od naglowka (regex) do domykajacego nawiasu klamrowego."""
    m = re.search(naglowek, zrodlo)
    if not m:
        return None
    i = zrodlo.index("{", m.end() - 1)
    gl = 0
    for j in range(i, len(zrodlo)):
        if zrodlo[j] == "{":
            gl += 1
        elif zrodlo[j] == "}":
            gl -= 1
            if gl == 0:
                return zrodlo[i:j + 1]
    return None


PUNKT = "PunktKontrolnyAudio::punkt()"

naglowek = tresc("jadro/dzwiek/punkt_kontrolny_audio.h")
obsluz = cialo_funkcji(naglowek, r"Wynik obsluz\(S& s, Statystyki& st, bool& w_toku\)\s*\{")
wymagaj(obsluz, "punkt_kontrolny_audio.h: brak obsluz()")
if obsluz:
    kolejnosc = [obsluz.find(t) for t in ("s.praca_oczekuje()", "s.przerwania_wlaczone()",
                                          "__atomic_exchange_n(&w_toku", "s.blokada_audio_zajeta()", "s.wykonaj()")]
    wymagaj(all(k >= 0 for k in kolejnosc) and kolejnosc == sorted(kolejnosc),
            "obsluz(): walidacja kontekstu poza kolejnoscia praca -> IF -> zagniezdzenie -> blokada -> praca")
    wymagaj(obsluz.count("__atomic_store_n(&w_toku, false") == 2,
            "obsluz(): flaga w_toku musi byc zwalniana po pracy i po odroczeniu przez blokade")
wymagaj("hda_blokada_aktywna()" in naglowek and "(1ULL << 9)" in naglowek,
        "punkt_kontrolny_audio.h: srodowisko jadra bez sprawdzenia IF lub blokady HDA")
wymagaj(not re.search(r"\bstruct Licznik\b|CO_ITERACJI|CO_STRON", naglowek), "punkt_kontrolny_audio.h: wrocil licznik 'co N'")

sys_ = tresc("jadro/bws/syscalls.cpp")
cialo = cialo_funkcji(sys_, r"uint64_t bws_alokuj_sterte\([^)]*\)\s*\{")
wymagaj(cialo, "syscalls.cpp: brak bws_alokuj_sterte")
if cialo:
    petla = cialo[cialo.find("for (uint64_t i = 0;"):]
    wymagaj(PUNKT in petla[:400], "bws_alokuj_sterte: petla mapowania stron bez punktu kontrolnego audio")
    wymagaj(petla.find(PUNKT) < petla.find("ZaalokujRamke()"),
            "bws_alokuj_sterte: punkt musi stac przed ZaalokujRamke (poza sekcja PMM)")

blk = tresc("sterowniki/virtio/virtio_blk.cpp")
petle = re.findall(r"while \((?:spin_guard > 0|!completed && poll_guard > 0)\) \{.*?asm volatile\(\"pause\"", blk, re.S)
wymagaj(len(petle) >= 4, "virtio_blk.cpp: oczekiwane 4 petle oczekiwania (reset, MSI-X x2, INTx)")
for blok in petle:
    wymagaj("blk_punkt_audio(" in blok or PUNKT in blok, "virtio_blk.cpp: petla oczekiwania bez punktu kontrolnego audio")
wymagaj(blk.count("blk_punkt_audio(now_ns, &deadline_ns, &przedluzono_ns") == 3,
        "virtio_blk.cpp: petle z terminem czasowym musza oddawac czas pracy audio (blk_punkt_audio)")
wymagaj("Licznik" not in blk, "virtio_blk.cpp: wrocil licznik 'co N'")

ahci = tresc("sterowniki/magazyn/ahci.cpp")
cialo = cialo_funkcji(ahci, r"BlockError uruchom_i_czekaj_na_komende\([^)]*\)\s*\{")
wymagaj(cialo and PUNKT in cialo and "Licznik" not in cialo,
        "ahci.cpp: uruchom_i_czekaj_na_komende bez punktu kontrolnego audio (w kazdej iteracji)")
wymagaj(re.search(r"#ifdef BURSZTYN_AHCI_HOST_TEST\s*\n#include \"testy/ahci_rejestr_w1c_host.h\"", ahci),
        "ahci.cpp: rejestr testowy W1C musi byc wlaczany tylko pod BURSZTYN_AHCI_HOST_TEST")

gpu = tresc("sterowniki/virtio/virtio_gpu.cpp")
wymagaj("hda_wykonaj_zaplanowana_prace_odroczona();" in gpu, "virtio_gpu.cpp: zniknal istniejacy punkt kontrolny")

# Workery audio: brak wywolan uslug, ktore zawieraja punkty kontrolne lub blokuja (sciezka rekursji / cykl blokad).
ZAKAZANE = ("ZaalokujRamke", "ZwolnijRamke", "bws_alokuj_sterte", "virtio_blk", "ahci", "psf_", "vfs_",
            "blokowy_", "scheduler_przelacz", "scheduler_yield", "uspij", "skladacz_obrazu")
hda = tresc("sterowniki/dzwiek/hda.cpp")
ac97 = tresc("sterowniki/dzwiek/ac97.cpp")
for nazwa, zrodlo, wzor in (("hda_obsluz_okresowo", hda, r"\nvoid hda_obsluz_okresowo\(\)\s*\{"),
                            ("pcm_refill", hda, r"HdaPcm::Fault pcm_refill\(\)\s*\{"),
                            ("pcm_recover", hda, r"\nvoid pcm_recover\(uint64_t now, HdaPcm::Fault fault\)\s*\{"),
                            ("ac97_obsluz_okresowo", ac97, r"\nvoid ac97_obsluz_okresowo\(\)\s*\{")):
    c = cialo_funkcji(zrodlo, wzor)
    wymagaj(c, f"{nazwa}: nie znaleziono ciala")
    if c:
        for z in ZAKAZANE:
            wymagaj(z not in c, f"{nazwa}: wywolanie '{z}' w pracy audio (mozliwa rekursja do petli z punktem)")
wymagaj("(1ULL << 9)" in (cialo_funkcji(hda, r"\nvoid hda_obsluz_okresowo\(\)\s*\{") or ""),
        "hda_obsluz_okresowo: brak wlasnej odmowy przy IF=0")

# Atrapy testowe nie moga byc wlaczane przez kod jadra.
for katalog in ("jadro", "sterowniki", "bursztyn_ui", "programy"):
    for root, _, pliki in os.walk(katalog):
        for nazwa in pliki:
            if not nazwa.endswith((".cpp", ".h", ".inc")):
                continue
            sciezka = os.path.join(root, nazwa)
            t = tresc(sciezka)
            wymagaj("praca_audio_odroczona_host.h" not in t, f"{sciezka}: wlacza atrape pracy audio z testow")
            if "ahci_rejestr_w1c_host.h" in t:
                wymagaj(sciezka == os.path.join("sterowniki", "magazyn", "ahci.cpp"),
                        f"{sciezka}: wlacza testowy rejestr AHCI")
atrapa = tresc("testy/praca_audio_odroczona_host.h")
wymagaj("#error" in atrapa, "praca_audio_odroczona_host.h: brak blokady #error poza buildem hosta")

if bledy:
    for b in bledy:
        print("FAIL:", b)
    sys.exit(1)
print("punkty kontrolne audio w petlach jadra: OK (sterta, VirtIO-BLK, AHCI, VirtIO-GPU, kontrakt workerow, atrapy)")
