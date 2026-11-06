#!/usr/bin/env python3
"""Bursztyn OS — hostowa weryfikacja GUI / ABI aplikacji Aktualizacje (OTA).

Zakres (statyczny, szybki gate bez QEMU):
  1. ABI BWS OTA (84/85) i sizeof(BwsOtaStatus)==384.
  2. Polecenia / stany / kody bledow w bws_ota.h.
  3. Integracja aplikacji Ring 3 (include, wywolania BWS_OTA_*).
  4. Manifest opis.aplikacji (uprawnienia fail-closed).
  5. Rejestracja pulpitu / launchera.
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

zdane = 0
zlane = 0


def raport(nazwa: str, ok: bool, szczegoly: str = "") -> None:
    global zdane, zlane
    if ok:
        zdane += 1
        print("PASS %s" % nazwa)
    else:
        zlane += 1
        msg = (" (%s)" % szczegoly) if szczegoly else ""
        print("FAIL %s%s" % (nazwa, msg))


def read(rel: str) -> str:
    path = os.path.join(REPO, rel)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def test_bws_api() -> None:
    api = read("jadro/bws/bws_api.h")
    raport("bws_ota_status_84", "BWS_OTA_STATUS = 84" in api)
    raport("bws_ota_polecenie_85", "BWS_OTA_POLECENIE = 85" in api)


def test_bws_ota_abi() -> None:
    ota = read("jadro/bws/bws_ota.h")
    raport("ota_abi_wersja_1", "BWS_OTA_ABI_WERSJA 1U" in ota)
    raport(
        "ota_status_rozmiar_384",
        'static_assert(sizeof(BwsOtaStatus) == 384' in ota,
    )
    for cmd, num in (
        ("BWS_OTA_CMD_SPRAWDZ", 1),
        ("BWS_OTA_CMD_POBIERZ", 2),
        ("BWS_OTA_CMD_INSTALUJ", 3),
        ("BWS_OTA_CMD_REBOOT", 4),
        ("BWS_OTA_CMD_POTWIERDZ_ROZRUCH", 5),
        ("BWS_OTA_CMD_RESETUJ_STAN", 6),
    ):
        pat = re.compile(r"%s\s*=\s*%d\b" % (cmd, num))
        raport("ota_cmd_%s" % cmd.lower(), bool(pat.search(ota)))

    for blad in (
        "BWS_OTA_BLAD_BRAK_UPRAWNIEN",
        "BWS_OTA_BLAD_PODPISU",
        "BWS_OTA_BLAD_SHA256",
        "BWS_OTA_BLAD_WERSJI",
        "BWS_OTA_BLAD_SIECI",
    ):
        raport("ota_blad_%s" % blad.lower(), blad in ota)


def test_aplikacja_ring3() -> None:
    src = read("programy/aktualizacje/aktualizacje.cpp")
    raport("aktualizacje_include_bws_ota", '#include "bws_ota.h"' in src or '#include "../../bws_ota.h"' in src)
    raport("aktualizacje_pobierz_status", "bws_ota_pobierz_status" in src)
    raport("aktualizacje_wykonaj_polecenie", "bws_ota_wykonaj_polecenie" in src)
    for cmd in (
        "BWS_OTA_CMD_SPRAWDZ",
        "BWS_OTA_CMD_POBIERZ",
        "BWS_OTA_CMD_INSTALUJ",
        "BWS_OTA_CMD_REBOOT",
    ):
        raport("aktualizacje_uzywa_%s" % cmd.lower(), cmd in src)


def test_manifest() -> None:
    opis = read("programy/aktualizacje/opis.aplikacji")
    raport("manifest_nazwa", "Aktualizacje" in opis)
    raport("manifest_start_bur", 'plik_startowy = "aktualizacje.bur"' in opis)
    for prawo in ("okna", "pliki_czytaj", "pliki_zapisz", "system_config", "siec"):
        raport("manifest_prawo_%s" % prawo, ('"%s"' % prawo) in opis)
    raport("manifest_poziom_zaufania_ograniczony", re.search(r"poziom_zaufania\s*=\s*[1-4]\b", opis) is not None)


def test_desktop() -> None:
    desktop = read("jadro/gui/desktop_entries.h")
    raport(
        "desktop_wpis_aktualizacje",
        "/programy/aktualizacje.cebula/aktualizacje.bur" in desktop,
    )
    raport("desktop_typ_ikony", "TypIkonyAplikacji::Aktualizacje" in desktop)


def test_linker_presence() -> None:
    path = os.path.join(REPO, "programy", "aktualizacje", "aktualizacje_linker.ld")
    raport("aktualizacje_linker_ld", os.path.isfile(path))


def main() -> int:
    print("== Bursztyn OS: test GUI/ABI Aktualizacje (host) ==")
    test_bws_api()
    test_bws_ota_abi()
    test_aplikacja_ring3()
    test_manifest()
    test_desktop()
    test_linker_presence()
    print("PODSUMOWANIE: zdane=%d, zlane=%d" % (zdane, zlane))
    return 1 if zlane else 0


if __name__ == "__main__":
    sys.exit(main())
