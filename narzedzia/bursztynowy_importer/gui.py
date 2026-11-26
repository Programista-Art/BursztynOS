"""Bursztynowy Importer Plików - interfejs Tkinter (ttk). Narzędzie hosta, nie aplikacja Bursztyn OS.

Cała praca na obrazie odbywa się w wątku roboczym (transakcja.py). Wątek NIGDY nie dotyka widżetów: wysyła
zdarzenia do queue.Queue, a interfejs odbiera je co 100 ms przez after(). Zapis na dysk wykonuje wyłącznie
moduł transakcji, nie obsługa zdarzeń interfejsu.
"""
import os
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import bsp2, plan as planowanie, transakcja

TYTUL = 'Bursztynowy Importer Plików'
CELE = [('muzyka', 'Muzyka', planowanie.KATALOG_MUZYKI), ('obrazy', 'Obrazy', planowanie.KATALOG_OBRAZOW),
        ('dokumenty', 'Dokumenty', planowanie.KATALOG_DOKUMENTOW), ('wlasny', 'Własny katalog', None)]


class Aplikacja:
    def __init__(self, root):
        self.root = root
        root.title(TYTUL)
        root.minsize(900, 600)
        self.kolejka = queue.Queue()
        self.pozycje = []
        self.inspekcja = None
        self.watek = None
        self.trans = None
        self.anuluj_flaga = threading.Event()
        self.raport = []
        self._buduj()
        root.after(100, self._odbierz)
        root.protocol('WM_DELETE_WINDOW', self._zamknij)

    # ------------------------------------------------------------------ układ
    def _buduj(self):
        s = ttk.Style(self.root)
        if 'clam' in s.theme_names() and os.name != 'nt':
            s.theme_use('clam')
        s.configure('Naglowek.TLabel', font=('TkDefaultFont', 10, 'bold'))
        glowna = ttk.Frame(self.root, padding=10)
        glowna.pack(fill='both', expand=True)

        ob = ttk.LabelFrame(glowna, text='Obraz dysku Bursztyn OS', padding=8)
        ob.pack(fill='x')
        self.var_obraz = tk.StringVar(value='(nie wybrano)')
        ttk.Label(ob, textvariable=self.var_obraz).grid(row=0, column=0, columnspan=4, sticky='w')
        self.b_wybierz = ttk.Button(ob, text='Wybierz obraz dysku', command=self.wybierz_obraz)
        self.b_sprawdz = ttk.Button(ob, text='Sprawdź dysk', command=self.sprawdz, state='disabled')
        self.b_pokaz = ttk.Button(ob, text='Pokaż zawartość', command=self.pokaz_zawartosc, state='disabled')
        for i, b in enumerate((self.b_wybierz, self.b_sprawdz, self.b_pokaz)):
            b.grid(row=1, column=i, padx=(0, 6), pady=6, sticky='w')
        self.var_info = {}
        for r, (k, t) in enumerate((('fs', 'System plików'), ('part', 'Partycje'), ('poj', 'Pojemność'),
                                    ('zaj', 'Zajęte'), ('wol', 'Wolne'), ('wez', 'Wolne węzły'))):
            ttk.Label(ob, text=t + ':').grid(row=2 + r // 3, column=(r % 3) * 2, sticky='w', padx=(0, 4))
            self.var_info[k] = tk.StringVar(value='–')
            ttk.Label(ob, textvariable=self.var_info[k]).grid(row=2 + r // 3, column=(r % 3) * 2 + 1, sticky='w', padx=(0, 18))

        lf = ttk.LabelFrame(glowna, text='Pliki do importu', padding=8)
        lf.pack(fill='both', expand=True, pady=8)
        pasek = ttk.Frame(lf)
        pasek.pack(fill='x')
        ttk.Button(pasek, text='Dodaj pliki', command=self.dodaj_pliki).pack(side='left')
        ttk.Button(pasek, text='Dodaj folder', command=self.dodaj_folder).pack(side='left', padx=6)
        ttk.Button(pasek, text='Usuń z listy importu', command=self.usun_z_listy).pack(side='left')
        ttk.Label(pasek, text='   Cel zaznaczonych:').pack(side='left')
        self.var_cel = tk.StringVar(value='auto')
        for kod, nazwa, _ in CELE:
            ttk.Radiobutton(pasek, text=nazwa, value=kod, variable=self.var_cel, command=self.zmien_cel).pack(side='left', padx=2)
        self.var_wlasny = tk.StringVar(value='/dane/')
        self.e_wlasny = ttk.Entry(pasek, textvariable=self.var_wlasny, width=24)
        self.e_wlasny.pack(side='left', padx=4)
        ttk.Button(pasek, text='Zastosuj', command=self.zmien_cel).pack(side='left')

        konfl = ttk.Frame(lf)
        konfl.pack(fill='x', pady=(6, 0))
        ttk.Label(konfl, text='Gdy plik istnieje:').pack(side='left')
        self.var_konflikt = tk.StringVar(value=planowanie.NAZWY_KONFLIKTOW[planowanie.POMIN])
        self.c_konflikt = ttk.Combobox(konfl, textvariable=self.var_konflikt, state='readonly', width=14,
                                       values=list(planowanie.NAZWY_KONFLIKTOW.values()))
        self.c_konflikt.pack(side='left', padx=4)
        ttk.Button(konfl, text='Ustaw dla zaznaczonych', command=self.ustaw_konflikt).pack(side='left')
        ttk.Button(konfl, text='Przyjmij proponowaną nazwę', command=self.przyjmij_nazwe).pack(side='left', padx=6)

        kol = ('plik', 'rozmiar', 'kat', 'cel', 'nazwa', 'status')
        self.drzewo = ttk.Treeview(lf, columns=kol, show='headings', selectmode='extended', height=12)
        for k, t, w in zip(kol, ('Plik źródłowy', 'Rozmiar', 'Kategoria', 'Katalog docelowy', 'Nazwa docelowa', 'Stan'),
                           (260, 80, 80, 210, 180, 170)):
            self.drzewo.heading(k, text=t)
            self.drzewo.column(k, width=w, anchor='e' if k == 'rozmiar' else 'w', stretch=k in ('plik', 'status'))
        sb = ttk.Scrollbar(lf, orient='vertical', command=self.drzewo.yview)
        self.drzewo.configure(yscrollcommand=sb.set)
        self.drzewo.pack(side='left', fill='both', expand=True, pady=6)
        sb.pack(side='left', fill='y', pady=6)

        dol = ttk.Frame(glowna)
        dol.pack(fill='x')
        self.var_suma = tk.StringVar(value='Razem: 0 plików, 0 B')
        ttk.Label(dol, textvariable=self.var_suma, style='Naglowek.TLabel').pack(side='left')
        self.b_raport = ttk.Button(dol, text='Pokaż raport', command=self.pokaz_raport)
        self.b_raport.pack(side='right')
        self.b_anuluj = ttk.Button(dol, text='Anuluj', command=self.anuluj, state='disabled')
        self.b_anuluj.pack(side='right', padx=6)
        self.b_start = ttk.Button(dol, text='Rozpocznij import', command=self.start, state='disabled')
        self.b_start.pack(side='right')
        self.postep = ttk.Progressbar(glowna, mode='determinate', maximum=1000)
        self.postep.pack(fill='x', pady=(8, 2))
        self.var_etap = tk.StringVar(value='Wybierz obraz dysku zainstalowanego Bursztyn OS. Maszyna wirtualna musi być wyłączona.')
        ttk.Label(glowna, textvariable=self.var_etap).pack(fill='x')
        self.t_ostrz = tk.Text(glowna, height=6, wrap='word', state='disabled')
        self.t_ostrz.pack(fill='x', pady=(6, 0))

    # ------------------------------------------------------------------ pomocnicze
    def _komunikat(self, tekst):
        self.t_ostrz.configure(state='normal')
        self.t_ostrz.insert('end', tekst + '\n')
        self.t_ostrz.see('end')
        self.t_ostrz.configure(state='disabled')
        self.raport.append(tekst)

    def _zajety(self):
        return self.watek is not None and self.watek.is_alive()

    def _odswiez_liste(self):
        if self.inspekcja and not self._zajety():
            try:
                with bsp2.Dysk(self.inspekcja.sciezka) as d:
                    planowanie.ocen(self.pozycje, d.wolumin)
            except Exception as e:     # obraz zmienil sie lub zniknal
                self._komunikat('Nie można ocenić listy względem dysku: %s' % e)
        self.drzewo.delete(*self.drzewo.get_children())
        razem = 0
        for i, p in enumerate(self.pozycje):
            stan = p.wynik and {'zaimportowano': 'Zaimportowano', 'zastapiono': 'Zastąpiono',
                                'pominieto': 'Pominięto'}.get(p.wynik) or p.status or ('Błąd' if p.blad else '')
            if p.blad:
                stan = '⚠ ' + (p.status or 'Błąd')
            self.drzewo.insert('', 'end', iid=str(i), values=(p.zrodlo, transakcja.opis_rozmiaru(p.rozmiar),
                               planowanie.KATEGORIE.get(p.kategoria, ('Inne',))[0], p.katalog or '–',
                               p.nazwa_koncowa or p.nazwa, stan))
            razem += p.rozmiar
        self.var_suma.set('Razem: %d plików, %s' % (len(self.pozycje), transakcja.opis_rozmiaru(razem)))
        self.b_start.configure(state='normal' if self.inspekcja and self.pozycje and not self._zajety() else 'disabled')

    def _zaznaczone(self):
        return [self.pozycje[int(i)] for i in self.drzewo.selection()]

    # ------------------------------------------------------------------ obraz
    def wybierz_obraz(self):
        s = filedialog.askopenfilename(title='Wybierz obraz dysku Bursztyn OS',
                                       filetypes=[('Obraz dysku (raw)', '*.img'), ('Wszystkie pliki', '*.*')])
        if not s:
            return
        self.var_obraz.set(s)
        self.inspekcja = None
        self.b_sprawdz.configure(state='normal')
        self.b_pokaz.configure(state='disabled')
        self.sprawdz()

    def sprawdz(self):
        sciezka = self.var_obraz.get()
        if self._zajety() or not os.path.isfile(sciezka):
            return
        self.var_etap.set('Sprawdzanie dysku (tylko odczyt)…')
        self._uruchom(lambda: ('inspekcja', transakcja.Inspekcja(sciezka)))

    def pokaz_zawartosc(self):
        if not self.inspekcja:
            return
        okno = tk.Toplevel(self.root)
        okno.title('Zawartość dysku – ' + os.path.basename(self.inspekcja.sciezka))
        okno.geometry('640x480')
        t = ttk.Treeview(okno, columns=('rozmiar',), show='tree headings')
        t.heading('#0', text='Ścieżka w Bursztyn OS')
        t.heading('rozmiar', text='Rozmiar')
        t.column('rozmiar', width=110, anchor='e')
        t.pack(fill='both', expand=True)
        try:
            with bsp2.Dysk(self.inspekcja.sciezka) as d:
                d.wolumin.waliduj()
                wezly = {}
                for sciezka, w in d.wolumin.drzewo():
                    if not (sciezka in ('/', '/dane', '/uzytkownicy') or sciezka.startswith(('/dane/', '/uzytkownicy/'))):
                        continue
                    rodzic = sciezka.rsplit('/', 1)[0] or '/'
                    nazwa = sciezka if sciezka == '/' else sciezka.rsplit('/', 1)[1]
                    wezly[sciezka] = t.insert(wezly.get(rodzic, ''), 'end', text=nazwa, open=sciezka.count('/') < 2,
                                              values=('' if w.typ == bsp2.TYP_KATALOG else transakcja.opis_rozmiaru(w.rozmiar),))
        except Exception as e:
            messagebox.showerror(TYTUL, 'Nie można odczytać zawartości: %s' % e, parent=okno)

    # ------------------------------------------------------------------ lista
    def _katalog_wlasny(self):
        return planowanie.sprawdz_katalog_docelowy(self.var_wlasny.get().strip())

    def dodaj_pliki(self):
        pliki = filedialog.askopenfilenames(title='Dodaj pliki do importu')
        if pliki:
            for k in planowanie.dodaj_pliki(self.pozycje, list(pliki)):
                self._komunikat(k)
            self._odswiez_liste()

    def dodaj_folder(self):
        folder = filedialog.askdirectory(title='Dodaj folder do importu (rekurencyjnie, bez dowiązań)')
        if folder:
            for k in planowanie.dodaj_folder(self.pozycje, folder):
                self._komunikat(k)
            self._odswiez_liste()

    def usun_z_listy(self):
        if self._zajety():
            return
        wybrane = set(map(int, self.drzewo.selection()))
        self.pozycje = [p for i, p in enumerate(self.pozycje) if i not in wybrane]
        self._odswiez_liste()

    def zmien_cel(self):
        zaznaczone = self._zaznaczone()
        if not zaznaczone:
            return
        kod = self.var_cel.get()
        try:
            for p in zaznaczone:
                if kod == 'wlasny':
                    p.katalog = self._katalog_wlasny()
                elif kod == 'auto':
                    p.katalog = planowanie.domyslny_katalog(p.kategoria)
                else:
                    p.katalog = dict((c[0], c[2]) for c in CELE)[kod]
                p.blad = None if p.katalog else p.blad
        except ValueError as e:
            messagebox.showerror(TYTUL, str(e))
        self._odswiez_liste()

    def ustaw_konflikt(self):
        kod = {v: k for k, v in planowanie.NAZWY_KONFLIKTOW.items()}[self.var_konflikt.get()]
        zaznaczone = self._zaznaczone()
        if kod == planowanie.ZASTAP and zaznaczone and not messagebox.askyesno(
                TYTUL, 'Zastąpić istniejące pliki o tych samych nazwach (%d zaznaczonych pozycji)?\n\n'
                       'Stara zawartość zostanie usunięta z obrazu po imporcie (oryginalny obraz pozostanie '
                       'w kopii zapasowej).' % len(zaznaczone), icon='warning'):
            return
        for p in zaznaczone:
            p.konflikt = kod
        self._odswiez_liste()

    def przyjmij_nazwe(self):
        for p in self._zaznaczone():
            if p.propozycja:
                p.nazwa = p.propozycja
                p.zgoda_na_nazwe = True
                self._komunikat('Przyjęto nazwę "%s" dla %s.' % (p.nazwa, p.nazwa_zrodla))
        self._odswiez_liste()

    # ------------------------------------------------------------------ import
    def start(self):
        if self._zajety() or not self.inspekcja:
            return
        if any(p.blad for p in self.pozycje):
            messagebox.showwarning(TYTUL, 'Niektóre pozycje mają błędy (oznaczone ⚠). Popraw je albo usuń z listy.')
            return
        if not messagebox.askokcancel(TYTUL, 'Przed importem maszyna wirtualna z tym dyskiem musi być całkowicie '
                                             'wyłączona.\n\nImporter utworzy kopię roboczą obrazu, zapisze pliki w kopii, '
                                             'sprawdzi ją i dopiero wtedy zapyta o zastąpienie oryginału. Kontynuować?'):
            return
        self.anuluj_flaga.clear()
        self.raport = []
        sciezka = self.inspekcja.sciezka

        def praca():
            t = transakcja.Transakcja(sciezka, self.pozycje, zglos=lambda r, d: self.kolejka.put((r, d)),
                                      anuluj=self.anuluj_flaga.is_set)
            t.przygotuj()
            t.wykonaj()
            return ('gotowe', t)
        self.b_anuluj.configure(state='normal')
        self._uruchom(praca)

    def anuluj(self):
        self.anuluj_flaga.set()
        self.var_etap.set('Anulowanie – zakończenie na najbliższej bezpiecznej granicy (między plikami)…')

    def _uruchom(self, funkcja):
        def opakowanie():
            try:
                self.kolejka.put(funkcja())
            except Exception as e:      # przekazanie bledu do watku interfejsu
                self.kolejka.put(('blad', e))
        self.watek = threading.Thread(target=opakowanie, daemon=True)
        self.watek.start()
        for b in (self.b_start, self.b_sprawdz, self.b_wybierz):
            b.configure(state='disabled')

    def _koniec_pracy(self):
        self.b_anuluj.configure(state='disabled')
        self.b_wybierz.configure(state='normal')
        self.b_sprawdz.configure(state='normal' if os.path.isfile(self.var_obraz.get()) else 'disabled')

    def _odbierz(self):
        try:
            while True:
                rodzaj, dane = self.kolejka.get_nowait()
                if rodzaj == 'log':
                    self._komunikat(dane)
                elif rodzaj == 'postep':
                    etap, zrobione, razem = dane
                    self.postep['value'] = 1000 * zrobione // max(1, razem)
                    self.var_etap.set({'kopia': 'Kopia robocza', 'import': 'Zapis plików',
                                       'weryfikacja': 'Weryfikacja'}[etap] + ': %s z %s'
                                      % (transakcja.opis_rozmiaru(zrobione), transakcja.opis_rozmiaru(razem)))
                elif rodzaj == 'plik':
                    self._komunikat('Zapisano %s (%s).' % (dane.sciezka_docelowa, transakcja.opis_rozmiaru(dane.rozmiar)))
                elif rodzaj == 'inspekcja':
                    self._koniec_pracy()
                    self._po_inspekcji(dane)
                elif rodzaj == 'gotowe':
                    self._koniec_pracy()
                    self._po_weryfikacji(dane)
                elif rodzaj == 'zastapiono':
                    self._koniec_pracy()
                    self.var_etap.set('Import zakończony i zweryfikowany. Możesz uruchomić Bursztyn OS.')
                    messagebox.showinfo(TYTUL, 'Import zakończony.\n\nOryginał zachowano jako:\n%s' % dane.zapasowa)
                    self.sprawdz()
                elif rodzaj == 'blad':
                    self._koniec_pracy()
                    self.var_etap.set('Błąd: %s' % dane)
                    self._komunikat('BŁĄD: %s' % dane)
                    if not isinstance(dane, transakcja.Przerwano):
                        messagebox.showerror(TYTUL, str(dane) or dane.__class__.__name__)
                    else:
                        self.var_etap.set('Import anulowany. Oryginalny obraz nie został zmieniony.')
                    self._odswiez_liste()
        except queue.Empty:
            pass
        self.root.after(100, self._odbierz)

    def _po_inspekcji(self, ins):
        self.inspekcja = ins
        self.var_info['fs'].set('BSP2 / PSF (Bursztyn OS)')
        self.var_info['part'].set('; '.join(n for _, _, n in ins.partycje))
        self.var_info['poj'].set(transakcja.opis_rozmiaru(ins.pojemnosc))
        self.var_info['zaj'].set(transakcja.opis_rozmiaru(ins.zajete))
        self.var_info['wol'].set(transakcja.opis_rozmiaru(ins.wolne))
        self.var_info['wez'].set('%d z %d' % (ins.wolne_wezly, ins.wezly))
        self.b_pokaz.configure(state='normal')
        self.var_etap.set('Dysk poprawny. Dodaj pliki i rozpocznij import.')
        for l in ins.opis():
            self._komunikat(l)
        self._odswiez_liste()

    def _po_weryfikacji(self, t):
        self.trans = t
        self._odswiez_liste()
        if messagebox.askyesno(TYTUL, 'Kopia robocza została zapisana i zweryfikowana.\n\nZastąpić oryginalny obraz '
                                      'zweryfikowaną kopią? Oryginał zostanie zachowany jako kopia zapasowa.'):
            self._uruchom(lambda: (t.zastap(), ('zastapiono', t))[1])
        else:
            t.odrzuc()
            self.var_etap.set('Kopia robocza odrzucona. Oryginalny obraz nie został zmieniony.')

    def pokaz_raport(self):
        okno = tk.Toplevel(self.root)
        okno.title('Raport importu')
        t = tk.Text(okno, wrap='word', width=100, height=30)
        t.pack(fill='both', expand=True)
        linie = list(self.raport)
        for p in self.pozycje:
            linie.append('%s -> %s: %s%s' % (p.zrodlo, p.sciezka_docelowa if p.katalog else '–',
                                             p.wynik or p.status or '', (' (SHA-256 %s)' % p.sha256) if p.sha256 else ''))
        t.insert('end', '\n'.join(linie) or 'Brak wpisów.')
        t.configure(state='disabled')

    def _zamknij(self):
        if self._zajety():
            if not messagebox.askyesno(TYTUL, 'Trwa praca na obrazie. Anulować i zamknąć po bezpiecznym przerwaniu?'):
                return
            self.anuluj_flaga.set()
            self.var_etap.set('Zamykanie po bezpiecznym przerwaniu pracy…')
            self.root.after(200, self._zamknij_po_watku)
            return
        if self.trans and self.trans.zweryfikowano and not self.trans.zastapiono:
            self.trans.odrzuc()
        self.root.destroy()

    def _zamknij_po_watku(self):
        if self._zajety():
            self.root.after(200, self._zamknij_po_watku)
            return
        self._zamknij()


def main():
    root = tk.Tk()
    Aplikacja(root)
    root.mainloop()
