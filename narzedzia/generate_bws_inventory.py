#!/usr/bin/env python3
"""
Generuje maszynowo czytelny inwentarz 93 wywolan systemowych BWS (1..93)
oraz macierz uprawnien PZB/PRAWO_* do katalogu docs/security-hardening-2026-11-04/.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "docs/security-hardening-2026-11-04"
OUT_DIR.mkdir(parents=True, exist_ok=True)

PZB_LEVELS = [
    {"id": 0, "name": "PZB_JADRO", "description": "Jadro (Ring 0 / PID 0)"},
    {"id": 1, "name": "PZB_STEROWNIKI", "description": "Sterowniki urzadzen"},
    {"id": 2, "name": "PZB_USLUGI", "description": "Uslugi systemowe (np. Menedzer Okien)"},
    {"id": 3, "name": "PZB_ZAUFANE", "description": "Aplikacje zaufane (np. Instalator, OTA, Ustawienia)"},
    {"id": 4, "name": "PZB_UZYTKOWNIK", "description": "Zwykle aplikacje uzytkownika"},
    {"id": 5, "name": "PZB_PIASKOWNICA", "description": "Piaskownica (najniższy poziom zaufania)"},
]

PRAWA_BITS = [
    {"bit": 0, "mask": "0x01", "name": "PRAWO_PLIKI_CZYTAJ"},
    {"bit": 1, "mask": "0x02", "name": "PRAWO_PLIKI_ZAPISZ"},
    {"bit": 2, "mask": "0x04", "name": "PRAWO_SIEC"},
    {"bit": 3, "mask": "0x08", "name": "PRAWO_GUI"},
    {"bit": 4, "mask": "0x10", "name": "PRAWO_URUCHOM_PROGRAM"},
    {"bit": 5, "mask": "0x20", "name": "PRAWO_SYSTEM_CONFIG"},
    {"bit": 6, "mask": "0x40", "name": "PRAWO_STEROWNIK"},
    {"bit": 7, "mask": "0x80", "name": "PRAWO_DEBUG"},
]

BWS_SPECS = [
    (1, "BWS_WYPISZ", "bws_wypisz", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* tekst (NUL-terminated, max 512B)"], [], [], "PZB_PIASKOWNICA (<=5)", ["Serial/terminal output"], ["Valid mapped user string"], "0 on error, 1 on success"),
    (2, "BWS_UTWORZ_PLIK", "bws_utworz_plik", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka"], [], ["PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4) or scoped write policy", ["Creates empty file in PSF/BSP2"], ["Safe normalized path, writable namespace"], "0 on error, 1 on success"),
    (3, "BWS_ZAPISZ_PLIK", "bws_zapisz_plik", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka", "arg2: const void* dane"], ["arg3: size_t rozmiar (<= MAKS_ROZMIAR_PLIKU)"], ["PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4) or scoped write policy", ["Writes file contents in PSF/BSP2"], ["Safe path, readable user buffer [arg2, arg2+arg3)"], "0 on error, 1 on success"),
    (4, "BWS_POBIERZ_ZNAK", "scheduler_pobierz_klawisz_lub_zablokuj", "jadro/bws/syscalls.cpp", [], [], [], [], [], "PZB_PIASKOWNICA (<=5)", ["Dequeues key from process keyboard queue"], ["Active Ring 3 process"], "uint8_t char or 0"),
    (5, "BWS_CZYTAJ_PLIK", "bws_czytaj_plik", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka", "arg2: void* bufor"], ["arg3: size_t pojemnosc (<= MAKS_ROZMIAR_PLIKU)"], ["PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Copies file contents to user buffer"], ["Safe path, writable user buffer [arg2, arg2+arg3)"], "Bytes read or 0"),
    (6, "BWS_LISTUJ_KATALOG", "bws_wylistuj_katalog", "jadro/bws/syscalls.cpp", [], ["WpisKatalogu[]"], ["arg1: const char* sciezka", "arg2: WpisKatalogu* bufor"], ["arg3: size_t maks_wpisow (<= 256)"], ["PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Reads directory entries to user array"], ["Safe path, writable user buffer"], "Entry count or 0"),
    (7, "BWS_USUN_TWOR", "bws_usun_twor", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka"], [], ["PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4) or scoped write policy", ["Removes file or directory in PSF/BSP2"], ["Safe path, non-system protected path"], "0 on error, 1 on success"),
    (8, "BWS_ZMIEN_NAZWE", "bws_zmien_nazwe", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* stara_sciezka", "arg2: const char* nowa_sciezka"], [], ["PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4)", ["Renames node in PSF/BSP2"], ["Safe source and destination paths"], "0 on error, 1 on success"),
    (9, "BWS_CZAS_RTC", "bws_rtc", "jadro/bws/syscalls.cpp", [], ["CzasRTC (8B)"], ["arg1: CzasRTC* wyjscie"], ["sizeof(CzasRTC)"], [], "PZB_PIASKOWNICA (<=5)", ["Reads RTC time into user struct"], ["Writable user struct arg1"], "0 on error, 1 on success"),
    (10, "BWS_URUCHOM_PROGRAM", "bws_uruchom", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka", "arg4: const char* arg_opcjonalny"], [], ["PRAWO_URUCHOM_PROGRAM"], "Child PZB cannot exceed caller PZB", ["Spawns new Ring 3 process from BUR image"], ["Valid BUR image, caller holds PRAWO_URUCHOM_PROGRAM"], "1 on success, 0 on error"),
    (11, "BWS_PING", "bws_siec_ping", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Sends ICMP echo request"], ["Caller holds PRAWO_SIEC"], "1 on reply, 0 on timeout/deny"),
    (12, "BWS_DNS_LEGACY", "bws_dns", "jadro/bws/syscalls.cpp", [], ["uint8_t ip[4]"], ["arg1: const char* domena", "arg2: uint8_t* wyjscie_ip"], ["4 bytes at arg2"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Resolves hostname via DNS"], ["Caller holds PRAWO_SIEC, valid domain & 4B out buffer"], "1 on success, 0 on error"),
    (13, "BWS_HTTP_DO_PLIKU_LEGACY", "bws_legacy_http_do_pliku", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* host", "arg2: const char* sciezka_url", "arg3: const char* plik_docelowy"], [], ["PRAWO_SIEC", "PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4)", ["Downloads HTTP response and saves to PSF"], ["Caller holds PRAWO_SIEC and PRAWO_PLIKI_ZAPISZ"], "Bytes saved or 0"),
    (14, "BWS_GUI_RYSUJ_OKNO", "bws_gui_rysuj_okno", "jadro/bws/syscalls.cpp", [], [], ["arg3: const char* tytul (max 64B)"], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Draws window frame in layer"], ["Caller holds PRAWO_GUI, valid title string"], "1 on success, 0 on error"),
    (15, "BWS_GUI_TEKST", "bws_gui_wypisz_tekst", "jadro/bws/syscalls.cpp", [], [], ["arg3: const char* tekst (max 1024B)"], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Draws text in layer"], ["Caller holds PRAWO_GUI, valid string"], "1 on success, 0 on error"),
    (16, "BWS_GUI_WYCZYSC", "bws_gui_wyczyscz_obszar", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Clears rectangle in layer"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (17, "BWS_GUI_ODSWIEZ", "bws_gui_odswiez", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Triggers screen composition"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (18, "BWS_GUI_POBIERZ_MYSZ", "bws_gui_pobierz_mysz", "jadro/bws/syscalls.cpp", [], ["int x", "int y", "uint8_t przyciski"], ["arg1: int* x", "arg2: int* y", "arg3: uint8_t* przyciski"], ["4B, 4B, 1B"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Copies mouse state to user pointers"], ["Caller holds PRAWO_GUI, 3 writable pointers"], "1 on success, 0 on error"),
    (19, "BWS_GUI_ODSWIEZ_PULPIT", "bws_gui_odswiez_pulpit", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Refreshes desktop background"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (20, "BWS_GUI_TEKST_KOLOR", "bws_gui_wypisz_tekst_kolor", "jadro/bws/syscalls.cpp", [], [], ["arg4: const char* tekst (max 1024B)"], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Draws colored text in layer"], ["Caller holds PRAWO_GUI, valid string"], "1 on success, 0 on error"),
    (21, "BWS_GUI_PROSTOKAT", "bws_gui_rysuj_prostokat", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Draws filled rectangle in layer"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (22, "BWS_GUI_PRZEJECIE_MYSZY", "bws_gui_ustaw_przejecie_myszy", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Toggles mouse grab"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (23, "BWS_GUI_ROZDZIELCZOSC", "bws_gui_pobierz_rozdzielczosc", "jadro/bws/syscalls.cpp", [], ["int szer", "int wys"], ["arg1: int* szer", "arg2: int* wys"], ["4B, 4B"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Writes screen resolution to user pointers"], ["Caller holds PRAWO_GUI, 2 writable int pointers"], "1 on success, 0 on error"),
    (24, "BWS_GUI_SZEROKOSC_ZNAKU", "bws_gui_pobierz_szerokosc_znaku", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Queries glyph width"], ["Caller holds PRAWO_GUI"], "Glyph width or 0"),
    (25, "BWS_RESET", "reset_ps2", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3) via operacja_systemowa_dozwolona", ["Reboots system via PS/2 controller"], ["operacja_systemowa_dozwolona(proces)"], "0 on deny"),
    (26, "BWS_SHUTDOWN", "wylacz_qemu", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3) via operacja_systemowa_dozwolona", ["Powers off system/QEMU"], ["operacja_systemowa_dozwolona(proces)"], "0 on deny"),
    (27, "BWS_DZWIEK_TON", "hda_test_ton", "jadro/bws/syscalls.cpp", [], [], [], ["arg1: hz (20..20000)", "arg2: ms (1..10000)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Plays test tone via HDA"], ["Caller holds PRAWO_GUI, frequency/duration in range"], "1 on success, 0 on error"),
    (28, "BWS_DNS", "bws_dns", "jadro/bws/syscalls.cpp", [], ["uint8_t ip[4]"], ["arg1: const char* domena", "arg2: uint8_t* wyjscie_ip"], ["4B"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Resolves DNS A record"], ["Caller holds PRAWO_SIEC, valid string & 4B out buffer"], "1 on success, 0 on error"),
    (29, "BWS_HTTP", "bws_http_common(false)", "jadro/bws/syscalls.cpp", ["BwsHttpZadanie"], ["BwsHttpOdpowiedz"], ["arg1: const BwsHttpZadanie*", "arg2: BwsHttpOdpowiedz*", "arg3: void* bufor_ciala"], ["arg4: size_t pojemnosc_ciala"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Performs HTTP request"], ["Caller holds PRAWO_SIEC, valid structs & body buffer"], "Bytes read or 0"),
    (30, "BWS_HTTPS", "bws_http_common(true)", "jadro/bws/syscalls.cpp", ["BwsHttpZadanie"], ["BwsHttpOdpowiedz"], ["arg1: const BwsHttpZadanie*", "arg2: BwsHttpOdpowiedz*", "arg3: void* bufor_ciala"], ["arg4: size_t pojemnosc_ciala"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Performs HTTPS TLS 1.2/1.3 request"], ["Caller holds PRAWO_SIEC, valid structs & body buffer"], "Bytes read or 0"),
    (31, "BWS_TLS_CERTYFIKAT", "kernel_tls_certyfikat_zaufany", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Queries whether last TLS certificate was trusted"], ["Caller holds PRAWO_SIEC"], "1 or 0"),
    (32, "BWS_ZAKONCZ_PROCES", "bws_zakoncz_biezacy_proces", "jadro/bws/syscalls.cpp", [], [], [], ["arg1: optional exit reason (e.g. BWS_EXIT_STACK_PROTECTOR_VIOLATION)"], [], "PZB_PIASKOWNICA (<=5)", ["Terminates calling Ring 3 process and frees resources; logs [STACKPROTECTORVIOLATION] if arg1 == 0x53535056494F4C01"], ["Active Ring 3 process"], "Noreturn (switches context)"),
    (33, "BWS_GUI_UTWORZ_WARSTWE", "utworz_warstwe", "jadro/bws/syscalls.cpp", [], [], [], ["arg1: x|y", "arg2: szer|wys (>0)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Allocates compositor layer for process"], ["Caller holds PRAWO_GUI, positive dimensions"], "layer_id+1 or 0"),
    (34, "BWS_GUI_PRZESUN_WARSTWE", "zaktualizuj_pozycje_warstwy", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Moves process compositor layer"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (35, "BWS_ALOKUJ_STERTE", "bws_alokuj_sterte", "jadro/bws/syscalls.cpp", [], [], [], ["arg1: size_t bajty (<= limit sterty Ring 3)"], [], "PZB_PIASKOWNICA (<=5)", ["Maps zeroed RW NX heap pages for process"], ["Size within [STERTA_PROCESU_BAZA, STERTA_PROCESU_LIMIT)"], "Virtual address or 0"),
    (36, "BWS_GUI_CZY_ZAMKNIETO_POWLOKE", "gui_czy_zamknieto_powloke", "jadro/bws/syscalls.cpp", [], [], [], [], [], "PZB_PIASKOWNICA (<=5)", ["Checks if window close was requested"], ["None"], "1 or 0"),
    (37, "BWS_ZDARZENIE_POLL", "scheduler_pobierz_zdarzenie", "jadro/bws/syscalls.cpp", [], ["bws_zdarzenie"], ["arg1: bws_zdarzenie* wyjscie"], ["sizeof(bws_zdarzenie)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Non-blocking event dequeue"], ["Caller holds PRAWO_GUI, writable bws_zdarzenie*"], "1 on event, 0 if empty/error"),
    (38, "BWS_ZDARZENIE_CZEKAJ", "scheduler_czekaj_na_zdarzenie", "jadro/bws/syscalls.cpp", [], ["bws_zdarzenie"], ["arg1: bws_zdarzenie* wyjscie"], ["sizeof(bws_zdarzenie), arg2: timeout_ms"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Blocking wait for GUI/input event"], ["Caller holds PRAWO_GUI, writable bws_zdarzenie*"], "1 on event, 2 on timeout, 0 on error"),
    (39, "BWS_GUI_CAPTURE", "bws_gui_ustaw_capture", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Sets mouse capture for caller"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (40, "BWS_GUI_SYSTEM_OVERLAY", "skladacz_obrazu_ustaw_overlay", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI", "PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3) via operacja_systemowa_dozwolona", ["Configures system overlay region"], ["operacja_systemowa_dozwolona && PRAWO_GUI"], "1 on success, 0 on error"),
    (41, "BWS_GUI_MINIMALIZUJ", "bws_gui_minimalizuj_warstwe", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Minimizes caller window layer"], ["Caller holds PRAWO_GUI"], "1 or 0"),
    (42, "BWS_GUI_OKNA_SNAPSHOT", "bws_snapshot_okien", "jadro/bws/syscalls.cpp", [], ["BwsOknoInfo[]"], ["arg1: BwsOknoInfo* bufor", "arg3: uint32_t* aktywne_pid"], ["arg2: maks_okien"], ["PRAWO_GUI", "PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3) via operacja_systemowa_dozwolona", ["Snapshots window list for window manager"], ["operacja_systemowa_dozwolona, writable buffer"], "Count or 0"),
    (43, "BWS_GUI_AKTYWUJ_OKNO", "bws_gui_aktywuj_okno", "jadro/bws/syscalls.cpp", [], [], [], ["arg1: target pid"], ["PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3) via operacja_systemowa_dozwolona", ["Activates/focuses window of target PID"], ["operacja_systemowa_dozwolona"], "1 or 0"),
    (44, "BWS_ROZMIAR_PLIKU", "bws_rozmiar_pliku", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka"], [], ["PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Queries file size in PSF/BSP2"], ["Caller holds PRAWO_PLIKI_CZYTAJ, valid path"], "File size or UINT64_MAX"),
    (45, "BWS_ARGUMENT_STARTOWY", "bws_pobierz_argument", "jadro/bws/syscalls.cpp", [], ["char[]"], ["arg1: char* bufor"], ["arg2: size_t pojemnosc (1..256)"], [], "PZB_PIASKOWNICA (<=5)", ["Copies startup argument string to user buffer"], ["Writable user buffer [arg1, arg1+arg2)"], "Length or 0"),
    (46, "BWS_UTWORZ_KATALOG", "bws_utworz_katalog", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka"], [], ["PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4)", ["Creates directory in PSF/BSP2"], ["Caller holds PRAWO_PLIKI_ZAPISZ, safe path"], "1 on success, 0 on error"),
    (47, "BWS_METADANE_PLIKU", "bws_pobierz_metadane", "jadro/bws/syscalls.cpp", [], ["MetadanePliku"], ["arg1: const char* sciezka", "arg2: MetadanePliku* wyjscie"], ["sizeof(MetadanePliku)"], ["PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Reads inode metadata from PSF/BSP2"], ["Caller holds PRAWO_PLIKI_CZYTAJ, safe path, writable struct"], "1 on success, 0 on error"),
    (48, "BWS_PRZENIES_TWOR", "bws_przenies", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* zrodlo", "arg2: const char* cel"], [], ["PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4)", ["Moves/renames file or directory in PSF/BSP2"], ["Caller holds PRAWO_PLIKI_ZAPISZ, safe paths"], "1 on success, 0 on error"),
    (49, "BWS_GUI_CELE_DROP", "bws_rejestruj_cele_drop", "jadro/bws/syscalls.cpp", ["BwsCelDrop[]"], [], ["arg1: const BwsCelDrop* cele"], ["arg2: liczba_celow (<= 32)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Registers drag-and-drop targets for window"], ["Caller holds PRAWO_GUI, readable array"], "1 on success, 0 on error"),
    (50, "BWS_GUI_AKTUALIZUJ_DRAG", "bws_aktualizuj_drag", "jadro/bws/syscalls.cpp", [], [], ["arg3: const char* etykieta"], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Updates drag state in compositor"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (51, "BWS_GUI_WYSOKOSC_FONTU", "bws_czcionka_kopiuj", "jadro/bws/syscalls.cpp", [], ["uint8_t font_data[]"], ["arg1: void* bufor_opcjonalny"], ["arg2: rozmiar, arg3: tryb"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Returns font metrics or copies font data to user"], ["Caller holds PRAWO_GUI"], "Font height or copy status"),
    (52, "BWS_SCHOWEK_PLIK_USTAW", "bws_ustaw_schowek_plikow", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka"], ["arg2: wytnij_flaga"], ["PRAWO_GUI", "PRAWO_PLIKI_CZYTAJ"], "PZB_UZYTKOWNICA (<=4)", ["Sets file clipboard path"], ["Caller holds PRAWO_GUI and file permissions"], "1 on success, 0 on error"),
    (53, "BWS_SCHOWEK_PLIK_POBIERZ", "bws_pobierz_schowek_plikow", "jadro/bws/syscalls.cpp", [], ["char sciezka[]", "uint8_t* wytnij"], ["arg1: char* bufor", "arg3: uint8_t* wytnij"], ["arg2: pojemnosc"], ["PRAWO_GUI", "PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Reads file clipboard path"], ["Caller holds PRAWO_GUI & PRAWO_PLIKI_CZYTAJ, writable buffer"], "1 on success, 0 on error"),
    (54, "BWS_SCHOWEK_PLIK_WYCZYSC", "bws_wyczysc_schowek_plikow", "jadro/bws/syscalls.cpp", [], [], [], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Clears file clipboard"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (55, "BWS_KOPIUJ_TWOR", "bws_kopiuj_twor", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* zrodlo", "arg2: const char* cel"], [], ["PRAWO_PLIKI_CZYTAJ", "PRAWO_PLIKI_ZAPISZ"], "PZB_UZYTKOWNIK (<=4)", ["Copies file or directory tree in PSF/BSP2"], ["Caller holds read+write file permissions, safe paths"], "1 on success, 0 on error"),
    (56, "BWS_GUI_POPUP_APLIKACJI", "skladacz_obrazu_ustaw_popup_aplikacji", "jadro/bws/syscalls.cpp", [], [], [], ["arg1: aktywny, arg2: x|y, arg3: w|h"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5) + active window owner", ["Configures popup layer for active app"], ["Caller holds PRAWO_GUI and is active window when enabling"], "1 or 0"),
    (57, "BWS_GUI_BLIT", "bws_gui_blit / bws_gui_zastap_regiony", "jadro/bws/syscalls.cpp", ["BwsGuiBatch / uint32_t pixels[]"], [], ["arg1: BwsGuiBatch* (when arg4==4) or x|y", "arg3: const uint32_t* piksele"], ["arg2: sizeof(BwsGuiBatch) or szer|wys (<=2048x2048)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Blits ARGB pixel buffer(s) into process layer"], ["Caller holds PRAWO_GUI, overflow-checked pixel range mapped in VMM"], "1 on success, 0 on error"),
    (58, "BWS_GUI_TYTUL_OKNA", "bws_gui_ustaw_tytul_okna", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* tytul"], [], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Updates window title"], ["Caller holds PRAWO_GUI, valid user string"], "1 on success, 0 on error"),
    (59, "BWS_CZYTAJ_FRAGMENT_PLIKU", "bws_czytaj_fragment_pliku", "jadro/bws/syscalls.cpp", [], [], ["arg1: const char* sciezka", "arg2: void* bufor"], ["arg3: offset, arg4: rozmiar"], ["PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Reads file slice at offset"], ["Caller holds PRAWO_PLIKI_CZYTAJ, safe path, writable buffer"], "Bytes read or 0"),
    (60, "BWS_CZAS_MONOTONICZNY_NS", "czas_monotoniczny_ns", "jadro/bws/syscalls.cpp", [], [], [], [], [], "PZB_PIASKOWNICA (<=5)", ["Reads monotonic HPET clock in nanoseconds"], ["None"], "Nanoseconds or 0"),
    (61, "BWS_GUI_GEOMETRIA", "zmien_geometrie_warstwy", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: x|y, a2: w|h, a3: max_state"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Resizes/moves window layer"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (62, "BWS_GUI_MONITOR_INFO", "monitor_info", "jadro/bws/bws_nowoczesne.cpp", [], ["GuiMonitorInfo"], ["a2: GuiMonitorInfo* wyjscie"], ["a3: sizeof(GuiMonitorInfo)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Copies monitor geometry/scale info to user"], ["Caller holds PRAWO_GUI, a1==0, a3>=sizeof(GuiMonitorInfo)"], "1 on success, 0 on error"),
    (63, "BWS_GUI_USTAW_WORK_AREA", "ustaw_work_area", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: x|y, a2: w|h, a3: skala"], ["PRAWO_GUI", "PRAWO_SYSTEM_CONFIG"], "PZB_USLUGI (<=2)", ["Updates desktop work area and UI scale"], ["proces.poziom_zaufania <= PZB_USLUGI && PRAWO_GUI && PRAWO_SYSTEM_CONFIG"], "1 on success, 0 on error"),
    (64, "BWS_SHM_CREATE", "shm_create", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: size_t rozmiar (1..16MiB), a2: flags (0)"], ["PRAWO_GUI"], "PZB_UZYTKOWNIK (<=4)", ["Allocates shared memory object"], ["poziom <= PZB_UZYTKOWNIK && (PRAWO_GUI || poziom <= PZB_ZAUFANE)"], "handle or 0"),
    (65, "BWS_SHM_MAP", "shm_map", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: handle, a2: flags"], ["PRAWO_GUI"], "PZB_UZYTKOWNIK (<=4)", ["Maps SHM pages into caller address space"], ["Valid SHM handle, authorized caller"], "User virtual address or 0"),
    (66, "BWS_SHM_UNMAP", "shm_unmap", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: handle"], [], "PZB_PIASKOWNICA (<=5)", ["Unmaps SHM pages from caller address space"], ["Mapped SHM handle owned/mapped by caller"], "1 on success, 0 on error"),
    (67, "BWS_SHM_CLOSE", "shm_close", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: handle"], [], "PZB_PIASKOWNICA (<=5)", ["Closes SHM handle and frees frames when refcount==0"], ["Owner or mapped participant"], "1 on success, 0 on error"),
    (68, "BWS_IPC_CREATE", "ipc_create", "jadro/bws/bws_nowoczesne.cpp", [], [], [], [], [], "PZB_UZYTKOWNIK (<=4)", ["Creates message queue endpoint"], ["poziom_zaufania <= PZB_UZYTKOWNIK"], "queue handle or 0"),
    (69, "BWS_IPC_SEND", "ipc_send / ipc_instalator / ipc_ekran / ipc_tapety", "jadro/bws/bws_nowoczesne.cpp", ["IpcMessage / InstalatorIpcMsg / EkranIpcKomunikat / TapetyIpcMsg"], [], ["a2: const void* msg"], ["sizeof(struct) by channel a1"], ["Channel-dependent: PRAWO_GUI / PRAWO_SYSTEM_CONFIG"], "Channel-dependent (SYSTEM_IPC: PZB_USLUGI + WM path; INSTALATOR_IPC: authorized installer PID; EKRAN_IPC: PZB_ZAUFANE + PRAWO_GUI|PRAWO_SYSTEM_CONFIG)", ["Sends structured IPC message or invokes system IPC service"], ["Valid channel handle and readable struct"], "1 on success, 0 on error"),
    (70, "BWS_IPC_RECEIVE", "ipc_receive", "jadro/bws/bws_nowoczesne.cpp", [], ["IpcMessage"], ["a2: IpcMessage* wyjscie"], ["sizeof(IpcMessage)"], [], "Owner PID of IPC queue", ["Dequeues IPC message into user struct"], ["Caller is owner_pid of queue a1, writable IpcMessage*"], "1 on message, 0 if empty/unauthorized"),
    (71, "BWS_IPC_CLOSE", "ipc_close", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: queue handle"], [], "Owner PID of IPC queue", ["Closes IPC queue"], ["Caller is owner_pid of queue a1"], "1 on success, 0 on error"),
    (72, "BWS_FILE_WATCH", "watch_dispatch", "jadro/bws/bws_nowoczesne.cpp", [], ["FileWatchEvent"], ["a2: const char* sciezka or FileWatchEvent*"], ["a1: op, a3: handle/flags"], ["PRAWO_PLIKI_CZYTAJ"], "PZB_PIASKOWNICA (<=5)", ["Creates/polls/closes filesystem watcher"], ["Caller holds PRAWO_PLIKI_CZYTAJ, valid path/buffer"], "handle/status or 0"),
    (73, "BWS_CLIPBOARD_ENUM", "clipboard_enum", "jadro/bws/bws_nowoczesne.cpp", [], ["uint32_t formats[]"], ["a1: uint32_t* bufor"], ["a2: max_count"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Enumerates available clipboard formats"], ["Caller holds PRAWO_GUI, writable array"], "Format count or 0"),
    (74, "BWS_CLIPBOARD_SIZE", "clipboard_size", "jadro/bws/bws_nowoczesne.cpp", [], ["uint64_t* rozmiar"], ["a2: uint64_t* wyjscie"], ["sizeof(uint64_t)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Queries payload size for clipboard format a1"], ["Caller holds PRAWO_GUI, writable uint64_t*"], "1 on success, 0 on error"),
    (75, "BWS_CLIPBOARD_READ", "clipboard_read", "jadro/bws/bws_nowoczesne.cpp", [], ["uint8_t dane[]", "uint64_t* out_len"], ["a2: void* bufor", "a4: uint64_t* out_len"], ["a3: pojemnosc (<=64KiB)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Copies clipboard payload to user buffer"], ["Caller holds PRAWO_GUI, writable buffer and length ptr"], "1 on success, 0 on error"),
    (76, "BWS_CLIPBOARD_SET", "clipboard_set", "jadro/bws/bws_nowoczesne.cpp", [], [], ["a2: const void* dane"], ["a3: rozmiar (<=64KiB)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Sets clipboard payload for format a1"], ["Caller holds PRAWO_GUI, readable user buffer"], "1 on success, 0 on error"),
    (77, "BWS_GUI_CURSOR_SHAPE", "bws_gui_ustaw_kursor_ksztalt", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: cursor shape enum"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Sets standard cursor shape for caller window"], ["Caller holds PRAWO_GUI"], "1 on success, 0 on error"),
    (78, "BWS_GUI_CURSOR_CUSTOM", "bws_gui_ustaw_kursor_custom", "jadro/bws/bws_nowoczesne.cpp", ["GuiCustomCursorInfo"], [], ["a1: const GuiCustomCursorInfo* (contains piksele ptr)"], ["szer*wys*4 <= 16384B (64x64 max)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Copies custom cursor bitmap into compositor"], ["Caller holds PRAWO_GUI, valid dimensions/hotspot/format, readable pixel buffer"], "1 on success, 0 on error"),
    (79, "BWS_SOCKET_CREATE", "socket_create", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: typ (TCP/UDP)"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5)", ["Allocates network socket descriptor"], ["Caller holds PRAWO_SIEC"], "socket handle or 0"),
    (80, "BWS_SOCKET_CONNECT", "socket_connect", "jadro/bws/bws_nowoczesne.cpp", ["SocketAddrV4"], [], ["a2: const SocketAddrV4*"], ["a3: sizeof(SocketAddrV4)"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5) + socket owner", ["Connects socket to IPv4 endpoint"], ["Caller holds PRAWO_SIEC, owns socket a1, valid SocketAddrV4"], "1 on success, 0 on error"),
    (81, "BWS_SOCKET_SEND", "socket_send", "jadro/bws/bws_nowoczesne.cpp", [], [], ["a2: const void* dane"], ["a3: len (1..32768)"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5) + socket owner", ["Sends data over connected socket"], ["Caller holds PRAWO_SIEC, owns socket a1, readable buffer"], "Bytes sent or 0"),
    (82, "BWS_SOCKET_RECV", "socket_recv", "jadro/bws/bws_nowoczesne.cpp", [], ["uint8_t dane[]", "uint64_t* out_len"], ["a2: void* bufor", "a4: uint64_t* out_len"], ["a3: capacity (1..32768)"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5) + socket owner", ["Receives data from connected socket"], ["Caller holds PRAWO_SIEC, owns socket a1, writable buffer"], "1 on success, 0 on error"),
    (83, "BWS_SOCKET_CLOSE", "socket_close", "jadro/bws/bws_nowoczesne.cpp", [], [], [], ["a1: socket handle"], ["PRAWO_SIEC"], "PZB_PIASKOWNICA (<=5) + socket owner", ["Closes network socket"], ["Caller holds PRAWO_SIEC and owns socket a1"], "1 on success, 0 on error"),
    (84, "BWS_OTA_STATUS", "aktualizacja_bws_status", "jadro/bws/bws_nowoczesne.cpp", [], ["BwsOtaStatus"], ["a1: BwsOtaStatus* wyjscie"], ["sizeof(BwsOtaStatus)"], ["PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3)", ["Copies OTA update status to user struct"], ["Caller holds PRAWO_SYSTEM_CONFIG and PZB <= PZB_ZAUFANE"], "1 on success, 0 on error"),
    (85, "BWS_OTA_POLECENIE", "aktualizacja_bws_polecenie", "jadro/bws/bws_nowoczesne.cpp", ["BwsOtaPolecenieArg (optional)"], [], ["a2: const void* arg_opcjonalny"], ["a1: kod polecenia"], ["PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3)", ["Executes OTA update state machine command"], ["Caller holds PRAWO_SYSTEM_CONFIG and PZB <= PZB_ZAUFANE"], "1 on success, 0 on error"),
    (86, "BWS_MOTYW_POBIERZ", "motyw_bws_pobierz", "jadro/bws/bws_nowoczesne.cpp", [], ["BwsMotywStan"], ["a1: BwsMotywStan* wyjscie"], ["sizeof(BwsMotywStan)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Reads current UI theme palette/state"], ["Caller holds PRAWO_GUI, writable struct"], "1 on success, 0 on error"),
    (87, "BWS_MOTYW_POLECENIE", "motyw_bws_polecenie", "jadro/bws/bws_nowoczesne.cpp", ["BwsMotywPolecenie (optional)"], [], ["a2: const void* dane"], ["a1: op, a3: rozmiar"], ["PRAWO_GUI", "PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3)", ["Applies/saves system UI theme"], ["Caller holds PRAWO_GUI|PRAWO_SYSTEM_CONFIG and PZB <= PZB_ZAUFANE"], "1 on success, 0 on error"),
    (88, "BWS_PULPIT_GOTOWY", "bws_potwierdz_pulpit_gotowy", "jadro/bws/bws_nowoczesne.cpp", [], [], [], [], ["PRAWO_GUI", "PRAWO_SYSTEM_CONFIG"], "PZB_USLUGI (<=2) + path == /menedzer_okien.bur", ["Marks desktop session ready for boot health confirmation"], ["poziom <= PZB_USLUGI, PRAWO_GUI|PRAWO_SYSTEM_CONFIG, sciezka == /menedzer_okien.bur"], "1 on success, 0 on error"),
    (89, "BWS_HDA_DIAG", "bws_hda_diag", "jadro/bws/bws_nowoczesne.cpp", [], ["BwsHdaDiagStan / BwsHdaDiagSnapshot / BwsHdaDiagMetryki / BwsHdaDiagStrumien"], ["a2: void* wyjscie"], ["a3: rozmiar_wyjscia"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5) for read-only; PRAWO_STEROWNIK|PRAWO_DEBUG for scan control", ["Reads HDA audio diagnostic state or steps controlled scan"], ["Caller holds PRAWO_GUI, writable output struct of exact/min size"], "1 on success, 0 on error"),
    (90, "BWS_HDA_TEST", "bws_hda_test", "jadro/bws/bws_nowoczesne.cpp", ["BwsHdaTestKonfig"], [], ["a2: const BwsHdaTestKonfig*"], ["a3: sizeof(BwsHdaTestKonfig)"], ["PRAWO_GUI"], "PZB_UZYTKOWNIK (<=4)", ["Runs controlled HDA playback test"], ["Caller holds PRAWO_GUI, valid BwsHdaTestKonfig"], "1 on success, 0 on error"),
    (91, "BWS_DIAGNOSTYKA_SYSTEMU", "diag_read_request", "jadro/bws/bws_nowoczesne.cpp", [], ["BwsDiagSnapshot"], ["a2: void* bufor", "a4: uint64_t* out_len"], ["a3: pojemnosc"], ["PRAWO_DEBUG"], "PZB_ZAUFANE (<=3)", ["Reads kernel diagnostic ring/metrics"], ["Authorized caller, writable buffer"], "1 on success, 0 on error"),
    (92, "BWS_BSF_CONFIG", "bsf_config_request", "jadro/bws/bws_nowoczesne.cpp", ["BsfConfigRequest"], ["BsfConfigResponse"], ["a2: void* bufor"], ["a1: op, a3: rozmiar"], ["PRAWO_SYSTEM_CONFIG"], "PZB_ZAUFANE (<=3)", ["Reads/updates BSF package configuration"], ["Caller holds PRAWO_SYSTEM_CONFIG, PZB <= PZB_ZAUFANE"], "1 on success, 0 on error"),
    (93, "BWS_GUI_TYTUL_ALFA", "skladacz_obrazu_ustaw_maske_tytulu", "jadro/bws/syscalls.cpp", ["uint8_t alpha_mask[]"], [], ["arg2: const uint8_t* maska"], ["arg1: w|h (w<=512, h<=128, bytes=w*h<=65536)"], ["PRAWO_GUI"], "PZB_PIASKOWNICA (<=5)", ["Sets per-pixel alpha mask for window title bar"], ["Caller holds PRAWO_GUI, w in [0,512], h in [0,128], readable mask buffer mapped in VMM"], "1 on success, 0 on error"),
]

assert len(BWS_SPECS) == 93, f"Expected 93 BWS specs, got {len(BWS_SPECS)}"

inventory_items = []
matrix_items = []

for spec in BWS_SPECS:
    (
        sys_id,
        enum_name,
        handler_name,
        source_file,
        in_structs,
        out_structs,
        ptr_args,
        len_args,
        req_perms,
        req_trust,
        side_effects,
        preconditions,
        failure_return,
    ) = spec

    inventory_items.append({
        "syscall_id": sys_id,
        "enum_name": enum_name,
        "handler_name": handler_name,
        "source_file": source_file,
        "input_structures": in_structs,
        "output_structures": out_structs,
        "pointer_arguments": ptr_args,
        "length_arguments": len_args,
        "required_permissions": req_perms,
        "required_trust_levels": req_trust,
        "side_effects": side_effects,
        "preconditions": preconditions,
        "failure_return": failure_return,
    })

    # Determine max PZB number allowed (0..5)
    max_pzb = 5
    if "<=2" in req_trust:
        max_pzb = 2
    elif "<=3" in req_trust:
        max_pzb = 3
    elif "<=4" in req_trust:
        max_pzb = 4

    pzb_decisions = {}
    for lvl in PZB_LEVELS:
        lid = lvl["id"]
        pzb_decisions[lvl["name"]] = (
            "ALLOW_IF_RIGHTS_PRESENT" if lid <= max_pzb else "DENY_TRUST_LEVEL_TOO_LOW"
        )

    matrix_items.append({
        "syscall_id": sys_id,
        "enum_name": enum_name,
        "handler_name": handler_name,
        "max_allowed_pzb_level": max_pzb,
        "required_rights_mask": req_perms,
        "pzb_level_decisions": pzb_decisions,
        "additional_identity_or_state_checks": req_trust,
    })

inventory_doc = {
    "schema_version": "1.0",
    "generated_at": "2026-11-04",
    "source_files": ["jadro/bws/bws_api.h", "jadro/bws/syscalls.cpp", "jadro/bws/bws_nowoczesne.cpp", "jadro/bws/pzb.h"],
    "total_public_syscalls": len(inventory_items),
    "debug_syscall_99_status": "Removed from production builds; gated behind #if defined(BURSZTYN_DEBUG_STACK_OVERFLOW_BWS) && BURSZTYN_DEBUG_STACK_OVERFLOW_BWS and operacja_systemowa_dozwolona(*proc)",
    "syscalls": inventory_items,
}

matrix_doc = {
    "schema_version": "1.0",
    "generated_at": "2026-11-04",
    "pzb_levels": PZB_LEVELS,
    "prawa_bits": PRAWA_BITS,
    "policy_invariants": [
        "High PZB trust level alone NEVER grants PRAWO_* capabilities automatically (independent checks).",
        "Unknown permission bits (~PRAWA_ZNANE) invalidate the process descriptor and deny all capability checks.",
        "Child processes spawned via BWS_URUCHOM_PROGRAM (10) cannot escalate PZB trust level or capability bits beyond caller policy.",
        "System operations (BWS 25, 26, 40, 42, 43) require operacja_systemowa_dozwolona (PZB <= PZB_ZAUFANE and PRAWO_SYSTEM_CONFIG).",
        "BWS_PULPIT_GOTOWY (88) and SYSTEM_IPC require PZB <= PZB_USLUGI, PRAWO_GUI | PRAWO_SYSTEM_CONFIG, and canonical path /menedzer_okien.bur.",
    ],
    "syscall_privilege_matrix": matrix_items,
}

(OUT_DIR / "bws-inventory.json").write_text(
    json.dumps(inventory_doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
)
(OUT_DIR / "bws-privilege-matrix.json").write_text(
    json.dumps(matrix_doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
)
print("[BWS-INVENTORY] Generated bws-inventory.json and bws-privilege-matrix.json (93 syscalls).")
