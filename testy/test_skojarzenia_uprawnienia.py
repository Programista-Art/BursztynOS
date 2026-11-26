#!/usr/bin/env python3
"""Skojarzenia plikow (jadro/gui/skojarzenia_plikow.h) a prawa aplikacji uruchamianych z Eksploratora.

BWS 10 nadaje dziecku prawa = prawa rodzica & manifest dziecka (jadro/bws/syscalls.cpp). Aplikacja otwierana
dwuklikiem w Eksploratorze dostaje wiec tylko te swoje prawa, ktore ma tez Eksplorator. Test pilnuje, by
Grajek (.mp3/.wav) i Notatnik (.txt) dostawaly komplet praw z manifestu, a programy skojarzen istnialy w planie.
"""
import importlib.util
import pathlib
import re
import sys
import unittest

KORZEN = pathlib.Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("manifest_instalacji", KORZEN / "narzedzia/instalator/manifest_instalacji.py")
mi = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mi)


def manifesty():
    kernel = (KORZEN / "jadro/rdzen/kernel.cpp").read_text(encoding="utf-8")
    cialo = mi.bez_testow_psf(mi.cialo_funkcji(kernel, "void instaluj_wbudowane_programy() {"))
    surowe = {n: mi.literal_c(f).decode("utf-8") for n, f in re.findall(
        r"(?:static )?const char (\w+)\[\]\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;", cialo)}
    wynik = {}
    for nazwa, tekst in surowe.items():
        start = re.search(r'plik_startowy\s*=\s*"([^"]+)"', tekst)
        prawa = re.search(r"uprawnienia\s*=\s*\[(.*?)\]", tekst, re.S)
        if start:
            wynik[start.group(1)] = set(re.findall(r'"([^"]+)"', prawa.group(1))) if prawa else set()
    return wynik


def program_skojarzony(stala):
    h = (KORZEN / "jadro/gui/skojarzenia_plikow.h").read_text(encoding="utf-8")
    return re.search(rf'constexpr const char\* {stala} = "([^"]+)"', h).group(1)


class SkojarzeniaUprawnienia(unittest.TestCase):
    def setUp(self):
        self.m = manifesty()
        self.eksplorator = self.m["eksplorator-plikow.bur"]

    def prawa(self, stala):
        return self.m[program_skojarzony(stala).rsplit("/", 1)[1]]

    def test_grajek_dostaje_wszystkie_prawa_manifestu(self):
        self.assertTrue(self.prawa("GRAJEK") <= self.eksplorator, self.prawa("GRAJEK") - self.eksplorator)
        self.assertIn("ustawienia_aplikacji", self.prawa("GRAJEK"))

    def test_notatnik_dostaje_prawa_plikow(self):
        self.assertTrue({"okna", "pliki_czytaj", "pliki_zapisz"} <= self.prawa("NOTATNIK") & self.eksplorator)

    def test_eksplorator_bez_nowych_praw_systemowych(self):
        self.assertEqual(self.eksplorator, {"okna", "pliki_czytaj", "pliki_zapisz", "uruchom_program", "ustawienia_aplikacji"})

    def test_znane_ograniczenie_okno_podrzedne(self):
        # Wczesniejsze, udokumentowane ograniczenie (nie zmieniane tutaj): Notatnik i Przegladarka Obrazow otwarte z
        # Eksploratora nie dostaja "okno_podrzedne" (Eksplorator go nie ma). Test wykryje kazda inna utrate praw.
        for stala in ("NOTATNIK", "PRZEGLADARKA_OBRAZOW"):
            self.assertEqual(self.prawa(stala) - self.eksplorator, {"okno_podrzedne"}, stala)

    def test_programy_skojarzen_sa_w_planie_instalacji(self):
        kernel = (KORZEN / "jadro/rdzen/kernel.cpp").read_text(encoding="utf-8")
        tozsamosc = (KORZEN / "jadro/gui/app_identity.h").read_text(encoding="utf-8")
        for stala in ("NOTATNIK", "PRZEGLADARKA_OBRAZOW", "GRAJEK"):
            sciezka = program_skojarzony(stala)
            self.assertTrue(f'"{sciezka}"' in kernel or f'"{sciezka}"' in tozsamosc, sciezka)


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False, verbosity=1).result.wasSuccessful() else 1)
