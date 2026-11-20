#pragma once
/*
 * Bursztyn OS - Bursztynowy Zasobnik po stronie jadra: autoryzacja i budowa odpowiedzi BWS 107-109 (czysta logika,
 * bez stanu i bez sprzetu - wspolna dla bws_nowoczesne.cpp i testow hosta).
 */
#include <stddef.h>
#include <stdint.h>
#include "zasobnik_api.h"
#include "powiadomienia.h"
#include "popup_autoryzacja.h"
#include "sterowniki/dzwiek/hda.h"

namespace ZasobnikJadro {

/* BWS 107, 108, 109 POBIERZ: tylko Menedzer Okien (prawa systemowe + sciezka pliku z loadera). */
inline bool menedzer_okien(const PopupAutoryzacja::Wywolujacy& w) {
    return PopupAutoryzacja::uprawnienia_systemowe(w) && PopupAutoryzacja::menedzer_okien(w);
}
/* BWS 109 ZGLOS: kazda aplikacja z oknami (PRAWO_GUI); nadawca i tak pochodzi z jadra. */
inline bool moze_zglaszac(const PopupAutoryzacja::Wywolujacy& w) { return (w.prawa & PRAWO_GUI) == PRAWO_GUI; }

/* Bufor Ring 3: niezerowy adres i rozmiar >= struktury (zakres/mapowanie sprawdza jadro przed kopia). */
inline bool bufor_poprawny(uint64_t adres, uint64_t rozmiar, uint64_t wymagany) { return adres != 0 && rozmiar >= wymagany; }

inline void dopisz(char* o, size_t n, size_t& w, const char* s) {
    for (size_t i = 0; s && s[i] && w + 1 < n; ++i) o[w++] = s[i];
    o[w] = '\0';
}

/* Nazwa urzadzenia audio z PCI vendor i celu trasy (HDA_CEL_*: 1 glosnik, 2 sluchawki, 3 linia, 4 HDMI). */
inline void nazwa_audio(char* o, size_t n, uint16_t vendor, uint8_t cel) {
    if (!o || !n) return;
    size_t w = 0;
    o[0] = '\0';
    dopisz(o, n, w, vendor == 0x8086 ? "Intel HDA" : vendor == 0x10DE ? "NVIDIA HDA" :
                    (vendor == 0x1002 || vendor == 0x1022) ? "AMD HDA" : "Kontroler HDA");
    const char* c = cel == 1 ? "głośnik" : cel == 2 ? "słuchawki" : cel == 3 ? "wyjście liniowe" : cel == 4 ? "HDMI" : nullptr;
    if (c) { dopisz(o, n, w, " · "); dopisz(o, n, w, c); }
}

inline void dzwiek(BwsZasDzwiekV1& o, const HdaGlosnoscInfo& h) {
    o = BwsZasDzwiekV1{};
    o.glosnosc = h.procent > 100 ? 100 : h.procent;
    o.generacja = h.generacja;
    if (!h.urzadzenie) return;
    o.flagi |= BWS_DZW_URZADZENIE;
    if (h.odtwarza) o.flagi |= BWS_DZW_ODTWARZA;
    if (h.backend == HDA_GLOSNOSC_WZMACNIACZ || h.backend == HDA_GLOSNOSC_PCM) {
        o.backend = h.backend == HDA_GLOSNOSC_WZMACNIACZ ? BWS_DZW_BACKEND_WZMACNIACZ : BWS_DZW_BACKEND_PCM;
        o.flagi |= BWS_DZW_REGULACJA;
        if (h.wyciszanie) o.flagi |= BWS_DZW_WYCISZANIE;
    }
    if (h.wyciszony) o.flagi |= BWS_DZW_WYCISZONY;
    o.pci_vendor = h.pci_vendor; o.pci_device = h.pci_device;
    o.kodek = h.kodek; o.wezel = h.wezel; o.kroki = h.kroki;
    nazwa_audio(o.urzadzenie, sizeof(o.urzadzenie), h.pci_vendor, h.cel);
}

/* Konfiguracja IPv4 (gotowa = DHCP ACK) do odpowiedzi; brama/DNS tylko niezerowe. */
inline void ipv4(BwsZasSiecV1& o, bool gotowa, const uint8_t ip[4], const uint8_t maska[4], const uint8_t brama[4],
                 const uint8_t dns[4]) {
    if (!gotowa) return;
    o.flagi |= BWS_ZSIEC_IPV4 | BWS_ZSIEC_DHCP;
    bool b = false, d = false;
    for (int i = 0; i < 4; ++i) {
        o.ip[i] = ip[i]; o.maska[i] = maska[i]; o.brama[i] = brama[i]; o.dns[i] = dns[i];
        b |= brama[i] != 0; d |= dns[i] != 0;
    }
    if (b) o.flagi |= BWS_ZSIEC_BRAMA;
    if (d) o.flagi |= BWS_ZSIEC_DNS;
}

/* Nosnik z wpisu BWS 105 (ten sam opis dysku): tylko wymienne; bezpieczne usuwanie nie istnieje w jadrze. */
inline bool nosnik(BwsZasNosnikV1& o, const BwsDyskInfoV1& d) {
    if (!(d.flagi & BWS_DYSK_WYMIENNY)) return false;
    o = BwsZasNosnikV1{};
    o.id = d.id; o.typ = d.typ; o.rozmiar_sektora = d.rozmiar_sektora; o.sektory = d.sektory;
    o.flagi = BWS_NOS_WYMIENNY;
    if (d.flagi & BWS_DYSK_TYLKO_ODCZYT) o.flagi |= BWS_NOS_TYLKO_ODCZYT;
    if (d.flagi & BWS_DYSK_SYSTEMOWY) o.flagi |= BWS_NOS_SYSTEMOWY;
    for (size_t i = 0; i < sizeof(o.model) && i < sizeof(d.model); ++i) o.model[i] = d.model[i];
    o.model[sizeof(o.model) - 1] = '\0';
    return true;
}

/* BWS 108 dla powiadomien (glosnosc obsluguje wywolujacy - sprzet). */
inline uint64_t polecenie_powiadomien(Powiadomienia::Kolejka& k, uint64_t pol, uint64_t arg) {
    switch (pol) {
        case BWS_ZAS_POL_PRZECZYTANE: return k.oznacz_przeczytane(arg);
        case BWS_ZAS_POL_USUN: return k.usun(arg);
        case BWS_ZAS_POL_WYCZYSC: k.wyczysc(); return BWS_ZAS_OK;
        case BWS_ZAS_POL_PRZECZYTANE_WSZYSTKIE: k.przeczytaj_wszystkie(); return BWS_ZAS_OK;
        default: return BWS_ZAS_ZLE_ARGUMENTY;
    }
}

inline uint64_t wynik_glosnosci(uint32_t h) {
    return h == HDA_GLOSNOSC_OK ? BWS_ZAS_OK : h == HDA_GLOSNOSC_ZAJETE ? BWS_ZAS_ZAJETE :
           h == HDA_GLOSNOSC_ZLE_ARGUMENTY ? BWS_ZAS_ZLE_ARGUMENTY : BWS_ZAS_NIEOBSLUGIWANE;
}

}  // namespace ZasobnikJadro
