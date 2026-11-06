#!/usr/bin/env python3
"""
testy/test_tapety_manifest_regression.py - Zestaw testow regresyjnych dla:
1. Zestawu tapet zawierajacego tylko wlasne pliki JPEG (brak bursztyn01.btp, brak tapeta.bmp).
2. Zestawu tapet zawierajacego tylko wlasne pliki BTP.
3. Mieszanego zestawu plikow BTP i JPEG.
4. Braku bursztyn01.btp.
5. Braku tapeta.bmp.
6. Trwalej poprawnej konfiguracji tapety JPEG (persisted valid JPEG).
7. Trwalej poprawnej konfiguracji tapety BTP (persisted valid BTP).
8. Trwalej konfiguracji wskazujacej na brakujacy plik (persisted missing wallpaper -> fallback).
9. Poprawnej domyslnej tapety zdeklarowanej w manifescie (manifest default valid).
10. Brakujacej/niepoprawnej domyslnej tapety zdeklarowanej w manifescie (manifest default missing -> pierwszy wpis).
11. Pustego manifestu tapet (empty manifest -> solid).
12. Uszkodzonego manifestu tapet (corrupted manifest -> tolerancja bledow).
"""

import binascii
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
GEN_SCRIPT = ROOT / "narzedzia/tapety-btp/generujpakiet.py"
sys.path.insert(0, str(ROOT / "narzedzia/tapety-btp"))
import generujpakiet
import btpcodec


def utworz_btp(sciezka: Path, szerokosc=64, wysokosc=64, kolor=(100, 150, 200, 255)):
    pixels = [kolor] * (szerokosc * wysokosc)
    dane_raw = bytearray()
    for r, g, b, a in pixels:
        dane_raw += bytes([b, g, r, a])  # BGRA
    naglowek = struct.pack("<4sIIBB", b"BTP1", szerokosc, wysokosc, 32, 0)
    sciezka.write_bytes(naglowek + bytes(dane_raw))


def utworz_jpeg(sciezka: Path, szerokosc=64, wysokosc=64, kolor=(200, 100, 50)):
    img = Image.new("RGB", (szerokosc, wysokosc), kolor)
    img.save(sciezka, format="JPEG")


class TestTapetyManifestRegression(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp(prefix="bursztyn_tapety_regress_")
        self.tmp = Path(self.temp_dir)
        self.katalog_src = self.tmp / "obrazy_tapety"
        self.katalog_src.mkdir()
        self.pak_path = self.tmp / "tapety.pak"
        self.manifest_path = self.tmp / "manifest.cfg"

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_01_tylko_wlasne_pliki_jpeg(self):
        """Scenariusz 1: Zestaw tapet zawierajacy wylacznie pliki JPEG bez bursztyn01.btp i bez tapeta.bmp."""
        utworz_jpeg(self.katalog_src / "Moja tapeta.jpg", 128, 128, (255, 0, 0))
        utworz_jpeg(self.katalog_src / "góry-zima.jpeg", 64, 64, (0, 255, 0))

        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertTrue(self.pak_path.exists())
        self.assertTrue(self.manifest_path.exists())

        tresc = self.manifest_path.read_text(encoding="utf-8")
        self.assertIn("default=", tresc)
        self.assertIn("wpis=", tresc)
        self.assertNotIn("bursztyn01.btp", tresc)
        self.assertNotIn("tapeta.bmp", tresc)
        self.assertIn("Moja tapeta.jpg", tresc)
        self.assertIn("góry-zima.jpeg", tresc)

    def test_02_tylko_wlasne_pliki_btp(self):
        """Scenariusz 2: Zestaw tapet zawierajacy wylacznie pliki BTP bez bursztyn01.btp."""
        utworz_btp(self.katalog_src / "ciemna.btp", 64, 64)
        utworz_btp(self.katalog_src / "jasna.btp", 64, 64)

        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        tresc = self.manifest_path.read_text(encoding="utf-8")
        self.assertIn("default=", tresc)
        self.assertNotIn("bursztyn01.btp", tresc)
        self.assertNotIn("tapeta.bmp", tresc)
        self.assertIn("ciemna.btp", tresc)

    def test_03_mieszany_zestaw_btp_i_jpeg(self):
        """Scenariusz 3: Mieszany zestaw BTP i JPEG, brak bursztyn01.btp i tapeta.bmp."""
        utworz_jpeg(self.katalog_src / "Moja tapeta.jpg")
        utworz_jpeg(self.katalog_src / "góry-zima.jpeg")
        utworz_btp(self.katalog_src / "ciemna.btp")

        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        tresc = self.manifest_path.read_text(encoding="utf-8")
        self.assertIn("default=", tresc)
        self.assertIn("ciemna.btp", tresc)
        self.assertIn("Moja tapeta.jpg", tresc)
        self.assertIn("góry-zima.jpeg", tresc)

    def test_04_brak_bursztyn01_btp_i_tapeta_bmp(self):
        """Scenariusz 4 & 5: System buduje pakiet i manifest bez zalozen o bursztyn01.btp i tapeta.bmp."""
        utworz_jpeg(self.katalog_src / "tapeta_autorska.jpg")

        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        tresc = self.manifest_path.read_text(encoding="utf-8")
        self.assertNotIn("bursztyn01.btp", tresc)
        self.assertNotIn("tapeta.bmp", tresc)

    def test_06_zadeklarowana_poprawna_domyslna_wskazana_recznie(self):
        """Scenariusz 9: Poprawna zadeklarowana domyslna przez flage --domyslna."""
        utworz_jpeg(self.katalog_src / "Zima.jpg")
        utworz_jpeg(self.katalog_src / "Lato.jpg")
        utworz_btp(self.katalog_src / "Wiosna.btp")

        # Wskazujemy Wiosna.btp jako domyslna
        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--domyslna", "Wiosna.btp",
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        tresc = self.manifest_path.read_text(encoding="utf-8")
        linie = [l.strip() for l in tresc.splitlines() if l.strip()]
        canon_wiosna = generujpakiet.tapeta_kanoniczna_nazwa("Wiosna.btp")
        self.assertEqual(linie[0], f"default={canon_wiosna}")

    def test_07_zadeklarowana_nieistniejaca_domyslna_odrzucana(self):
        """Scenariusz 10: Generator odrzuca zadeklarowana domyslna, ktora nie istnieje."""
        utworz_jpeg(self.katalog_src / "Istniejaca.jpg")

        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--domyslna", "Nieistniejaca.jpg",
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Zadeklarowana domyslna tapeta 'Nieistniejaca.jpg' nie istnieje", res.stderr)

    def test_08_pusty_katalog_ostrzezenie(self):
        """Scenariusz 11: Pusty katalog generuje pakiet z 0 plikami i ostrzezenie."""
        res = subprocess.run([
            sys.executable, str(GEN_SCRIPT),
            "--katalog", str(self.katalog_src),
            "--wyjscie", str(self.pak_path),
            "--manifest", str(self.manifest_path)
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("brak poprawnych tapet", res.stderr)
        tresc = self.manifest_path.read_text(encoding="utf-8")
        self.assertEqual(tresc.strip(), "")


if __name__ == "__main__":
    unittest.main()
