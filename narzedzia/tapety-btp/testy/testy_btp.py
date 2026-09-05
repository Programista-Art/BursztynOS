#!/usr/bin/env python3
"""Testy hostowe; budowanie, obrazy i pliki wynikowe tylko w katalogu tymczasowym."""

import errno
import os
from pathlib import Path
import platform
import random
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

KATALOG = Path(__file__).resolve().parent
sys.path.insert(0, str(KATALOG.parent))
import konwerterbtp as btp
from PIL import Image


def referencja(plik):
    """Niezalezny, scisly dekoder formatu do bajtow BGRA (bez renderowania)."""
    if len(plik) < 14:
        raise ValueError("header")
    magic, w, h, bpp, tryb = struct.unpack_from("<4sIIBB", plik)
    if magic != b"BTP1" or not 1 <= w <= 8192 or not 1 <= h <= 8192 or bpp != 32:
        raise ValueError("header values")
    if tryb == 0:
        if len(plik) != 14 + w * h * 4:
            raise ValueError("raw length")
        return bytes(plik[14:])
    if tryb != 1:
        raise ValueError("compression")
    wynik = bytearray()
    for at in range(14, len(plik), 5):
        if len(plik) - at < 5 or not plik[at]:
            raise ValueError("packet")
        if len(wynik) // 4 + plik[at] > w * h:
            raise ValueError("overlong run")
        wynik.extend(plik[at + 1:at + 5] * plik[at])
    if len(wynik) != w * h * 4:
        raise ValueError("incomplete")
    return bytes(wynik)


def wzorzec_bgra(rgba, alfa):
    wynik = bytearray()
    for at in range(0, len(rgba), 4):
        r, g, b, a = rgba[at:at + 4]
        wynik.extend((b, g, r, a if alfa else 255))
    return bytes(wynik)


def obrazy_testowe():
    rng = random.Random(0xB7F1)
    probki = []

    def dodaj(nazwa, w, h, kolor, alfa=False):
        rgba = bytes(kanal for y in range(h) for x in range(w) for kanal in kolor(x, y))
        probki.append((nazwa, w, h, rgba, alfa))

    dodaj("czarny_1x1", 1, 1, lambda x, y: (0, 0, 0, 255))
    dodaj("bialy_1x1", 1, 1, lambda x, y: (255, 255, 255, 255))
    dodaj("jednolity_10x10", 10, 10, lambda x, y: (255, 191, 0, 255))
    dodaj("seria_600", 20, 30, lambda x, y: (26, 11, 0, 255))
    dodaj("szachownica", 9, 7, lambda x, y: ((x + y) % 2 * 255,) * 3 + (255,))
    dodaj("pasy_poziome", 13, 9, lambda x, y: (y // 2 * 40, 11, 0, 255))
    dodaj("pasy_pionowe", 9, 13, lambda x, y: (x // 2 * 40, 11, 0, 255))
    dodaj("gradient", 17, 11, lambda x, y: (x * 15, y * 23, (x + y) * 9, 255))
    dodaj("szum", 31, 29, lambda x, y: (rng.randrange(256), rng.randrange(256), rng.randrange(256), 255))
    dodaj("probka_fotograficzna", 23, 19,
          lambda x, y: (min(255, x * 8 + rng.randrange(16)),
                        min(255, y * 10 + rng.randrange(16)),
                        min(255, (x + y) * 5 + rng.randrange(16)), 255))
    przezroczyste = [(0, 0, 0, 0), (40, 20, 10, 0), (255, 191, 0, 128),
                    (255, 255, 255, 255), (0, 0, 0, 1), (17, 65, 129, 64)]
    dodaj("alfa_wymuszona", 3, 2, lambda x, y: przezroczyste[y * 3 + x])
    dodaj("alfa_zachowana", 3, 2, lambda x, y: przezroczyste[y * 3 + x], True)
    dodaj("nieparzysta_szerokosc", 5, 4, lambda x, y: (x, y, 99, 255))
    dodaj("nieparzysta_wysokosc", 4, 5, lambda x, y: (x, y, 199, 255))
    dodaj("stres_257x255", 257, 255, lambda x, y: (x % 256, y, (x ^ y) % 256, 255))
    kolory_remisu = [(0, 0, 0, 255), (0, 0, 0, 255), (255, 255, 255, 255),
                    (255, 191, 0, 255), (255, 0, 0, 255)]
    dodaj("remis", 5, 1, lambda x, y: kolory_remisu[x])
    return probki


def pakiet_probki(nazwa, w, h, rgba, alfa):
    with Image.frombytes("RGBA", (w, h), rgba) as obraz:
        piksele = btp.przygotujpiksele(obraz, zachowajalfa=alfa)
    return piksele, wzorzec_bgra(rgba, alfa)


class TestEncoder(unittest.TestCase):
    def setUp(self):
        self.katalog = tempfile.TemporaryDirectory(prefix="btp-python-")
        self.addCleanup(self.katalog.cleanup)
        self.tmp = Path(self.katalog.name)

    def test_wszystkie_probki_raw_rle_auto(self):
        print("\nPróbka | RAW B | RLE B | AUTO | BTP B", flush=True)
        for probka in obrazy_testowe():
            nazwa, w, h, _, _ = probka
            piksele, oczekiwane = pakiet_probki(*probka)
            self.assertEqual(piksele, oczekiwane)
            for tryb in ("raw", "rle", "auto"):
                with self.subTest(probka=nazwa, tryb=tryb):
                    dane = btp.kodujbtp(piksele, w, h, tryb)
                    plik = dane.naglowek + dane.payload
                    self.assertEqual(referencja(plik), oczekiwane)
                    self.assertEqual(dane.rozmiarpliku, len(plik))
                    self.assertEqual(dane.naglowek, struct.pack("<4s I I B B", b"BTP1", w, h, 32, dane.kompresja))
                    self.assertEqual(dane.rozmiarrle, len(btp.kodujrle(piksele)))
                    if tryb == "auto":
                        self.assertEqual(dane.kompresja, int(dane.rozmiarrle < dane.rozmiarraw))
                        self.assertLessEqual(len(dane.payload), len(piksele))
                        print(f"{nazwa} | {dane.rozmiarraw} | {dane.rozmiarrle} | {dane.tryb} | {dane.rozmiarpliku}", flush=True)

    def test_auto_remis_i_wymuszenie(self):
        solid = b"\0\0\0\xff" * 100
        self.assertEqual(btp.kodujbtp(solid, 10, 10).tryb, "RLE")
        self.assertEqual(btp.kodujbtp(solid, 10, 10, "raw").tryb, "RAW")
        remis, _ = pakiet_probki(*obrazy_testowe()[-1])
        dane = btp.kodujbtp(remis, 5, 1)
        self.assertEqual((dane.rozmiarraw, dane.rozmiarrle, dane.tryb), (20, 20, "RAW"))
        self.assertEqual(btp.kodujbtp(remis, 5, 1, "rle").tryb, "RLE")
        noise, _ = pakiet_probki(*obrazy_testowe()[8])
        self.assertEqual(btp.kodujbtp(noise, 31, 29).tryb, "RAW")

    def test_rle_puste_255_256_i_koniec(self):
        self.assertEqual(btp.kodujraw(b""), b"")
        self.assertEqual(btp.kodujrle(b""), b"")
        self.assertEqual(btp.rozmiarrle(b""), 0)
        kolor = b"\x12\x34\x56\xff"
        for n, runs in ((1, [1]), (255, [255]), (256, [255, 1]), (600, [255, 255, 90])):
            kod = btp.kodujrle(kolor * n)
            self.assertEqual(kod, b"".join(bytes([run]) + kolor for run in runs))
        self.assertEqual(btp.kodujrle(kolor * 6), bytes([6]) + kolor)

    def test_kwantyzacja_rgb_i_alfa(self):
        rgba = bytes((255, 191, 7, 0, 17, 129, 254, 127))
        with Image.frombytes("RGBA", (2, 1), rgba) as obraz:
            self.assertEqual(btp.przygotujpiksele(obraz), bytes((7, 191, 255, 255, 254, 129, 17, 255)))
            self.assertEqual(btp.przygotujpiksele(obraz, True, True), bytes((0, 184, 248, 0, 248, 128, 16, 127)))
            self.assertEqual(obraz.tobytes(), rgba, "preparation must not mutate caller image")
            for optimize in (False, True):
                raw = btp.przygotujpiksele(obraz, optimize, True)
                dane = btp.kodujbtp(raw, 2, 1)
                self.assertEqual(dane.rozmiarrle, len(btp.kodujrle(raw)))

    def test_nieprawidlowe_argumenty(self):
        for w, h in ((0, 1), (1, 0), (-1, 1), (1, -1), (8193, 1), (1, 8193), (True, 1)):
            with self.assertRaises(btp.BladKonwersji):
                btp.sprawdz_wymiary(w, h)
        self.assertEqual(btp.sprawdz_wymiary(8192, 8192), 67108864)
        for raw in (b"a", b"abc", bytearray(4)):
            with self.assertRaises(btp.BladKonwersji):
                btp.kodujraw(raw)
        with self.assertRaises(btp.BladKonwersji):
            btp.kodujbtp(b"", 1, 1)
        with self.assertRaises(btp.BladKonwersji):
            btp.wybierztryb(4, 5, "zip")

    def test_formaty_i_zmiana_rozmiaru(self):
        for rozszerzenie in ("png", "jpg", "jpeg", "bmp"):
            plik = self.tmp / f"żółta tapeta.{rozszerzenie}"
            with Image.new("RGB", (123, 61), (255, 191, 0)) as obraz:
                obraz.save(plik)
            with btp.wczytajobraz(plik, 60, 50) as obraz:
                self.assertEqual(obraz.mode, "RGBA")
                self.assertEqual(obraz.size, (60, 30))
            with btp.wczytajobraz(plik, 1920, 1080) as obraz:
                self.assertEqual(obraz.size, (123, 61))
            wyjscie, dane = btp.konwertuj(plik, self.tmp / "wyniki" / "żółta tapeta.btp")
            with Image.open(plik) as zrodlo:
                with zrodlo.convert("RGBA") as rgba:
                    self.assertEqual(referencja(wyjscie.read_bytes()), wzorzec_bgra(rgba.tobytes(), False))
        # Paletowy PNG tez musi przejsc przez LANCZOS, a nie NEAREST.
        plik = self.tmp / "paleta.png"
        with Image.new("P", (9, 7)) as obraz:
            obraz.putpalette([0, 0, 0, 255, 255, 255] + [0] * 762)
            obraz.putdata([(x + y) % 2 for y in range(7) for x in range(9)])
            obraz.save(plik)
        with btp.wczytajobraz(plik, 4, 4) as obraz:
            self.assertTrue(any(0 < r < 255 for r, g, b, a in obraz.getdata()))

    def test_exif_i_przezroczysty_png(self):
        sciezka = self.tmp / "obrot.jpg"
        with Image.new("RGB", (20, 10), "red") as obraz:
            exif = Image.Exif()
            exif[274] = 6
            obraz.save(sciezka, exif=exif)
        with btp.wczytajobraz(sciezka) as obraz:
            self.assertEqual(obraz.size, (10, 20))
        sciezka = self.tmp / "przezroczysty.png"
        with Image.new("RGBA", (1, 1), (0, 0, 0, 0)) as obraz:
            obraz.save(sciezka)
        for alfa in (False, True):
            cel, _ = btp.konwertuj(sciezka, self.tmp / "alfa.btp", zachowajalfa=alfa)
            self.assertEqual(referencja(cel.read_bytes()), b"\0\0\0" + bytes([0 if alfa else 255]))

    def test_bledy_pillow_i_bomby(self):
        zly = self.tmp / "zly.png"
        zly.write_bytes(b"not an image")
        with self.assertRaises(btp.BladKonwersji):
            btp.konwertuj(zly, self.tmp / "zly.btp")
        self.assertFalse((self.tmp / "zly.btp").exists())
        sciezka = self.tmp / "bomb.png"
        with Image.new("RGB", (10, 10)) as obraz:
            obraz.save(sciezka)
        for limit in (60, 1):
            with mock.patch.object(Image, "MAX_IMAGE_PIXELS", limit):
                with self.assertRaisesRegex(btp.BladKonwersji, "bombę"):
                    btp.wczytajobraz(sciezka)
        # Rzeczywisty, waski PNG z wymiarem poza limitem; odrzucany przed load.
        with Image.new("RGB", (8193, 1)) as obraz:
            obraz.save(sciezka)
        with self.assertRaises(btp.BladKonwersji):
            btp.wczytajobraz(sciezka)
        with Image.new("RGB", (10, 10)) as obraz:
            obraz.save(sciezka)
        with mock.patch.object(btp, "MAKS_PAMIECI", 1):
            with self.assertRaisesRegex(btp.BladKonwersji, "budżet"):
                btp.wczytajobraz(sciezka)
        _, ImageOps = btp.pillow()
        with mock.patch.object(ImageOps, "exif_transpose", side_effect=TypeError("bad EXIF")):
            with self.assertRaisesRegex(btp.BladKonwersji, "Nie można odczytać"):
                btp.wczytajobraz(sciezka)
        sciezka.write_bytes(sciezka.read_bytes()[:45])
        with self.assertRaises(btp.BladKonwersji):
            btp.wczytajobraz(sciezka)

    def test_atomowy_zapis_kolizje_i_sprzatanie(self):
        dane = btp.kodujbtp(b"\0\0\0\xff", 1, 1)
        nazwa = self.tmp / "Żółć.BTP"
        pierwszy = btp.zapiszbtp(nazwa, dane)
        stary = pierwszy.read_bytes()
        drugi = btp.zapiszbtp(nazwa, dane)
        self.assertEqual((pierwszy.name, drugi.name), ("Żółć.btp", "Żółć_1.btp"))
        self.assertEqual(pierwszy.read_bytes(), stary)
        self.assertEqual(drugi.read_bytes(), stary)
        self.assertFalse(list(self.tmp.glob(".btp-*.tmp")))
        for funkcja in ("fsync", "link"):
            with mock.patch.object(btp.os, funkcja, side_effect=OSError("test failure")):
                with self.assertRaises(OSError):
                    btp.zapiszbtp(self.tmp / "blad.btp", dane)
            self.assertFalse((self.tmp / "blad.btp").exists())
            self.assertFalse(list(self.tmp.glob(".btp-*.tmp")))
        # Kolizja powstajaca dopiero przy publikacji, po utworzeniu temp.
        prawdziwy_link = os.link
        cel = self.tmp / "wyscig.btp"

        def wyscig(zrodlo, wyjscie):
            if wyjscie == cel:
                cel.write_bytes(b"istniejacy plik")
            return prawdziwy_link(zrodlo, wyjscie)

        with mock.patch.object(btp.os, "link", side_effect=wyscig):
            zapisany = btp.zapiszbtp(cel, dane)
        self.assertEqual(cel.read_bytes(), b"istniejacy plik")
        self.assertEqual(zapisany.name, "wyscig_1.btp")

    def test_cli_i_brak_zaleznosci(self):
        skrypt = KATALOG.parent / "konwerterbtp.py"
        plik = self.tmp / "czarny.png"
        with Image.new("RGB", (1, 1)) as obraz:
            obraz.save(plik)
        wynik = subprocess.run([sys.executable, "-B", str(skrypt), str(plik)],
                                cwd=self.tmp, text=True, capture_output=True)
        self.assertEqual(wynik.returncode, 0, wynik.stderr)
        self.assertIn("Tryb RAW", wynik.stdout)
        self.assertIn("BTP 18 B", wynik.stdout)
        self.assertTrue((self.tmp / "konwertowane/czarny.btp").is_file())
        for flagi in (("--max-width", "0"), ("--max-height", "8193"), ("--force-raw", "--force-rle")):
            wynik = subprocess.run([sys.executable, "-B", str(skrypt), str(plik), *flagi],
                                    cwd=self.tmp, text=True, capture_output=True)
            self.assertNotEqual(wynik.returncode, 0)
        # Wykluczamy tkinter import-hookiem; pelna konwersja CLI nadal dziala.
        kod = """
import builtins, sys
sys.path.insert(0, sys.argv[1])
original = builtins.__import__
def no_tk(name, *args, **kwargs):
    if name == 'tkinter' or name.startswith('tkinter.'):
        raise ImportError('Tkinter intentionally unavailable')
    return original(name, *args, **kwargs)
builtins.__import__ = no_tk
import konwerterbtp
sys.exit(konwerterbtp.main([sys.argv[2], sys.argv[3], '--force-rle']))
"""
        wynik = subprocess.run([sys.executable, "-B", "-c", kod, str(KATALOG.parent), str(plik), str(self.tmp / "bez-tk.btp")],
                                text=True, capture_output=True)
        self.assertEqual(wynik.returncode, 0, wynik.stderr)
        self.assertIn("Tryb RLE", wynik.stdout)
        kod = kod.replace("name == 'tkinter' or name.startswith('tkinter.')", "name == 'PIL' or name.startswith('PIL.')")
        wynik = subprocess.run([sys.executable, "-B", "-c", kod, str(KATALOG.parent), str(plik), str(self.tmp / "bez-pil.btp")],
                                text=True, capture_output=True)
        self.assertEqual(wynik.returncode, 1)
        self.assertIn("python3-pil", wynik.stderr)
        self.assertIn("pip install Pillow", wynik.stderr)
        self.assertNotIn("Traceback", wynik.stderr)

    def test_losowy_encoder(self):
        rng = random.Random(67891)
        for _ in range(500):
            w, h = rng.randrange(1, 33), rng.randrange(1, 33)
            raw = rng.randbytes(w * h * 4)
            for tryb in ("raw", "rle", "auto"):
                dane = btp.kodujbtp(raw, w, h, tryb)
                self.assertEqual(referencja(dane.naglowek + dane.payload), raw)
        print("Python: 500 losowych obrazów × RAW/RLE/AUTO = 1500 round trips", flush=True)


class TestCpp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("g++"):
            raise unittest.SkipTest("Brak g++: testy C++ nie zostały uruchomione")
        cls.katalog = tempfile.TemporaryDirectory(prefix="btp-cpp-")
        cls.addClassCleanup(cls.katalog.cleanup)
        cls.tmp = Path(cls.katalog.name)

    def uruchom(self, polecenie, **opcje):
        wynik = subprocess.run([str(arg) for arg in polecenie], text=True, capture_output=True, **opcje)
        self.assertEqual(wynik.returncode, 0, f"{polecenie}\n{wynik.stdout}\n{wynik.stderr}")
        if wynik.stdout:
            print(wynik.stdout, end="", flush=True)
        return wynik

    def test_freestanding_bez_runtime(self):
        obiekt = self.tmp / "tapetybtp.o"
        self.uruchom(["g++", "-std=gnu++17", "-O2", "-ffreestanding", "-fno-asynchronous-unwind-tables",
                      "-fno-unwind-tables", "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse",
                      "-mno-sse2", "-fno-stack-protector", "-fcf-protection=none", "-fno-pic", "-fno-pie",
                      "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wconversion", "-Wsign-conversion",
                      "-fno-exceptions", "-fno-rtti", "-c", KATALOG.parent / "tapetybtp.cpp", "-o", obiekt])
        self.assertEqual(self.uruchom(["nm", "-u", obiekt]).stdout.strip(), "")
        print("Freestanding x86-64: PASS; brak nierozwiązanych symboli runtime", flush=True)

    def test_sanitizery_python_i_fuzz(self):
        rekordy = []
        for probka in obrazy_testowe():
            _, w, h, _, _ = probka
            raw, oczekiwane = pakiet_probki(*probka)
            for tryb in ("raw", "rle", "auto"):
                dane = btp.kodujbtp(raw, w, h, tryb)
                plik = dane.naglowek + dane.payload
                rekordy.append(struct.pack("<III", len(plik), w, h) + plik + oczekiwane)
        fixture = self.tmp / "probki.bin"
        fixture.write_bytes(struct.pack("<I", len(rekordy)) + b"".join(rekordy))
        program = self.tmp / "test_dekoder"
        self.uruchom(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                      "-fsanitize=address,undefined", "-fno-omit-frame-pointer", KATALOG / "test_dekoder.cpp", "-o", program])
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        self.uruchom([program, fixture], env=env, timeout=120)

    @unittest.skipUnless(sys.platform == "linux" and platform.machine() in ("x86_64", "amd64"), "Test i386 wymaga Linux x86-64")
    def test_runtime_32bit(self):
        program = self.tmp / "test_dekoder32"
        self.uruchom(["g++", "-m32", "-std=gnu++17", "-O2", "-ffreestanding", "-fno-exceptions",
                      "-fno-rtti", "-fno-stack-protector", "-fno-pic", "-fno-pie", "-Wall", "-Wextra",
                      "-Wpedantic", "-Werror", "-nostdlib", "-static", "-no-pie",
                      KATALOG / "test_dekoder32.cpp", "-o", program])
        try:
            wynik = subprocess.run([program], capture_output=True, timeout=10)
        except OSError as blad:
            if blad.errno == errno.ENOEXEC:
                self.skipTest("Skompilowano i386; host nie pozwala uruchomić pliku")
            raise
        if wynik.returncode == -signal.SIGSYS:
            self.skipTest("Skompilowano i386; sandbox zablokował runtime (SIGSYS)")
        self.assertEqual(wynik.returncode, 0)
        print("32-bit runtime: faktycznie uruchomiono; RAW + RLE overflow/black PASS", flush=True)


@unittest.skipUnless(os.environ.get("BTP_TEST_GUI") == "1", "GUI: uruchom z BTP_TEST_GUI=1 i dostępnym ekranem")
class TestGui(unittest.TestCase):
    def test_konwersja_wielu_plikow(self):
        import tkinter as tk
        try:
            root = tk.Tk()
        except tk.TclError as blad:
            self.skipTest(f"GUI nie uruchomiono: {blad}")
        self.addCleanup(root.destroy)
        root.withdraw()
        gui = btp.KonwerterGui(root)
        self.assertFalse(gui.alfa.get())
        self.assertFalse(gui.optymalizuj.get())
        gui.preset.set("800x600")
        gui.ustawpreset()
        self.assertEqual((gui.szerokosc.get(), gui.wysokosc.get()), ("800", "600"))
        with tempfile.TemporaryDirectory(prefix="btp-gui-") as tmp:
            tmp = Path(tmp)
            pliki = [tmp / "żółć.png", tmp / "żółć.bmp"]
            for plik in pliki:
                with Image.new("RGB", (10, 10), (255, 191, 0)) as obraz:
                    obraz.save(plik)
            with mock.patch.object(gui.filedialog, "askopenfilenames", return_value=[str(p) for p in pliki]):
                gui.wybierzpliki()
            gui.folder.set(str(tmp / "wyniki"))
            gui.przycisk.invoke()
            limit = time.monotonic() + 15
            while gui.zajety and time.monotonic() < limit:
                root.update()
                time.sleep(0.01)
            self.assertFalse(gui.zajety, "GUI worker did not finish")
            self.assertIn("Zapisano: 2, błędy: 0", gui.status.get())
            self.assertIn("RAW 400 B | RLE 5 B | BTP 19 B", gui.log.get("1.0", "end"))
            self.assertEqual(sorted(p.name for p in (tmp / "wyniki").iterdir()), ["żółć.btp", "żółć_1.btp"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
