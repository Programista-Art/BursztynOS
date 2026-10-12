#!/usr/bin/env python3
"""Host packaging audit for Test dźwięku (.cebula) and VirtIO mouse profiles."""
import os
import re
import struct
import subprocess
import sys
import tempfile
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
fail = 0
pass_ = 0


def check(cond: bool, msg: str) -> None:
    global fail, pass_
    if cond:
        pass_ += 1
    else:
        fail += 1
        print(f"FAIL {msg}")


def verify_image() -> None:
    """Close the final BIN -> blob -> kernel -> ISO chain without booting it."""
    app = (ROOT / "test_dzwieku.bin").read_bytes()
    kernel = (ROOT / "system_operacyjny.bin").read_bytes()
    iso = ROOT / "BursztynOS.iso"
    with tempfile.TemporaryDirectory(prefix="test-dzwieku-package-") as directory:
        extracted = Path(directory) / "kernel"
        subprocess.run(["xorriso", "-osirrox", "on", "-indev", str(iso), "-extract",
                        "/boot/system_operacyjny.bin", str(extracted)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        check(extracted.read_bytes() == kernel, "ISO contains the final kernel byte for byte")
        check((ROOT / "isodir/boot/system_operacyjny.bin").read_bytes() == kernel,
              "staged kernel equals the final kernel")
        blob = Path(directory) / "app"
        subprocess.run(["objcopy", "-O", "binary", "--only-section=.data",
                        str(ROOT / "test_dzwieku_blob.o"), str(blob)], check=True)
        check(blob.read_bytes() == app, "final package blob contains the tested executable")
    valid = len(kernel) >= 64 and kernel[:6] == b"\x7fELF\x02\x01"
    if valid:
        phoff = struct.unpack_from("<Q", kernel, 32)[0]
        phent, phnum = struct.unpack_from("<HH", kernel, 54)
        valid = phent == 56 and phoff <= len(kernel) and phnum <= (len(kernel) - phoff) // phent
    check(valid, "kernel ELF64 program header table is complete")
    if not valid:
        return
    symbols = subprocess.check_output(["nm", "--defined-only", "system_operacyjny.bin"], cwd=ROOT, text=True)
    found = {name: int(address, 16) for address, name in re.findall(
        r"^([0-9a-fA-F]+) [A-Za-z] (_binary_test_dzwieku_bin_(?:start|end))$", symbols, re.M)}
    start, end = found.get("_binary_test_dzwieku_bin_start"), found.get("_binary_test_dzwieku_bin_end")
    valid = start is not None and end is not None and end >= start and end - start == len(app)
    check(valid, "actual boot blob symbol range has the tested executable size")
    if not valid:
        return
    embedded = None
    for i in range(phnum):
        kind, _, off, va, _, stored, _, _ = struct.unpack_from("<IIQQQQQQ", kernel, phoff + i * phent)
        if kind == 1 and start >= va and end - va <= stored and off + stored <= len(kernel):
            embedded = kernel[off + start - va:off + end - va]
            break
    check(embedded == app, "kernel boot installer uses the exact executable tested on PSF")
    check(b"[TEST DZWIEKU]" not in app and b"[TEST DZWIEKU]" not in kernel,
          "temporary app startup tracing is absent from final production kernel and BUR")
    print("Final ISO SHA-256:", hashlib.sha256(iso.read_bytes()).hexdigest())
    print("Final BUR SHA-256:", hashlib.sha256(app).hexdigest())


def main() -> int:
    # --- source / path matching ---
    desktop = (ROOT / "desktop_entries.h").read_text(encoding="utf-8", errors="replace")
    kernel = (ROOT / "kernel.cpp").read_text(encoding="utf-8", errors="replace")
    psf = (ROOT / "psf_bootstrap.h").read_text(encoding="utf-8", errors="replace")
    wm = (ROOT / "menedzer_okien.cpp").read_text(encoding="utf-8", errors="replace")

    pkg = "/programy/test_dzwieku.cebula"
    exe = "test_dzwieku.bur"
    full = f"{pkg}/{exe}"

    check(full in desktop, f"desktop_entries.h launcher path {full}")
    check(full in kernel, f"kernel.cpp install path {full}")
    check(full in wm, f"menedzer_okien.cpp SCIEZKA {full}")
    check(pkg in psf, f"psf_bootstrap.h directory {pkg}")
    check(f'"{exe}"' in kernel or exe in kernel, "kernel manifest plik_startowy")
    check("Test dźwięku" in desktop and "Test dźwięku" in wm, "display name consistency")
    # case-sensitive exact match of directory name
    check("test_dzwieku.cebula" in desktop and "test_dzwieku.cebula" in kernel,
          "case-sensitive package directory")
    desktop_paths = re.findall(r'\{"Test dźwięku","([^"]+)"', desktop)
    check(desktop_paths == [full], "desktop launcher has exactly the canonical executable path")
    start_path = re.search(r'SCIEZKA_TEST_DZWIEKU\s*=\s*"([^"]+)"', wm)
    check(start_path is not None and start_path[1] == full, "Start menu constant has exact case and spelling")
    check('uruchom_lub_aktywuj(SCIEZKA_TEST_DZWIEKU, "Test dźwięku")' in wm,
          "Start menu launches its canonical executable constant")

    # --- BWS numbers ---
    api = (ROOT / "bws_api.h").read_text(encoding="utf-8", errors="replace")
    check("BWS_HDA_DIAG = 89" in api, "BWS_HDA_DIAG == 89")
    check("BWS_HDA_TEST = 90" in api, "BWS_HDA_TEST == 90")
    check("BWS_PIERWSZY_WOLNY = 91" in api, "next free is 91")

    # --- built artifact ---
    blob = ROOT / "test_dzwieku.bin"
    if blob.exists():
        data = blob.read_bytes()
        check(len(data) > 0, "test_dzwieku.bin nonzero")
        check(data[:4] == b"BUR\x00", "BUR magic")
        check(b"[TEST DZWIEKU]" not in data, "temporary startup logging absent from production BUR")
        if len(data) >= 60:
            entry, toff, tsz, tva, doff, dsz, dva = struct.unpack_from("<QQQQQQQ", data, 4)
            check(tsz > 0 and dsz > 0, "segment sizes nonzero")
            check(toff == 0x1000, f"text offset 0x1000 (got {toff:#x})")
            check(doff == toff + tsz, "data offset is contiguous after text")
            check(tva == 0x1001000, f"text VA (got {tva:#x})")
            check(dva == tva + tsz, "data VA contiguous")
            check(tsz % 4096 == 0 and dsz % 4096 == 0, "segment sizes 4KiB aligned")
            stored = min(dsz, max(0, len(data) - doff))
            check(toff + tsz <= len(data) and doff <= len(data), "all file-backed segment ranges present")
            check(0 < stored < dsz, "DATA stores initializers; BSS is implicit zero-fill")
            check(len(data) <= 524288, f"app budget: actual={len(data)} maximum=524288")
            check(tva <= entry < tva + tsz, "entry inside executable text")
            print(f"BUR file={len(data)} text={tsz} initialized_data={stored} zero_fill={dsz-stored} mapped={tsz+dsz}")
    else:
        check(False, "test_dzwieku.bin missing (build it first)")

    # --- QEMU profiles keep virtio-mouse-pci ---
    mk = (ROOT / "Makefile").read_text(encoding="utf-8", errors="replace")
    check("virtio-mouse-pci" in mk, "Makefile uses virtio-mouse-pci")
    check("virtio-tablet-pci" not in mk.split("QEMU_VIRTIO_COMMON_ARGS")[1].split("QEMU_")[0]
          if "QEMU_VIRTIO_COMMON_ARGS" in mk else True,
          "canonical virtio profile has no tablet workaround")
    check("tcg,thread=single" in mk, "tcg thread single preserved")

    app = (ROOT / "programy/test_dzwieku/test_dzwieku.cpp").read_text(encoding="utf-8")
    check('Okno("Test dźwięku"' in app and 'AplikacjaGUI app' in app, "native BursztynUI window/application")
    for control in ["PanelDokowany", "PasekInformacyjny", "ListaRozwijana", "ListaWirtualna", "Przycisk", "Siatka"]:
        check(control in app, f"retained control {control}")
    check("BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1" in app, "system Unicode font opt-in")
    check("CzcionkaUI::postep_znaku" in app, "report wrap uses measured Unicode advances")
    strings = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', app)
    check(all(ord(c) < 1170 for text in strings for c in text), "visible app literals use glyphs available in the system Unicode font")
    check(not re.search(r'0x(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\b', app), "no app-local RGB palette")
    check("gui_rysuj_prostokat" not in app and "gui_wypisz_tekst" not in app, "no legacy manual interface")
    for label in ["Test dźwięku", "Kontroler", "Kodeki", "Widgety", "Ścieżki audio", "Testy", "Strumień", "Raport",
                  "Odśwież", "Zapisz raport", "Głośniki", "Lewy kanał", "Prawy kanał", "Oba kanały", "Błąd",
                  "Brak kontrolera HDA", "Skanowanie kodeków", "Skanowanie widgetów", "Gotowe"]:
        check(label in app, f"UTF-8 Polish label: {label}")
    check('"/dane/diagnostyka/hda.txt"' in app, "canonical report path")
    symbols = __import__("subprocess").check_output(["nm", "test_dzwieku.elf"], cwd=ROOT, text=True)
    check("nowa_czcionka_16x16" not in symbols and "nowa_czcionka_szerokosci" not in symbols,
          "no embedded extronic16Bunicode font data")
    check(re.search(r'_start$', symbols, re.M) is not None, "linked Ring3 entry exists")

    if "--image" in sys.argv:
        verify_image()
    print(f"packaging-audit: {pass_} pass, {fail} fail")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
