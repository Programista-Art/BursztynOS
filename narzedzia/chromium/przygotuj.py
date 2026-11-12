#!/usr/bin/env python3
"""
Bursztyn OS - przygotowanie drzewa zrodel Chromium do cross-buildu.

Ten skrypt NIE kompiluje Chromium (to robi GN/Ninja na hoscie z depot_tools).
Wykonuje trzy kroki, ktore musza byc deterministyczne i wersjonowane:

  1. pobiera zrodla Chromium na przypietym tagu (nie `main`),
  2. kopiuje backend Ozone (narzedzia/chromium/ozone_*) do drzewa Chromium,
  3. kopiuje toolchain GN + args.gn i wypisuje nastepne polecenia.

Swiat zewnetrzny jest izolowany: wszystko trafia do `zewnetrzne/chromium/`,
ktory jest w .gitignore. Repozytorium Bursztyn pozostaje bez wendorowanych
setek megabajtow zrodel.

Wymagania: git, python3, depot_tools w PATH (gclient, gn, autoninja).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NARZEDZIA = ROOT / "narzedzia" / "chromium"
CEL = ROOT / "zewnetrzne" / "chromium"

# Przypiety tag. Zmiana wymaga zmiany tej stalej i przetestowania backendu:
# backend Ozone korzysta ze stabilnych API, ale nie zakladamy zgodnosci
# w przod.
CHROMIUM_TAG = "141.0.7390.65"

# Pliki backendu kopiowane do drzewa Chromium.
BACKEND_PLIKI = [
    "posix_bws.h",
    "posix_bws.cc",
]


def run(cmd, cwd=None, dry=False):
    print("  $", " ".join(str(c) for c in cmd))
    if dry:
        return
    res = subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None)
    if res.returncode != 0:
        raise SystemExit(f"Polecenie nie powiodlo sie: {' '.join(map(str, cmd))}")


def sprawdz_narzedzia():
    brakujace = [n for n in ("git", "python3") if shutil.which(n) is None]
    if brakujace:
        raise SystemExit(f"Brak narzedzi: {', '.join(brakujace)}")
    if shutil.which("gn") is None:
        print("[PRZYGOTUJ] UWAGA: `gn` nie jest w PATH - build nie ruszy.")
        print("[PRZYGOTUJ] Dodaj depot_tools: export PATH=\"$PWD/depot_tools:$PATH\"")


def pobierz_zrodla(dry: bool):
    if CEL.exists() and (CEL / ".git").exists():
        print(f"[PRZYGOTUJ] Zrodla juz sa w {CEL}, pomijam pobieranie.")
        return
    print(f"[PRZYGOTUJ] Pobieranie Chromium {CHROMIUM_TAG} do {CEL}")
    CEL.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", "--depth", "1", "--branch", CHROMIUM_TAG,
         "https://chromium.googlesource.com/chromium/src.git", CEL], dry=dry)


def zainstaluj_backend(dry: bool):
    print("[PRZYGOTUJ] Instalowanie backendu Ozone (platforma bursztyn)")
    cel = CEL / "ui" / "ozone" / "platform" / "bursztyn"
    bws = CEL / "third_party" / "bursztyn"
    if not dry:
        cel.mkdir(parents=True, exist_ok=True)
        bws.mkdir(parents=True, exist_ok=True)

    pliki = {
        NARZEDZIA / "ozone_BUILD.gn": cel / "BUILD.gn",
        NARZEDZIA / "ozone_platform_bursztyn.h": cel / "ozone_platform_bursztyn.h",
        NARZEDZIA / "ozone_platform_bursztyn.cc": cel / "ozone_platform_bursztyn.cc",
        NARZEDZIA / "platform_window_bursztyn.h": cel / "platform_window_bursztyn.h",
        NARZEDZIA / "platform_window_bursztyn.cc": cel / "platform_window_bursztyn.cc",
        NARZEDZIA / "surface_factory_bursztyn.h": cel / "surface_factory_bursztyn.h",
        NARZEDZIA / "surface_factory_bursztyn.cc": cel / "surface_factory_bursztyn.cc",
        NARZEDZIA / "surface_ozone_bursztyn.h": cel / "surface_ozone_bursztyn.h",
        NARZEDZIA / "surface_ozone_bursztyn.cc": cel / "surface_ozone_bursztyn.cc",
        NARZEDZIA / "platform_event_source_bursztyn.h": cel / "platform_event_source_bursztyn.h",
        NARZEDZIA / "platform_event_source_bursztyn.cc": cel / "platform_event_source_bursztyn.cc",
        NARZEDZIA / "client_native_pixmap_factory_bursztyn.h": cel / "client_native_pixmap_factory_bursztyn.h",
        NARZEDZIA / "client_native_pixmap_factory_bursztyn.cc": cel / "client_native_pixmap_factory_bursztyn.cc",
    }
    for src, dst in pliki.items():
        if not src.exists():
            raise SystemExit(f"Brak pliku zrodlowego backendu: {src}")
        print(f"  {src.name} -> {dst.relative_to(CEL)}")
        if not dry:
            shutil.copy2(src, dst)

    for nazwa in BACKEND_PLIKI:
        src = NARZEDZIA / nazwa
        print(f"  {nazwa} -> third_party/bursztyn/{nazwa}")
        if not dry:
            shutil.copy2(src, bws / nazwa)

    args = CEL / "out" / "bursztyn"
    if not dry:
        args.mkdir(parents=True, exist_ok=True)
        shutil.copy2(NARZEDZIA / "args.gn", args / "args.gn")


def wypisz_nastepne_kroki():
    print()
    print("=" * 68)
    print("Nastepne kroki (na hoscie z depot_tools, nie w tym repozytorium):")
    print("=" * 68)
    print(f"  export PATH=\"{ROOT}/depot_tools:$PATH\"")
    print(f"  cd {CEL}")
    print("  gclient sync --with_branch_heads --with_tags")
    print("  gn gen out/bursztyn --root=.")
    print("  autoninja -C out/bursztyn headless_shell")
    print()
    print("Artefakt: out/bursztyn/headless_shell (ELF64 statyczny).")
    print("Sprawdzenie backendu bez buildu:")
    print(f"  python3 {NARZEDZIA}/weryfikuj_backend.py")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dry-run", action="store_true",
                    help="wypisz plan bez pobierania i kopiowania")
    ap.add_argument("--skip-fetch", action="store_true",
                    help="nie pobieraj zrodel (uzyj istniejacego drzewa)")
    a = ap.parse_args()

    sprawdz_narzedzia()
    if not a.skip_fetch:
        pobierz_zrodla(a.dry_run)
    zainstaluj_backend(a.dry_run)
    wypisz_nastepne_kroki()
    return 0


if __name__ == "__main__":
    sys.exit(main())
