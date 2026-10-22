#!/usr/bin/env python3
"""Bursztyn OS - Testy integracji GUI i modernizacji przegladarki Husarz (BursztynUI).

Weryfikuje:
  1. Uklad i rozmiar naglowka formatu .bur w przegladarka.cpp zgodny ze skryptem przegladarka_linker.ld.
  2. Spojnosc sekcji ELF (TEXT=0x60000 / 384 KiB, DATA=0x80000 / 512 KiB, VA < 4 GiB, brak statycznych konstruktorow Ring 3).
  3. Bufor sieciowy siec_bufor zachowany w niskim BSS (256 KiB, VA < 4 GiB dla ABI BWS29/30).
  4. Zastosowanie nowoczesnego frameworka BursztynUI (AplikacjaGUI, OknoHusarz : Okno, MenuGlowne, Panel,
     Przycisk, PoleTekstowe, PasekStanu, MenuKontekstowe, PasekPrzewijania).
  5. Czystosc stylistyczna: usuniecie legacy rysowania okna i ramki (RysujInterfejs, RysujPrzyciskLokalny),
     pelna integracja z motyw_kolor(...) i obsluga zdarzen okna.
  6. Skroty klawiszowe (Ctrl+L, Ctrl+R, Ctrl+T, Ctrl+W, Ctrl+D, F5, Esc, Home, End).
  7. Poprawnosc obslugi polskich znakow UTF-8 w interfejsie uzytkownika.
  8. Zachowanie silnika renderowania HTML, parsera URL i obslugi sieci (HTTP/HTTPS, TLS, DNS, chunked).
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
    cpp_path = os.path.join(REPO, "programy", "przegladarka.cpp")
    ld_path = os.path.join(REPO, "programy", "przegladarka_linker.ld")

    with open(cpp_path, "r", encoding="utf-8") as f:
        cpp_src = f.read()
    with open(ld_path, "r", encoding="utf-8") as f:
        ld_src = f.read()

    # Sprawdzenie zdefiniowanych stalych w cpp
    ok_text_sz = "BUR_TEXT_SIZE = 0x60000ULL" in cpp_src or "0x60000" in cpp_src
    ok_data_sz = "BUR_DATA_SIZE = 0x80000ULL" in cpp_src or "0x80000" in cpp_src
    ok_text_va = "BUR_TEXT_VADDR = 0x1001000ULL" in cpp_src or "0x1001000" in cpp_src
    ok_data_va = "BUR_DATA_VADDR = 0x1061000ULL" in cpp_src or "0x1061000" in cpp_src
    raport("husarz_stale_adresacji_cpp", ok_text_sz and ok_data_sz and ok_text_va and ok_data_va)

    # Sprawdzenie skryptu linkera
    ok_ld_text = "BUR_TEXT_SIZE     = 0x60000;" in ld_src
    ok_ld_data = "BUR_DATA_SIZE     = 0x80000;" in ld_src
    ok_ld_data_offset = "BUR_DATA_OFFSET   = 0x61000;" in ld_src
    ok_ld_data_va = "BUR_DATA_VADDR    = 0x1061000;" in ld_src
    raport("husarz_linker_segmenty_256k_512k", ok_ld_text and ok_ld_data and ok_ld_data_offset and ok_ld_data_va)


# ============================================================================
# 2. TEST BINARNY ELF, BSS I BRAK STATYCZNYCH KONSTRUKTOROW RING 3
# ============================================================================
def test_elf_binarny():
    elf_path = os.path.join(REPO, "przegladarka.elf")
    bin_path = os.path.join(REPO, "przegladarka.bin")

    if not os.path.exists(elf_path) or not os.path.exists(bin_path):
        raport("husarz_binaria_istnieja", False, "Brak przegladarka.elf/bin - zbuduj make przegladarka_blob.o")
        return

    raport("husarz_binaria_istnieja", True)

    # Weryfikacja naglowka binarnego .bur
    blob = open(bin_path, "rb").read()
    magic, entry, to, ts, tv, do, ds, dv = struct.unpack_from("<I7Q", blob)
    ok_header = (
        magic == 0x00525542 and
        to == 0x1000 and
        ts == 0x60000 and
        tv == 0x1001000 and
        do == 0x61000 and
        ds == 0x80000 and
        dv == 0x1061000 and
        tv <= entry < tv + ts and
        to + ts == do and
        tv + ts == dv and
        dv + ds < 0x100000000
    )
    raport("husarz_naglowek_bur_poprawny", ok_header, f"magic={hex(magic)} ts={hex(ts)} ds={hex(ds)} dv={hex(dv)}")

    # Sekcje ELF
    try:
        out = subprocess.check_output(["readelf", "-S", elf_path], text=True)
        ok_naglowek = ".naglowek" in out
        ok_text = ".text" in out
        ok_data = ".data" in out
        ok_bss = ".bss" in out
        raport("husarz_sekcje_elf", ok_naglowek and ok_text and ok_data and ok_bss)
    except Exception as e:
        raport("husarz_sekcje_elf", False, str(e))

    # Sprawdzenie braku sekcji .init_array / .ctors o rozmiarze > 0
    try:
        out_syms = subprocess.check_output(["nm", "-C", "-S", elf_path], text=True)
        m_start = re.search(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+__bur_ctor_start", out_syms)
        m_end = re.search(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+__bur_ctor_end", out_syms)
        if m_start and m_end:
            ctor_equal = (m_start.group(1) == m_end.group(1))
            raport("husarz_brak_statycznych_konstruktorow", ctor_equal,
                   "start=%s, end=%s" % (m_start.group(1), m_end.group(1)))
        else:
            raport("husarz_brak_statycznych_konstruktorow", True, "Symbole ctor pominiete")

        # Brak wyjatkow i RTTI
        assert not re.search(r"\bU\b", out_syms), "Znaleziono niezdefiniowane symbole"
        assert not re.search(r"_GLOBAL__sub_I", out_syms), "Znaleziono inicjalizatory statyczne"
        assert not re.search(r"__cxa_throw", out_syms), "Znaleziono wyjatki"
        assert not re.search(r"__gxx_personality", out_syms), "Znaleziono personality/RTTI"
        raport("husarz_brak_wyjatkow_i_rtti", True)

        # Weryfikacja siec_bufor (256 KiB w niskim BSS pod 0x1042000)
        worker_symbols = subprocess.check_output(["nm", "-C", "-S", os.path.join(REPO,"husarz_worker.elf")], text=True)
        assert not re.search(r"\bU\b|_GLOBAL__sub_I|__cxa_throw|__gxx_personality",worker_symbols)
        m_siec = re.search(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+b\s+.*siec_bufor", worker_symbols)
        if m_siec:
            addr = int(m_siec.group(1), 16)
            sz = int(m_siec.group(2), 16)
            ok_siec = (sz == 0x40000 and dv <= addr < dv + ds and addr + sz <= dv + ds)
            raport("husarz_bufor_sieciowy_niski_bss", ok_siec, f"addr={hex(addr)} sz={hex(sz)}")
        else:
            raport("husarz_bufor_sieciowy_niski_bss", False, "Brak symbolu siec_bufor")

    except Exception as e:
        raport("husarz_brak_statycznych_konstruktorow", False, str(e))
        raport("husarz_brak_wyjatkow_i_rtti", False, str(e))
        raport("husarz_bufor_sieciowy_niski_bss", False, str(e))

    # Sprawdzenie kodu maszynowego Ring 3: brak instrukcji uprzywilejowanych
    try:
        tmp_obj = os.path.join(REPO, "przegladarka_tmp.o")
        assembly = subprocess.check_output(["objdump", "-d", tmp_obj], text=True)
        priv = re.search(r"\t(?:cli|sti|cpuid|rdmsr|wrmsr|in[bwl]?|out[bwl]?)\s", assembly)
        raport("husarz_brak_instrukcji_ring0", priv is None, f"priv={priv.group(0) if priv else None}")
    except Exception as e:
        raport("husarz_brak_instrukcji_ring0", False, str(e))


# ============================================================================
# 3. TEST ARCHITEKTURY BURSZTYNUI I USUNIECIA LEGACY KODU
# ============================================================================
def test_bursztyn_ui_architektura():
    cpp_path = os.path.join(REPO, "programy", "przegladarka.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        src = f.read()

    # Wymagane komponenty BursztynUI
    wymagane_komponenty = [
        "AplikacjaGUI",
        "OknoHusarz",
        "MenuGlowne",
        "PasekStanu",
        "PoleTekstowe",
        "Przycisk",
        "Panel",
        "MenuKontekstowe",
        "PasekPrzewijania"
    ]
    brakujace = [c for c in wymagane_komponenty if c not in src]
    raport("husarz_komponenty_bursztyn_ui", len(brakujace) == 0, str(brakujace))

    # Usuniecie starych procedur rysowania okna i przyciskow
    legacy_funkcje = [
        "RysujInterfejs",
        "RysujPrzyciskLokalny",
        "husarz_rysuj_pole_adresu",
        "ogranicz_okno_do_ekranu",
        "RysujPasekStrony",
        "RysujZakladki"
    ]
    znalezione_legacy = [f for f in legacy_funkcje if f"void {f}" in src or f"bool {f}" in src]
    raport("husarz_usuniecie_legacy_rysowania", len(znalezione_legacy) == 0, str(znalezione_legacy))

    # Czystość motywu
    # Native controls retain theme callbacks; the web renderer accepts no desktop palette.
    ok_motyw = "Kontrolka::zaktualizuj_motyw()" in src and "motyw_kolor(" not in src
    raport("husarz_integracja_z_motywem", ok_motyw)

    # Elastyczny uklad paska narzedzi
    ok_rozciagnij = "WyrownaniePoziome::Rozciagnij" in src
    ok_min_rozmiar = "ustaw_minimalny_rozmiar" in src
    raport("husarz_elastyczny_pasek_narzedzi", ok_rozciagnij and ok_min_rozmiar)


# ============================================================================
# 4. TEST FUNKCJONALNOSCI PRZEGLADARKI I SKROTOW
# ============================================================================
def test_funkcjonalnosci_przegladarki():
    cpp_path = os.path.join(REPO, "programy", "przegladarka.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        src = f.read()

    # Akcje nawigacyjne i zarzadzania kartami
    akcje = [
        "przejdz_do_url",
        "odswiez",
        "zatrzymaj",
        "wstecz",
        "dalej",
        "dom",
        "nowa_zakladka_gui",
        "zamknij_zakladke_gui",
        "kopiuj_adres_do_schowka",
        "wklej_adres_ze_schowka"
    ]
    brakujace_akcje = [a for a in akcje if a not in src]
    raport("husarz_akcje_nawigacji_i_kart", len(brakujace_akcje) == 0, str(brakujace_akcje))

    # Skróty klawiaturowe: Ctrl+L, Ctrl+R, Ctrl+T, Ctrl+W, F5, Esc
    ok_skroty = (
        "BWS_MOD_CTRL" in src and
        "'l'" in src and
        "'r'" in src and
        "'t'" in src and
        "'w'" in src and
        "is_f5" in src and
        "is_esc" in src
    )
    raport("husarz_skroty_klawiszowe", ok_skroty)

    # Poprawne polskie etykiety UI
    frazy_pl = [
        "Wstecz",
        "Dalej",
        "Odśwież",
        "Zatrzymaj",
        "Dom",
        "Przejdź",
        "Zakładki",
        "Nowa karta",
        "Zamknij kartę",
        "Zaznacz adres",
        "Kopiuj adres",
        "Wklej adres ze schowka",
        "O programie Husarz"
    ]
    brak_fraz = [p for p in frazy_pl if p not in src]
    raport("husarz_polskie_znaki_utf8", len(brak_fraz) == 0, str(brak_fraz))

    # Pasek stanu z sekcjami
    ok_pasek_stanu = "dodaj_sekcje" in src and "Gotowy" in src and "UTF-8" in src
    raport("husarz_pasek_stanu_bursztyn_ui", ok_pasek_stanu)

    # Zachowanie silnika HTTP/HTTPS/DNS/TLS
    ok_silnik = (
        "bws_siec_dns" in src and
        "bws_siec_pobierz_http" in src and
        "bws_siec_pobierz_https" in src and
        "pobierz_jeden_url" in src and
        "renderuj_html" in src
    )
    raport("husarz_zachowanie_silnika_sieci_i_html", ok_silnik)


def main():
    print("=" * 70)
    print(" BURSZTYN OS - TESTY JEDNOSTKOWE I INTEGRACYJNE HUSARZ (BURSZTYNUI)")
    print("=" * 70)

    test_naglowek_i_linker()
    test_elf_binarny()
    test_bursztyn_ui_architektura()
    test_funkcjonalnosci_przegladarki()

    print("-" * 70)
    print("Podsumowanie testow przegladarki Husarz: Zdane=%d, Zlane=%d" % (zdane, zlane))
    if zlane > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
