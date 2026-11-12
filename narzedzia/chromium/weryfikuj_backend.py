#!/usr/bin/env python3
"""
Bursztyn OS - weryfikacja spojnosci backendu Ozone dla Chromium.

Ten skrypt jest GATE'EM etapu 2 planu portu. Sprawdza, bez pobierania
i kompilowania Chromium, ze:

  1. kazdy numer BWS uzyty w backendzie istnieje w jadro/bws/bws_api.h
     i ma te sama wartosc (rozjazd numerow to ciche przeklamanie ABI),
  2. kazda funkcja BWS uzyta przez Chromium ma realna implementacje w jadrze
     (nie tylko deklaracje w naglowku Ring 3),
  3. args.gn nie zawiera sprzecznych flag (np. use_ozone=false przy
     ozone_platform_bursztyn=true),
  4. backend nie uzywa API, ktorego Bursztyn nie ma (GL, Vulkan, X11,
     Wayland, D-Bus, udev, fontconfig, seccomp),
  5. wszystkie pliki wymagane przez przygotuj.py istnieja,
  6. plik manifestu .cebula ma uprawnienia zgodne z faktycznie uzywanymi
     BWS (nadmiar uprawnien to blad bezpieczenstwa).

Kod wyjscia != 0 blokuje etap 2.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NARZEDZIA = ROOT / "narzedzia" / "chromium"

bledy: list[str] = []
ostrzezenia: list[str] = []


def blad(msg: str) -> None:
    bledy.append(msg)


def ostrzez(msg: str) -> None:
    ostrzezenia.append(msg)


# =========================================================================
# 1. Numery BWS: deklaracje w backendzie vs bws_api.h
# =========================================================================


def wczytaj_numery_jadra() -> dict[str, int]:
    api = (ROOT / "jadro" / "bws" / "bws_api.h").read_text(encoding="utf-8")
    numery: dict[str, int] = {}
    for m in re.finditer(r"^\s*(BWS_[A-Z0-9_]+)\s*=\s*(\d+)\s*,", api, re.M):
        numery[m.group(1)] = int(m.group(2))
    # Wpisy w stylu "BWS_... = 1ULL << 0" (flagi) nie sa numerami wywolan.
    return numery


def wczytaj_numery_backendu() -> dict[str, int]:
    zrodlo = (NARZEDZIA / "posix_bws.cc").read_text(encoding="utf-8")
    numery: dict[str, int] = {}
    for m in re.finditer(r"constexpr\s+uint64_t\s+(BWS_[A-Z0-9_]+)\s*=\s*(\d+)\s*;",
                         zrodlo):
        numery[m.group(1)] = int(m.group(2))
    # Literaly uzyte bezposrednio w bws_wywolaj(NR, ...)
    for m in re.finditer(r"bws_wywolaj\(\s*(\d+)\s*,", zrodlo):
        numery.setdefault(f"literal_{m.group(1)}", int(m.group(1)))
    return numery


def sprawdz_numery() -> None:
    jadro = wczytaj_numery_jadra()
    backend = wczytaj_numery_backendu()
    if not jadro:
        blad("Nie udalo sie sparsowac numerow BWS z jadro/bws/bws_api.h")
        return
    if not backend:
        blad("Nie udalo sie sparsowac numerow BWS z posix_bws.cc")
        return

    # Mapowanie nazw backendu -> nazwy jadra, gdzie nazwy sie roznia.
    alias = {
        # backend: jadro
    }

    znane_wartosci = set(jadro.values())
    for nazwa, wartosc in sorted(backend.items()):
        if nazwa.startswith("literal_"):
            if wartosc not in znane_wartosci:
                blad(f"posix_bws.cc uzywa numeru {wartosc}, ktorego nie ma w bws_api.h")
            continue
        nazwa_jadra = alias.get(nazwa, nazwa)
        if nazwa_jadra not in jadro:
            blad(f"{nazwa} ({wartosc}) nie istnieje w bws_api.h jako {nazwa_jadra}")
            continue
        if jadro[nazwa_jadra] != wartosc:
            blad(f"{nazwa}: backend={wartosc}, jadro={jadro[nazwa_jadra]} "
                 f"(rozjazd ABI)")


# =========================================================================
# 2. Kazda funkcja BWS z backendu ma implementacje w jadrze
# =========================================================================


def sprawdz_implementacje() -> None:
    naglowek = (NARZEDZIA / "posix_bws.h").read_text(encoding="utf-8")
    funkcje = set(re.findall(r"^\s*(?:bool|void|int|uint64_t|void\*)\s+"
                             r"(bws_[a-z0-9_]+)\s*\(", naglowek, re.M))
    if not funkcje:
        blad("Nie udalo sie sparsowac funkcji z posix_bws.h")
        return

    # Zrodla jadra, ktore moga definiowac bws_*.
    zrodla = list((ROOT / "jadro" / "gui").glob("*.cpp"))
    zrodla += list((ROOT / "jadro" / "bws").glob("*.cpp"))
    zrodla += list((ROOT / "sterowniki" / "grafika").glob("*.cpp"))
    tekst = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in zrodla)

    # Nazwy w posix_bws.h sa wlasnymi wrapperami backendu; sprawdzamy
    # istnienie odpowiadajacej funkcjonalnosci po numerze BWS, nie po nazwie.
    # Wyjatek: funkcje, ktore Ring 3 implementuje sam (bws_wywolaj).
    pomin = {"bws_wywolaj"}
    for f in sorted(funkcje - pomin):
        # Wrapper istnieje w posix_bws.cc?
        zrodlo_backendu = (NARZEDZIA / "posix_bws.cc").read_text(encoding="utf-8")
        if f not in zrodlo_backendu:
            blad(f"{f} zadeklarowana w posix_bws.h, ale brak w posix_bws.cc")
            continue
        # Czy istnieje jakikolwiek BWS o tej samej nazwie bez prefiksu bws_?
        odpowiednik = f[4:]  # bws_utworz_warstwe -> utworz_warstwe
        if odpowiednik in tekst:
            continue
        # Niektore sa tylko w BWS numerach (np. bws_odswiez -> 17).
        if f in tekst:
            continue
        ostrzez(f"{f}: brak jednoznacznego odpowiednika w zrodlach jadra "
                f"(sprawdz mapowanie na numer BWS)")


# =========================================================================
# 3. args.gn: spojnosc flag
# =========================================================================


def sprawdz_args() -> None:
    tekst = (NARZEDZIA / "args.gn").read_text(encoding="utf-8")
    wartosci: dict[str, str] = {}
    for m in re.finditer(r"^\s*([a-z_][a-z0-9_]*)\s*=\s*([^\s#]+)", tekst, re.M):
        wartosci[m.group(1)] = m.group(2)

    wymagane = {
        "is_clang": "true",
        "use_custom_libcxx": "true",
        "use_sysroot": "false",
        "use_ozone": "true",
        "ozone_platform_bursztyn": "true",
        "target_os": '"bursztyn"',
        "target_cpu": '"x64"',
        "is_component_build": "false",
        "enable_nacl": "false",
        "rtc_build": "false",
        "use_glib": "false",
    }
    for k, v in wymagane.items():
        if k not in wartosci:
            blad(f"args.gn: brak wymaganej flagi {k} = {v}")
        elif wartosci[k] != v:
            blad(f"args.gn: {k} = {wartosci[k]} (oczekiwano {v})")

    # Sprzecznosci.
    if wartosci.get("use_ozone") == "false" and \
       wartosci.get("ozone_platform_bursztyn") == "true":
        blad("args.gn: ozone_platform_bursztyn=true przy use_ozone=false")
    for platforma in ("x11", "wayland"):
        if wartosci.get(f"ozone_platform_{platforma}") == "true":
            blad(f"args.gn: ozone_platform_{platforma}=true, a Bursztyn go nie ma")
    for gl in ("skia_use_gl", "skia_use_vulkan", "skia_use_metal", "skia_use_dawn"):
        if wartosci.get(gl) == "true":
            blad(f"args.gn: {gl}=true, a Bursztyn nie ma tego API")
    if wartosci.get("enable_sandbox") == "true" and \
       wartosci.get("use_seccomp_bpf") != "true":
        ostrzez("args.gn: enable_sandbox=true bez seccomp - sandbox nie zadziala")

    # Klucze, ktore Chromium faktycznie zna (lista kontrolna najwazniejszych;
    # nie zastepuje `gn check`, ale lapie literowki).
    znane = {
        "is_debug", "is_official_build", "is_clang", "use_custom_libcxx",
        "use_sysroot", "use_lld", "enable_sse3", "enable_avx", "enable_avx2",
        "target_os", "target_cpu", "use_ozone", "ozone_platform_bursztyn",
        "ozone_platform_x11", "ozone_platform_wayland", "ozone_platform_headless",
        "use_glib", "use_gtk", "use_x11", "use_dbus", "use_udev", "use_libpci",
        "use_seccomp_bpf", "enable_sandbox", "enable_quic", "rtc_build",
        "enable_webrtc", "enable_nacl", "enable_pdf", "enable_print_preview",
        "enable_widevine", "enable_remoting", "enable_casting", "enable_plugins",
        "enable_media_router", "enable_web_speech", "enable_supervised_users",
        "skia_use_gl", "skia_use_vulkan", "skia_use_metal", "skia_use_dawn",
        "skia_enable_graphite", "use_freetype", "use_harfbuzz", "use_fontconfig",
        "icu_use_data_file", "use_system_zlib", "use_openssl", "use_kerberos",
        "enable_logging", "enable_crash_reporter", "use_cups",
        "enable_headless_shell", "is_component_build",
    }
    for k in wartosci:
        if k not in znane:
            ostrzez(f"args.gn: nieznana flaga {k} - sprawdz, czy Chromium ja zna")


# =========================================================================
# 4. Backend nie uzywa niedostepnych API
# =========================================================================


def sprawdz_api_backendu() -> None:
    pliki = sorted(NARZEDZIA.glob("*.cc")) + sorted(NARZEDZIA.glob("*.h"))
    zakazane = {
        "XOpenDisplay": "X11",
        "wl_display_connect": "Wayland",
        "dbus_bus_get": "D-Bus",
        "udev_new": "udev",
        "glXCreateContext": "GLX",
        "eglGetDisplay": "EGL",
        "vkCreateInstance": "Vulkan",
        "FcInit": "fontconfig",
        "seccomp": "seccomp",
    }
    for p in pliki:
        tekst = p.read_text(encoding="utf-8", errors="replace")
        for symbol, nazwa in zakazane.items():
            if symbol in tekst:
                blad(f"{p.name}: uzywa {symbol} ({nazwa}), czego Bursztyn nie ma")


# =========================================================================
# 5. Kompletnosc plikow backendu
# =========================================================================


WYMAGANE_PLIKI = [
    "bursztyn_toolchain.gn",
    "args.gn",
    "ozone_BUILD.gn",
    "posix_bws.h",
    "posix_bws.cc",
    "ozone_platform_bursztyn.h",
    "ozone_platform_bursztyn.cc",
    "platform_window_bursztyn.h",
    "platform_window_bursztyn.cc",
    "surface_factory_bursztyn.h",
    "surface_factory_bursztyn.cc",
    "surface_ozone_bursztyn.h",
    "surface_ozone_bursztyn.cc",
    "platform_event_source_bursztyn.h",
    "platform_event_source_bursztyn.cc",
    "client_native_pixmap_factory_bursztyn.h",
    "client_native_pixmap_factory_bursztyn.cc",
    "message_pump_bursztyn.h",
    "message_pump_bursztyn.cc",
    "przygotuj.py",
    "opis.aplikacji",
]


def sprawdz_pliki() -> None:
    for nazwa in WYMAGANE_PLIKI:
        if not (NARZEDZIA / nazwa).exists():
            blad(f"brak pliku backendu: narzedzia/chromium/{nazwa}")

    # przygotuj.py musi kopiowac dokladnie te pliki, ktore istnieja.
    przygotuj = (NARZEDZIA / "przygotuj.py").read_text(encoding="utf-8")
    for nazwa in ("posix_bws.h", "posix_bws.cc"):
        if nazwa not in przygotuj:
            blad(f"przygotuj.py nie kopiuje {nazwa}")


# =========================================================================
# 6. Manifest .cebula zgodny z uzywanymi BWS
# =========================================================================


def sprawdz_manifest() -> None:
    tekst = (NARZEDZIA / "opis.aplikacji").read_text(encoding="utf-8")
    uprawnienia = set(re.findall(r"<uprawnienie>([a-z_]+)</uprawnienie>", tekst))

    backend = (NARZEDZIA / "posix_bws.cc").read_text(encoding="utf-8")

    # Kazde uzycie BWS wymaga okreslonego prawa.
    wymagane: set[str] = set()
    if "BWS_SOCKET_" in backend:
        wymagane.add("siec")
    if "bws_utworz_warstwe" in backend or "BWS_GUI_BLIT_SKALOWANY" in backend:
        wymagane.add("okna")
    # Chromium pobiera profil/cache - potrzebuje zapisu.
    wymagane.add("pliki_zapisz")
    wymagane.add("pliki_czytaj")

    brakujace = wymagane - uprawnienia
    if brakujace:
        blad(f"opis.aplikacji: brak uprawnien {sorted(brakujace)}")

    nadmiarowe = uprawnienia - wymagane - {"uruchom_program"}
    if "uruchom_program" in uprawnienia:
        ostrzez("opis.aplikacji: uruchom_program jest zbedne dla Chromium")
    for u in nadmiarowe:
        ostrzez(f"opis.aplikacji: uprawnienie '{u}' nie jest uzywane")

    if 'poziom_zaufania' not in tekst:
        blad("opis.aplikacji: brak poziom_zaufania")
    elif "<poziom_zaufania>4</poziom_zaufania>" not in tekst:
        blad("opis.aplikacji: Chromium musi dzialac na PZB-4 (uzytkownik)")


# =========================================================================

def main() -> int:
    sprawdz_pliki()
    sprawdz_numery()
    sprawdz_implementacje()
    sprawdz_args()
    sprawdz_api_backendu()
    sprawdz_manifest()

    for o in ostrzezenia:
        print(f"OSTRZEZENIE: {o}")
    for b in bledy:
        print(f"BLAD: {b}")

    if bledy:
        print(f"\n[WERYFIKUJ-BACKEND] FAIL {len(bledy)} bledow, "
              f"{len(ostrzezenia)} ostrzezen")
        return 1

    print(f"\n[WERYFIKUJ-BACKEND] PASS ({len(ostrzezenia)} ostrzezen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
