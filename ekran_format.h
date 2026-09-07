#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ui_skala.h"

constexpr uint64_t EKRAN_IPC = 0x454B52414E000001ULL;
constexpr const char* EKRAN_PROGRAM = "/programy/ekran.cebula/ekran.bur";
constexpr const char* EKRAN_CFG = "/ustawienia/ekran.cfg";
constexpr size_t EKRAN_CFG_MAX = 192;
constexpr unsigned EKRAN_MAX_TRYBOW = 16;
struct TrybEkranu { uint32_t szerokosc, wysokosc, bpp, pitch, odswiezanie, flagi; };
struct MozliwosciEkranu {
    uint32_t mozna_wyliczyc_tryby, mozna_zmienic_runtime, wymagany_restart, backend;
};
enum : uint32_t { EKRAN_VESA=1, EKRAN_GOP=2, EKRAN_BOCHS=3 };
enum : uint32_t { EKRAN_STAN=1, EKRAN_ZASTOSUJ, EKRAN_ZACHOWAJ, EKRAN_PRZYWROC, EKRAN_TEST };
enum : uint32_t { EKRAN_OK=0, EKRAN_NIEOBSLUGIWANY, EKRAN_PAMIEC, EKRAN_SPRZET,
    EKRAN_ZAJETY, EKRAN_ZAPIS, EKRAN_OCZEKUJE, EKRAN_UPRAWNIENIA };
struct EkranZadanie {
    uint32_t wersja, operacja;
    uint64_t odpowiedz;
    uint32_t szerokosc, wysokosc, bpp, skala;
    uint64_t transakcja;
    uint32_t rezerwa[2];
};
struct EkranOdpowiedz {
    uint32_t wersja, status;
    MozliwosciEkranu mozliwosci;
    TrybEkranu aktywny;
    uint32_t skala, liczba_trybow;
    uint64_t transakcja;
    uint32_t pozostalo_sekund, generacja;
    uint64_t bufory_bajty, alokacje, zmiany, przywrocenia;
    TrybEkranu tryby[EKRAN_MAX_TRYBOW];
    uint32_t asercje,bledy;
};
static_assert(sizeof(EkranOdpowiedz) <= 512, "Odpowiedz miesci sie w BWS IPC");
inline bool ekran_rozmiar(const TrybEkranu& t, size_t* bajty, size_t* strony=nullptr) {
    if (!bajty || t.bpp!=32 || !t.szerokosc || !t.wysokosc ||
        t.szerokosc>8192 || t.wysokosc>8192 || (t.pitch&3) ||
        uint64_t(t.szerokosc)*4>t.pitch) return false;
    const uint64_t n=uint64_t(t.pitch)*t.wysokosc;
    if (!n || n>64ULL*1024*1024 || n>SIZE_MAX-4095) return false;
    *bajty=size_t(n);
    if (strony) *strony=(size_t(n)+4095)/4096;
    return true;
}
inline bool ekran_ten_sam(const TrybEkranu&a,const TrybEkranu&b) {
    return a.szerokosc==b.szerokosc && a.wysokosc==b.wysokosc && a.bpp==b.bpp && a.pitch==b.pitch;
}
struct EkranKonfiguracja { uint32_t szerokosc, wysokosc, bpp, skala; };
bool ekran_parsuj(const char*, size_t, EkranKonfiguracja*);
size_t ekran_serializuj(const EkranKonfiguracja&, char*, size_t);

struct GeometriaEkranu { int32_t x,y,szer,wys; };
inline GeometriaEkranu ekran_dopasuj(GeometriaEkranu r,int32_t w,int32_t h,bool max){
    if (w < 1 || h < 1) return {0, 0, 0, 0};
    if (max) return {0, 0, w, h};
    if (r.szer > w) r.szer = w;
    if (r.wys > h) r.wys = h;
    if (r.szer < 1) r.szer = 1;
    if (r.wys < 1) r.wys = 1;
    if (r.x > w - r.szer) r.x = w - r.szer;
    if (r.y > h - r.wys) r.y = h - r.wys;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}
