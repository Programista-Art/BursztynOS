"""Transakcyjny import: oryginal nigdy nie jest modyfikowany w miejscu.

  1. inspekcja i pelna walidacja oryginalu (tylko odczyt),
  2. plan (nazwy, konflikty, miejsce w Bursztyn OS) i kontrola miejsca na dysku hosta,
  3. wylacznosc (blokada) i sprawdzenie, czy obraz nie jest uzywany przez QEMU/VirtualBox,
  4. kopia robocza obok oryginalu (ten sam system plikow) + sprawdzenie kopii skrotem SHA-256,
  5. zapis na kopii roboczej (anulowanie miedzy plikami odrzuca kopie),
  6. pelna weryfikacja kopii: GPT, znacznik, superblok, cale drzewo, zawartosc kazdego importowanego pliku,
     oraz dowod, ze poza zmienionymi blokami KAZDY bajt obrazu jest identyczny z oryginalem,
  7. zamiana dopiero po jawnym potwierdzeniu: oryginal -> kopia zapasowa (zachowana), kopia robocza -> nazwa
     oryginalu. Dwie operacje rename w tym samym katalogu NIE sa razem atomowe; przy bledzie drugiej operacji
     kopia zapasowa wraca na miejsce. Oryginal jest zachowany jako kopia zapasowa do recznego usuniecia.
"""
import hashlib
import os
import shutil
import time

from . import bsp2, blokady, plan as planowanie
from .bsp2 import BLOK, SEKTOR, BladObrazu
from .zapis import Przerwano, Zapisywacz, bloki_dla_rozmiaru

MIB = 1 << 20
MARGINES_HOSTA = 64 * MIB


class BladImportu(Exception):
    pass


def opis_rozmiaru(b):
    for j, n in ((1 << 30, 'GB'), (1 << 20, 'MB'), (1 << 10, 'KB')):
        if b >= j:
            return ('%.1f %s' % (b / j, n)).replace('.', ',')
    return '%d B' % b


class Inspekcja:
    """Wynik sprawdzenia obrazu (tylko odczyt)."""

    def __init__(self, sciezka):
        self.sciezka = sciezka
        h_przed = os.stat(sciezka).st_mtime_ns
        with bsp2.Dysk(sciezka) as d:
            w = d.wolumin
            self.ostrzezenia = w.waliduj()
            self.bsp_start, self.bsp_sektory = d.bsp_start, d.bsp_sektory
            self.rozmiar_obrazu = d.rozmiar
            self.pojemnosc = w.bloki_danych * BLOK
            self.wolne = w.wolne_bloki() * BLOK
            self.zajete = self.pojemnosc - self.wolne
            self.wezly, self.wolne_wezly = w.liczba_wezlow, w.wolne_wezly()
            self.partycje = [(e[2], e[3], 'BSP2 (Bursztyn OS)' if e[0] == bsp2.TYP_BSP else
                              'ESP (EFI)' if e[0] == bsp2.TYP_ESP else 'inna') for e in d.gpt.wpisy]
            self.katalogi = {k: w.znajdz(k) is not None for k in
                             (planowanie.KATALOG_MUZYKI, planowanie.KATALOG_OBRAZOW, planowanie.KATALOG_DOKUMENTOW)}
        if os.stat(sciezka).st_mtime_ns != h_przed:
            raise BladImportu('Obraz zmienił się podczas sprawdzania - jest używany przez inny program?')

    def opis(self):
        l = ['Obraz: %s (%s)' % (self.sciezka, opis_rozmiaru(self.rozmiar_obrazu)),
             'System plików: BSP2 / PSF (Bursztyn OS), instalacja kompletna',
             'Partycje GPT: ' + '; '.join('%s od LBA %d, %s' % (n, s, opis_rozmiaru(c * SEKTOR)) for s, c, n in self.partycje),
             'Pojemność danych: %s, zajęte: %s, wolne: %s' % (opis_rozmiaru(self.pojemnosc), opis_rozmiaru(self.zajete),
                                                              opis_rozmiaru(self.wolne)),
             'Węzły (pliki + katalogi): %d, wolne: %d' % (self.wezly, self.wolne_wezly)]
        for k, jest in self.katalogi.items():
            l.append('%s: %s' % (k, 'istnieje' if jest else 'brak (zostanie utworzony przy imporcie)'))
        l += ['Uwaga: ' + o for o in self.ostrzezenia]
        return l


def wolne_miejsce_hosta(katalog):
    return shutil.disk_usage(katalog).free


def wymagane_miejsce_hosta(sciezka, bajty_importu):
    st = os.stat(sciezka)
    rzadki = os.name == 'posix' and hasattr(st, 'st_blocks')
    kopia = st.st_blocks * 512 if rzadki else st.st_size
    return min(kopia, st.st_size) + bajty_importu + MARGINES_HOSTA


class Transakcja:
    def __init__(self, sciezka, pozycje, zglos=None, anuluj=None):
        self.sciezka = os.path.abspath(sciezka)
        self.pozycje = pozycje
        self.zglos = zglos or (lambda *a: None)
        self.anuluj = anuluj or (lambda: False)
        self.kopia = self.sciezka + '.importer-kopia-robocza'
        self.zapasowa = None
        self.blokada = None
        self.raport = []
        self.zweryfikowano = False
        self.zastapiono = False
        # haki testowe (dla testow awarii)
        self.hak_wolne_miejsce = wolne_miejsce_hosta
        self.hak_po_zapisie = None
        self.hak_przed_zamiana = None
        self.hak_zapisu_bloku = None

    def _log(self, t):
        self.raport.append(t)
        self.zglos('log', t)

    # ------------------------------------------------------------------ 1-3
    def przygotuj(self):
        self._log('Sprawdzanie obrazu (tylko odczyt)...')
        blokady.sprawdz_czy_uzywany(self.sciezka)
        self.blokada = blokady.Blokada(self.sciezka).__enter__()
        try:
            self.inspekcja = Inspekcja(self.sciezka)
            if os.path.exists(self.kopia):
                raise BladImportu('Istnieje pozostałość poprzedniego importu: %s. Usuń ja ręcznie (nie jest '
                                  'oryginałem) i spróbuj ponownie.' % self.kopia)
            with bsp2.Dysk(self.sciezka) as d:
                d.wolumin.waliduj()
                self.z = planowanie.ocen(self.pozycje, d.wolumin)
            do_importu = [p for p in self.pozycje if p.nazwa_koncowa and not p.blad and not p.status.startswith('Istnieje')]
            if any(p.blad for p in self.pozycje):
                raise BladImportu('Lista importu zawiera pozycje z błędami - usuń je albo popraw przed importem.')
            if not do_importu:
                raise BladImportu('Brak plików do zaimportowania (wszystkie zostaną pominięte).')
            wolne_bloki = self.inspekcja.wolne // BLOK
            if self.z.bloki > wolne_bloki:
                raise BladImportu('Za mało miejsca w systemie plików Bursztyn OS: potrzeba %s, wolne %s.' %
                                  (opis_rozmiaru(self.z.bloki * BLOK), opis_rozmiaru(self.inspekcja.wolne)))
            if self.z.wezly > self.inspekcja.wolne_wezly:
                raise BladImportu('Za mało wolnych węzłów systemu plików: potrzeba %d, wolne %d (każdy plik i katalog '
                                  'zajmuje jeden węzeł).' % (self.z.wezly, self.inspekcja.wolne_wezly))
            potrzeba = wymagane_miejsce_hosta(self.sciezka, self.z.bloki * BLOK)
            wolne_host = self.hak_wolne_miejsce(os.path.dirname(self.sciezka))
            if potrzeba > wolne_host:
                raise BladImportu('Za mało miejsca na dysku komputera na bezpieczną kopię roboczą: potrzeba %s, '
                                  'wolne %s w %s. Zwolnij miejsce - import w miejscu (bez kopii) nie jest wykonywany.'
                                  % (opis_rozmiaru(potrzeba), opis_rozmiaru(wolne_host), os.path.dirname(self.sciezka)))
            self._log('Plan: %d plików, %s danych, %d bloków i %d węzłów BSP2.' %
                      (self.z.pliki, opis_rozmiaru(self.z.bajty), self.z.bloki, self.z.wezly))
        except Exception:
            self.zwolnij()
            raise

    # ------------------------------------------------------------------ 4
    def _kopiuj(self):
        self._log('Tworzenie kopii roboczej: %s' % self.kopia)
        rozmiar = os.path.getsize(self.sciezka)
        self.skroty = []
        zero = bytes(MIB)
        h = hashlib.sha256()
        with open(self.sciezka, 'rb') as a, open(self.kopia, 'xb') as b:
            pozycja = 0
            while True:
                c = a.read(MIB)
                if not c:
                    break
                h.update(c)
                self.skroty.append(hashlib.sha256(c).digest())
                if c == zero[:len(c)]:
                    b.seek(len(c), 1)            # rzadki zapis zer (Linux/ext4, NTFS bez flagi = zwykle zera)
                else:
                    b.write(c)
                pozycja += len(c)
                self.zglos('postep', ('kopia', pozycja, rozmiar))
                if self.anuluj():
                    raise Przerwano()
            b.truncate(rozmiar)
            b.flush()
            os.fsync(b.fileno())
        self.skrot_oryginalu = h.hexdigest()
        h2 = hashlib.sha256()
        with open(self.kopia, 'rb') as b:
            for c in iter(lambda: b.read(MIB), b''):
                h2.update(c)
        if h2.hexdigest() != self.skrot_oryginalu:
            raise BladImportu('Kopia robocza różni się od oryginału (błąd odczytu/zapisu dysku hosta).')
        self._log('Kopia robocza zgodna z oryginałem (SHA-256 %s).' % self.skrot_oryginalu[:16])

    # ------------------------------------------------------------------ 5
    def _zapisz(self):
        with bsp2.Dysk(self.sciezka) as oryg:
            oryg.wolumin.waliduj()
            z = Zapisywacz(self.kopia, oryg)
            try:
                if self.hak_zapisu_bloku:
                    pierwotny = z._pisz_blok

                    def pisz(bid, dane):
                        self.hak_zapisu_bloku(bid)
                        pierwotny(bid, dane)
                    z._pisz_blok = pisz
                for k in self.z.katalogi_do_utworzenia:
                    z.zapewnij_katalog(k)
                    self._log('Utworzono katalog %s' % k)
                razem = sum(p.rozmiar for p in self.pozycje if self._importowana(p)) or 1
                zrobione = [0]

                def postep(n):
                    zrobione[0] += n
                    self.zglos('postep', ('import', zrobione[0], razem))
                for p in self.pozycje:
                    if not self._importowana(p):
                        if p.nazwa_koncowa:
                            p.wynik = 'pominięto'
                        continue
                    if self.anuluj():
                        raise Przerwano()              # bezpieczna granica: miedzy plikami
                    z.zapewnij_katalog(p.katalog)
                    zastap = p.status.startswith('Zastąpi')
                    if zastap:
                        z.usun_plik(p.katalog, p.nazwa_koncowa)
                    rozmiar, sha = z.dodaj_plik(p.katalog, p.nazwa_koncowa, p.zrodlo, postep, self.anuluj)
                    p.rozmiar, p.sha256 = rozmiar, sha
                    p.wynik = 'zastapiono' if zastap else 'zaimportowano'
                    self.zglos('plik', p)
                z.zatwierdz()
                self.zmiany = (set(z.nowe_bloki), set(z.zwolnione_bloki), set(z.zmienione_wezly))
            finally:
                z.zamknij()

    def _importowana(self, p):
        return bool(p.nazwa_koncowa) and not p.blad and not p.status.startswith('Istnieje')

    # ------------------------------------------------------------------ 6
    def _weryfikuj(self):
        self._log('Weryfikacja kopii roboczej...')
        nowe, zwolnione, wezly = self.zmiany
        with bsp2.Dysk(self.sciezka) as o, bsp2.Dysk(self.kopia) as k:
            ost_o, ost_k = o.wolumin.waliduj(), k.wolumin.waliduj()
            if ost_o != ost_k:
                raise BladImportu('Weryfikacja: zmieniły się ostrzeżenia spójności systemu plików: %s' % ost_k)
            wo, wk = o.wolumin, k.wolumin
            # a) kazdy importowany plik: typ, rozmiar i SHA-256 odczytane niezaleznym czytnikiem
            for p in self.pozycje:
                if p.wynik not in ('zaimportowano', 'zastapiono'):
                    continue
                w = wk.znajdz(p.sciezka_docelowa)
                if w is None or w.typ != bsp2.TYP_PLIK or w.rozmiar != p.rozmiar:
                    raise BladImportu('Weryfikacja: brak lub zły rozmiar pliku %s.' % p.sciezka_docelowa)
                h = hashlib.sha256()
                for c in wk.czytaj_plik(w):
                    h.update(c)
                if h.hexdigest() != p.sha256:
                    raise BladImportu('Weryfikacja: zawartość %s różni się od pliku źródłowego.' % p.sciezka_docelowa)
            # b) nowe bloki byly wolne w oryginale; zmienione wezly to katalogi docelowe, zastapione pliki albo nowe
            for b in nowe:
                if wo.blok_zajety(b):
                    raise BladImportu('Weryfikacja: użyto bloku zajętego w oryginale.')
            for wid in wezly:
                stary = wo.aktywne.get(wid)
                if stary is not None and stary.typ == bsp2.TYP_KATALOG:
                    nowy = wk.aktywne.get(wid)
                    if nowy is None or nowy.typ != bsp2.TYP_KATALOG:
                        raise BladImportu('Weryfikacja: katalog zniknął lub zmienił typ.')
                    przed = {n: c for (n, c, _, _) in wo.wpisy(stary)}
                    po = {n: c for (n, c, _, _) in wk.wpisy(nowy)}
                    for n, c in przed.items():
                        if n not in po:
                            raise BladImportu('Weryfikacja: z katalogu zniknął wpis %r.' % n)
                        if po[n] != c and not (wo.aktywne[c].typ == bsp2.TYP_PLIK and
                                               any(p.wynik == 'zastapiono' and p.nazwa_koncowa.encode() == n for p in self.pozycje)):
                            raise BladImportu('Weryfikacja: wpis %r wskazuje inny węzeł.' % n)
            for wid, w in wo.aktywne.items():
                if wid not in wezly and wk.wezel(wid).surowe != w.surowe:
                    raise BladImportu('Weryfikacja: zmieniony węzeł %d, który nie był celem importu.' % wid)
            # c) metadane: roznice tylko w bitach bitmapy nowych/zwolnionych blokow i w zmienionych wezlach
            dozwolone = set()
            for b in nowe | zwolnione:
                dozwolone.add(BLOK + b // 8)
            for wid in wezly:
                o0 = (wo.start_wezlow + wid - 1) * BLOK
                dozwolone.update(range(o0, o0 + BLOK))
            for i, (x, y) in enumerate(zip(wo.meta, wk.meta)):
                if x != y and i not in dozwolone:
                    raise BladImportu('Weryfikacja: nieoczekiwana zmiana metadanych (bajt %d).' % i)
            start, koniec_meta = wk.start, wk.start + wk.start_danych * BLOK
            zmienione_bajty = [(wk.start + (wk.start_danych + b) * BLOK) for b in (nowe | zwolnione)]
        self._porownaj_poza_zmianami(start, koniec_meta, sorted(zmienione_bajty))
        self.zweryfikowano = True
        self._log('Weryfikacja zakończona: system plików spójny, zawartość plików zgodna, pozostałe bajty obrazu '
                  '(GPT, ESP, znacznik instalacji, istniejące pliki) identyczne z oryginałem.')

    def _porownaj_poza_zmianami(self, meta_od, meta_do, zmienione):
        """Kazdy MiB kopii poza zmienionymi blokami i metadanymi musi miec skrot oryginalu."""
        zm = set(zmienione)
        dotkniete = {z // MIB for z in zm}
        rozmiar = os.path.getsize(self.kopia)
        if rozmiar != os.path.getsize(self.sciezka):
            raise BladImportu('Weryfikacja: rozmiar kopii różni się od oryginału.')
        with open(self.kopia, 'rb') as k, open(self.sciezka, 'rb') as o:
            for i, skrot in enumerate(self.skroty):
                od = i * MIB
                c = k.read(MIB)
                dotyka = (od < meta_do and od + len(c) > meta_od) or i in dotkniete
                if not dotyka:
                    if hashlib.sha256(c).digest() != skrot:
                        raise BladImportu('Weryfikacja: zmieniony obszar obrazu poza importem (offset %d).' % od)
                    continue
                o.seek(od)
                oc = o.read(MIB)
                for b in range(0, len(c), BLOK):
                    a = od + b
                    if meta_od <= a < meta_do or a in zm:
                        continue
                    if c[b:b + BLOK] != oc[b:b + BLOK]:
                        raise BladImportu('Weryfikacja: zmieniony blok poza importem (offset %d).' % a)
                self.zglos('postep', ('weryfikacja', od + len(c), rozmiar))

    def wykonaj(self):
        """Kroki 4-6. Po sukcesie kopia robocza czeka na decyzje (zastap/odrzuc). Przy bledzie kopia jest usuwana."""
        try:
            self._kopiuj()
            self._zapisz()
            if self.hak_po_zapisie:
                self.hak_po_zapisie(self.kopia)
            self._weryfikuj()
        except Przerwano:
            self._log('Import anulowany. Oryginalny obraz nie został zmieniony.')
            self._usun_kopie()
            self.zwolnij()
            raise
        except Exception as e:
            self._log('Import przerwany: %s Oryginalny obraz nie został zmieniony.' % e)
            self._usun_kopie()
            self.zwolnij()
            raise

    # ------------------------------------------------------------------ 7
    def zastap(self):
        if not self.zweryfikowano:
            raise BladImportu('Zamiana jest możliwa wyłącznie po udanej weryfikacji.')
        znacznik = time.strftime('%Y%m%d-%H%M%S')
        self.zapasowa = '%s.przed-importem-%s' % (self.sciezka, znacznik)
        if os.path.exists(self.zapasowa):
            raise BladImportu('Plik kopii zapasowej już istnieje: %s' % self.zapasowa)
        try:
            blokady.sprawdz_czy_uzywany(self.sciezka)
            if self.hak_przed_zamiana:
                self.hak_przed_zamiana('1')
            os.replace(self.sciezka, self.zapasowa)
            try:
                if self.hak_przed_zamiana:
                    self.hak_przed_zamiana('2')
                os.replace(self.kopia, self.sciezka)
            except Exception:
                os.replace(self.zapasowa, self.sciezka)       # przywrocenie oryginalu
                self.zapasowa = None
                raise
            self.zastapiono = True
            with bsp2.Dysk(self.sciezka) as d:
                d.wolumin.waliduj()
            self._log('Obraz zastąpiony zweryfikowaną kopią. Oryginał zachowano jako: %s (możesz go usunac po '
                      'sprawdzeniu plików w Bursztyn OS).' % self.zapasowa)
        finally:
            self.zwolnij()

    def odrzuc(self):
        self._usun_kopie()
        self._log('Kopia robocza odrzucona. Oryginalny obraz nie został zmieniony.')
        self.zwolnij()

    def _usun_kopie(self):
        try:
            if os.path.exists(self.kopia):
                os.unlink(self.kopia)
        except OSError:
            self._log('Nie można usunąć kopii roboczej %s - usuń ją ręcznie.' % self.kopia)

    def zwolnij(self):
        if self.blokada:
            self.blokada.__exit__()
            self.blokada = None
