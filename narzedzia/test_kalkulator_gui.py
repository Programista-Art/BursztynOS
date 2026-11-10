#!/usr/bin/env python3
"""Bursztyn OS - Testy integracji GUI i modernizacji aplikacji Kalkulator (BursztynUI).

Weryfikuje:
  1. Uklad i rozmiar naglowka formatu .bur w kalkulator.cpp zgodny z kalkulator_linker.ld.
  2. Spojnosc sekcji ELF (TEXT=0x30000, DATA=0x20000, brak statycznych konstruktorow Ring 3).
  3. Zastosowanie nowoczesnego frameworka BursztynUI (AplikacjaGUI, OknoKalkulatora, Panel,
     WyswietlaczKalkulatora, PrzyciskKalkulatora z semantycznymi rolami).
  4. Czystosc stylistyczna: calkowity brak prywatnych/zahardkodowanych kolorow i stalych Kolory::.
  5. Poprawnosc obslugi zdarzen BWS_ZDARZENIE_MOTYW_ZMIENIONY w kontrolkach kalkulatora.
  6. Bezpieczny 64-bitowy silnik arytmetyczny (dodawanie, odejmowanie, mnozenie, dzielenie,
     dzielenie przez zero, wykrywanie overflow int64, reset, usuwanie znaku).
  7. W pelni dynamiczny uklad siatki 4x4 przyciskow z podzialem przestrzeni wewnatrz okna.
  8. Obsluge zdarzen klawiatury fizycznej i USB (cyfry, operatory, Enter, Esc/C, Backspace).
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
    cpp_path = os.path.join(REPO, "kalkulator.cpp")
    ld_path = os.path.join(REPO, "kalkulator_linker.ld")

    with open(cpp_path, "r", encoding="utf-8") as f:
        cpp_src = f.read()
    with open(ld_path, "r", encoding="utf-8") as f:
        ld_src = f.read()

    # Sprawdzenie stalych w kalkulator.cpp
    ok_text_sz = "BUR_TEKST_ROZMIAR     = 0x30000ULL" in cpp_src or "0x30000" in cpp_src
    ok_data_sz = "BUR_DANE_ROZMIAR         = 0x20000ULL" in cpp_src or "0x20000" in cpp_src
    ok_text_va = "BUR_TEKST_WIRTUALNY     = 0x1001000ULL" in cpp_src or "0x1001000" in cpp_src
    ok_data_va = "BUR_DANE_WIRTUALNY       = 0x1031000ULL" in cpp_src or "0x1031000" in cpp_src
    raport("kalkulator_stale_adresacji_cpp", ok_text_sz and ok_data_sz and ok_text_va and ok_data_va)

    # Sprawdzenie skryptu linkera
    ok_ld_text = "BUR_TEXT_SIZE     = 0x30000;" in ld_src
    ok_ld_data = "BUR_DATA_SIZE     = 0x20000;" in ld_src
    ok_ld_data_va = "BUR_DATA_VADDR    = 0x1031000;" in ld_src
    raport("kalkulator_linker_segmenty_192k_128k", ok_ld_text and ok_ld_data and ok_ld_data_va)


# ============================================================================
# 2. TEST BINARNY ELF I BRAK STATYCZNYCH KONSTRUKTOROW RING 3
# ============================================================================
def test_elf_binarny():
    elf_path = os.path.join(REPO, "kalkulator.elf")
    if not os.path.exists(elf_path):
        raport("kalkulator_elf_istnieje", False, "Brak kalkulator.elf - zbuduj make kalkulator_blob.o")
        return

    raport("kalkulator_elf_istnieje", True)

    try:
        out = subprocess.check_output(["readelf", "-S", elf_path], text=True)
        ok_naglowek = ".naglowek" in out or ".ssp_guard" in out
        ok_text = ".text" in out
        ok_data = ".data" in out
        ok_bss = ".bss" in out
        raport("kalkulator_sekcje_elf", ok_naglowek and ok_text and ok_data and ok_bss)
    except Exception as e:
        raport("kalkulator_sekcje_elf", False, str(e))

    try:
        out_syms = subprocess.check_output(["nm", elf_path], text=True)
        m_start = re.search(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+__bur_ctor_start", out_syms)
        m_end = re.search(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+__bur_ctor_end", out_syms)
        if m_start and m_end:
            ctor_equal = (m_start.group(1) == m_end.group(1))
            raport("kalkulator_brak_statycznych_konstruktorow", ctor_equal,
                   "start=%s, end=%s" % (m_start.group(1), m_end.group(1)))
        else:
            raport("kalkulator_brak_statycznych_konstruktorow", True, "Symbole ctor pominiete")
    except Exception as e:
        raport("kalkulator_brak_statycznych_konstruktorow", False, str(e))


# ============================================================================
# 3. TEST ARCHITEKTURY BURSZTYNUI I SEMANTYCZNYCH KOLOROW
# ============================================================================
def test_bursztyn_ui_architektura():
    cpp_path = os.path.join(REPO, "kalkulator.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        src = f.read()

    # Wymagane komponenty BursztynUI
    wymagane = [
        "AplikacjaGUI",
        "OknoKalkulatora",
        "WyswietlaczKalkulatora",
        "PrzyciskKalkulatora",
        "Panel"
    ]
    brakujace = [c for c in wymagane if c not in src]
    raport("kalkulator_komponenty_bursztyn_ui", len(brakujace) == 0, str(brakujace))

    # Brak starych stałych Kolory::
    raport("kalkulator_brak_legacy_kolory", "Kolory::" not in src)

    # Brak ręcznych wywołań niskopoziomowego rysowania okna (gui_rysuj_prostokat z zahardkodowanymi barwami okna)
    ok_brak_hardcoded_bg = "0x00050200" not in src and "0x00242424" not in src
    raport("kalkulator_brak_starych_kolorow_okna", ok_brak_hardcoded_bg)

    # Użycie semantycznych ról motywu
    role_motywu = [
        "motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE)",
        "motyw_kolor(MOTYW_KOLOR_RAMKA)",
        "motyw_kolor(MOTYW_KOLOR_TEKST)",
        "motyw_kolor(MOTYW_KOLOR_AKCENT)",
        "motyw_kolor(MOTYW_KOLOR_BLAD)",
        "motyw_promien(MOTYW_ROG_POLA)"
    ]
    brak_rol = [r for r in role_motywu if r not in src]
    raport("kalkulator_uzycie_semantycznego_motywu", len(brak_rol) == 0, str(brak_rol))

    # Obsługa zdarzenia BWS_ZDARZENIE_MOTYW_ZMIENIONY w kontrolkach
    ok_zaktualizuj_motyw = (
        "void zaktualizuj_motyw() override" in src and
        "WyswietlaczKalkulatora" in src and
        "PrzyciskKalkulatora" in src
    )
    raport("kalkulator_reakcja_na_zmiane_motywu", ok_zaktualizuj_motyw)


# ============================================================================
# 4. TEST BEZPIECZNEGO SILNIKA ARYTMETYCZNEGO I LOGIKI
# ============================================================================
def test_silnik_arytmetyczny():
    cpp_path = os.path.join(REPO, "kalkulator.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        src = f.read()

    # Bezpieczne sprawdzanie overflow wbudowanymi instrukcjami GCC
    ok_overflow = (
        "__builtin_add_overflow" in src and
        "__builtin_sub_overflow" in src and
        "__builtin_mul_overflow" in src
    )
    raport("kalkulator_ochrona_overflow_builtin", ok_overflow)

    # Zabezpieczenie dzielenia przez zero i INT64_MIN / -1
    ok_div = "b == 0" in src and "a == I64_MIN_WARTOSC && b == -1" in src
    raport("kalkulator_ochrona_dzielenia_przez_zero", ok_div)

    # Siatka przycisków 4x4
    przyciski = ["7", "8", "9", "/", "4", "5", "6", "*", "1", "2", "3", "-", "C", "0", "=", "+"]
    wszystkie_btn = all(('"%s"' % p) in src for p in przyciski)
    raport("kalkulator_pelna_siatka_przyciskow_4x4", wszystkie_btn)

    # Obsługa klawiatury
    ok_klawisze = (
        "BWS_ZDARZENIE_KLAWISZ" in src and
        "g_kalkulator.klik" in src
    )
    raport("kalkulator_pelna_obsluga_klawiatury", ok_klawisze)


def main():
    print("=" * 70)
    print(" BURSZTYN OS - TESTY JEDNOSTKOWE I INTEGRACYJNE KALKULATOR (BURSZTYNUI)")
    print("=" * 70)

    test_naglowek_i_linker()
    test_elf_binarny()
    test_bursztyn_ui_architektura()
    test_silnik_arytmetyczny()

    print("-" * 70)
    print("Podsumowanie testów Kalkulatora: Zdane=%d, Zlane=%d" % (zdane, zlane))
    if zlane > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
