#include <map>
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <vector>

#define BURSZTYN_HOST_TEST 1

#include "../motyw.h"
#include "../stos_jadra.h"
#include "../motyw.cpp"

#include "../bursztyn_gui.h"
#include "../pulpit_geometria.h"

// Implementacje makiet funkcji GUI i BWS na potrzeby host test
typedef uint64_t (*TestBwsHookFn)(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
static TestBwsHookFn g_test_bws_hook = nullptr;

extern "C" {
    uint64_t bws_wywolaj(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
        if (g_test_bws_hook) return g_test_bws_hook(nr, a1, a2, a3, a4);
        return 0;
    }
    __attribute__((noreturn)) void bws_zakoncz_proces() { exit(0); }
}

bool wylistuj_katalog_uzytkownika(const char* sciezka, char* bufor, uint32_t maksymalna_dlugosc) {
    if (!sciezka || !bufor || maksymalna_dlugosc == 0) return false;
    return bws_wywolaj(6, reinterpret_cast<uint64_t>(sciezka),
                          reinterpret_cast<uint64_t>(bufor),
                          static_cast<uint64_t>(maksymalna_dlugosc), 0) == 1ULL;
}

bool pobierz_metadane_pliku(const char* sciezka, BwsMetadanePliku* metadane) {
    return sciezka && metadane &&
           bws_wywolaj(47, reinterpret_cast<uint64_t>(sciezka),
                           reinterpret_cast<uint64_t>(metadane), 0, 0) == 1ULL;
}

bool pobierz_rozmiar_pliku(const char* sciezka, uint32_t* rozmiar) {
    if (!sciezka || !rozmiar) return false;
    return bws_wywolaj(4, reinterpret_cast<uint64_t>(sciezka),
                          reinterpret_cast<uint64_t>(rozmiar), 0, 0) == 1ULL;
}

bool utworz_katalog_uzytkownika(const char* sciezka) {
    if (!sciezka) return false;
    return bws_wywolaj(46, reinterpret_cast<uint64_t>(sciezka), 0, 0, 0) == 1ULL;
}

static char g_mock_clipboard[1024] = {0};
static size_t g_mock_clipboard_len = 0;
static char g_mock_clipboard_mime[64]{};

bool clipboard_set(const char* mime, const void* dane, uint64_t rozmiar) {
    snprintf(g_mock_clipboard_mime,sizeof(g_mock_clipboard_mime),"%s",mime?mime:"");
    if (!dane || rozmiar == 0) {
        g_mock_clipboard_len = 0;
        g_mock_clipboard[0] = '\0';
        return true;
    }
    size_t copy_len = (rozmiar < sizeof(g_mock_clipboard)) ? static_cast<size_t>(rozmiar) : (sizeof(g_mock_clipboard) - 1);
    memcpy(g_mock_clipboard, dane, copy_len);
    g_mock_clipboard[copy_len] = '\0';
    g_mock_clipboard_len = copy_len;
    return true;
}

bool clipboard_size(const char* mime,uint64_t* size) {*size=strcmp(mime,g_mock_clipboard_mime)?0:g_mock_clipboard_len;return *size>0;}

bool clipboard_read(const char* mime, void* dane, uint64_t pojemnosc, uint64_t* odczytano) {
    if (!dane || pojemnosc == 0 || strcmp(mime,g_mock_clipboard_mime)) return false;
    size_t copy_len = (g_mock_clipboard_len < pojemnosc) ? g_mock_clipboard_len : (pojemnosc - 1);
    memcpy(dane, g_mock_clipboard, copy_len);
    static_cast<char*>(dane)[copy_len] = '\0';
    if (odczytano) *odczytano = copy_len;
    return true;
}

void* gui_malloc(unsigned long rozmiar) { return malloc(rozmiar); }
void gui_free(void* ptr) { free(ptr); }
void* gui_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
struct TestPaintRect {int x,y,w,h;uint32_t color=0;};
static int test_popup_blits=0,test_refreshes=0;
static bool test_capture_pixels=false;
static std::map<std::pair<int,int>,uint32_t> test_pixels;
static bool test_capture_paints=false;
static std::vector<TestPaintRect> test_paint_rects;
void gui_rysuj_prostokat(int x,int y,int w,int h,uint32_t color) {
    if(test_capture_paints)test_paint_rects.push_back({x,y,w,h,color});
    if(test_capture_pixels)for(int j=0;j<h;++j)for(int i=0;i<w;++i)test_pixels[{x+i,y+j}]=color;
}
bool gui_blit(int x,int y,int w,int h,const uint32_t* data) {
    if(test_capture_paints)test_paint_rects.push_back({x,y,w,h});
    if(test_capture_pixels)for(int j=0;j<h;++j)for(int i=0;i<w;++i)test_pixels[{x+i,y+j}]=data[j*w+i];
    return true;
}
static int test_region_replacements=0;
bool gui_zastap_region(int x,int y,int w,int h,const uint32_t* pixels) {
    ++test_region_replacements;
    if(test_capture_paints)test_paint_rects.push_back({x,y,w,h});
    if(test_capture_pixels)for(int j=0;j<h;++j)for(int i=0;i<w;++i)test_pixels[{x+i,y+j}]=pixels[j*w+i];
    return true;
}
static std::vector<uint32_t> test_popup_pixels;
static struct {int x,y,szerokosc,wysokosc;} test_popup_rect;
bool gui_blit_popup(int x,int y,int w,int h,const uint32_t* pixels) {
    ++test_popup_blits;test_popup_rect={x,y,w,h};test_popup_pixels.assign(pixels,pixels+w*h);return true;
}
static uint64_t test_activated_window=0;
static const char* test_running_title=nullptr;
uint32_t gui_pobierz_okna(GuiOknoInfo* out,uint32_t capacity) {
    if(!test_running_title || !capacity)return 0;
    out[0]={};out[0].window_id=42;strncpy(out[0].tytul,test_running_title,sizeof(out[0].tytul)-1);return 1;
}
bool gui_aktywuj_okno(uint64_t id) {test_activated_window=id;return true;}
void gui_wypisz_tekst_kolor_skala(int, int, uint32_t, int, const char*) {}
void motyw_odswiez_lokalny() {}
bool gui_ustaw_tytul_okna(const char*) { return true; }
uint32_t gui_skala_procent() { return 100; }
static void (*test_publish_observer)()=nullptr;
void gui_odswiez() {++test_refreshes;if(test_publish_observer)test_publish_observer();}
gui_akcja_belki gui_hit_test_belki(int, int, int, int, int) { return GUI_BELKA_BRAK; }
void gui_rysuj_standardowa_belke(int, int, int, const char*, bool) {}
bool gui_minimalizuj_okno() { return true; }
__attribute__((noreturn)) void gui_zakoncz_aplikacje() { exit(0); }
uint64_t gui_czas_monotoniczny_ns() { return 1000000000ULL; }
bool gui_pobierz_zdarzenie(bws_zdarzenie*) { return false; }
bool gui_czekaj_na_zdarzenie(bws_zdarzenie*) { return false; }
bool gui_czekaj_na_zdarzenie_z_timeoutem(bws_zdarzenie*, uint64_t) { return false; }
static bool test_mouse_capture=false;
void gui_ustaw_capture_myszy(bool value) {test_mouse_capture=value;}
bool gui_ustaw_geometrie_okna(int, int, int, int, int) { return true; }
bool gui_pobierz_monitor(uint32_t, GuiMonitorInfo*) { return false; }
void bws_przesun_warstwe(int, int) {}
static bool test_popup_registered=false;
bool gui_ustaw_popup_aplikacji(bool open, int, int, int, int) {test_popup_registered=open;return true;}
void gui_pobierz_rozdzielczosc(int* w, int* h) {
    if (w) *w = 1280;
    if (h) *h = 720;
}
static const BursztynMotyw* g_aktywny_test_motyw = &g_domyslny_motyw_fallback;
const BursztynMotyw* gui_pobierz_motyw() {
    return g_aktywny_test_motyw;
}

uint32_t motyw_kolor(uint32_t rola) {
    const BursztynMotyw* m = gui_pobierz_motyw();
    switch (rola) {
        case MOTYW_KOLOR_PULPIT: return m->pulpit;
        case MOTYW_KOLOR_OKNOTLO: return m->oknotlo;
        case MOTYW_KOLOR_PANEL: return m->panel;
        case MOTYW_KOLOR_PODNIESIONY: return m->podniesiony;
        case MOTYW_KOLOR_RAMKA: return m->ramka;
        case MOTYW_KOLOR_TEKST: return m->tekst;
        case MOTYW_KOLOR_TEKST_DRUGI: return m->tekstdrugorzedny;
        case MOTYW_KOLOR_WYLACZONY: return m->wylaczony;
        case MOTYW_KOLOR_AKCENT: return m->akcent;
        case MOTYW_KOLOR_AKCENT_NAJECHANY: return m->akcentnajechany;
        case MOTYW_KOLOR_AKCENT_AKTYWNY: return m->akcentaktywny;
        case MOTYW_KOLOR_BLAD: return m->blad;
        case MOTYW_KOLOR_SUKCES: return m->sukces;
        case MOTYW_KOLOR_OSTRZEZENIE: return m->ostrzezenie;
        case MOTYW_KOLOR_INFORMACJA: return m->informacja;
        case MOTYW_KOLOR_POLETEKSTOWE: return m->poletekstowe;
        case MOTYW_KOLOR_ZAZNACZENIE: return m->zaznaczenie;
        case MOTYW_KOLOR_PRZYCISK: return m->przycisk;
        case MOTYW_KOLOR_PRZYCISK_NAJECHANY: return m->przycisknajechany;
        case MOTYW_KOLOR_TEKST_AKCENTU: return m->tekstakcentu;
        case MOTYW_KOLOR_PULPIT_ZAZNACZENIE: return motyw_pulpit_zaznaczenie(*m);
        case MOTYW_KOLOR_PULPIT_RAMKA: return motyw_pulpit_ramka(*m);
        case MOTYW_KOLOR_PULPIT_TEKST: return m->tekst;
        case MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY: return motyw_pulpit_tekst_zaznaczony(*m);
        default: return m->tekst;
    }
}

uint16_t motyw_promien(uint32_t element) {
    const BursztynMotyw* m = gui_pobierz_motyw();
    switch (element) {
        case MOTYW_ROG_OKNO: return m->rogokna;
        case MOTYW_ROG_PANEL: return m->rogpanelu;
        case MOTYW_ROG_PRZYCISK: return m->rogprzycisku;
        case MOTYW_ROG_POLE: return m->rogpola;
        case MOTYW_ROG_MENU: return m->rogmenu;
        case MOTYW_ROG_PODPOWIEDZ: return m->rogpodpowiedzi;
        default: return 0;
    }
}

uint8_t motyw_grubosc_ramki() {
    return gui_pobierz_motyw()->gruboscramki;
}

uint8_t motyw_cien_okna() {
    return gui_pobierz_motyw()->cienokna;
}

void gui_pobierz_widoczna_geometrie(int* x, int* y, int* w, int* h) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = 1280;
    if (h) *h = 720;
}
void gui_log_geom_trace(const char*, int, int, int, int, int, int, int, int, bool, bool, int, int, int, int) {}
bool gui_legacy_czy_wlaczony() { return false; }
void gui_legacy_wlacz() {}

#include "../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

void test_ustaw_aktywny_motyw(const BursztynMotyw* m) {
    g_aktywny_test_motyw = m ? m : &g_domyslny_motyw_fallback;
    Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
}

