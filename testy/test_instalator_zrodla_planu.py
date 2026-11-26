#!/usr/bin/env python3
"""Zrodla planu instalacji musza przetrwac caly etap SYSTEMFILES (regresja natywnego bledu 7, plik 108/120)."""
import importlib.util
import pathlib
import re
import sys
import unittest
import zlib

KORZEN = pathlib.Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("manifest_instalacji", KORZEN / "narzedzia/instalator/manifest_instalacji.py")
mi = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mi)


class ZrodlaPlanu(unittest.TestCase):
    def cialo(self):
        kernel = (KORZEN / "jadro/rdzen/kernel.cpp").read_text(encoding="utf-8")
        return mi.bez_testow_psf(mi.cialo_funkcji(kernel, "void instaluj_wbudowane_programy() {"))

    def test_kernel_nie_ma_zrodel_na_stosie(self):
        self.assertEqual(mi.nietrwale_zrodla(self.cialo()), [])

    def test_manifesty_pcm_sa_static(self):
        cialo = self.cialo()
        for nazwa in ("manifest440", "manifest660"):
            self.assertIn(f"static const char {nazwa}[]", cialo)

    def test_wykrywa_dawny_blad(self):
        # Dokladnie dawna postac z kernel.cpp: lokalna tablica przekazana do instaluj_paczke.
        cialo = ('    const char manifest440[] = "nazwa = \\"PCM 440 Hz\\"\\n";\n'
                 '    if (!instaluj_paczke("/programy/pcm440.cebula/opis.aplikacji", manifest440,\n'
                 '        "/programy/pcm440.cebula/pcm440.bur", _binary_pcm440_bin_bz_start, _binary_pcm440_bin_bz_end)) x();\n'
                 '    static const char MANIFEST_OK[] = "a";\n'
                 '    instaluj_paczke("/p/opis.aplikacji", MANIFEST_OK, "/p/p.bur", _binary_p_start, _binary_p_end);\n'
                 '    const char tylko_log[] = "x"; wypisz_log(tylko_log);\n')
        self.assertEqual(mi.nietrwale_zrodla(cialo), ["manifest440"])

    def test_oczekiwane_bajty_pcm440(self):
        # Zamierzona tresc pliku 108/120: 130 B, CRC-32 0xb1ce25d5 (natywnie oczekiwano 0x559d4d22 = smieci ze stosu).
        kernel_manifesty = {n: mi.literal_c(f) for n, f in re.findall(
            r"(?:static )?const char (\w+)\[\]\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;", self.cialo())}
        dane = kernel_manifesty["manifest440"]
        self.assertEqual(len(dane), 130)
        self.assertEqual(zlib.crc32(dane) & 0xFFFFFFFF, 0xB1CE25D5)


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False, verbosity=1).result.wasSuccessful() else 1)
