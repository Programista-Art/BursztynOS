#pragma once
#include "psf.h"

// Shared verbatim by kernel boot, installer and real-PSF host tests.
inline bool katalog_istnieje(const char* sciezka) {
    return czy_katalog_istnieje(sciezka);
}
inline bool zapewnij_katalog_startowy(const char* sciezka, PsfStanStartu* stan = nullptr) {
    PsfStanStartu lokalny{};
    return psf_przygotuj_katalog_startowy(sciezka, stan ? stan : &lokalny);
}

static const char* const KATALOGI_SYSTEMOWE[] = {
        "/sloty",
        "/sloty/a",
        "/sloty/a/programy",
        "/sloty/a/system",
        "/sloty/a/system/jadro",
        "/sloty/a/system/biblioteki",
        "/sloty/a/system/konfig",
        "/sloty/a/system/motywy",
        "/sloty/a/system/boot",
        "/sloty/b",
        "/sloty/b/programy",
        "/sloty/b/system",
        "/sloty/b/system/jadro",
        "/sloty/b/system/biblioteki",
        "/sloty/b/system/konfig",
        "/sloty/b/system/motywy",
        "/sloty/b/system/boot",
        "/jadro",
        "/system",
        "/dane",
        "/dane/sponsorze",
        "/dane/zdjecia",
        "/system/konfig",
        "/system/motywy",
        "/programy",
        "/programy/notatnik.cebula",
        "/programy/eksplorator.cebula",
        "/programy/kalkulator.cebula",
        "/programy/przegladarka.cebula",
        "/programy/husarz-worker.cebula",
        "/programy/fpu-a.cebula",
        "/programy/fpu-b.cebula",
        "/programy/przegladarka-obrazow.cebula",
        "/programy/demo_ui.cebula",
        "/programy/kontrolki2.cebula",
        "/programy/kontrolki3.cebula",
        "/programy/kontrolki4.cebula",
        "/programy/aktualizacje.cebula",
        "/programy/test.cebula",
        "/programy/ekran.cebula",
        "/programy/tapety.cebula",
        "/programy/test_dzwieku.cebula",
        "/programy/diagnostyka.cebula",
        "/gry",
        "/gry/snake.cebula",
        "/uslugi",
        "/sterowniki",
        "/uzytkownicy",
        "/uzytkownicy/Pulpit",
        "/ustawienia",
        "/logi",
        "/piaskownica",
        "/tymczasowe",
        "/obrazy",
        "/aktualizacje",
        "/aktualizacje/przygotowanie",
        "/czcionki",
        "/czcionki/ttf",
        "/czcionki/otf",
        "/czcionki/ttc",
        "/czcionki/otc",
        "/programy/ustawienia.cebula",
        "/ikonki"
};

inline constexpr size_t LICZBA_KATALOGOW_SYSTEMOWYCH =
    sizeof(KATALOGI_SYSTEMOWE) / sizeof(KATALOGI_SYSTEMOWE[0]);

// Bounded formatting; only trusted base-tree paths are logged.
inline void psf_start_dopisz(char* dst, size_t cap, const char* src) {
    size_t p=0; while(p<cap && dst[p])++p;
    if(p==cap)return;
    while(src && *src && p+1<cap)dst[p++]=*src++;
    dst[p]=0;
}
inline void psf_start_liczba(char* dst,size_t cap,uint64_t n) {
    char b[21]{};size_t p=20;
    do{b[--p]=char('0'+n%10);n/=10;}while(n);
    psf_start_dopisz(dst,cap,b+p);
}
inline void psf_start_panic(char* dst,size_t cap,const char* path,const PsfStanStartu& s) {
    if(!cap)return;
    dst[0]=0;
    psf_start_dopisz(dst,cap,"Nie mozna przygotowac katalogu ");
    psf_start_dopisz(dst,cap,path);psf_start_dopisz(dst,cap," ");
    psf_start_dopisz(dst,cap,psf_blad_startu_nazwa(s.blad));
}

#ifndef BURSZTYNDIAGPSFBOOTSTRAP
#define BURSZTYNDIAGPSFBOOTSTRAP 0
#endif
void wypisz_log(const char*);
inline void psf_start_log(size_t krok,const char* path,bool live,const PsfStanStartu& s) {
#if BURSZTYNDIAGPSFBOOTSTRAP
    char b[768]{};
    auto text=[&](const char* t){psf_start_dopisz(b,sizeof(b),t);};
    auto number=[&](const char* k,uint64_t v){text(k);psf_start_liczba(b,sizeof(b),v);};
    auto stage=[&](const char* k,PsfEtapStartu v){text(k);text(psf_etap_startu_nazwa(v));};
    text("[PSF-BOOTSTRAP]");number(" step=",krok);text(" path=");text(path);
    text(" physical=");text(s.sciezkaFizyczna);
    const char* physical=s.sciezkaFizyczna[0]?s.sciezkaFizyczna:path;
    text(" parent=");const char* last=physical;for(const char* p=physical;*p;++p)if(*p=='/')last=p;
    if(last==physical)text("/");else {char parent[160]{};size_t n=size_t(last-physical);if(n>=sizeof(parent))n=sizeof(parent)-1;for(size_t i=0;i<n;++i)parent[i]=physical[i];text(parent);}
    text(live?" mode=Live":" mode=installed");
    number(" mounted=",s.gotowy);number(" root=",s.korzen);
    text(" backend=");text(psf_backend_startu_nazwa(s.backend));number(" persistent=",s.trwaly);
    stage(" lookup=",s.lookup);number(" exists=",s.istnial);stage(" create=",s.tworzenie);
    text(" reason=");text(psf_blad_startu_nazwa(s.blad));
    number(" inode=",s.wezel);number(" parent_inode=",s.rodzic);number(" entries=",s.wpisyRodzica);
    number(" free_inodes=",s.wolneWezly);number(" free_blocks=",s.wolneBloki);
    stage(" inode_alloc=",s.alokacjaWezla);stage(" dir_insert=",s.wpis);
    stage(" block_alloc=",s.alokacjaBloku);stage(" write=",s.zapis);stage(" flush=",s.flush);
    number(" write_block=",s.blokZapisu);number(" block_error=",uint64_t(-int64_t(s.bladBlokowy)));
    wypisz_log(b);
#else
    (void)krok;(void)path;(void)live;(void)s;
#endif
}

/*
 * Rozszerzalna lista katalogow startowych.
 *
 * Baza w KATALOGI_SYSTEMOWE jest kompilowana, bo bootstrap dziala PRZED
 * zamontowaniem PSF - nie ma skad wczytac konfiguracji. Aby jednak dodanie
 * katalogu nie wymagalo rekompilacji jadra, po bazie odczytywany jest
 * opcjonalny plik /system/konfig/katalogi.cfg (jedna sciezka na linie,
 * '#' komentarz). Wpisy z pliku sa walidowane tak samo jak wbudowane.
 *
 * Plik jest czytany przez warstwe PSF w kernel.cpp po pierwszym montazu
 * i przekazywany tu jako tablica linii.
 */
inline bool psf_sciezka_startowa_poprawna(const char* s) {
    if (!s || s[0] != '/') return false;
    size_t dlugosc = 0;
    while (s[dlugosc]) {
        /* Tylko bezpieczne znaki: brak '..', brak podwojnych ukosnikow. */
        if (s[dlugosc] == '\\') return false;
        if (s[dlugosc] == '/' && s[dlugosc + 1] == '/') return false;
        if (s[dlugosc] == '.' && s[dlugosc + 1] == '.') return false;
        if (dlugosc >= 200) return false;
        ++dlugosc;
    }
    return dlugosc >= 2;
}

/*
 * Przetwarza jedna linie konfiguracji i wywoluje akcje tworzenia katalogu.
 * Zwraca false tylko przy bledzie systemu plikow; linie puste i komentarze
 * sa pomijane bez bledu.
 */
inline bool psf_utworz_katalog_z_linii(const char* linia) {
    if (!linia) return true;

    char sciezka[208]{};
    size_t n = 0;
    while (*linia == ' ' || *linia == '\t') ++linia;
    if (*linia == '\0' || *linia == '#' || *linia == '\n' || *linia == '\r') {
        return true;
    }
    while (linia[n] && linia[n] != '\n' && linia[n] != '\r' &&
           linia[n] != ' ' && linia[n] != '\t') {
        if (n + 1 >= sizeof(sciezka)) return true;
        sciezka[n] = linia[n];
        ++n;
    }
    sciezka[n] = '\0';

    if (!psf_sciezka_startowa_poprawna(sciezka)) {
        return true; /* wpis niepoprawny jest ignorowany, nie blokuje bootu */
    }

    if (katalog_istnieje(sciezka)) return true;

    return zapewnij_katalog_startowy(sciezka);
}
