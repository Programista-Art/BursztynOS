#!/usr/bin/env python3
"""Generuje ikonki/app_menedzer_zadan.png (128x128 RGBA) - oryginalna ikona Menedzera zadan.

Narzedzie deweloperskie (Pillow); wynik jest zwyklym zasobem PNG wbudowywanym przez Makefile
tak jak pozostale ikony aplikacji. Nie modyfikuje zadnych innych plikow.

Motyw: bursztynowy kafel z ciemnym ekranem, wykres aktywnosci procesora i slupki pamieci.
Rysowane w 4x nadprobkowaniu i zmniejszane (wygladzanie krawedzi).
"""
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

    # Cien kafla
    shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((10 * S, 14 * S, 118 * S, 122 * S), radius=24 * S, fill=(0, 0, 0, 110))
    shadow = shadow.filter(ImageFilter.GaussianBlur(5 * S))
    img.alpha_composite(shadow)

    # Bursztynowy kafel z gradientem
    tile_box = (8 * S, 8 * S, 120 * S, 118 * S)
    tile = gradient(N, (255, 196, 64, 255), (205, 106, 8, 255))
    img.paste(tile, (0, 0), rounded_mask(N, tile_box, 24 * S))

    # Polysk w gornej czesci kafla
    gloss = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(gloss).rounded_rectangle((12 * S, 11 * S, 116 * S, 54 * S), radius=20 * S, fill=(255, 255, 255, 70))
    gloss = gloss.filter(ImageFilter.GaussianBlur(2 * S))
    img.alpha_composite(gloss)

    # Ciemny ekran
    d = ImageDraw.Draw(img)
    screen = (20 * S, 22 * S, 108 * S, 104 * S)
    d.rounded_rectangle(screen, radius=10 * S, fill=(26, 33, 44, 255), outline=(120, 64, 0, 255), width=2 * S)

    # Siatka
    for gy in (42, 62, 82):
        d.line((26 * S, gy * S, 102 * S, gy * S), fill=(58, 70, 88, 255), width=S)
    for gx in (45, 70, 95):
        d.line((gx * S, 28 * S, gx * S, 98 * S), fill=(48, 58, 74, 255), width=S)

    # Slupki pamieci (na dole ekranu)
    bars = [(30, 18), (44, 30), (58, 24), (72, 38), (86, 28)]
    for x, h in bars:
        d.rounded_rectangle((x * S, (98 - h) * S, (x + 9) * S, 98 * S), radius=2 * S, fill=(242, 169, 0, 200))

    # Wykres aktywnosci procesora (z poswiata)
    pts = [(26, 70), (36, 66), (44, 74), (52, 48), (60, 58), (68, 36), (76, 54), (86, 44), (94, 50), (102, 34)]
    pts = [(x * S, y * S) for x, y in pts]
    glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(glow).line(pts, fill=(80, 230, 160, 160), width=8 * S, joint="curve")
    glow = glow.filter(ImageFilter.GaussianBlur(3 * S))
    img.alpha_composite(glow)
    d = ImageDraw.Draw(img)
    d.line(pts, fill=(110, 245, 180, 255), width=4 * S, joint="curve")
    for p in (pts[3], pts[5], pts[-1]):
        d.ellipse((p[0] - 4 * S, p[1] - 4 * S, p[0] + 4 * S, p[1] + 4 * S), fill=(235, 255, 245, 255))

    img = img.resize((128, 128), Image.LANCZOS)
    img.save(out, "PNG", optimize=True)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "ikonki/app_menedzer_zadan.png")
