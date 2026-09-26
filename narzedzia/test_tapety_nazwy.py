#!/usr/bin/env python3
"""
test_tapety_nazwy.py - Testy jednostkowe i integracyjne dla modulow generowania,
nazewnictwa i walidacji pakietow tapet (generujpakiet.py i tapety_nazwy).
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


class TestTapetyNazwy(unittest.TestCase):
    def test_bezpieczna_nazwa_poprawne(self):
        poprawne = [
            "Żółta łąka.jpg",
            "BursztynowaŁąka.jpg",
            "morze-zachód.jpeg",
            "Tapeta Bursztyn OS.jpg",
            "góry2026-zima.jpg",
            "łąka zachód słońca.jpg",
            "Tapeta (nowa) 2026.jpg",
            "a.jpg",
            "bursztyn01.btp",
            "BURSZTYN10.BTP",
        ]
        for name in poprawne:
            self.assertTrue(generujpakiet.bezpieczna_nazwa(name), f"Powinno byc poprawne: {name}")

    def test_bezpieczna_nazwa_odrzucane(self):
        bledne = [
            "",
            ".",
            "..",
            ".ukryty.jpg",
            "../traversal.jpg",
            "katalog/plik.jpg",
            "katalog\\plik.jpg",
            "plik\r\n.jpg",
            'cudzyslow"plik.jpg',
            "brak_rozszerzenia",
            "zly_format.png",
            "zly_format.bmp",
            ".jpg",
            "a.j",
            b"\xc0\xaf_overlong.jpg".decode("utf-8", "surrogateescape"),
            b"\xed\xa0\x80_surrogate.jpg".decode("utf-8", "surrogateescape"),
        ]
        for name in bledne:
            self.assertFalse(generujpakiet.bezpieczna_nazwa(name), f"Powinno byc odrzucone: {name}")

    def test_kanoniczne_nazwy_i_stabilnosc(self):
        # Sprawdz zgodnosc wektora FNV-1a z testem C++:
        # "Żółta łąka.jpg" -> "wp_f0eeee7f3f3099f8.jpg"
        canon1 = generujpakiet.tapeta_kanoniczna_nazwa("Żółta łąka.jpg")
        self.assertEqual(canon1, "wp_f0eeee7f3f3099f8.jpg")

        # Systemowe tapety zachowuja oryginalna nazwe
        self.assertEqual(generujpakiet.tapeta_kanoniczna_nazwa("bursztyn01.btp"), "bursztyn01.btp")
        self.assertEqual(generujpakiet.tapeta_kanoniczna_nazwa("BURSZTYN10.BTP"), "bursztyn10.btp")

        # Rozszerzenie .jpeg
        canon2 = generujpakiet.tapeta_kanoniczna_nazwa("morze-zachód.jpeg")
        self.assertTrue(canon2.startswith("wp_"))
        self.assertTrue(canon2.endswith(".jpeg"))

        # Zgodnosc z alfabetem [a-z0-9_.]
        for c in canon2:
            self.assertTrue(c.isalnum() or c in "_.", f"Nieprawidlowy znak w nazwie kanonicznej: {c}")

    def test_generowanie_pakietu_i_wykrywanie_kolizji(self):
        with tempfile.TemporaryDirectory(prefix="test-tapety-") as tmpdir:
            tmp = Path(tmpdir)
            katalog_src = tmp / "src"
            katalog_src.mkdir()

            # Utworz prawidlowy maly plik JPEG z polska nazwa
            img_path = katalog_src / "Żółta łąka.jpg"
            img = Image.new("RGB", (64, 64), (255, 215, 0))
            img.save(img_path, format="JPEG")

            pak_path = tmp / "tapety.pak"
            dep_path = tmp / "tapety.pak.d"

            # 1. Zbuduj pakiet
            res = subprocess.run([
                "python3", str(GEN_SCRIPT),
                "--katalog", str(katalog_src),
                "--wyjscie", str(pak_path),
                "--dep", str(dep_path)
            ], capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Blad budowy pakietu: {res.stderr}")
            self.assertTrue(pak_path.is_file())

            # 2. Odczytaj i zweryfikuj naglowek oraz wpisy
            data = pak_path.read_bytes()
            magic, ver, count, hdr_sz, tbl_sz, dat_sz, total_sz = struct.unpack_from("<8sIIIIII", data, 0)
            self.assertEqual(magic, b"BTPK1\x00\x00\x00")
            self.assertEqual(ver, 1)
            self.assertEqual(count, 1)
            self.assertEqual(total_sz, len(data))

            nlen, rsz, crc, poff = struct.unpack_from("<IIII", data, hdr_sz)
            raw_name = data[hdr_sz + 16: hdr_sz + 16 + nlen].decode("utf-8")
            self.assertEqual(raw_name, "Żółta łąka.jpg")
            self.assertEqual(rsz, len(img_path.read_bytes()))
            self.assertEqual(crc, binascii.crc32(img_path.read_bytes()) & 0xFFFFFFFF)

            # 3. Test kolizji: dwa pliki o tej samej nazwie kanonicznej (np. zmockowane)
            # Stworz drugi plik z identyczna zawartoscia i sprawdz czy kolizja hashujaca jest wykrywana
            # Poniewaz hash to deterministyczny FNV1a na bajtach, symulujemy kolizje
            # przez nadpisanie tapeta_kanoniczna_nazwa w tescie:
            orig_func = generujpakiet.tapeta_kanoniczna_nazwa
            try:
                generujpakiet.tapeta_kanoniczna_nazwa = lambda x: "wp_kolizja.jpg"
                img2_path = katalog_src / "BursztynowaŁąka.jpg"
                img.save(img2_path, format="JPEG")
                with self.assertRaises(ValueError):
                    # Wywolaj main() w tym samym procesie
                    old_argv = sys.argv
                    sys.argv = ["generujpakiet.py", "-k", str(katalog_src), "-w", str(tmp / "kol.pak")]
                    try:
                        generujpakiet.main()
                    finally:
                        sys.argv = old_argv
            finally:
                generujpakiet.tapeta_kanoniczna_nazwa = orig_func


if __name__ == "__main__":
    unittest.main()
