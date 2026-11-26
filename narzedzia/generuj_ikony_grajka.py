#!/usr/bin/env python3
"""Ikony przelacznikow Grajka (losowo, powtarzanie, powtarzanie utworu, odtwarzanie w tle, wizualizacja).

Grafiki uzytkownika (ikonki/grajek/*.png) nie obejmuja tych akcji, wiec ikony sa rysowane tym skryptem w tym samym
stylu: bursztynowa linia ~10 px na kanwie 128x128 (kolor jak w "refresh.png"), zaokraglone konce, przezroczyste tlo.
Rysowanie w 4x (512x512) i zmniejszenie na PREMNOZONEJ alfie (bez jasnych obwodek), piksele z A=0 maja RGB=0.
Wynik to 8-bitowe RGBA 128x128 bez przeplotu - przygotuj_zasoby_grajka.py kopiuje je bajt w bajt.

Skrypt nie jest czescia budowania (pliki PNG sa w repozytorium); uruchamiany recznie po zmianie wygladu:
    python3 narzedzia/generuj_ikony_grajka.py [katalog]      (domyslnie ikonki/grajek)
"""
import math
import os
import sys

from PIL import Image, ImageDraw

SKALA = 4
BOK = 128
KOLOR = (207, 122, 6, 255)        # linia jak w ikonach uzytkownika (refresh.png)
WYPELNIENIE = (245, 164, 28, 255)  # jasniejszy bursztyn (wypelnienia w mute.png / play.png)
GRUBOSC = 10


def p(x, y):
    return (x * SKALA, y * SKALA)


def linia(d, punkty, grubosc=GRUBOSC, kolor=KOLOR):
    pts = [p(x, y) for x, y in punkty]
    w = grubosc * SKALA
    d.line(pts, fill=kolor, width=w, joint="curve")
    r = w / 2
    for x, y in (pts[0], pts[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=kolor)


def bezier(p0, p1, p2, p3, n=48):
    out = []
    for i in range(n + 1):
        t = i / n
        a = (1 - t) ** 3
        b = 3 * (1 - t) ** 2 * t
        c = 3 * (1 - t) * t * t
        e = t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + e * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + e * p3[1]))
    return out


def grot(d, czubek, kierunek_deg, dlugosc=22, szerokosc=26, kolor=KOLOR):
    """Wypelniony grot (trojkat) z czubkiem w punkcie, skierowany pod katem (0 = w prawo)."""
    a = math.radians(kierunek_deg)
    ux, uy = math.cos(a), math.sin(a)
    px_, py_ = -uy, ux
    bx, by = czubek[0] - ux * dlugosc, czubek[1] - uy * dlugosc
    pts = [czubek, (bx + px_ * szerokosc / 2, by + py_ * szerokosc / 2), (bx - px_ * szerokosc / 2, by - py_ * szerokosc / 2)]
    d.polygon([p(x, y) for x, y in pts], fill=kolor)
    # zaokraglenie naroznikow grotu: ten sam pedzel co linia, cienszy
    d.line([p(x, y) for x, y in pts + [pts[0]]], fill=kolor, width=4 * SKALA, joint="curve")


def losowo(d):
    linia(d, [(12, 36), (34, 36)] + bezier((34, 36), (60, 36), (66, 92), (92, 92)) + [(102, 92)])
    linia(d, [(12, 92), (34, 92)] + bezier((34, 92), (60, 92), (66, 36), (92, 36)) + [(102, 36)])
    grot(d, (118, 36), 0)
    grot(d, (118, 92), 0)


def petla(d):
    # gorny bieg w prawo, prawy luk w dol; dolny bieg w lewo, lewy luk w gore (dwie strzalki jak w refresh.png)
    linia(d, [(30, 82)] + bezier((30, 82), (30, 44), (30, 38), (48, 38))[1:] + [(92, 38)])
    linia(d, [(98, 46)] + bezier((98, 46), (98, 84), (98, 90), (80, 90))[1:] + [(36, 90)])
    grot(d, (110, 38), 0)
    grot(d, (18, 90), 180)


def powtarzanie(d):
    petla(d)


def powtarzanie_jeden(d):
    petla(d)
    # cyfra "1" w srodku petli: kreska pionowa z daszkiem i stopka
    linia(d, [(58, 56), (66, 50), (66, 78)], grubosc=8)
    linia(d, [(58, 78), (74, 78)], grubosc=8)


def w_tle(d):
    # okno (ramka z paskiem tytulu) i nuta przed nim - "muzyka gra, gdy okno jest schowane"
    linia(d, [(22, 72), (22, 30), (96, 30), (96, 48)], grubosc=9)
    linia(d, [(22, 44), (96, 44)], grubosc=7)
    linia(d, [(22, 72), (22, 92), (46, 92)], grubosc=9)
    # nuta: laska, choragiewka, glowka (wypelniona)
    linia(d, [(88, 104), (88, 60), (110, 70)], grubosc=8)
    cx, cy, rx, ry = 78, 104, 13, 10
    d.ellipse((p(cx - rx, cy - ry)[0], p(cx - rx, cy - ry)[1], p(cx + rx, cy + ry)[0], p(cx + rx, cy + ry)[1]), fill=KOLOR)
    # minimalizacja: strzalka w dol obok okna
    linia(d, [(60, 58), (60, 76)], grubosc=8)
    grot(d, (60, 88), 90, dlugosc=16, szerokosc=22)


def wizualizacja(d):
    # widmo: piec zaokraglonych slupkow roznej wysokosci na wspolnej linii bazowej
    baza = 108
    wys = [34, 66, 88, 56, 40]
    x0, szer, odstep = 14, 14, 9
    for i, h in enumerate(wys):
        x = x0 + i * (szer + odstep)
        r = szer * SKALA // 2
        d.rounded_rectangle((p(x, baza - h)[0], p(x, baza - h)[1], p(x + szer, baza)[0], p(x + szer, baza)[1]),
                            radius=r, fill=WYPELNIENIE if i == 2 else KOLOR)


IKONY = {
    "shuffle.png": losowo,
    "repeat.png": powtarzanie,
    "repeat one.png": powtarzanie_jeden,
    "background.png": w_tle,
    "visualization.png": wizualizacja,
}


def zapisz(rysuj, cel):
    duzy = Image.new("RGBA", (BOK * SKALA, BOK * SKALA), (0, 0, 0, 0))
    rysuj(ImageDraw.Draw(duzy))
    maly = duzy.convert("RGBa").resize((BOK, BOK), Image.LANCZOS).convert("RGBA")
    px = maly.load()
    for y in range(BOK):
        for x in range(BOK):
            if px[x, y][3] == 0:
                px[x, y] = (0, 0, 0, 0)
    maly.save(cel, "PNG", optimize=True)


def main():
    katalog = sys.argv[1] if len(sys.argv) > 1 else os.path.join("ikonki", "grajek")
    os.makedirs(katalog, exist_ok=True)
    for nazwa, f in IKONY.items():
        zapisz(f, os.path.join(katalog, nazwa))
        print("zapisano", os.path.join(katalog, nazwa))


if __name__ == "__main__":
    main()
