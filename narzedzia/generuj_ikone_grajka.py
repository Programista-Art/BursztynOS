#!/usr/bin/env python3
"""Generuje ikonki/app_grajek.png (128x128 RGBA) - oryginalna ikona odtwarzacza Grajek.

Narzedzie deweloperskie (Pillow), ten sam styl co narzedzia/generuj_ikone_kalendarza.py: bursztynowy kafel z cieniem
i polyskiem. Motyw: ciemna plyta winylowa z bursztynowa etykieta i biala nuta (wlasny rysunek, bez cudzych znakow).
Nie modyfikuje innych plikow; nie nadpisuje istniejacego pliku bez --nadpisz.
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

    # Plyta (cien + rowki + etykieta)
    cx, cy, r = 58, 66, 40
    pshadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(pshadow).ellipse(((cx - r) * S, (cy - r + 3) * S, (cx + r) * S, (cy + r + 3) * S), fill=(90, 40, 0, 130))
    img.alpha_composite(pshadow.filter(ImageFilter.GaussianBlur(2 * S)))
    d = ImageDraw.Draw(img)
    d.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), fill=(30, 26, 24, 255))
    for k in range(r - 4, 16, -4):
        shade = 44 if (k // 4) % 2 else 36
        d.ellipse(((cx - k) * S, (cy - k) * S, (cx + k) * S, (cy + k) * S), outline=(shade, shade - 4, shade - 6, 255), width=S)
    d.ellipse(((cx - 15) * S, (cy - 15) * S, (cx + 15) * S, (cy + 15) * S), fill=(242, 140, 0, 255))
    d.ellipse(((cx - 3) * S, (cy - 3) * S, (cx + 3) * S, (cy + 3) * S), fill=(30, 26, 24, 255))
    # odblask na plycie
    hl = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(hl).pieslice(((cx - r + 4) * S, (cy - r + 4) * S, (cx + r - 4) * S, (cy + r - 4) * S), 200, 240, fill=(255, 255, 255, 40))
    img.alpha_composite(hl.filter(ImageFilter.GaussianBlur(S)))

    # Nuta (osemka) po prawej
    d = ImageDraw.Draw(img)
    white = (252, 250, 246, 255)
    edge = (120, 60, 4, 255)
    hx, hy = 96, 92
    d.ellipse(((hx - 9) * S, (hy - 6) * S, (hx + 7) * S, (hy + 6) * S), fill=white, outline=edge, width=S)
    d.rectangle(((hx + 4) * S, 40 * S, (hx + 8) * S, hy * S), fill=white, outline=edge, width=S)
    d.polygon([((hx + 6) * S, 40 * S), ((hx + 20) * S, 50 * S), ((hx + 18) * S, 60 * S), ((hx + 8) * S, 52 * S)], fill=white, outline=edge)

    img = img.resize((128, 128), Image.LANCZOS)
    img.save(out, "PNG", optimize=True)


if __name__ == "__main__":
    out = next((a for a in sys.argv[1:] if not a.startswith("--")), "ikonki/app_grajek.png")
    if os.path.exists(out) and "--nadpisz" not in sys.argv:
        sys.exit(f"{out} juz istnieje - nie nadpisuje (uzyj --nadpisz)")
    main(out)
