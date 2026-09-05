#!/usr/bin/env python3
"""Hostowy konwerter obrazów do BTP1. Bez argumentów uruchamia GUI Tkinter."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import queue
import struct
import sys
import tempfile
import threading
import warnings


NAGLOWEK = struct.Struct("<4sIIBB")
MAKS_WYMIAR = 8192
MAKS_PIKSELI = MAKS_WYMIAR * MAKS_WYMIAR
# Konserwatywny budzet buforow, nie gwarancja limitu RSS biblioteki Pillow.
MAKS_PAMIECI = 768 * 1024 * 1024
PRESETY = ("800x600", "1024x768", "1280x720", "1920x1080")
POMOC_PILLOW = (
    "Brak biblioteki Pillow. Debian/Ubuntu: zainstaluj pakiet python3-pil. "
    "W środowisku pip: pip install Pillow."
)


class BladKonwersji(ValueError):
    """Błąd danych lub ustawień, który można wyświetlić bez tracebacku."""


def pillow():
    # Import leniwy: --help i sam encoder nie potrzebuja Pillow ani Tkinter.
    try:
        from PIL import Image, ImageOps
    except ImportError as blad:
        raise BladKonwersji(POMOC_PILLOW) from blad
    if not hasattr(Image, "Resampling"):
        raise BladKonwersji("Wymagana jest biblioteka Pillow 9.1 lub nowsza. " + POMOC_PILLOW)
    return Image, ImageOps


def sprawdz_wymiary(szerokosc: int, wysokosc: int) -> int:
    if (type(szerokosc) is not int or type(wysokosc) is not int or
            not 1 <= szerokosc <= MAKS_WYMIAR or
            not 1 <= wysokosc <= MAKS_WYMIAR):
        raise BladKonwersji("Wymiary muszą mieścić się w zakresie 1–8192 pikseli.")
    return szerokosc * wysokosc


def wczytajobraz(sciezka, max_width: int = 1920, max_height: int = 1080):
    """Zwraca RGBA; wywolujacy zamyka obraz. Waliduje przed load/convert."""
    sprawdz_wymiary(max_width, max_height)
    Image, ImageOps = pillow()
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", Image.DecompressionBombWarning)
            with Image.open(sciezka) as zrodlo:
                piksele = sprawdz_wymiary(*zrodlo.size)
                cel = min(piksele, max_width * max_height)
                if 16 * piksele + 20 * cel > MAKS_PAMIECI:
                    raise BladKonwersji(
                        "Obraz przekracza budżet pamięci konwertera (768 MiB). "
                        "Zmniejsz maksymalne wymiary docelowe lub obraz źródłowy."
                    )
                if getattr(zrodlo, "n_frames", 1) != 1:
                    raise BladKonwersji("Wybierz obraz statyczny, z jedną klatką.")
                zrodlo.load()
                sprawdz_wymiary(*zrodlo.size)
                with ImageOps.exif_transpose(zrodlo) as obraz:
                    sprawdz_wymiary(*obraz.size)
                    # thumbnail nie powieksza; RGBA przed resize zapewnia LANCZOS
                    # rowniez dla paletowych PNG (tryb P wymusza NEAREST w Pillow).
                    with obraz.convert("RGBA") as rgba:
                        rgba.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
                        sprawdz_wymiary(*rgba.size)
                        return rgba.copy()
    except BladKonwersji:
        raise
    except (Image.DecompressionBombWarning, Image.DecompressionBombError) as blad:
        raise BladKonwersji("Pillow odrzucił obraz jako możliwą bombę dekompresyjną.") from blad
    except MemoryError as blad:
        raise BladKonwersji("Za mało pamięci do odczytu obrazu.") from blad
    except Exception as blad:
        # Granica niezaufanych dekoderow/metadanych Pillow (takze wadliwy EXIF).
        # KeyboardInterrupt i SystemExit nie sa przechwytywane.
        raise BladKonwersji(f"Nie można odczytać obrazu {sciezka}: {blad}") from blad


def przygotujpiksele(obraz, optymalizuj: bool = False,
                     zachowajalfa: bool = False) -> bytes:
    """Pillow -> BGRA bez listy krotek pikseli; RGB & 0xF8 jest stratne."""
    liczba = sprawdz_wymiary(*obraz.size)
    if 20 * liczba > MAKS_PAMIECI:
        raise BladKonwersji("Obraz przekracza budżet pamięci. Zmniejsz wymiary.")
    rgb = [kanal & 0xF8 if optymalizuj else kanal for kanal in range(256)]
    alfa = list(range(256)) if zachowajalfa else [255] * 256
    # LUT ma tylko 1024 pozycje. Nie kwantyzujemy alfa. Domyslnie wymuszamy
    # nieprzezroczystosc, zachowujac RGB (bez mieszania z kolorem tla).
    if obraz.mode == "RGBA":
        with obraz.point(rgb * 3 + alfa) as gotowy:
            return gotowy.tobytes("raw", "BGRA")
    with obraz.convert("RGBA") as rgba:
        with rgba.point(rgb * 3 + alfa) as gotowy:
            return gotowy.tobytes("raw", "BGRA")


def kodujraw(piksele: bytes) -> bytes:
    if not isinstance(piksele, bytes):
        raise BladKonwersji("Piksele muszą być niezmiennym buforem bytes BGRA.")
    if len(piksele) % 4 or len(piksele) > MAKS_PIKSELI * 4:
        raise BladKonwersji("Nieprawidłowy rozmiar bufora BGRA.")
    return piksele  # Bez kopii, paddingu i metadanych. Pusty bufor jest bezpieczny.


def serie_rle(piksele: bytes):
    """Pary (liczba, offset BGRA), lacznie z seriami przecinajacymi wiersze."""
    kodujraw(piksele)
    liczba = 0
    poczatek = 0
    poprzedni = None
    # Native I sluzy WYLACZNIE porownaniu rownosci 4 bajtow, nigdy zapisowi
    # koloru. Bajty wyjscia sa kopiowane z BGRA, niezaleznie od endianness hosta.
    widok = memoryview(piksele).cast("I")
    if widok.itemsize != 4:
        raise BladKonwersji("Host musi obsługiwać 32-bitowy unsigned int.")
    for indeks, kolor in enumerate(widok):
        if liczba and kolor == poprzedni and liczba < 255:
            liczba += 1
        else:
            if liczba:
                yield liczba, poczatek
            liczba, poczatek, poprzedni = 1, indeks * 4, kolor
    if liczba:
        yield liczba, poczatek


def rozmiarrle(piksele: bytes) -> int:
    return sum(5 for _ in serie_rle(piksele))


def kodujrle(piksele: bytes) -> bytearray:
    wynik = bytearray()
    widok = memoryview(piksele)
    for liczba, offset in serie_rle(piksele):
        wynik.append(liczba)
        wynik.extend(widok[offset:offset + 4])
    return wynik


def wybierztryb(rozmiarraw: int, rozmiarrle: int, tryb: str = "auto") -> int:
    if tryb not in ("auto", "raw", "rle") or min(rozmiarraw, rozmiarrle) < 0:
        raise BladKonwersji("Nieprawidłowy tryb lub rozmiar payloadu.")
    return int(tryb == "rle" or (tryb == "auto" and rozmiarrle < rozmiarraw))


@dataclass(frozen=True)
class DaneBtp:
    szerokosc: int
    wysokosc: int
    kompresja: int
    payload: bytes | bytearray
    rozmiarraw: int
    rozmiarrle: int

    @property
    def naglowek(self) -> bytes:
        return NAGLOWEK.pack(b"BTP1", self.szerokosc, self.wysokosc, 32, self.kompresja)

    @property
    def tryb(self) -> str:
        return "RLE" if self.kompresja else "RAW"

    @property
    def rozmiarpliku(self) -> int:
        return NAGLOWEK.size + len(self.payload)

    @property
    def oszczednosc(self) -> float:
        # Uwzgledniamy naglowek: RAW kosztuje o 14 bajtow wiecej od pikseli.
        return 100.0 * (1.0 - self.rozmiarpliku / self.rozmiarraw)


def kodujbtp(piksele: bytes, szerokosc: int, wysokosc: int,
             tryb: str = "auto") -> DaneBtp:
    oczekiwane = sprawdz_wymiary(szerokosc, wysokosc) * 4
    raw = kodujraw(piksele)
    if len(raw) != oczekiwane:
        raise BladKonwersji("Liczba pikseli nie odpowiada wymiarom obrazu.")
    rozmiar = rozmiarrle(raw)
    kompresja = wybierztryb(len(raw), rozmiar, tryb)
    payload = kodujrle(raw) if kompresja else raw
    return DaneBtp(szerokosc, wysokosc, kompresja, payload, len(raw), rozmiar)


def zapiszbtp(sciezka, dane: DaneBtp) -> Path:
    """Publikuje kompletny plik atomowo, bez nadpisywania (takze przy wyscigu)."""
    sciezka = Path(sciezka).with_suffix(".btp")
    sciezka.parent.mkdir(parents=True, exist_ok=True)
    tymczasowy = None
    try:
        with tempfile.NamedTemporaryFile(dir=sciezka.parent, prefix=".btp-",
                                         suffix=".tmp", delete=False) as plik:
            tymczasowy = Path(plik.name)
            plik.write(dane.naglowek)
            plik.write(dane.payload)
            plik.flush()
            os.fsync(plik.fileno())
        for numer in range(10000):
            cel = sciezka if numer == 0 else sciezka.with_name(
                f"{sciezka.stem}_{numer}.btp")
            try:
                # rename/replace moze nadpisac cudzy plik; link jest atomowym
                # opublikowaniem kompletnego pliku z warunkiem "cel nie istnieje".
                os.link(tymczasowy, cel)
            except FileExistsError:
                continue
            return cel
        raise BladKonwersji("Brak wolnej nazwy wyjściowej (10000 kolizji).")
    finally:
        if tymczasowy is not None:
            tymczasowy.unlink(missing_ok=True)


def konwertuj(wejscie, wyjscie=None, *, max_width=1920, max_height=1080,
              optymalizuj=False, zachowajalfa=False, tryb="auto"):
    wejscie = Path(wejscie)
    with wczytajobraz(wejscie, max_width, max_height) as obraz:
        szerokosc, wysokosc = obraz.size
        piksele = przygotujpiksele(obraz, optymalizuj, zachowajalfa)
    dane = kodujbtp(piksele, szerokosc, wysokosc, tryb)
    if wyjscie is None:
        wyjscie = Path("konwertowane") / wejscie.with_suffix(".btp").name
    return zapiszbtp(wyjscie, dane), dane


def raport(wejscie, wyjscie, dane: DaneBtp) -> str:
    stosunek = 100.0 * dane.rozmiarpliku / dane.rozmiarraw
    return (
        f"{Path(wejscie).name} → {wyjscie}\n"
        f"{dane.szerokosc}×{dane.wysokosc} | Tryb {dane.tryb}\n"
        f"RAW {dane.rozmiarraw} B | RLE {dane.rozmiarrle} B | "
        f"BTP {dane.rozmiarpliku} B\n"
        f"Rozmiar BTP / piksele RAW: {stosunek:.1f}% | "
        f"Oszczędność: {dane.oszczednosc:.1f}% (z nagłówkiem)"
    )


class KonwerterGui:
    def __init__(self, root):
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk

        self.root, self.tk = root, tk
        self.filedialog, self.messagebox = filedialog, messagebox
        self.pliki = []
        self.zajety = False
        self.kolejka = queue.Queue()
        root.title("Konwerter BTP Bursztyn OS")
        root.geometry("850x680")
        root.minsize(650, 540)
        root.protocol("WM_DELETE_WINDOW", self.zamknij)
        ramka = ttk.Frame(root, padding=12)
        ramka.pack(fill="both", expand=True)
        ramka.columnconfigure(0, weight=1)
        ramka.rowconfigure(2, weight=1)
        ramka.rowconfigure(8, weight=2)
        pasek = ttk.Frame(ramka)
        pasek.grid(row=0, column=0, sticky="ew")
        ttk.Button(pasek, text="Wybierz pliki", command=self.wybierzpliki).pack(side="left")
        ttk.Button(pasek, text="Usuń zaznaczone", command=self.usunpliki).pack(side="left", padx=6)
        ttk.Label(ramka, text="Lista wybranych plików").grid(row=1, column=0, sticky="w", pady=(8, 0))
        lista = ttk.Frame(ramka)
        lista.grid(row=2, column=0, sticky="nsew")
        self.lista = tk.Listbox(lista, selectmode="extended", height=6)
        suwak = ttk.Scrollbar(lista, command=self.lista.yview)
        self.lista.configure(yscrollcommand=suwak.set)
        suwak.pack(side="right", fill="y")
        self.lista.pack(fill="both", expand=True)
        ustawienia = ttk.LabelFrame(ramka, text="Docelowy ekran", padding=8)
        ustawienia.grid(row=3, column=0, sticky="ew", pady=8)
        self.preset = tk.StringVar(value="1920x1080")
        wybor = ttk.Combobox(ustawienia, textvariable=self.preset, values=PRESETY,
                             state="readonly", width=14)
        wybor.grid(row=0, column=0)
        wybor.bind("<<ComboboxSelected>>", self.ustawpreset)
        self.szerokosc = tk.StringVar(value="1920")
        self.wysokosc = tk.StringVar(value="1080")
        ttk.Label(ustawienia, text="Maks. szerokość").grid(row=0, column=1, padx=(12, 4))
        ttk.Entry(ustawienia, textvariable=self.szerokosc, width=7).grid(row=0, column=2)
        ttk.Label(ustawienia, text="Maks. wysokość").grid(row=0, column=3, padx=(12, 4))
        ttk.Entry(ustawienia, textvariable=self.wysokosc, width=7).grid(row=0, column=4)
        ttk.Label(ustawienia, text="Zachowuje proporcje; nie powiększa mniejszych obrazów.").grid(
            row=1, column=0, columnspan=5, sticky="w", pady=(6, 0))
        opcje = ttk.Frame(ramka)
        opcje.grid(row=4, column=0, sticky="w")
        self.optymalizuj = tk.BooleanVar(value=False)
        self.alfa = tk.BooleanVar(value=False)
        ttk.Checkbutton(opcje, text="Optymalizuj kolory (stratnie: RGB do 5 bitów)",
                        variable=self.optymalizuj).pack(anchor="w")
        ttk.Checkbutton(opcje, text="Zachowaj kanał alfa", variable=self.alfa).pack(anchor="w")
        folder = ttk.Frame(ramka)
        folder.grid(row=5, column=0, sticky="ew", pady=8)
        folder.columnconfigure(1, weight=1)
        self.folder = tk.StringVar(value="konwertowane")
        ttk.Label(folder, text="Folder wyjściowy").grid(row=0, column=0, padx=(0, 8))
        ttk.Entry(folder, textvariable=self.folder).grid(row=0, column=1, sticky="ew")
        ttk.Button(folder, text="Wybierz folder", command=self.wybierzfolder).grid(row=0, column=2, padx=8)
        self.przycisk = ttk.Button(ramka, text="Konwertuj do BTP", command=self.start)
        self.przycisk.grid(row=6, column=0, sticky="w")
        self.status = tk.StringVar(value="Gotowy. Tryb AUTO: RLE wygrywa tylko, gdy jest mniejszy od RAW.")
        ttk.Label(ramka, textvariable=self.status, wraplength=620).grid(row=7, column=0, sticky="w", pady=8)
        wyniki = ttk.Frame(ramka)
        wyniki.grid(row=8, column=0, sticky="nsew")
        self.log = tk.Text(wyniki, height=10, wrap="word", state="disabled")
        przewijanie = ttk.Scrollbar(wyniki, command=self.log.yview)
        self.log.configure(yscrollcommand=przewijanie.set)
        przewijanie.pack(side="right", fill="y")
        self.log.pack(fill="both", expand=True)
        root.after(100, self.odczytajkolejke)

    def ustawpreset(self, _zdarzenie=None):
        szerokosc, wysokosc = self.preset.get().split("x")
        self.szerokosc.set(szerokosc)
        self.wysokosc.set(wysokosc)

    def wybierzpliki(self):
        nazwy = self.filedialog.askopenfilenames(title="Wybierz obrazy", filetypes=[
            ("Obrazy PNG, JPG, JPEG, BMP", "*.png *.jpg *.jpeg *.bmp *.PNG *.JPG *.JPEG *.BMP"),
            ("Wszystkie pliki", "*")])
        for nazwa in nazwy:
            if nazwa not in self.pliki:
                self.pliki.append(nazwa)
                self.lista.insert("end", nazwa)

    def usunpliki(self):
        for indeks in reversed(self.lista.curselection()):
            del self.pliki[indeks]
            self.lista.delete(indeks)

    def wybierzfolder(self):
        folder = self.filedialog.askdirectory(title="Folder wyjściowy")
        if folder:
            self.folder.set(folder)

    def start(self):
        if self.zajety:
            return
        try:
            if not self.pliki:
                raise BladKonwersji("Najpierw wybierz pliki do konwersji.")
            szerokosc, wysokosc = int(self.szerokosc.get()), int(self.wysokosc.get())
            sprawdz_wymiary(szerokosc, wysokosc)
            if not self.folder.get().strip():
                raise BladKonwersji("Wybierz folder wyjściowy.")
            pillow()
        except ValueError as blad:
            self.messagebox.showerror("Nie można rozpocząć konwersji", str(blad))
            return
        ustawienia = dict(max_width=szerokosc, max_height=wysokosc,
                           optymalizuj=self.optymalizuj.get(), zachowajalfa=self.alfa.get())
        self.zajety = True
        self.przycisk.configure(state="disabled")
        self.status.set(f"Konwersja {len(self.pliki)} plików…")
        threading.Thread(target=self.pracuj, args=(tuple(self.pliki), Path(self.folder.get()),
                                                   ustawienia), daemon=False).start()

    def pracuj(self, pliki, folder, ustawienia):
        sukcesy, bledy = 0, 0
        for numer, wejscie in enumerate(pliki, 1):
            self.kolejka.put(("status", f"{numer}/{len(pliki)}: {Path(wejscie).name}"))
            try:
                wyjscie, dane = konwertuj(wejscie, folder / Path(wejscie).with_suffix(".btp").name,
                                         **ustawienia)
                self.kolejka.put(("log", raport(wejscie, wyjscie, dane)))
                del dane  # Zwolnij payload przed dekodowaniem kolejnego pliku.
                sukcesy += 1
            except Exception as blad:
                self.kolejka.put(("log", f"BŁĄD: {wejscie}: {blad}"))
                bledy += 1
        self.kolejka.put(("done", f"Zakończono. Zapisano: {sukcesy}, błędy: {bledy}."))

    def odczytajkolejke(self):
        try:
            while True:
                typ, tekst = self.kolejka.get_nowait()
                if typ == "log":
                    self.log.configure(state="normal")
                    self.log.insert("end", tekst + "\n\n")
                    self.log.configure(state="disabled")
                    self.log.see("end")
                else:
                    self.status.set(tekst)
                    if typ == "done":
                        self.zajety = False
                        self.przycisk.configure(state="normal")
        except queue.Empty:
            pass
        self.root.after(100, self.odczytajkolejke)

    def zamknij(self):
        if self.zajety:
            self.messagebox.showinfo("Trwa konwersja", "Poczekaj na zakończenie zapisu plików.")
        else:
            self.root.destroy()


def uruchomgui() -> int:
    pillow()
    try:
        import tkinter as tk
    except ImportError as blad:
        raise BladKonwersji("Brak Tkinter. Zainstaluj python3-tk lub użyj trybu CLI.") from blad
    try:
        root = tk.Tk()
    except tk.TclError as blad:
        raise BladKonwersji(f"Nie można uruchomić GUI: {blad}. Użyj trybu CLI.") from blad
    KonwerterGui(root)
    root.mainloop()
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Konwerter BTP Bursztyn OS; bez argumentów uruchamia GUI.")
    parser.add_argument("input", nargs="?", help="obraz PNG, JPG, JPEG lub BMP")
    parser.add_argument("output", nargs="?", help="plik .btp; kolizje dostają numer _1, _2, …")
    parser.add_argument("--output-dir", default="konwertowane", help="folder, gdy pominięto output")
    parser.add_argument("--max-width", type=int, default=1920, help="maksymalna szerokość, 1–8192")
    parser.add_argument("--max-height", type=int, default=1080, help="maksymalna wysokość, 1–8192")
    parser.add_argument("--optimize", action="store_true", help="stratne RGB & 0xF8; bez kwantyzacji alfa")
    parser.add_argument("--preserve-alpha", action="store_true", help="zachowaj alfa zamiast wymuszać 255")
    tryby = parser.add_mutually_exclusive_group()
    tryby.add_argument("--force-raw", action="store_true", help="wymuś RAW (domyślnie AUTO)")
    tryby.add_argument("--force-rle", action="store_true", help="wymuś RLE, nawet jeśli jest większy")
    argumenty = list(sys.argv[1:] if argv is None else argv)
    args = parser.parse_args(argumenty)
    if argumenty and args.input is None:
        parser.error("podaj obraz wejściowy; GUI uruchamia się bez argumentów")
    try:
        if args.input is None:
            return uruchomgui()
        sprawdz_wymiary(args.max_width, args.max_height)
        tryb = "raw" if args.force_raw else "rle" if args.force_rle else "auto"
        wyjscie = args.output or Path(args.output_dir) / Path(args.input).with_suffix(".btp").name
        cel, dane = konwertuj(args.input, wyjscie, max_width=args.max_width,
                             max_height=args.max_height, optymalizuj=args.optimize,
                             zachowajalfa=args.preserve_alpha, tryb=tryb)
        print(raport(args.input, cel, dane))
        return 0
    except (BladKonwersji, OSError, MemoryError) as blad:
        print(f"Błąd: {blad}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Konwersja przerwana.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
