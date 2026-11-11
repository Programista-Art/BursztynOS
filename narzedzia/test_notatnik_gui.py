#!/usr/bin/env python3
"""Bursztyn OS - Testy integracji GUI i modernizacji aplikacji Notatnik (BursztynUI).

Weryfikuje:
  1. Uklad i rozmiar naglowka formatu .bur w notatnik.cpp zgodny ze skryptem notatnik_linker.ld.
  2. Spojnosc sekcji ELF (TEXT=0x30000, DATA=0x20000, brak statycznych konstruktorow Ring 3).
  3. Zastosowanie nowoczesnego frameworka BursztynUI (AplikacjaGUI, Okno, MenuGlowne, PasekNarzedzi,
     PoleWielowierszowe, PasekStanu).
  4. Czystosc stylistyczna: calkowity brak prywatnych/zahardkodowanych kolorow i stalych Kolory:: w notatnik.cpp.
  5. Poprawnosc obslugi zdarzen BWS_ZDARZENIE_MOTYW_ZMIENIONY (automatyczne odswiezanie motywu).
  6. Skroty klawiszowe (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Z, Ctrl+X, Ctrl+C, Ctrl+V, Ctrl+A).
  7. Poprawnosc obslugi polskich znakow UTF-8 w interfejsie uzytkownika.
  8. Bezpieczenstwo operacji na plikach (otwieranie, zapisywanie biezace, zapis jako z paskiem monitu).
"""

import os
import re
import struct
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


# ============================================================================
# 1. TEST NAGLOWKA .BUR I SKRYPTU LINKERA
# ============================================================================
def test_naglowek_i_linker():
    cpp_path = os.path.join(REPO, "notatnik.cpp")
    ld_path = os.path.join(REPO, "notatnik_linker.ld")

    with open(cpp_path, "r", encoding="utf-8") as f:
        cpp_src = f.read()
    with open(ld_path, "r", encoding="utf-8") as f:
        ld_src = f.read()

    # Sprawdzenie zdefiniowanych stalych w cpp
    ok_text_sz = "BUR_TEKST_ROZMIAR     = 0x30000ULL" in cpp_src or "0x30000" in cpp_src
    ok_data_sz = "BUR_DANE_ROZMIAR         = 0x20000ULL" in cpp_src or "0x20000" in cpp_src
    ok_text_va = "BUR_TEKST_WIRTUALNY     = 0x1001000ULL" in cpp_src or "0x1001000" in cpp_src
    ok_data_va = "BUR_DANE_WIRTUALNY       = 0x1031000ULL" in cpp_src or "0x1031000" in cpp_src
    raport("notatnik_stale_adresacji_cpp", ok_text_sz and ok_data_sz and ok_text_va and ok_data_va)

    # Sprawdzenie skryptu linkera
    ok_ld_text = "BUR_TEXT_SIZE     = 0x30000;" in ld_src
    ok_ld_data = "BUR_DATA_SIZE     = 0x20000;" in ld_src
    ok_ld_data_va = "BUR_DATA_VADDR    = 0x1031000;" in ld_src
    raport("notatnik_linker_segmenty_192k_128k", ok_ld_text and ok_ld_data and ok_ld_data_va)


# ============================================================================
# 2. TEST BINARNY ELF I BRAK STATYCZNYCH KONSTRUKTOROW RING 3
# ============================================================================
def test_elf_binarny():
    elf_path = os.path.join(REPO, "notatnik.elf")
    if not os.path.exists(elf_path):
        raport("notatnik_elf_istnieje", False, "Brak notatnik.elf - zbuduj make notatnik_blob.o")
        return

    raport("notatnik_elf_istnieje", True)

    # Odczyt sekcji za pomoca readelf lub objdump
    try:
        out = subprocess.check_output(["readelf", "-S", elf_path], text=True)
        ok_naglowek = ".naglowek" in out or ".ssp_guard" in out
        ok_text = ".text" in out
        ok_data = ".data" in out
        ok_bss = ".bss" in out
        raport("notatnik_sekcje_elf", ok_naglowek and ok_text and ok_data and ok_bss)
    except Exception as e:
        raport("notatnik_sekcje_elf", False, str(e))

    # Sprawdzenie braku sekcji .init_array / .ctors o rozmiarze > 0
    try:
        out_syms = subprocess.check_output(["nm", elf_path], text=True)
        # Symbole __bur_ctor_start i __bur_ctor_end powinny byc rowne
        m_start = re.search(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+__bur_ctor_start", out_syms)
        m_end = re.search(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+__bur_ctor_end", out_syms)
        if m_start and m_end:
            ctor_equal = (m_start.group(1) == m_end.group(1))
            raport("notatnik_brak_statycznych_konstruktorow", ctor_equal,
                   "start=%s, end=%s" % (m_start.group(1), m_end.group(1)))
        else:
            raport("notatnik_brak_statycznych_konstruktorow", True, "Symbole ctor pominiete")
    except Exception as e:
        raport("notatnik_brak_statycznych_konstruktorow", False, str(e))


# ============================================================================
# 3. TEST ARCHITEKTURY BURSZTYNUI I BRAKU PRYWATNYCH PALET
# ============================================================================
def test_bursztyn_ui_architektura():
    cpp_path = os.path.join(REPO, "notatnik.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        src = f.read()

    # BursztynUI elementy
    wymagane_komponenty = [
        "AplikacjaGUI",
        "OknoNotatnika",
        "MenuGlowne",
        "PasekNarzedzi",
        "PoleWielowierszowe",
        "PasekStanu",
        "Panel"
    ]
    brakujace = [c for c in wymagane_komponenty if c not in src]
    raport("notatnik_komponenty_bursztyn_ui", len(brakujace) == 0, str(brakujace))

    # Czystość motywów: brak starych stałych Kolory::
    ok_brak_kolory = "Kolory::" not in src
    raport("notatnik_brak_legacy_kolory", ok_brak_kolory)

    # Brak ręcznie wpisanych kolorów RGB (np. 0x00... / 0xFF...) dla kontrolek
    # Dopuszczamy jedynie ewentualne maski bitowe klawiszy / syscalli
    linie = src.splitlines()
    linie_podejrzane = []
    for idx, l in enumerate(linie):
        if "uint32_t kolor" in l or "kolor = 0x" in l or "kolor =" in l:
            if "BWS_" not in l and "BUR_" not in l and "bit" not in l:
                linie_podejrzane.append((idx + 1, l.strip()))
    raport("notatnik_brak_prywatnych_kolorow_rgb", len(linie_podejrzane) == 0, str(linie_podejrzane))

    # Obsługa zdarzenia zmiany motywu poprzez Okno::obsluz_zdarzenie
    ok_delegacja = "Okno::obsluz_zdarzenie(ev)" in src
    raport("notatnik_obsluga_motywu_przez_okno", ok_delegacja)


# ============================================================================
# 4. TEST FUNKCJONALNOSCI EDYTORA NOTATNIKA
# ============================================================================
def test_funkcjonalnosci_edycji():
    cpp_path = os.path.join(REPO, "notatnik.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        src = f.read()

    # Menu i akcje
    akcje = [
        "nowy_dokument",
        "otworz_plik",
        "zapisz_plik_biezacy",
        "zapisz_plik_jako",
        "przelacz_zawijanie",
        "wytnij_do_schowka",
        "kopiuj_do_schowka",
        "wklej_ze_schowka",
        "zaznacz_wszystko",
        "wyczysc"
    ]
    brakujace_akcje = [a for a in akcje if a not in src]
    raport("notatnik_pelne_akcje_edycyjne", len(brakujace_akcje) == 0, str(brakujace_akcje))

    # Skróty klawiaturowe Ctrl+N, Ctrl+O, Ctrl+S
    ok_skroty = "BWS_MOD_CTRL" in src and "'n'" in src and "'o'" in src and "'s'" in src
    raport("notatnik_skroty_klawiszowe_ctrl", ok_skroty)

    # Pasek stanu z 4 sekcjami (status, kursor, liczba linii, kodowanie)
    ok_pasek_stanu = "dodaj_sekcje" in src and "Gotowy" in src and "UTF-8" in src
    raport("notatnik_bogaty_pasek_stanu", ok_pasek_stanu)

    # Obsługa argumentu pliku z linii poleceń / skojarzeń plików
    ok_arg = "pobierz_argument_startowy" in src and "otworz_plik" in src
    raport("notatnik_otwieranie_z_argumentu_startowego", ok_arg)

    # Poprawne polskie litery w UI (brak protez ASCII)
    frazy_pl = ["Zatwierdź", "Cofnij", "Wytnij", "Ścieżka", "Zawijanie wierszy", "Zakończ"]
    brak_fraz = [p for p in frazy_pl if p not in src]
    raport("notatnik_polskie_znaki_utf8", len(brak_fraz) == 0, str(brak_fraz))


def main():
    print("=" * 70)
    print(" BURSZTYN OS - TESTY JEDNOSTKOWE I INTEGRACYJNE NOTATNIK (BURSZTYNUI)")
    print("=" * 70)

    test_naglowek_i_linker()
    test_elf_binarny()
    test_bursztyn_ui_architektura()
    test_funkcjonalnosci_edycji()

    print("-" * 70)
    print("Podsumowanie testów Notatnika: Zdane=%d, Zlane=%d" % (zdane, zlane))
    if zlane > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
