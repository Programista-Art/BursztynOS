#pragma once
/*
 * Menedzer zadan - tresc karty Wydajnosc (bez GUI, bez alokacji).
 *
 * Kategorie sa budowane dynamicznie z migawek: Procesor, Pamiec, kazdy wykryty dysk (BWS 105), kazdy interfejs
 * sieciowy i aktywny backend grafiki. Nie ma wpisow "Dysk 0"/"Ethernet" bez urzadzenia. Wartosci niedostepne
 * sa opisane slowami ("Niedostępne", "Brak licznika sprzętowego"), nigdy jako 0. Obciazenie GPU nie jest
 * wyliczane z zadnego licznika programowego.
 */
#include "model.h"
#include "urzadzenia.h"

namespace MenedzerZadan {

enum class Widok : uint8_t { Procesor = 0, Pamiec = 1, Dysk = 2, Siec = 3, Grafika = 4 };
inline uint64_t znacznik_widoku(Widok w, uint32_t id = 0) { return (uint64_t(static_cast<uint8_t>(w)) << 32) | id; }
inline Widok rodzaj_widoku(uint64_t z) { const uint32_t r = uint32_t(z >> 32); return r <= 4 ? static_cast<Widok>(r) : Widok::Procesor; }
inline uint32_t id_widoku(uint64_t z) { return static_cast<uint32_t>(z); }

inline constexpr size_t MAKS_KATEGORII = 2 + BWS_URZ_MAKS_DYSKOW + BWS_URZ_MAKS_SIECI + BWS_URZ_MAKS_GRAFIKI;

struct Kategorie {
    uint64_t z[MAKS_KATEGORII]{};
    size_t n = 0;
    bool operator==(const Kategorie& o) const {
        if (n != o.n) return false;
        for (size_t i = 0; i < n; ++i) if (z[i] != o.z[i]) return false;
        return true;
    }
    int indeks(uint64_t x) const { for (size_t i = 0; i < n; ++i) if (z[i] == x) return int(i); return -1; }
};

inline Kategorie kategorie(const ModelUrzadzen& u) {
    Kategorie k;
    k.z[k.n++] = znacznik_widoku(Widok::Procesor);
    k.z[k.n++] = znacznik_widoku(Widok::Pamiec);
    for (uint32_t i = 0; i < u.liczba_dyskow(); ++i) k.z[k.n++] = znacznik_widoku(Widok::Dysk, u.dysk(i)->info.id);
    for (uint32_t i = 0; i < u.liczba_sieci(); ++i) k.z[k.n++] = znacznik_widoku(Widok::Siec, u.siec(i)->info.id);
    if (u.grafika()) k.z[k.n++] = znacznik_widoku(Widok::Grafika);
    return k;
}

inline const char* krotki_backend(uint32_t b) {
    switch (b) { case 1: return "VESA"; case 2: return "UEFI GOP"; case 3: return "Bochs VBE"; case 4: return "VirtIO-GPU"; default: return "?"; }
}

/* Nazwa kategorii (pierwsza linia na liscie, naglowek szczegolow). */
inline void nazwa_kategorii(uint64_t z, const ModelUrzadzen& u, char* out, size_t cap) {
    Tekst t(out, cap);
    switch (rodzaj_widoku(z)) {
        case Widok::Procesor: t.dodaj("Procesor"); return;
        case Widok::Pamiec: t.dodaj("Pamięć"); return;
        case Widok::Dysk: {
            const Dysk* d = u.znajdz_dysk(id_widoku(z));
            t.dodaj("Dysk "); t.liczba(id_widoku(z));
            if (d) { t.dodaj(" ("); t.dodaj(nazwa_typu_dysku(d->info.typ)); t.dodaj(")"); }
            return;
        }
        case Widok::Siec: {
            const Siec* s = u.znajdz_siec(id_widoku(z));
            t.dodaj("Ethernet");
            if (s && s->info.nazwa[0]) { t.dodaj(" "); t.dodaj(s->info.nazwa); }
            return;
        }
        case Widok::Grafika: {
            const Grafika* g = u.grafika();
            t.dodaj("Grafika");
            if (g) { t.dodaj(" ("); t.dodaj(krotki_backend(g->info.backend)); t.dodaj(")"); }
            return;
        }
    }
}

inline void tempo_lub_brak(bool ok, uint64_t bps, char* out, size_t cap) {
    if (!ok) { Tekst t(out, cap); t.dodaj("Niedostępne"); return; }
    format_tempa(bps, out, cap);
}

/* Druga linia na liscie: biezaca wartosc. */
inline void wartosc_kategorii(uint64_t z, const System& s, const ModelUrzadzen& u, char* out, size_t cap) {
    Tekst t(out, cap);
    char a[32], b[32];
    switch (rodzaj_widoku(z)) {
        case Widok::Procesor: format_procentu(s.cpu_promile, a, sizeof(a)); t.dodaj(a); return;
        case Widok::Pamiec:
            if (!s.ram_dostepna) { t.dodaj("Niedostępne"); return; }
            format_pamieci(s.ram_uzywana, a, sizeof(a)); format_procentu(s.ram_promile, b, sizeof(b));
            t.dodaj(a); t.dodaj(" ("); t.dodaj(b); t.dodaj(")"); return;
        case Widok::Dysk: {
            const Dysk* d = u.znajdz_dysk(id_widoku(z));
            if (!d || !d->tempo) { t.dodaj(d && !(d->info.flagi & BWS_DYSK_ONLINE) ? "Offline" : "Niedostępne"); return; }
            format_tempa(d->odczyt_bps, a, sizeof(a)); format_tempa(d->zapis_bps, b, sizeof(b));
            t.dodaj("Odcz. "); t.dodaj(a); t.dodaj(", zap. "); t.dodaj(b); return;
        }
        case Widok::Siec: {
            const Siec* n = u.znajdz_siec(id_widoku(z));
            if (!n || !n->tempo) { t.dodaj("Niedostępne"); return; }
            format_tempa(n->tx_bps, a, sizeof(a)); format_tempa(n->rx_bps, b, sizeof(b));
            t.dodaj("Wys. "); t.dodaj(a); t.dodaj(", odb. "); t.dodaj(b); return;
        }
        case Widok::Grafika: {
            const Grafika* g = u.grafika();
            if (!g || !g->tempo) { t.dodaj("Kompozytor: niedostępne"); return; }
            t.dziesietna(g->klatki_10ps); t.dodaj(" klatek/s"); return;
        }
    }
}

/* ------------------------------------------------------------ szczegoly (tabela Wlasciwosc / Wartosc) */
struct Szczegoly {
    static constexpr size_t MAKS = 18;
    char klucz[MAKS][48]{};
    char wartosc[MAKS][104]{};
    size_t n = 0;
    void dodaj(const char* k, const char* v) {
        if (n >= MAKS) return;
        { Tekst t(klucz[n], sizeof(klucz[n])); t.dodaj(k); }
        { Tekst t(wartosc[n], sizeof(wartosc[n])); t.dodaj(v); }
        ++n;
    }
    bool operator==(const Szczegoly& o) const {
        if (n != o.n) return false;
        for (size_t i = 0; i < n; ++i) if (!rowne(klucz[i], o.klucz[i]) || !rowne(wartosc[i], o.wartosc[i])) return false;
        return true;
    }
};

inline void szczegoly_procesora(const System& s, uint64_t czas_systemu_ns, Szczegoly& o) {
    char v[104];
    format_procentu(s.cpu_promile, v, sizeof(v)); o.dodaj("Użycie", v);
    format_procentu(s.jadro_promile, v, sizeof(v)); o.dodaj("Praca jądra (PID 0)", v);
    format_procentu(s.bezczynnosc_promile, v, sizeof(v)); o.dodaj("Bezczynność (HLT)", v);
    { Tekst t(v, sizeof(v)); t.liczba(s.procesow); if (s.obcieta) t.dodaj(" (lista obcięta)"); } o.dodaj("Procesy", v);
    { Tekst t(v, sizeof(v));
      if (s.hpet) t.dodaj("HPET: czas między przełączeniami kontekstu");
      else if (s.ticki) { t.dodaj("Tick schedulera co "); t.liczba(s.okres_ticku_ns / 1000000); t.dodaj(" ms"); }
      else t.dodaj("Niedostępne"); }
    o.dodaj("Źródło pomiaru", v);
    if (czas_systemu_ns) { format_czasu(czas_systemu_ns, v, sizeof(v)); o.dodaj("Czas działania systemu", v); }
}

inline void szczegoly_pamieci(const System& s, Szczegoly& o) {
    char v[104], a[32];
    if (!s.ram_dostepna) { o.dodaj("Liczniki pamięci", "Niedostępne"); return; }
    { Tekst t(v, sizeof(v)); format_pamieci(s.ram_uzywana, a, sizeof(a)); t.dodaj(a);
      format_procentu(s.ram_promile, a, sizeof(a)); t.dodaj(" ("); t.dodaj(a); t.dodaj(")"); }
    o.dodaj("W użyciu", v);
    format_pamieci(s.ram_wolna, v, sizeof(v)); o.dodaj("Dostępna", v);
    format_pamieci(s.ram_zarzadzana, v, sizeof(v)); o.dodaj("Zarządzana przez system", v);
    format_pamieci(s.ram_procesow, v, sizeof(v)); o.dodaj("Przypisana procesom", v);
    format_pamieci(s.ram_reszta, v, sizeof(v)); o.dodaj("Jądro, bufory, pamięć współdzielona", v);
    if (s.ram_firmware) { format_pamieci(s.ram_firmware, v, sizeof(v)); o.dodaj("Zgłoszona przez firmware", v); }
}

inline void suma_z_liczba(uint64_t bajty, uint64_t ile, const char* jednostka, char* v, size_t cap) {
    char a[32]; Tekst t(v, cap);
    format_ilosci(bajty, a, sizeof(a)); t.dodaj(a); t.dodaj(" ("); t.liczba(ile); t.dodaj(" "); t.dodaj(jednostka); t.dodaj(")");
}

inline void szczegoly_dysku(const Dysk& d, bool psf_w_ram, Szczegoly& o) {
    char v[104], a[32], b[32];
    const BwsDyskInfoV1& i = d.info;
    o.dodaj("Wykres", "pomarańczowa linia: odczyt, niebieska: zapis");
    tempo_lub_brak(d.tempo, d.odczyt_bps, v, sizeof(v)); o.dodaj("Odczyt", v);
    tempo_lub_brak(d.tempo, d.zapis_bps, v, sizeof(v)); o.dodaj("Zapis", v);
    { Tekst t(v, sizeof(v));
      if (!d.tempo) t.dodaj("Niedostępne");
      else { t.liczba(d.odczyty_ps); t.dodaj(" odczytu, "); t.liczba(d.zapisy_ps); t.dodaj(" zapisu"); } }
    o.dodaj("Operacje na sekundę", v);
    { Tekst t(v, sizeof(v));
      if (!d.tempo) t.dodaj("Niedostępne");
      else if (!d.czas) t.dodaj("Niedostępne (brak pomiaru czasu)");
      else if (!d.polecenia_przedzialu) t.dodaj("Brak operacji w ostatniej sekundzie");
      else { t.dziesietna((d.sredni_czas_us + 50) / 100); t.dodaj(" ms"); } }
    o.dodaj("Średni czas operacji", v);
    { Tekst t(v, sizeof(v));
      if (d.czas_io_promile < 0) t.dodaj("Niedostępne");
      else { t.liczba(static_cast<uint64_t>(d.czas_io_promile)); t.dodaj(" ms na sekundę (oczekiwanie na urządzenie)"); } }
    o.dodaj("Czas I/O", v);
    suma_z_liczba(i.bajty_odczytane, i.operacje_odczytu, "operacji", v, sizeof(v)); o.dodaj("Łącznie odczytano", v);
    suma_z_liczba(i.bajty_zapisane, i.operacje_zapisu, "operacji", v, sizeof(v)); o.dodaj("Łącznie zapisano", v);
    { Tekst t(v, sizeof(v)); t.liczba(i.bledy_odczytu); t.dodaj(" / "); t.liczba(i.bledy_zapisu); t.dodaj(" / ");
      t.liczba(i.bledy_oprozniania); t.dodaj(" (FLUSH udane: "); t.liczba(i.oprozniania); t.dodaj(")"); }
    o.dodaj("Błędy odczytu / zapisu / FLUSH", v);
    o.dodaj("Typ i sterownik", nazwa_typu_dysku(i.typ));
    o.dodaj("Model", (i.flagi & BWS_DYSK_MODEL_URZADZENIA) && i.model[0] ? i.model : "Niedostępny (sterownik nie podaje modelu)");
    { Tekst t(v, sizeof(v));
      const uint64_t sektor = i.rozmiar_sektora ? i.rozmiar_sektora : 512;
      if (i.sektory && i.sektory <= UINT64_MAX / sektor) { format_pamieci(i.sektory * sektor, a, sizeof(a)); t.dodaj(a); }
      else t.dodaj("Niedostępna");
      t.dodaj(" ("); t.liczba(sektor); t.dodaj(" B/sektor)"); }
    o.dodaj("Pojemność", v);
    { Tekst t(v, sizeof(v)); t.dodaj((i.flagi & BWS_DYSK_ONLINE) ? "Online" : "Offline (sterownik niegotowy)");
      if (i.flagi & BWS_DYSK_TYLKO_ODCZYT) t.dodaj(", tylko do odczytu");
      if (i.flagi & BWS_DYSK_WYMIENNY) t.dodaj(", wymienny"); }
    o.dodaj("Stan", v);
    o.dodaj("System plików", (i.flagi & BWS_DYSK_SYSTEMOWY) ? "Zamontowany na tym dysku (BSP2)"
                             : psf_w_ram ? "Działa w RAM (Live): operacje na plikach nie są ruchem dysku"
                                         : "Na innym nośniku");
    (void)b;
}

inline void format_mac(const uint8_t* m, char* out, size_t cap) {
    static const char h[] = "0123456789ABCDEF";
    Tekst t(out, cap);
    for (int i = 0; i < 6; ++i) { if (i) t.znak(':'); t.znak(h[m[i] >> 4]); t.znak(h[m[i] & 15]); }
}

inline void szczegoly_sieci(const Siec& s, Szczegoly& o) {
    char v[104];
    const BwsSiecInfoV1& i = s.info;
    o.dodaj("Wykres", "pomarańczowa linia: odbiór, niebieska: wysyłanie");
    tempo_lub_brak(s.tempo, s.rx_bps, v, sizeof(v)); o.dodaj("Odbiór", v);
    tempo_lub_brak(s.tempo, s.tx_bps, v, sizeof(v)); o.dodaj("Wysyłanie", v);
    { Tekst t(v, sizeof(v));
      if (!s.tempo) t.dodaj("Niedostępne");
      else { t.liczba(s.rx_pps); t.dodaj(" odebranych, "); t.liczba(s.tx_pps); t.dodaj(" wysłanych"); } }
    o.dodaj("Pakiety na sekundę", v);
    suma_z_liczba(i.rx_bajty, i.rx_pakiety, "pakietów", v, sizeof(v)); o.dodaj("Łącznie odebrano", v);
    suma_z_liczba(i.tx_bajty, i.tx_pakiety, "pakietów", v, sizeof(v)); o.dodaj("Łącznie wysłano", v);
    { Tekst t(v, sizeof(v)); t.liczba(i.rx_bledy); t.dodaj(" / "); t.liczba(i.tx_bledy); } o.dodaj("Błędy RX / TX", v);
    { Tekst t(v, sizeof(v)); t.liczba(i.rx_odrzucone); t.dodaj(" / "); t.liczba(i.tx_odrzucone); } o.dodaj("Odrzucone RX / TX", v);
    o.dodaj("Sterownik", nazwa_sterownika_sieci(i.typ));
    if (i.flagi & BWS_SIEC_MAC) { format_mac(i.mac, v, sizeof(v)); o.dodaj("Adres MAC", v); }
    else o.dodaj("Adres MAC", "Niedostępny");
    o.dodaj("Łącze", !(i.flagi & BWS_SIEC_LACZE_ZNANE) ? "Stan nieznany (sterownik nie raportuje)"
                      : (i.flagi & BWS_SIEC_LACZE) ? "Aktywne" : "Nieaktywne (brak łącza)");
    if (i.flagi & BWS_SIEC_PREDKOSC) { Tekst t(v, sizeof(v)); t.liczba(i.predkosc_mbps); t.dodaj(" Mb/s"); o.dodaj("Prędkość łącza", v); }
    else o.dodaj("Prędkość łącza", "Niedostępna (sterownik nie raportuje)");
    { Tekst t(v, sizeof(v)); t.liczba(i.mtu); t.dodaj(" B"); } o.dodaj("MTU", v);
    o.dodaj("Rola", !(i.flagi & BWS_SIEC_AKTYWNA) ? "Nieaktywny" : (i.flagi & BWS_SIEC_GLOWNA) ? "Interfejs główny stosu IP"
                                                                                                : "Dodatkowy (stos IP go nie używa)");
    o.dodaj("Zakres liczników", "Ramki Ethernet z nagłówkiem, bez FCS");
}

inline const char* format_virtio(uint32_t f) {
    switch (f) {
        case 1: return "B8G8R8A8"; case 2: return "B8G8R8X8"; case 3: return "A8R8G8B8"; case 4: return "X8R8G8B8";
        case 67: return "R8G8B8A8"; case 68: return "X8B8G8R8"; case 121: return "A8B8G8R8"; case 134: return "R8G8B8X8";
        default: return nullptr;
    }
}

inline void udzial_czasu(int promile, char* v, size_t cap) {
    Tekst t(v, cap);
    if (promile < 0) { t.dodaj("Niedostępne"); return; }
    t.dziesietna(static_cast<uint64_t>(promile)); t.dodaj(" % czasu");
}

inline void szczegoly_grafiki(const Grafika& g, Szczegoly& o) {
    char v[104], a[32];
    const BwsGrafikaInfoV1& i = g.info;
    const bool virtio = (i.flagi & BWS_GRAF_LICZNIKI_VIRTIO) != 0;
    o.dodaj("Obciążenie GPU", "Brak licznika sprzętowego");
    o.dodaj("Backend", nazwa_backendu(i.backend));
    if (i.flagi & BWS_GRAF_PCI) {
        static const char h[] = "0123456789ABCDEF";
        Tekst t(v, sizeof(v));
        for (int s = 12; s >= 0; s -= 4) t.znak(h[(i.pci_vendor >> s) & 15]);
        t.znak(':');
        for (int s = 12; s >= 0; s -= 4) t.znak(h[(i.pci_device >> s) & 15]);
        o.dodaj("Urządzenie PCI", v);
    } else o.dodaj("Urządzenie PCI", "Niedostępne (framebuffer firmware)");
    { Tekst t(v, sizeof(v)); t.liczba(i.szerokosc); t.dodaj(" × "); t.liczba(i.wysokosc); t.dodaj(", "); t.liczba(i.bpp); t.dodaj(" bpp");
      if (i.pitch) { t.dodaj(", wiersz "); t.liczba(i.pitch); t.dodaj(" B"); } }
    o.dodaj("Tryb ekranu", v);
    if (virtio && format_virtio(i.format_virtio)) o.dodaj("Format pikseli", format_virtio(i.format_virtio));
    o.dodaj("Kursor", (i.flagi & BWS_GRAF_KURSOR_SPRZETOWY) ? "Sprzętowy (płaszczyzna kursora urządzenia)" : "Programowy (rysowany przez kompozytor)");
    { Tekst t(v, sizeof(v));
      if (!g.tempo) t.dodaj("Niedostępne");
      else { t.dziesietna(g.klatki_10ps); t.dodaj(" na sekundę (pełne łącznie: "); t.liczba(i.pelne_klatki); t.dodaj(")"); } }
    o.dodaj("Klatki kompozytora", v);
    udzial_czasu(g.tempo ? g.skladanie_promile : NIEDOSTEPNE, v, sizeof(v)); o.dodaj("Składanie programowe (CPU)", v);
    udzial_czasu(g.tempo ? g.prezentacja_promile : NIEDOSTEPNE, v, sizeof(v)); o.dodaj("Przekazanie klatki do urządzenia", v);
    { Tekst t(v, sizeof(v));
      if (!g.tempo) t.dodaj("Niedostępne");
      else { format_tempa(g.transfer_bps, a, sizeof(a)); t.dodaj(a); t.dodaj(virtio ? " (TRANSFER_TO_HOST_2D)" : " (kopie do framebuffera)"); } }
    o.dodaj("Transfer do urządzenia", v);
    if (virtio) {
        { Tekst t(v, sizeof(v)); if (!g.tempo) t.dodaj("Niedostępne"); else { t.liczba(g.polecenia_ps); t.dodaj(" na sekundę"); } }
        o.dodaj("Polecenia VirtIO-GPU", v);
        { Tekst t(v, sizeof(v)); t.liczba(i.bledy_gpu); t.dodaj(" / "); t.liczba(i.spoznione_gpu);
          if (i.flagi & BWS_GRAF_BLAD_TRWALY) t.dodaj(" - trwała awaria sterownika"); }
        o.dodaj("Błędy / spóźnione zakończenia", v);
    }
    format_pamieci(i.pamiec_warstw_bajtow, v, sizeof(v)); o.dodaj("Pamięć warstw okien", v);
    format_pamieci(i.pamiec_buforow_bajtow, v, sizeof(v)); o.dodaj("Bufory ekranu (jądro)", v);
}

}  // namespace MenedzerZadan
