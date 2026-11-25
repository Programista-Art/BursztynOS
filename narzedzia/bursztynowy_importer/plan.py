"""Planowanie importu: kategorie, katalogi docelowe, nazwy, konflikty i dokladne zapotrzebowanie na miejsce.

Katalogi docelowe sa kanonicznymi katalogami Bursztyn OS (nie zgadywanymi):
  * muzyka    -> /dane/muzyka                      (jadro/fs/psf_bootstrap.h, Grajek: programy/grajek/biblioteka.h)
  * obrazy    -> /dane/zdjecia                     (psf_bootstrap.h, galeria zdjec: jadro/fs/galeria_kopiowanie.h)
  * dokumenty -> /uzytkownicy/domyslny/Dokumenty   (jadro/fs/katalogi_uzytkownika.h)
Wlasny katalog musi lezec pod /dane albo /uzytkownicy (drzewa systemowe sa chronione, jak w ochrona_sciezek.h).
"""
import os
import stat
import unicodedata

from . import bsp2
from .zapis import bloki_dla_rozmiaru

KATALOG_MUZYKI = '/dane/muzyka'
KATALOG_OBRAZOW = '/dane/zdjecia'
KATALOG_DOKUMENTOW = '/uzytkownicy/domyslny/Dokumenty'
DOZWOLONE_KORZENIE = ('/dane', '/uzytkownicy')
GRAJEK_MAKS_SCIEZKA = 191        # programy/grajek/zrodlo.h: MAKS_SCIEZKA = 192 (z NUL)

KATEGORIE = {
    'muzyka': ('Muzyka', KATALOG_MUZYKI, {'.mp3', '.wav'}),
    'obrazy': ('Obrazy', KATALOG_OBRAZOW, {'.jpg', '.jpeg', '.png', '.webp', '.gif', '.bmp'}),
    'dokumenty': ('Dokumenty', KATALOG_DOKUMENTOW, {'.txt', '.pdf', '.doc', '.docx', '.odt', '.rtf'}),
}

POMIN, ZMIEN_NAZWE, ZASTAP = 'pomin', 'zmien_nazwe', 'zastap'
NAZWY_KONFLIKTOW = {POMIN: 'Pomiń', ZMIEN_NAZWE: 'Zmień nazwę', ZASTAP: 'Zastąp'}


def kategoria(nazwa):
    ext = os.path.splitext(nazwa)[1].lower()
    for k, (_, _, rozsz) in KATEGORIE.items():
        if ext in rozsz:
            return k
    return 'inne'


def domyslny_katalog(kat):
    return KATEGORIE[kat][1] if kat in KATEGORIE else None


def sprawdz_katalog_docelowy(sciezka):
    """Zwraca znormalizowana sciezke albo zglasza ValueError z polskim opisem."""
    if not sciezka or not sciezka.startswith('/'):
        raise ValueError('Katalog docelowy musi być ścieżką bezwzględną Bursztyn OS (np. /dane/muzyka).')
    if '\\' in sciezka or '//' in sciezka or sciezka.endswith('/') and sciezka != '/':
        raise ValueError('Niedozwolone znaki w ścieżce docelowej (\\\\, //, końcowy /).')
    segmenty = sciezka.strip('/').split('/')
    if any(s in ('', '.', '..') for s in segmenty):
        raise ValueError('Ścieżka docelowa nie może zawierać "." ani "..".')
    if not any(sciezka == k or sciezka.startswith(k + '/') for k in DOZWOLONE_KORZENIE):
        raise ValueError('Import jest dozwolony tylko do katalogów pod /dane albo /uzytkownicy.')
    for s in segmenty:
        if not bsp2.nazwa_poprawna(s.encode('utf-8')):
            raise ValueError('Segment "%s" nie jest poprawną nazwą Bursztyn OS (1-55 bajtów UTF-8).' % s)
    if len(sciezka.encode('utf-8')) > bsp2.MAKS_SCIEZKA:
        raise ValueError('Ścieżka docelowa przekracza 511 bajtów.')
    return sciezka


def blad_nazwy(nazwa):
    """None gdy nazwa jest poprawna w PSF; inaczej polski opis problemu."""
    try:
        b = nazwa.encode('utf-8')
    except UnicodeEncodeError:
        return 'Nazwa zawiera znaki, których nie można zapisać w UTF-8.'
    if nazwa in ('.', '..') or not b:
        return 'Nazwa "." i ".." jest zarezerwowana.'
    if any(ord(c) < 0x20 for c in nazwa) or '/' in nazwa or '\\' in nazwa:
        return 'Nazwa zawiera znak sterujący, "/" lub "\\".'
    if len(b) > bsp2.MAKS_NAZWA:
        return 'Nazwa ma %d bajtów UTF-8, a system plików Bursztyn OS przyjmuje najwyżej %d.' % (len(b), bsp2.MAKS_NAZWA)
    return None


def _obetnij_utf8(tekst, limit_bajtow):
    wynik = ''
    for znak in tekst:
        if len((wynik + znak).encode('utf-8')) > limit_bajtow:
            break
        wynik += znak
    return wynik


def proponowana_nazwa(nazwa, zajete=(), znacznik='~'):
    """Bezpieczna propozycja (<= 55 bajtow, zachowane rozszerzenie, poprawne UTF-8). Nigdy nie stosowana bez zgody."""
    nazwa = unicodedata.normalize('NFC', nazwa)
    nazwa = ''.join('_' if ord(c) < 0x20 or c in '/\\' else c for c in nazwa).strip() or 'plik'
    stem, ext = os.path.splitext(nazwa)
    if len(ext.encode('utf-8')) > 12:
        stem, ext = nazwa, ''
    for nr in range(1, 1000):
        dopisek = '' if nr == 1 and len(nazwa.encode('utf-8')) <= bsp2.MAKS_NAZWA else '%s%d' % (znacznik, nr)
        miejsce = bsp2.MAKS_NAZWA - len((dopisek + ext).encode('utf-8'))
        kand = _obetnij_utf8(stem, miejsce).rstrip(' .') + dopisek + ext
        if kand not in zajete and blad_nazwy(kand) is None:
            return kand
    raise ValueError('Nie można zaproponować unikalnej nazwy.')


def nazwa_po_zmianie(nazwa, zajete):
    """Rozwiazanie konfliktu "Zmien nazwe": "utwor (2).mp3", "utwor (3).mp3"... w limicie 55 bajtow."""
    stem, ext = os.path.splitext(nazwa)
    for nr in range(2, 10000):
        dopisek = ' (%d)' % nr
        miejsce = bsp2.MAKS_NAZWA - len((dopisek + ext).encode('utf-8'))
        kand = _obetnij_utf8(stem, miejsce) + dopisek + ext
        if kand not in zajete:
            return kand
    raise ValueError('Nie można wyznaczyć nowej nazwy.')


class Pozycja:
    def __init__(self, zrodlo, rozmiar, kat, katalog, nazwa):
        self.zrodlo = zrodlo
        self.rozmiar = rozmiar
        self.kategoria = kat
        self.katalog = katalog            # katalog docelowy w Bursztyn OS
        self.nazwa_zrodla = os.path.basename(zrodlo)
        self.nazwa = nazwa                # nazwa docelowa (po decyzji uzytkownika)
        self.konflikt = POMIN             # domyslnie nigdy nie nadpisujemy
        self.zgoda_na_nazwe = False       # proponowana nazwa wymaga jawnej zgody
        self.propozycja = None            # bezpieczna nazwa proponowana przy zbyt dlugiej nazwie
        self.status = ''
        self.blad = None
        self.ostrzezenia = []
        self.wynik = None                 # 'zaimportowano' | 'pominieto' | 'zastapiono' | 'blad'
        self.sha256 = None
        self.nazwa_koncowa = None

    @property
    def sciezka_docelowa(self):
        return self.katalog.rstrip('/') + '/' + (self.nazwa_koncowa or self.nazwa)


def dodaj_pliki(lista, sciezki, katalog_wlasny=None):
    """Dodaje zwykle pliki. Dowiazania symboliczne i nie-pliki sa odrzucane z komunikatem."""
    komunikaty = []
    for s in sciezki:
        try:
            st = os.lstat(s)
        except OSError as e:
            komunikaty.append('Nie można odczytać %s: %s' % (s, e.strerror))
            continue
        if stat.S_ISLNK(st.st_mode):
            komunikaty.append('Pominięto dowiązanie symboliczne: %s' % s)
            continue
        if not stat.S_ISREG(st.st_mode):
            komunikaty.append('Pominięto element, który nie jest zwykłym plikiem: %s' % s)
            continue
        if any(os.path.samefile(p.zrodlo, s) for p in lista if os.path.exists(p.zrodlo)):
            komunikaty.append('Plik jest już na liście: %s' % s)
            continue
        kat = kategoria(s)
        katalog = katalog_wlasny or domyslny_katalog(kat)
        nazwa = unicodedata.normalize('NFC', os.path.basename(s))
        p = Pozycja(os.path.abspath(s), st.st_size, kat, katalog, nazwa)
        if katalog is None:
            p.blad = 'Nieznany typ pliku - wybierz "Własny katalog" pod /dane lub /uzytkownicy.'
        lista.append(p)
    return komunikaty


def dodaj_folder(lista, folder, katalog_wlasny=None, maks_plikow=5000):
    """Rekurencyjnie (bez podazania za dowiazaniami symbolicznymi). Struktura podfolderow nie jest odtwarzana:
    pliki trafiaja do katalogu swojej kategorii (Grajek skanuje /dane/muzyka z podkatalogami, ale import plaski
    jest przewidywalny i nie tworzy niechcianych drzew)."""
    komunikaty = []
    sciezki = []
    for korzen, katalogi, pliki in os.walk(folder, followlinks=False):
        for k in list(katalogi):
            if os.path.islink(os.path.join(korzen, k)):
                komunikaty.append('Pominięto dowiązanie symboliczne do folderu: %s' % os.path.join(korzen, k))
                katalogi.remove(k)
        for p in sorted(pliki):
            sciezki.append(os.path.join(korzen, p))
            if len(sciezki) > maks_plikow:
                komunikaty.append('Folder zawiera ponad %d plików - dodano tylko pierwsze %d.' % (maks_plikow, maks_plikow))
                break
        if len(sciezki) > maks_plikow:
            break
    komunikaty += dodaj_pliki(lista, sciezki[:maks_plikow], katalog_wlasny)
    return komunikaty


class Zapotrzebowanie:
    def __init__(self):
        self.bloki = 0
        self.wezly = 0
        self.bajty = 0
        self.pliki = 0


def ocen(lista, wolumin):
    """Wypelnia status/bledy pozycji wzgledem stanu dysku i zwraca Zapotrzebowanie (bloki, wezly).

    Pesymistycznie: kazdy nowy katalog i kazdy modyfikowany blok katalogu moze wymagac nowego bloku
    (kopiowanie przy zapisie), zastapione pliki nie zwalniaja miejsca w ramach tej samej transakcji."""
    z = Zapotrzebowanie()
    istniejace = {}            # katalog -> set nazw
    nowe_katalogi = set()

    def nazwy_w(katalog):
        if katalog not in istniejace:
            w = wolumin.znajdz(katalog)
            if w is None:
                istniejace[katalog] = {}
                nowe_katalogi.add(katalog)
            elif w.typ != bsp2.TYP_KATALOG:
                raise ValueError('%s istnieje w Bursztyn OS, ale nie jest katalogiem.' % katalog)
            else:
                istniejace[katalog] = {n.decode('utf-8', 'replace'): cel for (n, cel, _, _) in wolumin.wpisy(w)}
        return istniejace[katalog]

    zmienione_katalogi = {}     # katalog -> liczba dodawanych wpisow
    zastapienia = {}
    for p in lista:
        p.status, p.ostrzezenia, p.nazwa_koncowa = '', [], None
        if p.blad and p.katalog is None:
            p.status = 'Wybierz katalog'
            continue
        p.blad = None
        try:
            sprawdz_katalog_docelowy(p.katalog)
            nazwy = nazwy_w(p.katalog)
        except ValueError as e:
            p.blad = str(e)
            p.status = 'Błąd celu'
            continue
        p.propozycja = None
        problem = blad_nazwy(p.nazwa)
        if problem:
            p.propozycja = proponowana_nazwa(p.nazwa, nazwy)
            p.blad = problem + ' Proponowana nazwa: "%s" (wymaga Twojej zgody).' % p.propozycja
            p.status = 'Nazwa za długa'
            continue
        if p.rozmiar > bsp2.MAKS_ROZMIAR_PLIKU:
            p.blad = ('Plik ma %d bajtów, a format BSP2 przyjmuje najwyżej %d bajtów na plik.'
                      % (p.rozmiar, bsp2.MAKS_ROZMIAR_PLIKU))
            p.status = 'Za duży'
            continue
        if not os.path.isfile(p.zrodlo):
            p.blad = 'Plik źródłowy nie istnieje.'
            p.status = 'Brak pliku'
            continue
        if os.path.getsize(p.zrodlo) != p.rozmiar:
            p.rozmiar = os.path.getsize(p.zrodlo)
        nazwa = p.nazwa
        if nazwa in nazwy:
            cel = nazwy[nazwa]
            jest_katalogiem = cel > 0 and wolumin.wezel(cel).typ == bsp2.TYP_KATALOG
            if p.konflikt == POMIN:
                p.status = 'Istnieje – pominięty'
                p.nazwa_koncowa = nazwa
                continue
            if p.konflikt == ZASTAP:
                if jest_katalogiem or cel == -1:
                    p.blad = 'W celu istnieje katalog albo inny importowany plik o tej nazwie - nie można zastąpić.'
                    p.status = 'Konflikt'
                    continue
                p.status = 'Zastąpi istniejący'
                zastapienia[p.katalog] = zastapienia.get(p.katalog, 0) + 1
            else:
                nazwa = nazwa_po_zmianie(nazwa, nazwy)
                p.status = 'Nowa nazwa: ' + nazwa
        else:
            p.status = 'Gotowy'
        p.nazwa_koncowa = nazwa
        nazwy[nazwa] = -1                         # rezerwacja w ramach planu (duplikaty na liscie)
        n, posr = bloki_dla_rozmiaru(p.rozmiar)
        z.bloki += n + posr
        z.wezly += 1
        z.bajty += p.rozmiar
        z.pliki += 1
        zmienione_katalogi[p.katalog] = zmienione_katalogi.get(p.katalog, 0) + 1
        if p.kategoria == 'muzyka' and len(p.sciezka_docelowa.encode('utf-8')) > GRAJEK_MAKS_SCIEZKA:
            p.ostrzezenia.append('Ścieżka jest dłuższa niż 191 bajtów - Grajek tego pliku nie odtworzy.')
    # katalogi do utworzenia (wraz z brakujacymi rodzicami)
    do_utworzenia = set()
    for k in nowe_katalogi:
        if k not in zmienione_katalogi:
            continue
        czesci = k.strip('/').split('/')
        for i in range(1, len(czesci) + 1):
            s = '/' + '/'.join(czesci[:i])
            if wolumin.znajdz(s) is None:
                do_utworzenia.add(s)
    z.wezly += len(do_utworzenia)
    # Wpisy dodawane do kazdego katalogu (pliki + tworzone podkatalogi). Modyfikowany blok katalogu z oryginalu jest
    # kopiowany do nowego bloku (oryginalne bloki nie sa nadpisywane), wiec gorne oszacowanie na katalog:
    # 2 * (ceil(wpisy / 64) + 1) blokow, plus jedna kopia bloku na kazde usuniecie wpisu przy zastapieniu.
    wpisy = dict(zmienione_katalogi)
    for k in do_utworzenia:
        rodzic = k.rsplit('/', 1)[0] or '/'
        wpisy[rodzic] = wpisy.get(rodzic, 0) + 1
    for k, n in wpisy.items():
        z.bloki += 2 * ((n + bsp2.WPISOW_NA_BLOK - 1) // bsp2.WPISOW_NA_BLOK + 1)
    z.bloki += sum(zastapienia.values())
    z.katalogi_do_utworzenia = sorted(do_utworzenia)
    return z
