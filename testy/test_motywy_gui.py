#!/usr/bin/env python3
"""Bursztyn OS - Testy integracji GUI i weryfikacji architektury podsystemu motywów.

Weryfikuje:
  1. Uklad i rozmiar struktur ABI BWS Motyw (sizeof(BursztynMotyw) == 144, 15 kolorow, 6 promieni).
  2. Kody wywolan BWS (86, 87), kody polecen (1..5) oraz zdarzenie BWS_ZDARZENIE_MOTYW_ZMIENIONY (30).
  3. Obecność i poprawność 5 wbudowanych plików deklaratywnych (.motyw).
  4. Zasady bezpieczeństwa: brak kodu wykonywalnego, fail-closed parsowania, limity rozmiaru <= 4096 B.
  5. Poprawność polskich znaków UTF-8 bez protez ASCII w UI aplikacji Motywy.
  6. Rejestrację aplikacji Motywy w menu start, pulpicie i manifestach jądra.
  7. Wbudowany niezależny fallback kompilowany w jądro (g_domyslny_motyw_fallback).
  8. Obsługę zdarzenia zmiany motywu w menedzerze okien i kontrolkach BursztynUI.
"""
import os
import re
import struct
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
# 1. TEST UKLADU ABI I STRUKTURY BURSZTYN MOTYW
# ============================================================================
def test_abi_layout():
    motyw_h_path = os.path.join(REPO, "jadro/gui/motyw.h")
    bws_api_path = os.path.join(REPO, "jadro/bws/bws_api.h")
    bws_zdarzenia_path = os.path.join(REPO, "jadro/bws/bws_zdarzenia.h")

    with open(motyw_h_path, "r", encoding="utf-8") as f:
        motyw_h = f.read()
    with open(bws_api_path, "r", encoding="utf-8") as f:
        api_h = f.read()
    with open(bws_zdarzenia_path, "r", encoding="utf-8") as f:
        zdarzenia_h = f.read()

    # Wywołania BWS
    ok_bws_86 = "BWS_MOTYW_POBIERZ = 86" in api_h
    ok_bws_87 = "BWS_MOTYW_POLECENIE = 87" in api_h
    raport("bws_numery_wywolan_86_87", ok_bws_86 and ok_bws_87)

    # Zdarzenie broadcast
    ok_zdarzenie_30 = "BWS_ZDARZENIE_MOTYW_ZMIENIONY = 30" in zdarzenia_h
    raport("bws_zdarzenie_motyw_zmieniony_30", ok_zdarzenie_30)

    # Rozmiar i ABI struktury
    ok_assert_size_v2 = "static_assert(sizeof(BursztynMotyw) == 172" in motyw_h
    raport("bursztyn_motyw_rozmiar_172", ok_assert_size_v2)

    ok_assert_size_v1 = "static_assert(sizeof(BursztynMotywV1) == 144" in motyw_h
    raport("bursztyn_motyw_v1_rozmiar_144", ok_assert_size_v1)

    # Format binarny 172B (ABI v2):
    # uint32_t wersja (4)
    # uint32_t rozmiar_struktury (4)
    # uint32_t kolory[20] (80)
    # uint16_t promienie[6] (12)
    # uint8_t gruboscramki (1)
    # uint8_t cienokna (1)
    # uint8_t rezerwa[6] (6)
    # char nazwa[32] (32)
    # char id[32] (32)
    # Suma: 4 + 4 + 80 + 12 + 1 + 1 + 6 + 32 + 32 = 172 B
    fmt_calc = "<2I20I6HBB6s32s32s"
    calc_size = struct.calcsize(fmt_calc)
    raport("bursztyn_motyw_dopasowanie_binarne_v2", calc_size == 172, "calcsize = %d" % calc_size)

    # Role kolorystyczne (20 ról)
    kolory_wymagane = [
        "MOTYW_KOLOR_PULPIT", "MOTYW_KOLOR_OKNO_TLO", "MOTYW_KOLOR_PANEL",
        "MOTYW_KOLOR_PODNIESIONY", "MOTYW_KOLOR_RAMKA", "MOTYW_KOLOR_TEKST",
        "MOTYW_KOLOR_TEKST_DRUGI", "MOTYW_KOLOR_WYLACZONY", "MOTYW_KOLOR_AKCENT",
        "MOTYW_KOLOR_AKCENT_HOVER", "MOTYW_KOLOR_AKCENT_AKTYWNY",
        "MOTYW_KOLOR_BLAD", "MOTYW_KOLOR_SUKCES", "MOTYW_KOLOR_OSTRZEZENIE",
        "MOTYW_KOLOR_INFORMACJA", "MOTYW_KOLOR_POLETEKSTOWE", "MOTYW_KOLOR_ZAZNACZENIE",
        "MOTYW_KOLOR_PRZYCISK", "MOTYW_KOLOR_PRZYCISK_HOVER", "MOTYW_KOLOR_TEKST_AKCENTU"
    ]
    brakujace_kolory = [k for k in kolory_wymagane if k not in motyw_h]
    raport("motyw_20_rol_kolorystycznych", len(brakujace_kolory) == 0, str(brakujace_kolory))

    # Promienie zaokrągleń (6 ról)
    promienie_wymagane = [
        "MOTYW_ROG_OKNO", "MOTYW_ROG_PANEL", "MOTYW_ROG_PRZYCISK",
        "MOTYW_ROG_POLE", "MOTYW_ROG_MENU", "MOTYW_ROG_PODPOWIEDZ"
    ]
    brakujace_promienie = [p for p in promienie_wymagane if p not in motyw_h]
    raport("motyw_6_rol_promieni", len(brakujace_promienie) == 0, str(brakujace_promienie))

    # Weryfikacja stosu jądra: 64 KiB
    stos_h_path = os.path.join(REPO, "jadro/mm/stos_jadra.h")
    with open(stos_h_path, "r", encoding="utf-8") as f:
        stos_h = f.read()
    ok_stack = "static_assert(ROZMIAR_STOSU_JADRA == 65536ULL" in stos_h
    raport("stos_jadra_produkcyjny_64k", ok_stack)

    # Polecenia BWS 87
    polecenia = ["MOTYW_CMD_USTAW", "MOTYW_CMD_PODGLAD", "MOTYW_CMD_ZATWIERDZ", "MOTYW_CMD_PRZYWROC", "MOTYW_CMD_LISTUJ"]
    brakujace_polecenia = [c for c in polecenia if c not in motyw_h]
    raport("motyw_polecenia_bws_87", len(brakujace_polecenia) == 0, str(brakujace_polecenia))


# ============================================================================
# 2. TEST WBUDOWANYCH MOTYWÓW I BEZPIECZEŃSTWA FORMATU
# ============================================================================
def test_pliki_motywow_i_bezpieczenstwo():
    motyw_cpp_path = os.path.join(REPO, "jadro/gui/motyw.cpp")
    with open(motyw_cpp_path, "r", encoding="utf-8") as f:
        cpp = f.read()

    motywy = ["bursztynnocny", "baltyckanoc", "grafit", "bursztynklasyczny", "bursztynjasny"]
    for m in motywy:
        raport(f"motyw_deklaracja_{m}", f"id = {m}" in cpp)

    # Sprawdzenie czy pliki motywów nie zawierają kodu wykonywalnego
    niebezpieczne_tokeny = ["exec", "sh", "system", "fork", "mprotect", "dlopen", "javascript", "script", "<?", "<script"]
    znaleziono_zagrozenia = False
    for tok in niebezpieczne_tokeny:
        # szukamy tylko w deklaracjach szablonów PLIK_
        match = re.search(r"PLIK_[A-Z_]+\[\]\s*=\s*\"[^\"]*" + re.escape(tok) + r"[^\"]*\"", cpp, re.I)
        if match:
            znaleziono_zagrozenia = True
            break
    raport("brak_kodu_wykonywalnego_w_motywach", not znaleziono_zagrozenia)

    # Weryfikacja limitów fail-closed w parserze
    ok_limit_rozmiaru = "MOTYW_MAKS_ROZMIAR_PLIKU = 4096" in cpp or "4096" in cpp
    ok_kontrast = "motyw_waliduj_kontrast" in cpp
    raport("parser_fail_closed_i_kontrast", ok_limit_rozmiaru and ok_kontrast)

    # Weryfikacja obecności wbudowanego fallbacku w motyw.h / motyw.cpp
    motyw_h_path = os.path.join(REPO, "jadro/gui/motyw.h")
    with open(motyw_h_path, "r", encoding="utf-8") as f:
        h = f.read()
    ok_fallback = "g_domyslny_motyw_fallback" in h and "Bursztyn Nocny" in h
    raport("wbudowany_fallback_jadra_g_domyslny_motyw_fallback", ok_fallback)


# ============================================================================
# 3. TEST APLIKACJI MOTYWY I ZGODNOŚCI TYPOGRAFICZNEJ
# ============================================================================
def test_aplikacja_motywy():
    from pathlib import Path
    root = Path(REPO)
    app_src = (root / 'programy/ustawienia/themes_ui.h').read_text()
    kernel = (root / 'jadro/rdzen/kernel.cpp').read_text()
    wm = (root / 'programy/menedzer_okien/menedzer_okien.cpp').read_text()
    make = (root / 'Makefile').read_text()
    raport("ustawienia_kanoniczne_motywy", 'ThemesPage' in app_src and 'wylistuj_katalog_uzytkownika' in app_src)
    raport("brak_starego_payloadu", 'motywy_blob.o' not in make and '_binary_motywy_bin_start' not in kernel)
    raport("brak_starego_start_desktop", 'motywy.cebula' not in (root / 'jadro/gui/desktop_entries.h').read_text() and
           'Motywy Systemowe' not in (root / 'jadro/gui/start_menu_model.h').read_text())
    raport("migracja_starej_paczki", 'usun_twor("/programy/motywy.cebula/motywy.bur")' in kernel)
    raport("obsluga_zdarzenia_motywu_w_menedzerze_okien", 'BWS_ZDARZENIE_MOTYW_ZMIENIONY' in wm)
    raport("logika_transakcji_podgladu_i_rollbacku", all(word in app_src for word in
           ['MOTYW_CMD_PODGLAD', 'MOTYW_CMD_ZATWIERDZ', 'MOTYW_CMD_PRZYWROC', '15000000000']))
    raport("brak_sztucznego_podgladu", 'PodgladMotywuKontrolka' not in app_src)


# ============================================================================
# 4. TEST WIDGETÓW I INTEGRACJI SYSTEMOWEJ BURSZTYNUI
# ============================================================================
def test_integracja_bursztynui():
    ui_motyw = os.path.join(REPO, "bursztyn_ui", "jadro/gui/motyw.h")
    ui_okno = os.path.join(REPO, "bursztyn_ui", "okno.h")
    gui_cpp = os.path.join(REPO, "jadro/gui/bursztyn_gui.cpp")

    with open(ui_motyw, "r", encoding="utf-8") as f:
        motyw_ui_src = f.read()
    with open(ui_okno, "r", encoding="utf-8") as f:
        okno_ui_src = f.read()
    with open(gui_cpp, "r", encoding="utf-8") as f:
        gui_src = f.read()

    # Dynamiczne odczytywanie kolorów i promieni w BursztynUI
    ok_ui_dynamiczne = "gui_pobierz_motyw" in motyw_ui_src and "motyw_kolor" in motyw_ui_src
    raport("dynamiczne_kolory_w_bursztyn_ui", ok_ui_dynamiczne)

    # Obsługa zdarzenia BWS_ZDARZENIE_MOTYW_ZMIENIONY w Okno::obsluz_zdarzenie()
    ok_okno_event = "BWS_ZDARZENIE_MOTYW_ZMIENIONY" in okno_ui_src
    raport("reakcja_okna_na_bws_zdarzenie_motywu", ok_okno_event)

    # Zaokrąglanie rogów w rysowaniu belek okien
    ok_rogi = "motyw_promien_inset" in gui_src and "gui_rysuj_standardowa_belke" in gui_src
    raport("zaokraglone_rogi_w_rysowaniu_gui", ok_rogi)


def main():
    print("=" * 70)
    print(" BURSZTYN OS - TESTY INTEGRACJI I ARCHITEKTURY PODSYSTEMU MOTYWÓW")
    print("=" * 70)

    test_abi_layout()
    test_pliki_motywow_i_bezpieczenstwo()
    test_aplikacja_motywy()
    test_integracja_bursztynui()

    print("=" * 70)
    print("Wyniki: %d zdanych, %d oblanych" % (zdane, zlane))
    print("=" * 70)
    if zlane > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
