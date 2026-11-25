"""Format dysku Bursztyn OS: GPT instalatora + system plikow BSP2/PSF. Czytnik i walidator TYLKO DO ODCZYTU.

Odwzorowanie 1:1 z kodu zrodlowego (nie z zalozen):
  * jadro/fs/storage_format.cpp - GPT (header(), gpt_read()), typy partycji, znacznik instalacji (marker(), complete()),
  * jadro/fs/psf.h / psf.cpp    - superblok (44 B), wezel 4096 B, wpis katalogu 64 B, bitmapa od bloku 1,
                                  wskazniki RELATYWNE do start_danych, 490 bezposrednich + 1x i 2x posrednie,
                                  wylicz_geometrie(), waliduj_metadane(), wskazniki_pliku_poprawne(), poprawna_nazwa().
Walidacja jest co najmniej tak scisla jak przy montowaniu w jadrze, a dodatkowo wykrywa niespojnosci, ktore jadro
naprawia po cichu przy starcie (wiszace wpisy, duplikaty nazw) albo ignoruje (wspoldzielone bloki, cykle katalogow).
"""
import binascii
import os
import struct

SEKTOR = 512
BLOK = 4096
SEKTORY_NA_BLOK = BLOK // SEKTOR
MAKS_NAZWA = 55                         # PSF_MAX_NAZWA - 1 (bajty UTF-8, bez NUL)
MAKS_SCIEZKA = 511                      # MAKS_DLUGOSC_SCIEZKI - 1
BEZPOSREDNIE = 490                      # PSF_MAX_BLOKOW_W_WEZLE
NA_BLOK = BLOK // 8                     # wskazniki w bloku posrednim
MAKS_BLOKOW_PLIKU = BEZPOSREDNIE + NA_BLOK + NA_BLOK * NA_BLOK
MAKS_ROZMIAR_PLIKU = MAKS_BLOKOW_PLIKU * BLOK
BRAK = 0xFFFFFFFFFFFFFFFF               # BARK_BLOKU
TYP_WOLNY, TYP_PLIK, TYP_KATALOG = 0, 1, 2
MIN_BLOKOW = 10                         # MINIMALNA_LICZBA_BLOKOW
RAM_METADANYCH = 128 * 1024 * 1024      # ROZMIAR_RAM_DYSKU_PSF: metadane musza zmiescic sie w RAM jadra
BSP_MAKS_BAJTY = 2 * 1024 * 1024 * 1024  # Storage::BspMaxBytes
WPISOW_NA_BLOK = BLOK // 64

META_MAGIC = 0x5053464D00000000
META_MAGIC_MASKA = 0xFFFFFFFF00000000
META_WERSJA_2 = 0x0000000002000000
META_CZAS_WAZNY = 0x100
META_PZB_WAZNY = 0x200
PZB_UZYTKOWNIK = 4

TYP_BSP = bytes([0x3c, 0xe7, 0x1f, 0xb2, 0x14, 0x04, 0x85, 0x45, 0xbd, 0x07, 0x8a, 0xb4, 0x98, 0x0f, 0x9c, 0x88])
TYP_ESP = bytes([0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b])

# Uklad wezla (static_assert w psf.h)
OFF_TYP, OFF_ROZMIAR, OFF_CZAS, OFF_FLAGI, OFF_WSK = 0x000, 0x001, 0x009, 0x011, 0x019
OFF_POSR1, OFF_POSR2, OFF_POSR3 = 0xF69, 0xF71, 0xF79


class BladObrazu(Exception):
    """Obraz odrzucony: nieobslugiwany format albo niespojne metadane (komunikat po polsku)."""


def crc32(dane):
    return binascii.crc32(dane) & 0xFFFFFFFF


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


def nazwa_poprawna(nazwa_bajty):
    """poprawna_nazwa() z psf.cpp: 1..55 bajtow, nie '.'/'..', bez '/', '\\' i bajtow < 0x20."""
    if not 1 <= len(nazwa_bajty) <= MAKS_NAZWA or nazwa_bajty in (b'.', b'..'):
        return False
    return all(c >= 0x20 and c not in (0x2F, 0x5C) for c in nazwa_bajty)


class Gpt:
    def __init__(self, uuid, wpisy, sektory):
        self.uuid = uuid
        self.wpisy = wpisy          # [(typ, uuid, start, liczba)]
        self.sektory = sektory


def czytaj_gpt(f, sektory):
    """gpt_read(): oba naglowki, CRC, identyczne tablice, brak nakladania sie partycji."""
    if sektory < 68:
        raise BladObrazu('Obraz jest za mały, aby zawierać tablice partycji GPT.')

    def czytaj(lba, n=1):
        f.seek(lba * SEKTOR)
        d = f.read(n * SEKTOR)
        if len(d) != n * SEKTOR:
            raise BladObrazu('Obraz jest obcięty (nie można odczytać sektora %d).' % lba)
        return d

    def naglowek(gdzie):
        h = bytearray(czytaj(gdzie))
        if h[:8] != b'EFI PART' or u32(h, 8) != 0x10000 or u32(h, 12) != 92 or u32(h, 20):
            return None
        c = u32(h, 16)
        h[16:20] = b'\0\0\0\0'
        if crc32(bytes(h[:92])) != c:
            return None
        struct.pack_into('<I', h, 16, c)
        first, last, arr = u64(h, 40), u64(h, 48), u64(h, 72)
        n, rozm = u32(h, 80), u32(h, 84)
        if u64(h, 24) != gdzie or u64(h, 32) != (sektory - 1 if gdzie == 1 else 1):
            return None
        if not (first >= 34 and last >= first and last < sektory - 33 and 0 < n <= 128 and rozm == 128):
            return None
        dl = (n * 128 + 511) // 512
        if arr + dl > sektory:
            return None
        if gdzie == 1 and not (arr >= 2 and arr + dl <= first):
            return None
        if gdzie != 1 and not (arr > last and arr + dl <= gdzie):
            return None
        return bytes(h)

    h, b = naglowek(1), naglowek(sektory - 1)
    if h is None and b is None:
        raise BladObrazu('Brak poprawnej tablicy GPT - to nie jest dysk zainstalowanego Bursztyn OS.')
    if h is None or b is None:
        raise BladObrazu('Uszkodzona tablica GPT (jedna z kopii nagłówka jest niepoprawna).')
    if h[40:72] != b[40:72] or h[80:92] != b[80:92]:
        raise BladObrazu('Kopie nagłówka GPT się różnią.')
    n = u32(h, 80)
    tab_a, tab_b = czytaj(u64(h, 72), (n * 128 + 511) // 512), czytaj(u64(b, 72), (n * 128 + 511) // 512)
    if tab_a[:n * 128] != tab_b[:n * 128]:
        raise BladObrazu('Kopie tablicy partycji GPT się różnią.')
    if crc32(tab_a[:n * 128]) != u32(h, 88):
        raise BladObrazu('Niepoprawna suma CRC tablicy partycji GPT.')
    wpisy = []
    for i in range(n):
        p = tab_a[i * 128:(i + 1) * 128]
        if p[:16] == bytes(16):
            continue
        s, e = u64(p, 32), u64(p, 40)
        if s < u64(h, 40) or e < s or e > u64(h, 48) or p[16:32] == bytes(16):
            raise BladObrazu('Niepoprawny wpis partycji GPT.')
        for (_, uu, st, ln) in wpisy:
            if (s < st + ln and st <= e) or uu == p[16:32]:
                raise BladObrazu('Partycje GPT nakładają się albo mają ten sam identyfikator.')
        wpisy.append((p[:16], p[16:32], s, e - s + 1))
    return Gpt(h[56:72], wpisy, sektory)


class Wezel:
    __slots__ = ('id', 'typ', 'rozmiar', 'czas', 'flagi', 'wsk', 'posr1', 'posr2', 'posr3', 'surowe')

    @classmethod
    def z_bajtow(cls, wid, b):
        w = cls()
        w.id = wid
        w.surowe = bytes(b)
        w.typ = b[OFF_TYP]
        w.rozmiar = u64(b, OFF_ROZMIAR)
        w.czas = u64(b, OFF_CZAS)
        w.flagi = u64(b, OFF_FLAGI)
        w.wsk = list(struct.unpack_from('<%dQ' % BEZPOSREDNIE, b, OFF_WSK))
        w.posr1, w.posr2, w.posr3 = u64(b, OFF_POSR1), u64(b, OFF_POSR2), u64(b, OFF_POSR3)
        return w


class Wolumin:
    """Zamontowany TYLKO DO ODCZYTU wolumin BSP2 na partycji obrazu. Metadane sa w pamieci (jak w jadrze)."""

    def __init__(self, f, start_lba, liczba_sektorow):
        self.f = f
        self.start = start_lba * SEKTOR          # bajt poczatku partycji
        self.sektory = liczba_sektorow
        f.seek(self.start)
        sb = f.read(44)
        if len(sb) != 44:
            raise BladObrazu('Obraz jest obcięty w obszarze superbloku BSP2.')
        if sb[:4] != b'BSP2':
            raise BladObrazu('Partycja Bursztyn OS nie zawiera systemu plików BSP2 (brak sygnatury).')
        self.calkowity, self.bloki, self.korzen, self.start_wezlow, self.start_danych = struct.unpack_from('<5Q', sb, 4)
        self._geometria()
        f.seek(self.start)
        self.meta = bytearray(f.read(self.start_danych * BLOK))
        if len(self.meta) != self.start_danych * BLOK:
            raise BladObrazu('Obraz jest obcięty w obszarze metadanych BSP2.')

    # ------------------------------------------------------------------ wylicz_geometrie()
    def _geometria(self):
        c = self.calkowity
        if c < BLOK * MIN_BLOKOW or c % BLOK or c % SEKTOR:
            raise BladObrazu('Superblok BSP2: niepoprawny rozmiar systemu plików.')
        if self.start_danych * BLOK > RAM_METADANYCH:
            raise BladObrazu('Superblok BSP2: metadane nie zmieszczą się w pamięci jądra.')
        if self.bloki != c // BLOK or self.bloki < MIN_BLOKOW:
            raise BladObrazu('Superblok BSP2: liczba bloków nie zgadza się z rozmiarem.')
        if not (1 < self.start_wezlow < self.start_danych < self.bloki):
            raise BladObrazu('Superblok BSP2: niepoprawny układ obszarów.')
        bity = (self.start_wezlow - 1) * BLOK * 8
        self.bloki_danych = self.bloki - self.start_danych
        if self.bloki_danych > bity:
            raise BladObrazu('Superblok BSP2: bitmapa za mała dla obszaru danych.')
        self.liczba_wezlow = self.start_danych - self.start_wezlow    # 1 wezel = 1 blok
        if not 1 <= self.korzen <= self.liczba_wezlow:
            raise BladObrazu('Superblok BSP2: niepoprawny węzeł korzenia.')
        if self.bloki * SEKTORY_NA_BLOK > self.sektory:
            raise BladObrazu('Superblok BSP2 deklaruje system plików większy niż partycja.')

    # ------------------------------------------------------------------ dostep
    def wezel(self, wid):
        if not 1 <= wid <= self.liczba_wezlow:
            raise BladObrazu('Odwołanie do nieistniejącego węzła %d.' % wid)
        o = (self.start_wezlow + wid - 1) * BLOK
        return Wezel.z_bajtow(wid, self.meta[o:o + BLOK])

    def blok_zajety(self, bid):
        bajt = BLOK + bid // 8
        return bid < self.bloki_danych and (self.meta[bajt] >> (bid & 7)) & 1 == 1

    def czytaj_blok(self, bid):
        if not 0 <= bid < self.bloki_danych:
            raise BladObrazu('Wskaźnik bloku danych poza obszarem danych (%d).' % bid)
        self.f.seek(self.start + (self.start_danych + bid) * BLOK)
        d = self.f.read(BLOK)
        if len(d) != BLOK:
            raise BladObrazu('Obraz jest obcięty w obszarze danych.')
        return d

    def tablica(self, bid):
        return list(struct.unpack('<%dQ' % NA_BLOK, self.czytaj_blok(bid)))

    def wolne_bloki(self):
        n = self.bloki_danych
        pelne, reszta = divmod(n, 8)
        b = self.meta[BLOK:BLOK + pelne]
        zajete = sum(bin(x).count('1') for x in b)
        if reszta:
            zajete += bin(self.meta[BLOK + pelne] & ((1 << reszta) - 1)).count('1')
        return n - zajete

    def wolne_wezly(self):
        return sum(1 for i in range(1, self.liczba_wezlow + 1)
                   if self.meta[(self.start_wezlow + i - 1) * BLOK] == TYP_WOLNY)

    # ------------------------------------------------------------------ pliki i katalogi
    def bloki_pliku(self, w):
        """Lista (indeks, blok|BRAK) dla blokow danych pliku i lista blokow posrednich (pobierz_wskaznik_bloku)."""
        n = (w.rozmiar + BLOK - 1) // BLOK
        if n > MAKS_BLOKOW_PLIKU:
            raise BladObrazu('Plik przekracza maksymalną liczbę bloków formatu.')
        dane = list(w.wsk[:min(n, BEZPOSREDNIE)])
        posrednie = []
        if n > BEZPOSREDNIE:
            ile = min(n - BEZPOSREDNIE, NA_BLOK)
            if w.posr1 == BRAK:
                dane += [BRAK] * ile
            else:
                posrednie.append(w.posr1)
                dane += self.tablica(w.posr1)[:ile]
        if n > BEZPOSREDNIE + NA_BLOK:
            reszta = n - BEZPOSREDNIE - NA_BLOK
            if w.posr2 == BRAK:
                dane += [BRAK] * reszta
            else:
                posrednie.append(w.posr2)
                l1 = self.tablica(w.posr2)
                for t in range((reszta + NA_BLOK - 1) // NA_BLOK):
                    ile = min(NA_BLOK, reszta - t * NA_BLOK)
                    if l1[t] == BRAK:
                        dane += [BRAK] * ile
                    else:
                        posrednie.append(l1[t])
                        dane += self.tablica(l1[t])[:ile]
        return dane, posrednie

    def wszystkie_wskazniki_pliku(self, w):
        """Wszystkie zajmowane bloki pliku (rowniez poza rozmiarem - tak sprawdza wskazniki_pliku_poprawne())."""
        wynik = [b for b in w.wsk if b != BRAK]
        if w.posr1 != BRAK:
            wynik.append(w.posr1)
            wynik += [b for b in self.tablica(w.posr1) if b != BRAK]
        if w.posr2 != BRAK:
            wynik.append(w.posr2)
            for b2 in self.tablica(w.posr2):
                if b2 != BRAK:
                    wynik.append(b2)
                    wynik += [b for b in self.tablica(b2) if b != BRAK]
        return wynik

    def wpisy(self, w):
        """[(nazwa_bajty, id_wezla, blok, indeks)] - tylko bezposrednie bloki katalogu (jak jadro)."""
        wynik = []
        for k, bid in enumerate(w.wsk):
            if bid == BRAK:
                continue
            d = self.czytaj_blok(bid)
            for j in range(WPISOW_NA_BLOK):
                wid = u64(d, j * 64)
                if wid == 0:
                    continue
                pole = d[j * 64 + 8:j * 64 + 64]
                if 0 not in pole:
                    raise BladObrazu('Wpis katalogu bez zakończenia nazwy.')
                wynik.append((pole[:pole.index(0)], wid, bid, j))
        return wynik

    def znajdz(self, sciezka):
        """Wezel dla sciezki bezwzglednej (bez translacji slotow - surowe drzewo)."""
        wid = self.korzen
        if sciezka == '/':
            return self.wezel(wid)
        for seg in sciezka.strip('/').split('/'):
            w = self.wezel(wid)
            if w.typ != TYP_KATALOG:
                return None
            nb = seg.encode('utf-8')
            for (n, cel, _, _) in self.wpisy(w):
                if n == nb:
                    wid = cel
                    break
            else:
                return None
        return self.wezel(wid)

    def czytaj_plik(self, w, porcja=1 << 20):
        """Generator zawartosci pliku (rzadkie bloki = zera)."""
        dane, _ = self.bloki_pliku(w)
        zostalo = w.rozmiar
        bufor = bytearray()
        for bid in dane:
            kawalek = bytes(BLOK) if bid == BRAK else self.czytaj_blok(bid)
            bufor += kawalek[:min(BLOK, zostalo)]
            zostalo -= min(BLOK, zostalo)
            if len(bufor) >= porcja:
                yield bytes(bufor)
                bufor = bytearray()
        if bufor:
            yield bytes(bufor)

    # ------------------------------------------------------------------ pelna walidacja
    def waliduj(self):
        """Pelna walidacja (waliduj_metadane() jadra + kontrole krzyzowe). Zwraca liste ostrzezen."""
        ostrzezenia = []
        korzen = self.wezel(self.korzen)
        if korzen.typ != TYP_KATALOG:
            raise BladObrazu('Korzeń systemu plików nie jest katalogiem.')
        wlasciciel = {}          # blok -> wezel
        aktywne = {}
        for wid in range(1, self.liczba_wezlow + 1):
            w = self.wezel(wid)
            if w.typ == TYP_WOLNY:
                continue
            if w.typ not in (TYP_PLIK, TYP_KATALOG):
                raise BladObrazu('Węzeł %d ma nieznany typ %d.' % (wid, w.typ))
            aktywne[wid] = w
            if w.typ == TYP_PLIK:
                if w.posr3 != BRAK:
                    raise BladObrazu('Węzeł %d używa nieobsługiwanego wskaźnika 3x pośredniego.' % wid)
                if w.rozmiar > MAKS_ROZMIAR_PLIKU:
                    raise BladObrazu('Plik w węźle %d przekracza limit rozmiaru formatu.' % wid)
                if w.posr1 != BRAK and w.posr1 == w.posr2:
                    raise BladObrazu('Węzeł %d: powtórzony blok pośredni.' % wid)
                for b in (w.posr1, w.posr2):
                    if b != BRAK and (not 0 <= b < self.bloki_danych or not self.blok_zajety(b)):
                        raise BladObrazu('Węzeł %d: niepoprawny blok pośredni.' % wid)
                bloki = self.wszystkie_wskazniki_pliku(w)
            else:
                if (w.posr1, w.posr2, w.posr3) != (BRAK, BRAK, BRAK):
                    raise BladObrazu('Katalog w węźle %d ma bloki pośrednie.' % wid)
                bloki = [b for b in w.wsk if b != BRAK]
            for b in bloki:
                if not 0 <= b < self.bloki_danych or not self.blok_zajety(b):
                    raise BladObrazu('Węzeł %d wskazuje nieprzydzielony lub nieistniejący blok danych.' % wid)
                if b in wlasciciel:
                    raise BladObrazu('Blok danych %d jest współdzielony (węzły %d i %d).' % (b, wlasciciel[b], wid))
                wlasciciel[b] = wid
        # drzewo katalogow: kazdy wezel osiagalny dokladnie raz, bez cykli
        rodzic = {self.korzen: 0}
        stos = [self.korzen]
        while stos:
            wid = stos.pop()
            w = aktywne[wid]
            nazwy = set()
            wpisy = self.wpisy(w)
            for (n, cel, _, _) in wpisy:
                if not nazwa_poprawna(n):
                    raise BladObrazu('Niepoprawna nazwa w katalogu (węzeł %d).' % wid)
                if cel > self.liczba_wezlow or cel == wid or cel == self.korzen:
                    raise BladObrazu('Wpis katalogu wskazuje niepoprawny węzeł.')
                if cel not in aktywne:
                    raise BladObrazu('Katalog zawiera wpis do zwolnionego węzła (jądro naprawi go przy '
                                     'starcie). Uruchom i poprawnie zamknij Bursztyn OS, potem spróbuj ponownie.')
                if n in nazwy:
                    raise BladObrazu('Katalog zawiera powtórzoną nazwę (jądro naprawi ją przy starcie). '
                                     'Uruchom i poprawnie zamknij Bursztyn OS, potem spróbuj ponownie.')
                nazwy.add(n)
                if cel in rodzic:
                    raise BladObrazu('Węzeł %d jest podpięty w drzewie więcej niż raz (cykl lub twarde dowiązanie).' % cel)
                rodzic[cel] = wid
                if aktywne[cel].typ == TYP_KATALOG:
                    stos.append(cel)
            if w.rozmiar != len(wpisy):
                ostrzezenia.append('Licznik wpisów katalogu (węzeł %d) różni się od rzeczywistej liczby.' % wid)
        sieroty = [wid for wid in aktywne if wid not in rodzic]
        if sieroty:
            ostrzezenia.append('Węzły bez wpisu w katalogu (nieosiągalne): %d.' % len(sieroty))
        zajete = self.bloki_danych - self.wolne_bloki()
        if zajete != len(wlasciciel):
            ostrzezenia.append('Bloki oznaczone jako zajęte, ale nieużywane: %d.' % (zajete - len(wlasciciel)))
        self.wlasciciel = wlasciciel
        self.aktywne = aktywne
        self.rodzic = rodzic
        return ostrzezenia

    def drzewo(self, sciezka='/', wid=None):
        """Generator (sciezka, wezel) w porzadku przejscia; wymaga wczesniejszej waliduj()."""
        wid = self.korzen if wid is None else wid
        w = self.wezel(wid)
        yield sciezka, w
        if w.typ == TYP_KATALOG:
            for (n, cel, _, _) in sorted(self.wpisy(w)):
                yield from self.drzewo((sciezka.rstrip('/') + '/' + n.decode('utf-8', 'replace')), cel)


class Dysk:
    """Obraz zainstalowanego Bursztyn OS: GPT + partycja BSP2 z kompletnym znacznikiem instalacji."""

    def __init__(self, sciezka):
        self.sciezka = sciezka
        st = os.stat(sciezka)
        if not os.path.isfile(sciezka):
            raise BladObrazu('Wybrana ścieżka nie jest zwykłym plikiem obrazu dysku.')
        self.rozmiar = st.st_size
        self.f = open(sciezka, 'rb')
        try:
            self._format()
            if st.st_size % SEKTOR:
                raise BladObrazu('Rozmiar obrazu nie jest wielokrotnością 512 bajtów (obraz obcięty lub inny format).')
            self._otworz()
        except Exception:
            self.f.close()
            raise

    def _format(self):
        """Rozpoznanie popularnych formatow, ktore NIE sa surowym obrazem (bez zgadywania po rozszerzeniu)."""
        f = self.f
        f.seek(0)
        mbr = f.read(SEKTOR)
        if len(mbr) == SEKTOR and mbr[:4] == b'QFI\xfb':
            raise BladObrazu('To jest obraz QCOW2 - obsługiwane są tylko surowe obrazy (raw .img) Bursztyn OS.')
        if len(mbr) >= 64 and mbr[:7] == b'<<< Ora':
            raise BladObrazu('To jest obraz VirtualBox VDI - obsługiwane są tylko surowe obrazy (raw .img).')
        f.seek(64 * SEKTOR)
        if f.read(6)[1:6] == b'CD001':
            raise BladObrazu('To jest obraz ISO9660 (instalator), a nie dysk zainstalowanego systemu.')

    def _otworz(self):
        f = self.f
        self.gpt = czytaj_gpt(f, self.rozmiar // SEKTOR)
        bsp = [e for e in self.gpt.wpisy if e[0] == TYP_BSP]
        if not bsp:
            raise BladObrazu('Tablica GPT nie zawiera partycji Bursztyn OS (BSP2).')
        if len(bsp) > 1:
            raise BladObrazu('Obraz zawiera więcej niż jedną partycję Bursztyn OS - niejednoznaczny cel.')
        _, self.uuid_partycji, self.bsp_start, self.bsp_sektory = bsp[0]
        if self.bsp_sektory * SEKTOR > BSP_MAKS_BAJTY + 0 or self.bsp_sektory < 10:
            raise BladObrazu('Partycja Bursztyn OS ma nieobsługiwany rozmiar.')
        self._znacznik()
        self.wolumin = Wolumin(f, self.bsp_start, self.bsp_sektory)
        if self.wolumin.calkowity != self.znacznik_bajty:
            raise BladObrazu('Znacznik instalacji nie zgadza się z superblokiem BSP2.')
        self.esp = [e for e in self.gpt.wpisy if e[0] == TYP_ESP]

    def _znacznik(self):
        """complete(): dwie identyczne kopie BSPINST1 w ostatnich sektorach partycji, stan 2, UUID dysku, CRC."""
        f = self.f
        p = self.bsp_start
        f.seek((p + self.bsp_sektory - 2) * SEKTOR)
        a = f.read(SEKTOR)
        b = f.read(SEKTOR)
        if len(a) != SEKTOR or len(b) != SEKTOR:
            raise BladObrazu('Obraz jest obcięty (brak znacznika instalacji).')
        if a != b or a[:8] != b'BSPINST1' or u32(a, 8) != 1:
            raise BladObrazu('Brak poprawnego znacznika instalacji - instalacja niedokończona lub uszkodzona.')
        if u32(a, 12) != 2:
            raise BladObrazu('Instalacja Bursztyn OS nie została ukończona (znacznik w stanie początkowym).')
        if a[16:32] != self.gpt.uuid or crc32(a[:508]) != u32(a, 508):
            raise BladObrazu('Znacznik instalacji nie pasuje do tego dysku (UUID/CRC).')
        self.znacznik_bajty = u64(a, 32)
        if self.znacznik_bajty > (self.bsp_sektory - 8) * SEKTOR:
            raise BladObrazu('Znacznik instalacji deklaruje zbyt duży system plików.')

    def zamknij(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.zamknij()
