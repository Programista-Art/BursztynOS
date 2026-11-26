#!/usr/bin/env python3
"""Bursztynowy Importer Plikow - testy na JEDNORAZOWYCH obrazach (nigdy na wirtualny_dysk.img uzytkownika).

Obrazy testowe tworzy narzedzie zbudowane z produkcyjnego kodu jadra (narzedzia/bursztynowy_importer/
psf_obraz_host.cpp: storage_format.cpp + psf.cpp), wiec maja dokladnie uklad instalatora. Po imporcie ten sam
kod jadra montuje obraz (jak zainstalowany system), waliduje metadane i liczy SHA-256 kazdego pliku - to
niezalezny czytnik wzgledem zapisu w Pythonie. Zmienne: BURSZTYN_PSF_HOST (sciezka narzedzia),
IMPORTER_TMP (katalog roboczy, domyslnie tymczasowy).
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest

KORZEN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(KORZEN, 'narzedzia'))
from bursztynowy_importer import blokady, bsp2, plan, transakcja  # noqa: E402
from bursztynowy_importer.bsp2 import BLOK, BladObrazu  # noqa: E402

NARZEDZIE = os.environ.get('BURSZTYN_PSF_HOST', os.path.join(KORZEN, '.importer-test', 'psf_obraz_host'))
GRAJEK_E2E = os.environ.get('BURSZTYN_GRAJEK_E2E', os.path.join(KORZEN, '.importer-test', 'grajek_e2e'))
ROZMIAR_MIB = 660
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for c in iter(lambda: f.read(1 << 20), b''):
            h.update(c)
    return h.hexdigest()


def jadro(*a):
    return subprocess.run([NARZEDZIE] + list(a), capture_output=True, text=True, env=ENV)


def jadro_sprawdz(obraz, *korzenie):
    r = jadro('sprawdz', obraz, *(korzenie or ('/dane', '/uzytkownicy')))
    pliki = {}
    for l in r.stdout.splitlines():
        if l.startswith('PLIK '):
            _, h, n, s = l.split(' ', 3)
            pliki[s] = (h, int(n))
    return r.returncode, pliki, r.stderr


class Baza(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(NARZEDZIE):
            raise unittest.SkipTest('brak narzedzia %s (make test-importer je buduje)' % NARZEDZIE)
        cls.tmp = tempfile.mkdtemp(prefix='importer-', dir=os.environ.get('IMPORTER_TMP'))
        cls.wzor = os.path.join(cls.tmp, 'wzor.img')
        r = jadro('utworz', cls.wzor, str(ROZMIAR_MIB))
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.d = tempfile.mkdtemp(dir=self.tmp)
        self.obraz = os.path.join(self.d, 'wirtualny_dysk_test.img')
        subprocess.run(['cp', '--sparse=always', self.wzor, self.obraz], check=True)
        self.src = os.path.join(self.d, 'zrodla')
        os.mkdir(self.src)

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def plik(self, nazwa, rozmiar=None, dane=None):
        p = os.path.join(self.src, nazwa)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(dane if dane is not None else os.urandom(rozmiar))
        return p

    def importuj(self, sciezki, konflikt=None, katalog=None, zastap=True, **haki):
        lista = []
        self.assertEqual(plan.dodaj_pliki(lista, sciezki, katalog), [])
        if konflikt:
            for p in lista:
                p.konflikt = konflikt
        t = transakcja.Transakcja(self.obraz, lista, **{k: v for k, v in haki.items() if k in ('anuluj', 'zglos')})
        for k, v in haki.items():
            if k.startswith('hak_'):
                setattr(t, k, v)
        t.przygotuj()
        t.wykonaj()
        if zastap:
            t.zastap()
        return t, lista

    def assertNietkniety(self, sciezka, skrot):
        self.assertEqual(sha(sciezka), skrot)
        self.assertFalse(os.path.exists(sciezka + '.importer-kopia-robocza'))
        self.assertFalse(os.path.exists(sciezka + '.importer-blokada'))


class TestWykrywanie(Baza):
    def test_poprawny_obraz_tylko_odczyt(self):
        przed, mtime = sha(self.obraz), os.stat(self.obraz).st_mtime_ns
        ins = transakcja.Inspekcja(self.obraz)
        self.assertEqual(ins.ostrzezenia, [])
        self.assertTrue(all(ins.katalogi.values()))
        self.assertGreater(ins.wolne, 100 << 20)
        self.assertEqual(ins.pojemnosc - ins.zajete, ins.wolne)
        self.assertTrue(any('BSP2' in n for _, _, n in ins.partycje) and any('ESP' in n for _, _, n in ins.partycje))
        self.assertEqual((sha(self.obraz), os.stat(self.obraz).st_mtime_ns), (przed, mtime))

    def _uszkodz(self, offset, dane):
        with open(self.obraz, 'r+b') as f:
            f.seek(offset)
            f.write(dane)

    def _odrzucony(self, fragment):
        przed = sha(self.obraz)
        with self.assertRaises((BladObrazu, transakcja.BladImportu)) as e:
            transakcja.Inspekcja(self.obraz)
        self.assertIn(fragment, str(e.exception))
        lista = []
        plan.dodaj_pliki(lista, [self.plik('a.mp3', 1000)])
        with self.assertRaises((BladObrazu, transakcja.BladImportu)):
            transakcja.Transakcja(self.obraz, lista).przygotuj()
        self.assertNietkniety(self.obraz, przed)

    def _bsp(self):
        with bsp2.Dysk(self.obraz) as d:
            return d.bsp_start * 512, d.bsp_sektory, d.wolumin.start_wezlow, d.wolumin.start_danych, d.wolumin.korzen

    def test_obce_formaty(self):
        with open(self.obraz, 'wb') as f:
            f.write(os.urandom(1 << 20))
        self._odrzucony('GPT')
        with open(self.obraz, 'wb') as f:
            f.write(b'QFI\xfb' + bytes(1 << 20))
        self._odrzucony('QCOW2')
        with open(self.obraz, 'wb') as f:
            f.write(bytes(64 * 512) + b'\x01CD001' + bytes(1 << 20))
        self._odrzucony('ISO9660')
        with open(self.obraz, 'wb') as f:
            f.write(bytes(1000))
        self._odrzucony('512')

    def test_obciety(self):
        with open(self.obraz, 'r+b') as f:
            f.truncate(300 << 20)
        self._odrzucony('GPT')

    def test_uszkodzona_gpt(self):
        self._uszkodz(512 + 40, b'\x99')
        self._odrzucony('GPT')

    def test_uszkodzony_superblok_i_znacznik(self):
        start, sektory, _, _, _ = self._bsp()
        self._uszkodz(start, b'XSP2')
        self._odrzucony('BSP2')
        self._uszkodz(start, b'BSP2')
        self._uszkodz(start + (sektory - 2) * 512 + 12, struct.pack('<I', 1))
        self._uszkodz(start + (sektory - 1) * 512 + 12, struct.pack('<I', 1))
        self._odrzucony('znacznik')

    def test_uszkodzone_metadane(self):
        start, _, sw, sd, korzen = self._bsp()
        with bsp2.Dysk(self.obraz) as d:
            k = d.wolumin.wezel(korzen)
            blok_korzenia = k.wsk[0]
            wolny = next(i for i in range(1, d.wolumin.liczba_wezlow + 1) if d.wolumin.wezel(i).typ == 0)
        # a) nieznany typ wezla
        self._uszkodz(start + (sw + wolny - 1) * BLOK, b'\x07')
        self._odrzucony('typ')
        self._uszkodz(start + (sw + wolny - 1) * BLOK, b'\x00')
        # b) bit bitmapy wyczyszczony dla uzywanego bloku katalogu glownego
        bajt = start + BLOK + blok_korzenia // 8
        with open(self.obraz, 'rb') as f:
            f.seek(bajt)
            v = f.read(1)[0]
        self._uszkodz(bajt, bytes([v & ~(1 << (blok_korzenia & 7)) & 0xFF]))
        self._odrzucony('nieprzydzielony')
        self._uszkodz(bajt, bytes([v]))
        # c) powtorzona nazwa w katalogu glownym (drugi wpis 'dane' -> jadro naprawiloby po cichu)
        blok = start + (sd + blok_korzenia) * BLOK
        with open(self.obraz, 'rb') as f:
            f.seek(blok)
            d = bytearray(f.read(BLOK))
        wpis_dane = next(d[i:i + 64] for i in range(0, BLOK, 64) if d[i + 8:i + 13] == b'dane\0')
        wolne = next(i for i in range(0, BLOK, 64) if d[i:i + 8] == bytes(8))
        self._uszkodz(blok + wolne, bytes(wpis_dane))
        self._odrzucony('powtórzoną')


class TestImport(Baza):
    def test_kategorie_i_katalogi(self):
        for ext in ('.mp3', '.wav'):
            self.assertEqual(plan.domyslny_katalog(plan.kategoria('x' + ext.upper())), '/dane/muzyka')
        for ext in ('.jpg', '.jpeg', '.png', '.webp', '.gif', '.bmp'):
            self.assertEqual(plan.domyslny_katalog(plan.kategoria('x' + ext)), '/dane/zdjecia')
        for ext in ('.txt', '.pdf', '.doc', '.docx', '.odt', '.rtf'):
            self.assertEqual(plan.domyslny_katalog(plan.kategoria('x' + ext)), '/uzytkownicy/domyslny/Dokumenty')
        self.assertIsNone(plan.domyslny_katalog(plan.kategoria('x.zip')))

    def test_import_i_niezalezna_weryfikacja_jadrem(self):
        pliki = [self.plik('Żółta łódź – utwór.mp3', 3_000_000), self.plik('Electric Pulse.wav', 48_044),
                 self.plik('zdjęcie.jpg', 70_000), self.plik('logo.png', 5000), self.plik('Raport roczny.pdf', 9000),
                 self.plik('notatka.txt', dane=b'Bursztyn OS\n'), self.plik('pusty.docx', dane=b'')]
        t, lista = self.importuj(pliki)
        self.assertTrue(t.zastapiono and os.path.exists(t.zapasowa))
        rc, w, err = jadro_sprawdz(self.obraz)
        self.assertEqual(rc, 0, err)
        for p in lista:
            self.assertEqual(w[p.sciezka_docelowa], (sha(p.zrodlo), os.path.getsize(p.zrodlo)), p.sciezka_docelowa)
        self.assertIn('/dane/muzyka/Żółta łódź – utwór.mp3', w)
        self.assertIn('/uzytkownicy/domyslny/Dokumenty/Raport roczny.pdf', w)
        # oryginal zachowany bajt w bajt jako kopia zapasowa
        self.assertEqual(sha(t.zapasowa), sha(self.wzor))
        # wlasny katalog (tworzony z rodzicami) pod /dane
        t2, l2 = self.importuj([self.plik('archiwum.zip', 10_000)], katalog='/dane/projekty/2026')
        rc, w, err = jadro_sprawdz(self.obraz)
        self.assertEqual(rc, 0, err)
        self.assertEqual(w['/dane/projekty/2026/archiwum.zip'][0], sha(l2[0].zrodlo))

    def test_nazwa_55_bajtow(self):
        n55 = 'ż' * 24 + 'abc.wav'              # 48 + 7 = 55 bajtow
        self.assertEqual(len(n55.encode()), 55)
        n56 = 'ż' * 24 + 'abcd.wav'
        self.assertEqual(len(n56.encode()), 56)
        self.assertIsNone(plan.blad_nazwy(n55))
        self.assertIn('56 bajtów', plan.blad_nazwy(n56))
        prop = plan.proponowana_nazwa(n56)
        self.assertLessEqual(len(prop.encode()), 55)
        self.assertTrue(prop.endswith('.wav'))
        prop.encode('utf-8').decode('utf-8')
        lista = []
        plan.dodaj_pliki(lista, [self.plik(n55, 100), self.plik(n56, 200)])
        with bsp2.Dysk(self.obraz) as d:
            plan.ocen(lista, d.wolumin)
        dlugi = [p for p in lista if p.nazwa == n56][0]
        self.assertTrue(dlugi.blad and dlugi.propozycja)                   # nigdy nie obcinane po cichu
        with self.assertRaises(transakcja.BladImportu):
            transakcja.Transakcja(self.obraz, lista).przygotuj()
        nowa = dlugi.propozycja
        dlugi.nazwa = nowa                                                 # jawna zgoda uzytkownika
        t = transakcja.Transakcja(self.obraz, lista)
        t.przygotuj(); t.wykonaj(); t.zastap()
        rc, w, err = jadro_sprawdz(self.obraz)
        self.assertEqual(rc, 0, err)
        self.assertIn('/dane/muzyka/' + n55, w)
        self.assertIn('/dane/muzyka/' + nowa, w)

    def test_konflikty(self):
        pierwszy = self.plik('a/utwór.mp3', 5000)
        self.importuj([pierwszy])
        drugi = self.plik('b/utwór.mp3', 7000)
        # domyslnie: Pomin
        lista = []
        plan.dodaj_pliki(lista, [drugi])
        t = transakcja.Transakcja(self.obraz, lista)
        with self.assertRaises(transakcja.BladImportu):      # nic do zrobienia: plik zostalby pominiety
            t.przygotuj()
        self.assertEqual(lista[0].status, 'Istnieje – pominięty')
        # Zmien nazwe
        t, l = self.importuj([drugi], konflikt=plan.ZMIEN_NAZWE)
        self.assertEqual(l[0].nazwa_koncowa, 'utwór (2).mp3')
        # Zastap
        trzeci = self.plik('c/utwór.mp3', 600_000)
        t, l = self.importuj([trzeci], konflikt=plan.ZASTAP)
        rc, w, err = jadro_sprawdz(self.obraz)
        self.assertEqual(rc, 0, err)
        self.assertEqual(w['/dane/muzyka/utwór.mp3'], (sha(trzeci), 600_000))
        self.assertEqual(w['/dane/muzyka/utwór (2).mp3'], (sha(drugi), 7000))
        # duplikaty na liscie: druga pozycja dostaje nowa nazwe tylko przy "Zmien nazwe"
        x, y = self.plik('d/nowy.wav', 10), self.plik('e/nowy.wav', 20)
        t, l = self.importuj([x, y], konflikt=plan.ZMIEN_NAZWE)
        self.assertEqual([p.nazwa_koncowa for p in l], ['nowy.wav', 'nowy (2).wav'])
        # zastapienie katalogu plikiem jest odrzucane
        lista = []
        plan.dodaj_pliki(lista, [self.plik('f/Dokumenty', dane=b'x')], '/uzytkownicy/domyslny')
        lista[0].konflikt = plan.ZASTAP
        with self.assertRaises(transakcja.BladImportu):
            transakcja.Transakcja(self.obraz, lista).przygotuj()
        self.assertIn('katalog', lista[0].blad)

    def test_duzy_plik_ponad_128_mib(self):
        p = os.path.join(self.src, 'Długi koncert.wav')
        wzor = os.urandom(1 << 20)
        with open(p, 'wb') as f:
            for i in range(130):
                f.write(struct.pack('<I', i) + wzor[4:])
        t, l = self.importuj([p])
        rc, w, err = jadro_sprawdz(self.obraz, '/dane/muzyka')
        self.assertEqual(rc, 0, err)
        self.assertEqual(w['/dane/muzyka/Długi koncert.wav'], (sha(p), 130 << 20))
        with bsp2.Dysk(self.obraz) as d:
            w8 = d.wolumin.znajdz('/dane/muzyka/Długi koncert.wav')
            self.assertNotEqual(w8.posr2, bsp2.BRAK)              # uzyte wskazniki 2x posrednie
        os.unlink(t.zapasowa)

    def test_limity_rozmiaru_i_miejsca(self):
        maks = os.path.join(self.src, 'maks.wav')
        wiecej = os.path.join(self.src, 'za_duzy.wav')
        for s, n in ((maks, bsp2.MAKS_ROZMIAR_PLIKU), (wiecej, bsp2.MAKS_ROZMIAR_PLIKU + 1)):
            with open(s, 'wb') as f:
                f.truncate(n)                                    # plik rzadki: bez zajmowania dysku hosta
        lista = []
        plan.dodaj_pliki(lista, [maks, wiecej])
        with bsp2.Dysk(self.obraz) as d:
            z = plan.ocen(lista, d.wolumin)
        self.assertIsNone(lista[0].blad)
        self.assertEqual(lista[1].status, 'Za duży')
        self.assertGreater(z.bloki, bsp2.MAKS_BLOKOW_PLIKU)
        przed = sha(self.obraz)
        with self.assertRaises(transakcja.BladImportu) as e:     # pozycja z bledem blokuje import
            transakcja.Transakcja(self.obraz, lista).przygotuj()
        del lista[1]
        with self.assertRaises(transakcja.BladImportu) as e:     # 1 GiB nie miesci sie w ~180 MiB wolnego
            transakcja.Transakcja(self.obraz, lista).przygotuj()
        self.assertIn('systemie plików Bursztyn OS', str(e.exception))
        self.assertNietkniety(self.obraz, przed)

    def test_brak_miejsca_na_hoscie(self):
        przed = sha(self.obraz)
        lista = []
        plan.dodaj_pliki(lista, [self.plik('a.mp3', 1000)])
        t = transakcja.Transakcja(self.obraz, lista)
        t.hak_wolne_miejsce = lambda katalog: 1 << 20
        with self.assertRaises(transakcja.BladImportu) as e:
            t.przygotuj()
        self.assertIn('dysku komputera', str(e.exception))
        self.assertNietkniety(self.obraz, przed)


class TestBezpieczenstwo(Baza):
    def test_przerwany_zapis(self):
        przed = sha(self.obraz)
        licznik = [0]

        def awaria(bid):
            licznik[0] += 1
            if licznik[0] == 50:
                raise OSError('symulowany blad zapisu dysku')
        with self.assertRaises(OSError):
            self.importuj([self.plik('a.mp3', 1_000_000)], hak_zapisu_bloku=awaria)
        self.assertNietkniety(self.obraz, przed)

    def test_anulowanie(self):
        przed = sha(self.obraz)
        zdarzenia = []
        anuluj = lambda: any(r == 'plik' for r, _ in zdarzenia)   # noqa: E731
        with self.assertRaises(transakcja.Przerwano):
            self.importuj([self.plik('a.mp3', 100_000), self.plik('b.mp3', 100_000)],
                          zglos=lambda r, d: zdarzenia.append((r, d)), anuluj=anuluj)
        self.assertNietkniety(self.obraz, przed)

    def test_nieudana_weryfikacja(self):
        for gdzie in ('plik', 'esp', 'istniejacy'):
            przed = sha(self.obraz)
            with bsp2.Dysk(self.obraz) as d:
                esp = d.esp[0][2] * 512 + 4096
                k = d.wolumin.wezel(d.wolumin.korzen)
                blok_istn = d.wolumin.start + (d.wolumin.start_danych + k.wsk[0]) * BLOK

            def psuj(kopia):
                with bsp2.Dysk(kopia) as d:
                    w = d.wolumin.znajdz('/dane/muzyka/a.mp3')
                    off = {'plik': d.wolumin.start + (d.wolumin.start_danych + w.wsk[3]) * BLOK + 7,
                           'esp': esp, 'istniejacy': blok_istn + 4000}[gdzie]
                with open(kopia, 'r+b') as f:
                    f.seek(off)
                    b = f.read(1)
                    f.seek(off)
                    f.write(bytes([b[0] ^ 0x55]))
            with self.assertRaises(transakcja.BladImportu) as e:
                self.importuj([self.plik('a.mp3', 100_000)], hak_po_zapisie=psuj)
            self.assertIn('Weryfikacja', str(e.exception))
            self.assertNietkniety(self.obraz, przed)

    def test_wycofanie_zamiany(self):
        przed = sha(self.obraz)

        def hak(etap):
            if etap == '2':
                raise OSError('symulowany blad zamiany')
        lista = []
        plan.dodaj_pliki(lista, [self.plik('a.mp3', 10_000)])
        t = transakcja.Transakcja(self.obraz, lista)
        t.hak_przed_zamiana = hak
        t.przygotuj()
        t.wykonaj()
        with self.assertRaises(OSError):
            t.zastap()
        self.assertEqual(sha(self.obraz), przed)                 # oryginal wrocil na miejsce
        self.assertFalse([f for f in os.listdir(self.d) if '.przed-importem-' in f])
        t.odrzuc()
        self.assertNietkniety(self.obraz, przed)

    def test_trwalosc_i_dalsza_praca_jadra(self):
        a = self.plik('pierwszy.mp3', 20_000)
        self.importuj([a])
        # "ponowne zamontowanie": jadro dopisuje plik do obrazu po imporcie (kontynuuje na metadanych importera)
        b = self.plik('z_jadra.wav', 3_000_000)
        r = jadro('zapisz', self.obraz, '/dane/muzyka/z_jadra.wav', b)
        self.assertEqual(r.returncode, 0, r.stderr)
        ins = transakcja.Inspekcja(self.obraz)                   # Python czyta zapis jadra
        self.assertEqual(ins.ostrzezenia, [])
        with bsp2.Dysk(self.obraz) as d:
            w = d.wolumin.znajdz('/dane/muzyka/z_jadra.wav')
            h = hashlib.sha256()
            for c in d.wolumin.czytaj_plik(w):
                h.update(c)
            self.assertEqual(h.hexdigest(), sha(b))
        c = self.plik('trzeci.wav', 70_000)
        self.importuj([c])
        for _ in range(2):
            rc, w, err = jadro_sprawdz(self.obraz, '/dane/muzyka')
            self.assertEqual(rc, 0, err)
            self.assertEqual({k: v[0] for k, v in w.items()},
                             {'/dane/muzyka/pierwszy.mp3': sha(a), '/dane/muzyka/z_jadra.wav': sha(b),
                              '/dane/muzyka/trzeci.wav': sha(c)})

    def test_wspolbiezny_dostep(self):
        przed = sha(self.obraz)
        lista = []
        plan.dodaj_pliki(lista, [self.plik('a.mp3', 1000)])
        # a) inny importer (zywy proces)
        with open(self.obraz + '.importer-blokada', 'w') as f:
            f.write('%d %s\n' % (os.getppid(), __import__('socket').gethostname()))
        with self.assertRaises(blokady.ObrazUzywany):
            transakcja.Transakcja(self.obraz, lista).przygotuj()
        os.unlink(self.obraz + '.importer-blokada')
        # b) martwa blokada tego hosta jest rozpoznawana
        with open(self.obraz + '.importer-blokada', 'w') as f:
            f.write('999999999 %s\n' % __import__('socket').gethostname())
        t = transakcja.Transakcja(self.obraz, lista)
        t.przygotuj()
        t.odrzuc()
        # c) proces trzymajacy obraz otwarty (jak QEMU) - wykrywany przez /proc i blokade fcntl
        kod = ('import fcntl,os,sys,time; fd=os.open(sys.argv[1], os.O_RDWR); fcntl.lockf(fd, fcntl.LOCK_EX); '
               'print("ok", flush=True); time.sleep(30)')
        pr = subprocess.Popen([sys.executable, '-c', kod, self.obraz], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(pr.stdout.readline().strip(), 'ok')
            with self.assertRaises(blokady.ObrazUzywany):
                transakcja.Transakcja(self.obraz, lista).przygotuj()
            with self.assertRaises(blokady.ObrazUzywany):
                with blokady.Blokada(self.obraz):
                    pass
        finally:
            pr.kill()
            pr.wait()
        self.assertNietkniety(self.obraz, przed)

    def test_dowiazania_i_sciezki(self):
        folder = os.path.join(self.src, 'folder')
        os.makedirs(os.path.join(folder, 'pod'))
        prawdziwy = self.plik('folder/pod/ok.mp3', 100)
        os.symlink('/etc/passwd', os.path.join(folder, 'haslo.txt'))
        os.symlink('/etc', os.path.join(folder, 'etc'))
        lista = []
        kom = plan.dodaj_folder(lista, folder)
        self.assertEqual([p.zrodlo for p in lista], [prawdziwy])
        self.assertTrue(any('dowiązanie' in k for k in kom) and len(kom) == 2)
        for zly in ('/dane/../system', '/system/x', '/programy', '/', '/dane//x', 'dane/x', '/sloty/a', '/jadro',
                    '/dane/' + 'x' * 56, '/aktualizacje'):
            with self.assertRaises(ValueError, msg=zly):
                plan.sprawdz_katalog_docelowy(zly)
        self.assertEqual(plan.sprawdz_katalog_docelowy('/dane/muzyka/Rock'), '/dane/muzyka/Rock')
        for zla in ('a/b.mp3', '..', '.', 'x\x01.mp3', 'a\\b.mp3'):
            self.assertIsNotNone(plan.blad_nazwy(zla), zla)


class TestGrajekE2E(Baza):
    def test_import_muzyki_i_odtwarzanie_z_dysku(self):
        """Przeplyw z wymagan: import WAV/MP3 -> start systemu (montowanie PSF jadra) -> Grajek znajduje i dekoduje."""
        if not os.path.isfile(GRAJEK_E2E):
            self.skipTest('brak %s' % GRAJEK_E2E)
        r = subprocess.run([GRAJEK_E2E, 'generuj', self.src], capture_output=True, text=True, env=ENV)
        self.assertEqual(r.returncode, 0, r.stderr)
        pliki = sorted(os.path.join(self.src, f) for f in os.listdir(self.src))
        t, lista = self.importuj(pliki)
        os.unlink(t.zapasowa)                                  # oszczednosc miejsca hosta (130 MiB)
        self.assertEqual({p.sciezka_docelowa for p in lista},
                         {'/dane/muzyka/Żółta łódź.wav', '/dane/muzyka/Bursztynowy test.mp3', '/dane/muzyka/Długi koncert.wav'})
        for _ in range(2):                                     # dwa kolejne "uruchomienia" - pliki trwale
            r = subprocess.run([GRAJEK_E2E, 'graj', self.obraz], capture_output=True, text=True, env=ENV)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('3 utwory', r.stdout)


class TestGui(Baza):
    def test_okno_bez_interakcji(self):
        if not os.environ.get('DISPLAY') and os.name != 'nt':
            self.skipTest('brak ekranu')
        import tkinter as tk
        from bursztynowy_importer import gui
        root = tk.Tk()
        root.withdraw()
        try:
            app = gui.Aplikacja(root)
            app.var_obraz.set(self.obraz)
            app._po_inspekcji(transakcja.Inspekcja(self.obraz))
            plan.dodaj_pliki(app.pozycje, [self.plik('a.mp3', 10), self.plik('b.png', 10), self.plik('c.zip', 10)])
            app._odswiez_liste()
            self.assertEqual(len(app.drzewo.get_children()), 3)
            self.assertIn('Wolne', ''.join(app.raport) + 'Wolne')
            self.assertEqual(app.var_info['fs'].get(), 'BSP2 / PSF (Bursztyn OS)')
            wiersz = app.drzewo.item('2')['values']
            self.assertIn('⚠', wiersz[5])                         # nieznany typ wymaga wyboru katalogu
            app.drzewo.selection_set('2')
            app.var_cel.set('wlasny')
            app.var_wlasny.set('/dane/archiwa')
            app.zmien_cel()
            self.assertEqual(app.drzewo.item('2')['values'][3], '/dane/archiwa')
            self.assertEqual(str(app.b_start['state']), 'normal')
        finally:
            root.destroy()


if __name__ == '__main__':
    unittest.main(verbosity=2)
