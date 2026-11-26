#!/usr/bin/env python3
"""Zasoby Grajka z grafik uzytkownika (krok budowania, oryginaly sa tylko czytane).

Ikony przyciskow: ikonki/grajek/*.png -> grajek_zasoby/grajek_<akcja>.png, 128x128 RGBA.
Ikona, ktora JUZ jest 8-bitowym RGBA 128x128 bez przeplotu (obecne grafiki uzytkownika), jest kopiowana BAJT W BAJT:
zadnego przeliczania, wiec kolory i alfa wygladzonych krawedzi zostaja oryginalne (dawniej przejscie 128 -> 128
przez premnozona alfe zmienialo RGB pikseli o malej alfie). Inny rozmiar: proporcje zachowane (dopasowanie do
kwadratu, przezroczyste marginesy), skalowanie Lanczos na premnozonej alfie (bez obwodek), bez zmiany kolorow. 128x128 to limit dekodera ikon
Ring 3 (dekoduj_ikone_png: <= 128x128, <= 256 KiB); BursztynUI skaluje dalej raz na rozmiar (srednia obszarowa).

Plyta winylowa: ikonki/plyta_winylowa.jpg jest osadzana BEZ zmian (Makefile linkuje oryginalny plik); to narzedzie
tylko sprawdza, ze istnieje i jest JPEG-iem.

Mapowanie plik -> akcja jest jawne (nazwy plikow uzytkownika, w tym "previovs.png"). Brak pliku z mapy = blad
budowania z nazwa pliku; plik spoza mapy jest zglaszany i pomijany (nie jest przypisywany do zadnej akcji).

Uzycie: przygotuj_zasoby_grajka.py <katalog_wyjscia>   |   --sprawdz (tylko walidacja zrodel)
"""
import hashlib
import os
import sys

ZRODLO_IKON = os.path.join("ikonki", "grajek")
PLYTA = os.path.join("ikonki", "plyta_winylowa.jpg")
BOK = 128

# plik uzytkownika -> nazwa akcji (plik wyjsciowy grajek_<akcja>.png, sciezka w systemie /ikonki/grajek_<akcja>.png)
MAPA_IKON = {
    "previovs.png": "poprzedni",
    "play.png": "odtworz",
    "pause.png": "pauza",
    "stop.png": "stop",
    "next.png": "nastepny",
    "mute.png": "wycisz",
    "refresh.png": "odswiez",
    "add to.png": "dodaj",
    "move up.png": "w_gore",
    "move down.png": "w_dol",
    "delete.png": "usun",
    "clear.png": "wyczysc",
    "save list.png": "zapisz",
    "load list.png": "wczytaj",
    # przelaczniki: ikony rysowane w stylu grafik uzytkownika (narzedzia/generuj_ikony_grajka.py)
    "shuffle.png": "losowo",
    "repeat.png": "powtarzanie",
    "repeat one.png": "powtarzanie_jeden",
    "background.png": "w_tle",
    "visualization.png": "wizualizacja",
}


def sprawdz_zrodla():
    bledy = []
    for plik in MAPA_IKON:
        p = os.path.join(ZRODLO_IKON, plik)
        if not os.path.isfile(p):
            bledy.append(f"brak ikony uzytkownika: {p}")
    if not os.path.isfile(PLYTA):
        bledy.append(f"brak grafiki plyty: {PLYTA}")
    else:
        with open(PLYTA, "rb") as f:
            if f.read(3) != b"\xff\xd8\xff":
                bledy.append(f"{PLYTA} nie jest plikiem JPEG")
        if os.path.getsize(PLYTA) > 256 * 1024:
            bledy.append(f"{PLYTA} > 256 KiB (limit zasobu osadzanego w jadrze)")
    nadmiarowe = sorted(set(os.listdir(ZRODLO_IKON)) - set(MAPA_IKON)) if os.path.isdir(ZRODLO_IKON) else []
    return bledy, nadmiarowe


def gotowa_ikona(zrodlo):
    """8-bitowe RGBA (typ koloru 6) BOK x BOK bez przeplotu - dekoder ikon przyjmuje je bez zmian."""
    with open(zrodlo, "rb") as f:
        n = f.read(29)
    if len(n) < 29 or n[:8] != b"\x89PNG\r\n\x1a\n" or n[12:16] != b"IHDR":
        return False
    w, h = int.from_bytes(n[16:20], "big"), int.from_bytes(n[20:24], "big")
    return w == BOK and h == BOK and n[24] == 8 and n[25] == 6 and n[28] == 0


def konwertuj(zrodlo, cel):
    tmp = cel + ".tmp"
    if gotowa_ikona(zrodlo):
        with open(zrodlo, "rb") as f, open(tmp, "wb") as g:
            g.write(f.read())
        os.replace(tmp, cel)
        return
    from PIL import Image
    with Image.open(zrodlo) as im:
        im.load()
        rgba = im.convert("RGBA")
    w, h = rgba.size
    skala = min(BOK / w, BOK / h)
    nw, nh = max(1, round(w * skala)), max(1, round(h * skala))
    # premnozona alfa: kolory przezroczystych pikseli nie przeciekaja do krawedzi
    male = rgba.convert("RGBa").resize((nw, nh), Image.LANCZOS).convert("RGBA")
    plotno = Image.new("RGBA", (BOK, BOK), (0, 0, 0, 0))
    plotno.paste(male, ((BOK - nw) // 2, (BOK - nh) // 2))
    plotno.save(tmp, format="PNG", optimize=True)
    os.replace(tmp, cel)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bledy, nadmiarowe = sprawdz_zrodla()
    for n in nadmiarowe:
        print(f"[zasoby grajka] pominieto plik spoza mapy akcji: {os.path.join(ZRODLO_IKON, n)}")
    if bledy:
        for b in bledy:
            print(f"[zasoby grajka] BLAD: {b}", file=sys.stderr)
        return 1
    if sys.argv[1] == "--sprawdz":
        print(f"[zasoby grajka] zrodla OK: {len(MAPA_IKON)} ikon, {PLYTA} ({os.path.getsize(PLYTA)} B)")
        return 0
    wyj = sys.argv[1]
    os.makedirs(wyj, exist_ok=True)
    for plik, akcja in MAPA_IKON.items():
        cel = os.path.join(wyj, f"grajek_{akcja}.png")
        konwertuj(os.path.join(ZRODLO_IKON, plik), cel)
        if os.path.getsize(cel) > 256 * 1024:
            print(f"[zasoby grajka] BLAD: {cel} > 256 KiB", file=sys.stderr)
            return 1
    with open(PLYTA, "rb") as f:
        print(f"[zasoby grajka] {len(MAPA_IKON)} ikon -> {wyj}; plyta bez zmian, sha256 {hashlib.sha256(f.read()).hexdigest()[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
