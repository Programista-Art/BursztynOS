#pragma once
/*
 * Punkty kontrolne pracy odroczonej wyjscia audio (HDA / AC97) w DLUGICH, NIEWYWLASZCZALNYCH petlach jadra.
 *
 * Mikser systemowy (BWS 110) nie ma wlasnego watku: okresy DMA sa przygotowywane wylacznie w punktach kontrolnych
 * (wejscie/wyjscie syscallu, petla PID 0, pasma BWS 57, kroki kompozytora, oczekiwanie VirtIO-GPU). Ring 0 nie jest
 * wywlaszczany, wiec syscall, ktory sam w sobie trwa dziesiatki milisekund, wstrzymuje mikser - transport HDA
 * pomija wtedy okres (cisza 21,3 ms, licznik bezpieczna_cisza / utracone_okresy) niezaleznie od tego, czy Grajek
 * wyprodukowal dane. Potwierdzone w kodzie zrodla takich przerw podczas korzystania z innych aplikacji:
 *  - bws_alokuj_sterte (syscalls.cpp): mapowanie i zerowanie stron pojedynczo; Przegladarka Obrazow alokuje
 *    powierzchnie do 72 MiB (18 432 strony) jednym wywolaniem,
 *  - oczekiwanie na zakonczenie zadania VirtIO-BLK i AHCI (aktywne odpytywanie z "pause"): kazdy odczyt pliku
 *    z dysku (Eksplorator: katalogi, metadane, miniatury; Przegladarka: porcje 512 KiB; Grajek: strumien MP3).
 *
 * KONTRAKT BEZPIECZENSTWA (audyt 2026-11-26, szczegoly w docs/grajek.md):
 *  - Praca wykonywana w punkcie: hda_wykonaj_zaplanowana_prace_odroczona() -> hda_obsluz_okresowo() /
 *    ac97_obsluz_okresowo(). Oba workery: tylko pamiec jadra (kolejki PCM sa kopiowane z Ring 3 w BWS 110 PRZED
 *    blokada miksera), MMIO wlasnego kontrolera, logi wylacznie na port szeregowy; nie alokuja (DMA przydziela tylko
 *    inicjalizacja), nie wolaja sterty procesu, VFS/PSF, VirtIO-BLK, AHCI ani schedulera (brak przelaczenia
 *    kontekstu). Nie ma wiec sciezki powrotu do petli, w ktorej stoi punkt (brak rekursji, brak cyklu blokad).
 *  - Blokady audio sa nieblokujace (try-lock HDA / AC97): zajeta blokada nigdy nie czeka, praca zostaje
 *    opublikowana (pcm_pending_ns / g_oczekuje_ns) i wykona ja nastepny punkt.
 *  - Punkt NIE stoi wewnatrz zadnej sekcji krytycznej: w bws_alokuj_sterte miedzy stronami (PMM odblokowany,
 *    strony [0,i) w pelni zmapowane, granica sterty jeszcze nieprzesunieta - proces nie dziala, bo to jego syscall),
 *    w petlach dysku miedzy odczytami rejestrow / pierscienia (zadanie zlozone, urzadzenie pracuje samo).
 *  - Ponizej (obsluz): jawna walidacja kontekstu przed praca - IF=1, brak zagniezdzenia, wolna blokada HDA.
 *    Praca niewykonana z tych powodow jest ODROCZONA, nie porzucona: flaga oczekiwania zostaje ustawiona.
 *
 * HARMONOGRAM: sprawdzenie w KAZDEJ iteracji (koszt bez pracy: dwa odczyty atomowe), wykonanie tylko, gdy przerwanie
 * zegara opublikowalo prace (raz na tik, gdy mikser jest uzywany). Opoznienie jest wiec ograniczone czasem jednej
 * iteracji, a nie liczba iteracji - poprzedni licznik "co 16" byl lokalny dla kazdego zadania dysku i zerowal sie,
 * wiec seria krotkich zadan (<16 iteracji kazde) w jednym syscallu nie wykonywala punktu wcale.
 */
#include <stdint.h>

namespace PunktKontrolnyAudio {

enum class Wynik : uint8_t {
    BrakPracy = 0,            // nic nie czeka (szybka sciezka, bez licznikow)
    Wykonano,                 // praca audio wykonana
    OdroczonoPrzerwania,      // IF=0: worker i tak by odmowil (wymaga IF=1), zegar nie publikuje nowej pracy
    OdrzuconoZagniezdzenie,   // punkt wywolany z wnetrza pracy audio (regresja w przyszlosci) - bez rekursji
    OdroczonoBlokada,         // blokada miksera HDA trzymana przez przerwany kod - nie wchodzimy do workera
};

// Liczniki diagnostyczne (tylko gdy praca czekala - brak kosztu na szybkiej sciezce). Wypisywane w raporcie
// [HDA-PCM] testow automatycznych; utracone okresy i cisza bezpieczna (prawdziwe niedobory) sa w pcm_stats.
struct Statystyki {
    uint64_t zgloszenia = 0;               // punkt zastal opublikowana prace
    uint64_t wykonane = 0;
    uint64_t odroczone_przerwania = 0;
    uint64_t odrzucone_zagniezdzone = 0;
    uint64_t odroczone_blokada = 0;
    uint64_t przedluzenia_terminu_ns = 0;  // czas pracy audio oddany terminom urzadzen (VirtIO-BLK)
};

// Czysta logika punktu (testowana na hoscie). S: praca_oczekuje(), przerwania_wlaczone(), blokada_audio_zajeta(),
// wykonaj(). `w_toku` - flaga wykonywania (atomowa wymiana: bezpieczna takze przy wielu CPU).
template <class S>
Wynik obsluz(S& s, Statystyki& st, bool& w_toku) {
    if (!s.praca_oczekuje()) return Wynik::BrakPracy;
    __atomic_add_fetch(&st.zgloszenia, 1, __ATOMIC_RELAXED);
    if (!s.przerwania_wlaczone()) {
        __atomic_add_fetch(&st.odroczone_przerwania, 1, __ATOMIC_RELAXED);
        return Wynik::OdroczonoPrzerwania;
    }
    if (__atomic_exchange_n(&w_toku, true, __ATOMIC_ACQ_REL)) {
        __atomic_add_fetch(&st.odrzucone_zagniezdzone, 1, __ATOMIC_RELAXED);
        return Wynik::OdrzuconoZagniezdzenie;   // zewnetrzne wywolanie nadal trwa - flagi nie zdejmujemy
    }
    if (s.blokada_audio_zajeta()) {
        __atomic_store_n(&w_toku, false, __ATOMIC_RELEASE);
        __atomic_add_fetch(&st.odroczone_blokada, 1, __ATOMIC_RELAXED);
        return Wynik::OdroczonoBlokada;
    }
    s.wykonaj();
    __atomic_store_n(&w_toku, false, __ATOMIC_RELEASE);
    __atomic_add_fetch(&st.wykonane, 1, __ATOMIC_RELAXED);
    return Wynik::Wykonano;
}

// Termin oczekiwania na urzadzenie po wykonaniu pracy audio: czas [przed, po] nie byl czasem odpytywania, wiec
// przesuwamy termin o niego (urzadzenie dostaje pelny limit), ale lacznie najwyzej o `limit_ns` (calkowity czas
// oczekiwania <= 2x limit). Brak zegara (0), cofniety zegar lub termin "nieskonczony" - bez zmian.
inline uint64_t przedluz_termin(uint64_t termin, uint64_t przed, uint64_t po, uint64_t& przedluzono,
                                uint64_t limit_ns) {
    if (!przed || !po || po <= przed || termin == UINT64_MAX || przedluzono >= limit_ns) return termin;
    uint64_t d = po - przed;
    if (d > limit_ns - przedluzono) d = limit_ns - przedluzono;
    przedluzono += d;
    return termin > UINT64_MAX - d ? UINT64_MAX : termin + d;
}

inline Statystyki g_statystyki{};
inline bool g_w_toku = false;

inline Statystyki statystyki() {
    Statystyki s{};
    s.zgloszenia = __atomic_load_n(&g_statystyki.zgloszenia, __ATOMIC_RELAXED);
    s.wykonane = __atomic_load_n(&g_statystyki.wykonane, __ATOMIC_RELAXED);
    s.odroczone_przerwania = __atomic_load_n(&g_statystyki.odroczone_przerwania, __ATOMIC_RELAXED);
    s.odrzucone_zagniezdzone = __atomic_load_n(&g_statystyki.odrzucone_zagniezdzone, __ATOMIC_RELAXED);
    s.odroczone_blokada = __atomic_load_n(&g_statystyki.odroczone_blokada, __ATOMIC_RELAXED);
    s.przedluzenia_terminu_ns = __atomic_load_n(&g_statystyki.przedluzenia_terminu_ns, __ATOMIC_RELAXED);
    return s;
}

inline void zapisz_przedluzenie(uint64_t ns) {
    if (ns) __atomic_add_fetch(&g_statystyki.przedluzenia_terminu_ns, ns, __ATOMIC_RELAXED);
}

} // namespace PunktKontrolnyAudio

#if !defined(BURSZTYN_VIRTIO_HOST_TEST) && !defined(BURSZTYN_PUNKT_AUDIO_HOST_TEST)
#include "sterowniki/dzwiek/hda_praca_odroczona.h"

namespace PunktKontrolnyAudio {

struct SrodowiskoJadra {
    bool praca_oczekuje() const { return hda_pcm_praca_oczekuje() || ac97_praca_oczekuje(); }
    bool przerwania_wlaczone() const {
        uint64_t rflags = 0;
        asm volatile("pushfq; popq %0" : "=r"(rflags) :: "memory");
        return (rflags & (1ULL << 9)) != 0;
    }
    // Zajeta blokada HDA oznacza, ze punkt przerwal kod miksera HDA (dzis niemozliwe - patrz kontrakt). Odmawiamy
    // zamiast proby, ktora i tak by sie nie udala i zawyzala licznik worker_lock_busy w kazdej iteracji petli.
    bool blokada_audio_zajeta() const { return hda_blokada_aktywna(); }
    void wykonaj() const { hda_wykonaj_zaplanowana_prace_odroczona(); }
};

// Punkt w petli jadra (oczekiwanie na urzadzenie, strony sterty). Wolany w kazdej iteracji.
inline Wynik punkt() {
    SrodowiskoJadra s;
    return obsluz(s, g_statystyki, g_w_toku);
}

} // namespace PunktKontrolnyAudio
#else
namespace PunktKontrolnyAudio {
// Buildy hosta sterownikow dysku (BURSZTYN_VIRTIO_HOST_TEST) nie linkuja miksera. Test moze podstawic model
// srodowiska (np. obsluz() z udawanym workerem, ktory przesuwa zegar), zeby sprawdzic semantyke petli dysku.
inline Wynik (*g_host_punkt)() = nullptr;
inline Wynik punkt() { return g_host_punkt ? g_host_punkt() : Wynik::BrakPracy; }
} // namespace PunktKontrolnyAudio
#endif
