#!/usr/bin/env python3
"""Deterministic native-code regression suite; never starts a VM."""
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".hda-test" / "repair"


def function(source, name):
    match = re.search(r'^(?:extern "C" |static )?(?:__attribute__\(\(noinline\)\) )?(?:bool|size_t|uint64_t|uint8_t|void\*|void) ' + name + r"\s*\(", source, re.M)
    if not match:
        raise RuntimeError(f"missing production function: {name}")
    start = source.index("{", match.start())
    depth = 1
    pos = start + 1
    # These particular functions contain no braces inside string literals.
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]


def extract_boot_install():
    """Compile the real boot installer unchanged against the real PSF."""
    kernel = (ROOT / "kernel.cpp").read_text()
    globals_ = kernel[kernel.index("bool scisla_instalacja="):
                      kernel.index("bool sciezka_ma_poczatek(")]
    names = ["dlugosc_tekstu_u32", "pobierz_rozmiar_bloba", "zapisz_plik_od_nowa_impl",
             "zapisz_plik_od_nowa", "zapisz_blob_bur", "zapisz_manifest", "instaluj_paczke"]
    manifest = re.search(r"static const char MANIFEST_TEST_DZWIEKU\[\] =.*?;", kernel, re.S)[0]
    call = re.search(r'instaluj_paczke\(\s*"/programy/test_dzwieku\.cebula/opis\.aplikacji".*?\)',
                     kernel, re.S)[0]
    call = call.replace("_binary_test_dzwieku_bin_start", "start").replace(
        "_binary_test_dzwieku_bin_end", "end")
    (OUT / "package_actual.inc").write_text(globals_ + "\n" + "\n".join(
        function(kernel, name) for name in names) + "\n" + manifest +
        "\nstatic bool boot_install(const uint8_t* start, const uint8_t* end) { return " + call + "; }\n")


def extract_filesystem_bws():
    source = (ROOT / "syscalls.cpp").read_text()
    helpers = ["znane_prawa", "proces_ma_prawo", "dlugosc_stringa_limit", "pobierz_string_user",
               "pobierz_sciezke_pliku", "segment_prefiksu", "sciezka_chroniona",
               "proces_moze_modyfikowac_sciezke", "poziom_pliku_pozwala_modyfikowac",
               "pobierz_rodzica_sciezki", "efektywny_pzb_dla_nowego", "proces_moze_utworzyc_w_sciezce",
               "proces_moze_modyfikowac_obiekt", "wyzeruj", "alokuj_bufor"]
    buffer_class = source[source.index("class BuforKernelowy {"):source.index("\n};", source.index("class BuforKernelowy {")) + 3]
    handlers = ["bws_utworz_plik", "bws_zapisz_plik", "bws_pobierz_metadane", "bws_utworz_katalog"]
    (OUT / "filesystem_actual.inc").write_text(
        "constexpr size_t MAX_SCIEZKA_PLIKU_BWS = 512;\n" +
        "\n".join(function(source, name) for name in helpers) + "\n" + buffer_class + "\n" +
        "\n".join(function(source, name) for name in handlers))


def extract_checked_vmm():
    source = (ROOT / "vmm.cpp").read_text()
    (OUT / "vmm_actual.inc").write_text("\n".join(function(source, name) for name in
        ("adres_kanoniczny", "adres_user_kanoniczny", "VmmMapujNowaStroneProcesu")))


def run(cmd, env=None):
    print("+", " ".join(str(x) for x in cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True, env=env)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    extract_boot_install()
    extract_filesystem_bws()
    extract_checked_vmm()
    run(["python3", "narzedzia/test_dzwieku_size.py", "test_dzwieku.bin"])
    from test_dzwieku_size import APP_MAXIMUM, verify
    negative = OUT / "one-byte-over-budget.bur"
    negative.write_bytes((ROOT / "test_dzwieku.bin").read_bytes().ljust(APP_MAXIMUM + 1, b"\0"))
    try:
        verify(negative)
    except ValueError:
        print("size gate: one byte above app budget rejected PASS")
    else:
        raise AssertionError("size regression gate accepted an oversized executable")
    finally:
        negative.unlink()
    run(["python3", "narzedzia/test_dzwieku_packaging.py"])
    flags = ["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic",
             "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections",
             "-fsanitize=address,undefined", "-Wl,--gc-sections", "-I."]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    for trace in (0, 1):
        binary = OUT / f"loader-{trace}"
        run(flags + ["-DBURSZTYN_LOADER_RUNTIME_HOST_TEST", "-DBURSZTYN_PSF_HOST_TEST",
                     f"-DBURSZTYNDIAGTESTDZWIEKU={trace}", "loader.cpp", "storage_format.cpp",
                     "narzedzia/test_dzwieku_loader.cpp", "-o", binary])
        run([binary, ROOT / "test_dzwieku.bin"], env)
    run(flags + ["-DBURSZTYNDIAGTESTDZWIEKU=1", "narzedzia/test_dzwieku_gui.cpp", "storage_format.cpp", "-o", OUT / "gui"])
    run([OUT / "gui", ROOT / "test_dzwieku.bin"], env)
    run(flags + ["narzedzia/test_dzwieku_hda_bws.cpp", "sterowniki/dzwiek/hda_diag.cpp", "-o", OUT / "hda-bws"])
    run([OUT / "hda-bws"], env)
    print("TEST DŹWIĘKU: ASan + UBSan PASS (production and bounded startup trace)")


if __name__ == "__main__":
    main()
