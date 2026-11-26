#!/usr/bin/env python3
"""Rzeczywisty manifest instalacji Bursztyn OS dla testow hosta (tylko odczyt repozytorium).

Odtwarza liste plikow, ktore jadro planuje w kroku SYSTEMFILES instalatora
(jadro/rdzen/kernel.cpp: instaluj_wbudowane_programy + bsf/bootstrap.inc), z tych samych bajtow:
  * pakiety .cebula (manifest z literalow C + program .bur rozpakowany z bloba BZB1),
  * bloby ld -b binary (_binary_<sciezka>_start -> plik zrodlowy repozytorium),
  * moduly Multiboot2 z wpisu instalatora w isodir/boot/grub/grub.cfg (czcionki bsf/*, bsf-settings,
    pulpit/test*.bmp),
oraz tapety z isodir/boot/tapety.pak (etap WALLPAPERS: /tapety/<nazwa>).

Wynik (katalog wyjsciowy): pliki z danymi i manifest.tsv:
    SYSTEM<TAB><sciezka BSP2><TAB><plik>
    TAPETA<TAB><sciezka BSP2><TAB><plik>
Kod 0 = OK; kazdy nierozwiazany symbol lub brakujacy plik konczy sie bledem (nic nie jest pomijane po cichu).
"""
import re
import struct
import sys
import zlib
from pathlib import Path

KORZEN = Path(__file__).resolve().parents[2]


def blad(msg):
    raise SystemExit(f"manifest_instalacji: {msg}")


def cialo_funkcji(tekst, naglowek):
    i = tekst.find(naglowek)
    if i < 0:
        blad(f"brak funkcji {naglowek}")
    j = tekst.index("{", i)
    gl = 0
    for k in range(j, len(tekst)):
        if tekst[k] == "{":
            gl += 1
        elif tekst[k] == "}":
            gl -= 1
            if gl == 0:
                return tekst[j:k]
    blad("niezamknieta funkcja")


def bez_testow_psf(tekst):
    """Usuwa galezie #if BURSZTYN_TESTY_PSF (zostawia #else), jak kompilacja z BURSZTYN_TESTY_PSF=0."""
    wynik, stos = [], []
    for linia in tekst.split("\n"):
        s = linia.strip()
        if s.startswith("#if"):
            stos.append("psf" if "BURSZTYN_TESTY_PSF" in s else "inne")
            if stos[-1] == "inne":
                wynik.append(linia)
            stos[-1] = (stos[-1], False)
            continue
        if s.startswith("#else") and stos:
            rodzaj, _ = stos[-1]
            stos[-1] = (rodzaj, True)
            if rodzaj == "inne":
                wynik.append(linia)
            continue
        if s.startswith("#endif") and stos:
            rodzaj, _ = stos.pop()
            if rodzaj == "inne":
                wynik.append(linia)
            continue
        if any(r == "psf" and not w for r, w in stos):
            continue
        wynik.append(linia)
    return "\n".join(wynik)


def literal_c(fragment):
    """Laczy sasiednie literaly C ("a" "b") i dekoduje sekwencje ucieczki (\\n, \\", \\\\)."""
    out = bytearray()
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', fragment):
        s = m.group(1).encode("utf-8")
        i = 0
        while i < len(s):
            c = s[i]
            if c == 0x5C:
                n = s[i + 1]
                out += {ord("n"): b"\n", ord("t"): b"\t", ord('"'): b'"', ord("\\"): b"\\"}.get(n) or blad(f"ucieczka \\{chr(n)}")
                i += 2
            else:
                out.append(c)
                i += 1
    return bytes(out)


def zmangluj(sciezka_wzgledna):
    return "_binary_" + re.sub(r"[^A-Za-z0-9]", "_", sciezka_wzgledna)


def indeks_blobow():
    kandydaci = {}
    for p in list(KORZEN.glob("*.bin.bz")) + list(KORZEN.glob("*.txt")) + [KORZEN / "dane/sponsorze/sponsorzy"]:
        if p.is_file():
            kandydaci[zmangluj(str(p.relative_to(KORZEN)))] = p
    for katalog in ("ikonki", "grajek_zasoby", "obrazy", "dane/sponsorze"):
        for p in (KORZEN / katalog).glob("*"):
            if p.is_file():
                kandydaci[zmangluj(str(p.relative_to(KORZEN)))] = p
    return kandydaci


def dane_bloba(symbol, kandydaci):
    p = kandydaci.get(symbol)
    if p is None:
        blad(f"nie znaleziono pliku dla symbolu {symbol}_start")
    dane = p.read_bytes()
    if dane[:4] == b"BZB1":
        rozmiar, crc = struct.unpack_from("<II", dane, 4)
        surowe = zlib.decompress(dane[12:])
        if len(surowe) != rozmiar or zlib.crc32(surowe) & 0xFFFFFFFF != crc:
            blad(f"uszkodzony blob {p}")
        return surowe
    return dane


def stala_sciezki(wyrazenie):
    """Literal albo stala (EKRAN_PROGRAM, AppIdentity::Calendar...) z naglowkow jadra."""
    if wyrazenie.startswith('"'):
        return wyrazenie.strip('"')
    nazwa = wyrazenie.split("::")[-1]
    naglowki = ["jadro/gui/app_identity.h", "jadro/gui/ekran_format.h", "jadro/gui/tapety_format.h",
                "jadro/diag/diagnostyka_systemu_format.h"]
    for _ in range(4):
        for n in naglowki:
            t = (KORZEN / n).read_text(encoding="utf-8")
            m = re.search(r"\b" + re.escape(nazwa) + r"\s*(?:\[\])?\s*=\s*([^;]+);", t)
            if m:
                w = m.group(1).strip()
                if w.startswith('"'):
                    return w.strip('"')
                nazwa = w.split("::")[-1]
                break
        else:
            break
    blad(f"nie rozwiazano stalej sciezki {wyrazenie}")


def moduly_instalatora(grub_cfg):
    tekst = grub_cfg.read_text(encoding="utf-8")
    m = re.search(r'menuentry "Bursztyn OS Instalator" \{(.*?)\n\}', tekst, re.S)
    if not m:
        blad("brak wpisu instalatora w grub.cfg")
    mods = {}
    for plik, etykieta in re.findall(r"module2\s+(\S+)\s+(\S+)", m.group(1)):
        mods[etykieta] = grub_cfg.parents[2] / plik.lstrip("/")
    return mods


def czcionka_katalog(nazwa):
    low = nazwa.lower()
    for ext, kat in ((".otf", "/czcionki/otf/"), (".ttc", "/czcionki/ttc/"), (".otc", "/czcionki/otc/")):
        if low.endswith(ext):
            return kat
    return "/czcionki/ttf/"


def nietrwale_zrodla(cialo):
    """Lokalne (nie-static) tablice przekazywane jako dane pliku do funkcji planu instalacji.

    Plan krokowy jadra zapamietuje tylko WSKAZNIK do danych pliku (planuj_plik), a zapis nastepuje w
    pozniejszych krokach instalatora: lokalna tablica funkcji planujacej jest wtedy martwa i nadpisana
    (natywny blad 7: /programy/pcm440.cebula/opis.aplikacji). Kazda tablica-zrodlo musi byc static.
    """
    lokalne = re.findall(r"(?m)^[ \t]*const (?:char|uint8_t|unsigned char) (\w+)\[\]\s*=", cialo)
    return sorted(n for n in set(lokalne)
                  if re.search(r"\b(?:instaluj_paczke|zapisz_manifest|zapisz_plik_od_nowa|zapisz_blob_bur)\([^;]*\b"
                               + re.escape(n) + r"\b", cialo, re.S))


def manifest(isodir):
    kernel = (KORZEN / "jadro/rdzen/kernel.cpp").read_text(encoding="utf-8")
    cialo = bez_testow_psf(cialo_funkcji(kernel, "void instaluj_wbudowane_programy() {"))
    kandydaci = indeks_blobow()
    mods = moduly_instalatora(isodir / "boot/grub/grub.cfg")
    manifesty = {n: literal_c(f) for n, f in re.findall(r"(?:static )?const char (\w+)\[\]\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;", cialo)}
    nietrwale = nietrwale_zrodla(cialo)
    if nietrwale:
        blad("zrodlo planu instalacji na stosie (brak static): " + ", ".join(nietrwale))
    pliki = []   # (sciezka, bajty) w kolejnosci planu jadra

    # bsf_import_fonts() jest wywolywane jako pierwsze; kolejnosc modulow jak w grub.cfg.
    for etykieta, plik in mods.items():
        if etykieta.startswith("bsf/"):
            nazwa = etykieta[4:]
            pliki.append((czcionka_katalog(nazwa) + nazwa, plik.read_bytes()))
    # Kolejnosc w ciele funkcji: wszystkie zapisy w kolejnosci wystapienia.
    wzorce = [
        ("paczka_mod", re.compile(r'instaluj_paczke\(\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*reinterpret_cast<const uint8_t\*>\(uintptr_t\(settings_payload\.start\)\)')),
        ("paczka", re.compile(r'instaluj_paczke\(\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*("[^"]+"|[\w:]+)\s*,\s*(_binary_\w+?)_start')),
        ("blob", re.compile(r'zapisz_blob_bur\(\s*"([^"]+)"\s*,\s*(_binary_\w+?)_start')),
        ("plik", re.compile(r'zapisz_plik_od_nowa\(\s*"([^"]+)"[^;]*?(_binary_\w+?)_start', re.S)),
        ("zasob", re.compile(r'\{\s*"(/[^"]+)"\s*,\s*(_binary_\w+?)_start\s*,')),
        ("bmp", re.compile(r'instaluj_bmp_z_modulu\(\s*obrazy_startowe::(\w+)\s*,\s*obrazy_startowe::(\w+)')),
    ]
    trafienia = []
    for rodzaj, wz in wzorce:
        for m in wz.finditer(cialo):
            trafienia.append((m.start(), rodzaj, m))
    trafienia.sort(key=lambda t: t[0])
    zajete = []
    obrazy_startowe = (KORZEN / "jadro/rdzen/obrazy_startowe.h").read_text(encoding="utf-8")
    stale = dict(re.findall(r'constexpr const char\* (\w+) = "([^"]+)";', obrazy_startowe))
    for poz, rodzaj, m in trafienia:
        if any(a <= poz < b for a, b in zajete):
            continue
        zajete.append((m.start(), m.end()))
        if rodzaj == "paczka_mod":
            if "bsf-settings" not in mods:
                blad("brak modulu bsf-settings")
            pliki.append((m.group(1), manifesty[m.group(2)]))
            pliki.append((m.group(3), mods["bsf-settings"].read_bytes()))
        elif rodzaj == "paczka":
            if m.group(2) not in manifesty:
                blad(f"brak manifestu {m.group(2)}")
            pliki.append((m.group(1), manifesty[m.group(2)]))
            pliki.append((stala_sciezki(m.group(3)), dane_bloba(m.group(4), kandydaci)))
        elif rodzaj in ("blob", "plik", "zasob"):
            pliki.append((m.group(1), dane_bloba(m.group(2), kandydaci)))
        elif rodzaj == "bmp":
            etykieta, sciezka = stale[m.group(1)], stale[m.group(2)]
            if etykieta in mods:
                pliki.append((sciezka, mods[etykieta].read_bytes()))
    return pliki


def tapety(isodir):
    d = (isodir / "boot/tapety.pak").read_bytes()
    magic, wersja, liczba, nagl, tabela, dane, razem = struct.unpack_from("<8sIIIIII", d, 0)
    if magic != b"BTPK1\0\0\0" or razem != len(d):
        blad("tapety.pak: zly naglowek")
    wynik, poz = [], nagl
    for _ in range(liczba):
        dl, rozmiar, crc, off = struct.unpack_from("<IIII", d, poz)
        nazwa = d[poz + 16:poz + 16 + dl].decode("utf-8")
        poz += 16 + ((dl + 3) & ~3)
        bajty = d[off:off + rozmiar]
        if zlib.crc32(bajty) & 0xFFFFFFFF != crc:
            blad(f"tapety.pak: CRC {nazwa}")
        wynik.append(("/tapety/" + nazwa, bajty))
    return wynik


def main():
    if len(sys.argv) != 3:
        blad("uzycie: manifest_instalacji.py <isodir> <katalog_wyjsciowy>")
    isodir, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    wiersze = []
    for rodzaj, lista in (("SYSTEM", manifest(isodir)), ("TAPETA", tapety(isodir))):
        for i, (sciezka, bajty) in enumerate(lista):
            plik = out / f"{rodzaj.lower()}_{i:03d}.bin"
            plik.write_bytes(bajty)
            wiersze.append(f"{rodzaj}\t{sciezka}\t{plik.resolve()}")
    (out / "manifest.tsv").write_text("\n".join(wiersze) + "\n", encoding="utf-8")
    sys_n = sum(1 for w in wiersze if w.startswith("SYSTEM"))
    print(f"manifest: {sys_n} plikow systemowych, {len(wiersze) - sys_n} tapet")


if __name__ == "__main__":
    main()
