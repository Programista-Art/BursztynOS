#!/usr/bin/env python3
"""Generuje ikonki/app_kalendarz.png (128x128 RGBA) - oryginalna ikona aplikacji Kalendarz.

Narzedzie deweloperskie (Pillow), ten sam styl co narzedzia/generuj_ikone_menedzera_zadan.py: bursztynowy kafel
z cieniem i polyskiem. Motyw: kartka kalendarza z dwoma pierscieniami, pasem naglowka i siatka dni (bez cyfr -
ikona nie zawiera zadnej konkretnej daty). Jedna komorka wyrozniona akcentem jako "dzisiaj".
Nie modyfikuje zadnych innych plikow; nie nadpisuje istniejacego pliku bez --nadpisz.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter

S = 4
N = 128 * S


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def gradient(size, top, bottom):
    img = Image.new("RGBA", (size, size))
    px = img.load()
    for y in range(size):
        c = lerp(top, bottom, y / (size - 1))
        for x in range(size):
            px[x, y] = c
    return img


def rounded_mask(size, box, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=255)
    return m


def main(out):
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))

    shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((10 * S, 14 * S, 118 * S, 122 * S), radius=24 * S, fill=(0, 0, 0, 110))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(5 * S)))

    tile_box = (8 * S, 8 * S, 120 * S, 118 * S)
    img.paste(gradient(N, (255, 196, 64, 255), (205, 106, 8, 255)), (0, 0), rounded_mask(N, tile_box, 24 * S))
    gloss = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(gloss).rounded_rectangle((12 * S, 11 * S, 116 * S, 54 * S), radius=20 * S, fill=(255, 255, 255, 70))
    img.alpha_composite(gloss.filter(ImageFilter.GaussianBlur(2 * S)))

    # Kartka kalendarza (z delikatnym cieniem)
    page = (22 * S, 26 * S, 106 * S, 106 * S)
    pshadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(pshadow).rounded_rectangle((page[0], page[1] + 3 * S, page[2], page[3] + 3 * S), radius=10 * S, fill=(90, 40, 0, 120))
    img.alpha_composite(pshadow.filter(ImageFilter.GaussianBlur(2 * S)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(page, radius=10 * S, fill=(250, 248, 244, 255), outline=(150, 80, 10, 255), width=2 * S)

    # Pas naglowka (ciemny grafit - jak pasek tytulu okien Bursztyna)
    head = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(head).rectangle((page[0], page[1], page[2], 46 * S), fill=(38, 46, 60, 255))
    img.paste(head, (0, 0), Image.composite(head, Image.new("RGBA", (N, N)), rounded_mask(N, page, 10 * S)).split()[3])
    d = ImageDraw.Draw(img)

    # Pierscienie
    for cx in (44, 84):
        d.rounded_rectangle(((cx - 4) * S, 18 * S, (cx + 4) * S, 34 * S), radius=4 * S, fill=(70, 78, 92, 255),
                            outline=(20, 24, 32, 255), width=S)
        d.ellipse(((cx - 2) * S, 37 * S, (cx + 2) * S, 41 * S), fill=(20, 24, 32, 255))

    # Siatka dni 7 x 4 (bez cyfr); jedna komorka "dzisiaj" w akcencie
    x0, y0, cw, ch = 30, 54, 10, 11
    for r in range(4):
        for c in range(7):
            x, y = x0 + c * cw, y0 + r * ch
            if r == 2 and c == 3:
                d.rounded_rectangle((x * S, y * S, (x + 8) * S, (y + 8) * S), radius=2 * S, fill=(242, 140, 0, 255))
            else:
                col = (232, 120, 40, 255) if c >= 5 else (150, 160, 175, 255)
                d.rounded_rectangle(((x + 1) * S, (y + 1) * S, (x + 7) * S, (y + 7) * S), radius=S, fill=col)

    img = img.resize((128, 128), Image.LANCZOS)
    img.save(out, "PNG", optimize=True)


if __name__ == "__main__":
    out = next((a for a in sys.argv[1:] if not a.startswith("--")), "ikonki/app_kalendarz.png")
    if os.path.exists(out) and "--nadpisz" not in sys.argv:
        sys.exit(f"{out} juz istnieje - nie nadpisuje (uzyj --nadpisz)")
    main(out)
