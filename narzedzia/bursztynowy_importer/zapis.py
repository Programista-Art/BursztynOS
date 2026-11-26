"""Zapis BSP2 offline - WYLACZNIE na kopii roboczej obrazu (nigdy na oryginale).

Odwzorowuje operacje jadra (jadro/fs/psf.cpp):
  * zaalokuj_wolny_wezel(): najnizszy wolny wezel, wyzerowany, wskazniki = BARK_BLOKU,
  * ustaw_metadane_nowego_wezla(): PSF_META_MAGIC | WERSJA_2 | PZB_WAZNY | poziom PZB (+ czas RTC YYYYMMDDhhmmss),
  * dodaj_wpis_do_katalogu(): pierwszy wolny wpis w blokach bezposrednich, nowy blok katalogu gdy brak miejsca,
    licznik wpisow katalogu += 1,
  * bloki danych: bezposrednie, 1x posredni (blok_posredni_1), 2x posredni (blok_posredni_2); bloki posrednie
    wypelnione BARK_BLOKU; zwalniany blok jest zerowany (jak zwolnij_blok_danych()).
Nowe bloki sa brane wylacznie z wolnych bitow bitmapy ORYGINALU - sprawdzane i zapisywane w `nowe_bloki`,
dzieki czemu weryfikacja moze udowodnic, ze zaden istniejacy blok nie zostal nadpisany.
"""
import hashlib
import os
import struct
import time

from . import bsp2
from .bsp2 import BLOK, BRAK, BEZPOSREDNIE, NA_BLOK, TYP_KATALOG, TYP_PLIK, TYP_WOLNY, BladObrazu

PORCJA = 1 << 20


class Przerwano(Exception):
    """Anulowanie na bezpiecznej granicy (miedzy plikami)."""


def czas_psf(t=None):
    lt = time.localtime(t)
    return int('%04d%02d%02d%02d%02d%02d' % (lt.tm_year, lt.tm_mon, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec))


def bloki_dla_rozmiaru(rozmiar):
    """(bloki danych, bloki posrednie) potrzebne na plik - tak jak ustaw_wskaznik_bloku() alokuje tablice."""
    n = (rozmiar + BLOK - 1) // BLOK
    if n > bsp2.MAKS_BLOKOW_PLIKU:
        raise BladObrazu('Plik przekracza maksymalny rozmiar formatu BSP2.')
    posr = 0
    if n > BEZPOSREDNIE:
        posr += 1
    if n > BEZPOSREDNIE + NA_BLOK:
        posr += 1 + (n - BEZPOSREDNIE - NA_BLOK + NA_BLOK - 1) // NA_BLOK
    return n, posr


class Zapisywacz:
    def __init__(self, sciezka_kopii, dysk_oryginalu):
        """`dysk_oryginalu` - zwalidowany bsp2.Dysk ORYGINALU (zrodlo prawdy o wolnych blokach)."""
        self.f = open(sciezka_kopii, 'r+b')
        self.w = bsp2.Wolumin(self.f, dysk_oryginalu.bsp_start, dysk_oryginalu.bsp_sektory)
        self.oryg = dysk_oryginalu.wolumin
        if self.w.meta != self.oryg.meta:
            raise BladObrazu('Kopia robocza różni się od oryginału - przerwano przed zapisem.')
        self.nowe_bloki = set()
        self.zwolnione_bloki = set()
        self.zmienione_wezly = set()
        self.nowe_wezly = set()
        self.czas = czas_psf()
        self._kursor = 0
        self._zwolnione_wezly = set()

    # ------------------------------------------------------------------ bitmapa i bloki
    def _ustaw_bit(self, bid, zajety):
        o = BLOK + bid // 8
        m = 1 << (bid & 7)
        self.w.meta[o] = (self.w.meta[o] | m) if zajety else (self.w.meta[o] & ~m & 0xFF)

    def wolne_bloki(self):
        return self.w.wolne_bloki()

    def _zaalokuj_blok(self):
        n = self.w.bloki_danych
        for przebieg in range(2):
            od, do = (self._kursor, n) if przebieg == 0 else (0, self._kursor)
            bid = od
            while bid < do:
                bajt = self.w.meta[BLOK + bid // 8]
                if bajt == 0xFF and bid % 8 == 0 and bid + 8 <= do:
                    bid += 8
                    continue
                if not (bajt >> (bid & 7)) & 1:
                    if self.oryg.blok_zajety(bid) and bid not in self.zwolnione_bloki:
                        raise BladObrazu('Wewnętrzny błąd: blok zajęty w oryginale wygląda na wolny.')
                    if self.oryg.blok_zajety(bid):
                        # Blok zwolniony w tej transakcji (zastapiony plik) - nie uzywamy go ponownie, aby
                        # weryfikacja mogla sprawdzic, ze dane istniejace w oryginale nie sa nadpisywane.
                        bid += 1
                        continue
                    self._ustaw_bit(bid, True)
                    self.nowe_bloki.add(bid)
                    self._kursor = bid + 1
                    return bid
                bid += 1
        raise BladObrazu('Brak wolnych bloków danych w systemie plików Bursztyn OS.')

    def _pisz_blok(self, bid, dane):
        if bid not in self.nowe_bloki:
            raise BladObrazu('Wewnętrzny błąd: zapis do bloku spoza nowo przydzielonych.')
        if len(dane) < BLOK:
            dane = dane + bytes(BLOK - len(dane))
        self.f.seek(self.w.start + (self.w.start_danych + bid) * BLOK)
        self.f.write(dane)

    def _zwolnij_blok(self, bid):
        # Jak zwolnij_blok_danych(): zerowanie zwalnianego bloku i wyczyszczenie bitu. Zerujemy dopiero po
        # zatwierdzeniu wszystkich zmian (oryginal pozostaje nietkniety, kopia robocza jest odrzucana przy bledzie).
        self.zwolnione_bloki.add(bid)
        self._ustaw_bit(bid, False)

    # ------------------------------------------------------------------ wezly
    def _zapisz_wezel(self, wid, typ, rozmiar, czas, flagi, wsk, p1, p2, p3=BRAK):
        b = bytearray(BLOK)
        b[0] = typ
        struct.pack_into('<QQQ', b, 1, rozmiar, czas, flagi)
        struct.pack_into('<%dQ' % BEZPOSREDNIE, b, bsp2.OFF_WSK, *wsk)
        struct.pack_into('<QQQ', b, bsp2.OFF_POSR1, p1, p2, p3)
        o = (self.w.start_wezlow + wid - 1) * BLOK
        self.w.meta[o:o + BLOK] = b
        self.zmienione_wezly.add(wid)

    def _flagi_nowego(self):
        return bsp2.META_MAGIC | bsp2.META_WERSJA_2 | bsp2.META_PZB_WAZNY | bsp2.META_CZAS_WAZNY | bsp2.PZB_UZYTKOWNIK

    def _zaalokuj_wezel(self, typ):
        for wid in range(1, self.w.liczba_wezlow + 1):
            o = (self.w.start_wezlow + wid - 1) * BLOK
            if self.w.meta[o] == TYP_WOLNY and wid not in self.zwolnione_wezly_set():
                self._zapisz_wezel(wid, typ, 0, self.czas, self._flagi_nowego(), [BRAK] * BEZPOSREDNIE, BRAK, BRAK)
                self.nowe_wezly.add(wid)
                return wid
        raise BladObrazu('Brak wolnych węzłów (każdy plik i katalog zajmuje jeden węzeł z puli systemu plików).')

    def zwolnione_wezly_set(self):
        return self._zwolnione_wezly

    def wolne_wezly(self):
        return self.w.wolne_wezly()

    # ------------------------------------------------------------------ katalogi
    def _dodaj_wpis(self, kat_id, nazwa_b, cel_id):
        k = self.w.wezel(kat_id)
        if k.typ != TYP_KATALOG:
            raise BladObrazu('Cel nie jest katalogiem.')
        for (n, _, _, _) in self.w.wpisy(k):
            if n == nazwa_b:
                raise BladObrazu('Element o tej nazwie już istnieje.')
        wsk = list(k.wsk)
        for i, bid in enumerate(wsk):
            if bid == BRAK:
                nowy = self._zaalokuj_blok()
                d = bytearray(BLOK)
                struct.pack_into('<Q', d, 0, cel_id)
                d[8:8 + len(nazwa_b)] = nazwa_b
                self._pisz_blok(nowy, bytes(d))
                wsk[i] = nowy
                self._zapisz_wezel(kat_id, TYP_KATALOG, k.rozmiar + 1, k.czas, k.flagi, wsk, k.posr1, k.posr2, k.posr3)
                return
            d = bytearray(self.w.czytaj_blok(bid))
            for j in range(bsp2.WPISOW_NA_BLOK):
                if bsp2.u64(d, j * 64) == 0:
                    if bid not in self.nowe_bloki:
                        # Blok katalogu istnial w oryginale: przenosimy jego zawartosc do nowego bloku
                        # (kopiowanie przy zapisie), wiec zaden blok oryginalu nie jest modyfikowany.
                        nowy = self._zaalokuj_blok()
                        self._zwolnij_blok(bid)
                        wsk[i] = nowy
                        bid = nowy
                    d[j * 64:j * 64 + 64] = struct.pack('<Q', cel_id) + nazwa_b + bytes(56 - len(nazwa_b))
                    self._pisz_blok(bid, bytes(d))
                    self._zapisz_wezel(kat_id, TYP_KATALOG, k.rozmiar + 1, k.czas, k.flagi, wsk, k.posr1, k.posr2, k.posr3)
                    return
        raise BladObrazu('Katalog osiągnął limit wpisów formatu BSP2.')

    def _usun_wpis(self, kat_id, nazwa_b):
        k = self.w.wezel(kat_id)
        wsk = list(k.wsk)
        for i, bid in enumerate(wsk):
            if bid == BRAK:
                continue
            d = bytearray(self.w.czytaj_blok(bid))
            for j in range(bsp2.WPISOW_NA_BLOK):
                if bsp2.u64(d, j * 64) and d[j * 64 + 8:j * 64 + 64].split(b'\0', 1)[0] == nazwa_b:
                    if bid not in self.nowe_bloki:
                        nowy = self._zaalokuj_blok()
                        self._zwolnij_blok(bid)
                        wsk[i] = nowy
                        bid = nowy
                    d[j * 64:j * 64 + 64] = bytes(64)
                    self._pisz_blok(bid, bytes(d))
                    self._zapisz_wezel(kat_id, TYP_KATALOG, max(0, k.rozmiar - 1), k.czas, k.flagi, wsk, k.posr1, k.posr2, k.posr3)
                    return
        raise BladObrazu('Nie znaleziono wpisu do usunięcia.')

    def znajdz_id(self, sciezka):
        wid = self.w.korzen
        if sciezka == '/':
            return wid
        for seg in sciezka.strip('/').split('/'):
            w = self.w.wezel(wid)
            if w.typ != TYP_KATALOG:
                return 0
            for (n, cel, _, _) in self.w.wpisy(w):
                if n == seg.encode('utf-8'):
                    wid = cel
                    break
            else:
                return 0
        return wid

    def zapewnij_katalog(self, sciezka):
        wid = self.w.korzen
        biezaca = ''
        for seg in sciezka.strip('/').split('/'):
            biezaca += '/' + seg
            nb = seg.encode('utf-8')
            if not bsp2.nazwa_poprawna(nb):
                raise BladObrazu('Niepoprawna nazwa katalogu: %s' % seg)
            cel = self.znajdz_id(biezaca)
            if cel:
                if self.w.wezel(cel).typ != TYP_KATALOG:
                    raise BladObrazu('%s istnieje i nie jest katalogiem.' % biezaca)
                wid = cel
                continue
            nowy = self._zaalokuj_wezel(TYP_KATALOG)
            self._dodaj_wpis(wid, nb, nowy)
            wid = nowy
        return wid

    # ------------------------------------------------------------------ pliki
    def usun_plik(self, katalog, nazwa):
        """Zastapienie: usuniecie istniejacego PLIKU (nigdy katalogu). Jego bloki nie sa ponownie uzywane."""
        kat_id = self.znajdz_id(katalog)
        nb = nazwa.encode('utf-8')
        wid = self.znajdz_id(katalog.rstrip('/') + '/' + nazwa)
        if not wid:
            return
        w = self.w.wezel(wid)
        if w.typ != TYP_PLIK:
            raise BladObrazu('Nie można zastąpić katalogu plikiem: %s' % nazwa)
        for b in self.w.wszystkie_wskazniki_pliku(w):
            self._zwolnij_blok(b)
        self._usun_wpis(kat_id, nb)
        self._zapisz_wezel(wid, TYP_WOLNY, 0, 0, 0, [BRAK] * BEZPOSREDNIE, BRAK, BRAK)   # jak wyzeruj_wezel()
        self._zwolnione_wezly.add(wid)

    def dodaj_plik(self, katalog, nazwa, zrodlo, postep=None, anuluj=None):
        """Strumieniowy zapis pliku hosta (porcje 1 MiB). Zwraca (rozmiar, sha256)."""
        nb = nazwa.encode('utf-8')
        if not bsp2.nazwa_poprawna(nb):
            raise BladObrazu('Niepoprawna nazwa pliku: %s' % nazwa)
        kat_id = self.znajdz_id(katalog)
        if not kat_id or self.w.wezel(kat_id).typ != TYP_KATALOG:
            raise BladObrazu('Katalog docelowy nie istnieje: %s' % katalog)
        h = hashlib.sha256()
        with open(zrodlo, 'rb') as src:
            rozmiar = os.fstat(src.fileno()).st_size
            n, _ = bloki_dla_rozmiaru(rozmiar)
            dane = []
            zapisane = 0
            while zapisane < rozmiar:
                porcja = src.read(min(PORCJA, rozmiar - zapisane))
                if not porcja:
                    raise BladObrazu('Plik źródłowy zmienił się podczas importu (krótszy niż oczekiwano): %s' % zrodlo)
                h.update(porcja)
                for o in range(0, len(porcja), BLOK):
                    bid = self._zaalokuj_blok()
                    self._pisz_blok(bid, porcja[o:o + BLOK])
                    dane.append(bid)
                zapisane += len(porcja)
                if postep:
                    postep(len(porcja))
                if anuluj and anuluj():
                    raise Przerwano()
            if src.read(1):
                raise BladObrazu('Plik źródłowy zmienił się podczas importu (dłuższy niż oczekiwano): %s' % zrodlo)
        if len(dane) != n:
            raise BladObrazu('Wewnętrzny błąd: liczba bloków pliku.')
        wsk = dane[:BEZPOSREDNIE] + [BRAK] * (BEZPOSREDNIE - min(BEZPOSREDNIE, len(dane)))
        p1 = p2 = BRAK
        if len(dane) > BEZPOSREDNIE:
            p1 = self._zaalokuj_blok()
            t = dane[BEZPOSREDNIE:BEZPOSREDNIE + NA_BLOK]
            self._pisz_blok(p1, struct.pack('<%dQ' % NA_BLOK, *(t + [BRAK] * (NA_BLOK - len(t)))))
        if len(dane) > BEZPOSREDNIE + NA_BLOK:
            p2 = self._zaalokuj_blok()
            reszta = dane[BEZPOSREDNIE + NA_BLOK:]
            l1 = []
            for i in range(0, len(reszta), NA_BLOK):
                b2 = self._zaalokuj_blok()
                t = reszta[i:i + NA_BLOK]
                self._pisz_blok(b2, struct.pack('<%dQ' % NA_BLOK, *(t + [BRAK] * (NA_BLOK - len(t)))))
                l1.append(b2)
            self._pisz_blok(p2, struct.pack('<%dQ' % NA_BLOK, *(l1 + [BRAK] * (NA_BLOK - len(l1)))))
        wid = self._zaalokuj_wezel(TYP_PLIK)
        self._zapisz_wezel(wid, TYP_PLIK, rozmiar, self.czas, self._flagi_nowego(), wsk, p1, p2)
        self._dodaj_wpis(kat_id, nb, wid)      # publikacja na koncu: wpis wskazuje gotowy wezel
        return rozmiar, h.hexdigest()

    # ------------------------------------------------------------------ zatwierdzenie
    def zatwierdz(self):
        """Zerowanie zwolnionych blokow, zapis metadanych (superblok bez zmian, bitmapa, wezly), fsync."""
        for bid in sorted(self.zwolnione_bloki):
            self.f.seek(self.w.start + (self.w.start_danych + bid) * BLOK)
            self.f.write(bytes(BLOK))
        if self.w.meta[:44] != self.oryg.meta[:44]:
            raise BladObrazu('Wewnętrzny błąd: superblok został zmieniony.')
        self.f.seek(self.w.start)
        self.f.write(self.w.meta)
        self.f.flush()
        os.fsync(self.f.fileno())

    def zamknij(self):
        self.f.close()
