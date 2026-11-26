#!/usr/bin/env python3
"""Zasoby Grajka: konwersja ikon uzytkownika przy budowaniu i spojnosc mapowania akcji.

Sprawdza (bez QEMU, bez zmiany plikow repozytorium):
  * kazdy plik z ikonki/grajek/ ma akcje w mapie narzedzia (i odwrotnie) - nic nie jest zgadywane ani pomijane,
  * wynik: 128x128 RGBA, przezroczyste tlo zachowane, proporcje ksztaltu zachowane (+-3 %), rozmiar <= 256 KiB,
  * oryginaly (ikony i ikonki/plyta_winylowa.jpg) sa bajt w bajt takie same przed i po konwersji,
  * plyta jest osadzana bez konwersji (Makefile linkuje oryginalny plik),
  * ta sama lista akcji w narzedziu, Makefile (GRAJEK_IKONY), tabeli plikow jadra i Grajku (sciezka_ikony).
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

KORZEN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(KORZEN, "narzedzia"))
os.chdir(KORZEN)
import przygotuj_zasoby_grajka as narz  # noqa: E402

sprawdzenia = 0


def check(warunek, opis):
    global sprawdzenia
    sprawdzenia += 1
    if not warunek:
        print(f"FAIL: {opis}")
        sys.exit(1)


def sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    from PIL import Image
    pliki = sorted(os.listdir(narz.ZRODLO_IKON))
    check(set(pliki) == set(narz.MAPA_IKON), f"mapa narzedzia = pliki uzytkownika ({pliki})")
    check(len(set(narz.MAPA_IKON.values())) == len(narz.MAPA_IKON), "akcje unikalne")
    oryginaly = [os.path.join(narz.ZRODLO_IKON, p) for p in pliki] + [narz.PLYTA]
    przed = {p: sha(p) for p in oryginaly}
    with tempfile.TemporaryDirectory(prefix="grajek-zasoby-") as wyj:
        r = subprocess.run([sys.executable, "narzedzia/przygotuj_zasoby_grajka.py", wyj], capture_output=True, text=True)
        check(r.returncode == 0, f"narzedzie: {r.stdout} {r.stderr}")
        for plik, akcja in narz.MAPA_IKON.items():
            cel = os.path.join(wyj, f"grajek_{akcja}.png")
            check(os.path.isfile(cel), f"wynik {cel}")
            check(os.path.getsize(cel) <= 256 * 1024, f"{cel} <= 256 KiB")
            with Image.open(cel) as im:
                check(im.size == (128, 128) and im.mode == "RGBA", f"{cel}: 128x128 RGBA")
                a = im.getchannel("A")
                check(a.getpixel((0, 0)) == 0 and a.getpixel((127, 127)) == 0, f"{cel}: przezroczyste rogi")
                bw = a.point(lambda v: 255 if v > 128 else 0).getbbox()
            with Image.open(os.path.join(narz.ZRODLO_IKON, plik)) as src:
                bs = src.convert("RGBA").getchannel("A").point(lambda v: 255 if v > 128 else 0).getbbox()
            pw = (bw[2] - bw[0]) / (bw[3] - bw[1])
            ps = (bs[2] - bs[0]) / (bs[3] - bs[1])
            check(abs(pw - ps) / ps < 0.03, f"{plik}: proporcje ksztaltu {ps:.3f} -> {pw:.3f}")
        # deterministycznie: drugi przebieg daje te same bajty
        pierwszy = {f: sha(os.path.join(wyj, f)) for f in os.listdir(wyj)}
        subprocess.run([sys.executable, "narzedzia/przygotuj_zasoby_grajka.py", wyj], check=True, capture_output=True)
        check(pierwszy == {f: sha(os.path.join(wyj, f)) for f in os.listdir(wyj)}, "wynik deterministyczny")
    for p in oryginaly:
        check(sha(p) == przed[p], f"oryginal niezmieniony: {p}")

    akcje = list(narz.MAPA_IKON.values())
    mk = open("Makefile", encoding="utf-8").read()
    m = re.search(r"^GRAJEK_IKONY := (.+)$", mk, re.M)
    check(m and m.group(1).split() == akcje, "Makefile GRAJEK_IKONY == mapa narzedzia (ta sama kolejnosc)")
    check(re.search(r"\$\(LD\) -r -b binary \$\(GRAJEK_IKONY_PNG\) ikonki/plyta_winylowa\.jpg", mk), "plyta linkowana bez konwersji")
    check("grajek_zasoby_blob.o" in mk.split("# 8. CELE GLOWNE")[0], "blob zasobow w obiektach jadra")
    kern = open("jadro/rdzen/kernel.cpp", encoding="utf-8").read()
    for a in akcje:
        check(f'{{"/ikonki/grajek_{a}.png",_binary_grajek_zasoby_grajek_{a}_png_start' in kern, f"jadro: /ikonki/grajek_{a}.png")
        check(f"GRAJEK_ZASOB({a})" in kern, f"jadro: symbol {a}")
    check('{"/ikonki/grajek_plyta_winylowa.jpg",_binary_ikonki_plyta_winylowa_jpg_start' in kern, "jadro: plyta")
    wid = open("programy/grajek/widoki.h", encoding="utf-8").read()
    sciezki = re.findall(r'"/ikonki/grajek_([a-z_]+)\.png"', wid)
    check(sciezki == akcje, f"Grajek sciezka_ikony == mapa ({sciezki})")
    print(f"Zasoby Grajka: {sprawdzenia} sprawdzen OK ({len(akcje)} ikon, plyta {os.path.getsize(narz.PLYTA)} B bez zmian)")


if __name__ == "__main__":
    main()
