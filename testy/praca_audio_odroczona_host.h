#pragma once
/*
 * TYLKO testy hosta kompozytora / grafiki: wierny model kontraktu pracy odroczonej audio
 * (sterowniki/dzwiek/hda_praca_odroczona.h), zamiast linkowania sterownikow HDA i AC97 (MMIO, DMA, PCI).
 *
 * Kompozytor (skladacz_obrazu.cpp) i kopia cienia (grafika.cpp) wywoluja inline'owy hak
 * hda_wykonaj_zaplanowana_prace_odroczona(), ktory odwoluje sie do czterech funkcji zdefiniowanych w hda.cpp
 * i ac97.cpp. Model zachowuje ich semantyke:
 *  - *_praca_oczekuje(): odczyt atomowy znacznika czasu publikacji (pcm_pending_ns / g_oczekuje_ns), != 0 = praca,
 *  - publikacja (przerwanie zegara, hda_pcm_watchdog_irq / ac97_zaplanuj_prace): cmpxchg 0 -> teraz,
 *  - *_obsluz_okresowo(): wymaga IF=1 i wolnej, NIEBLOKUJACEJ blokady miksera; przy zajetej blokadzie publikacja
 *    zostaje (blokady_pracy++), w przeciwnym razie worker zdejmuje ja atomowo i wykonuje porcje pracy.
 * Liczniki pozwalaja testom stwierdzic, ze produkcyjna sciezka faktycznie obsluguje oczekujaca prace audio
 * (regresja: kompozytor przestaje wolac punkt -> mikser glodzony podczas skladania klatek).
 */
#if !defined(BURSZTYN_COMPOSITOR_HOST_TEST) && !defined(BURSZTYN_GRAPHICS_HOST_TEST)
#error "praca_audio_odroczona_host.h jest wylacznie dla testow hosta (nie moze trafic do jadra)"
#endif
#include <stdint.h>

struct PracaAudioHost {
    struct Urzadzenie {
        uint64_t oczekuje_ns = 0;
        bool blokada = false;          // blokada miksera trzymana (try-lock sie nie powiedzie)
        uint64_t sprawdzenia = 0;      // wywolania *_praca_oczekuje()
        uint64_t obsluzone = 0;        // porcje pracy wykonane przez workera
        uint64_t blokady_pracy = 0;    // worker trafil na zajeta blokade
        void opublikuj(uint64_t teraz) {
            uint64_t pusty = 0;
            (void)__atomic_compare_exchange_n(&oczekuje_ns, &pusty, teraz ? teraz : 1, false,
                                              __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        }
        bool oczekuje() {
            ++sprawdzenia;
            return __atomic_load_n(&oczekuje_ns, __ATOMIC_ACQUIRE) != 0;
        }
        void obsluz() {
            if (!przerwania) return;                       // sterownik: tylko z IF=1
            if (blokada) { if (__atomic_load_n(&oczekuje_ns, __ATOMIC_ACQUIRE)) ++blokady_pracy; return; }
            if (__atomic_exchange_n(&oczekuje_ns, 0, __ATOMIC_ACQ_REL)) ++obsluzone;
        }
        bool przerwania = true;
    };
    Urzadzenie hda, ac97;
    void reset() { hda = {}; ac97 = {}; }
};

inline PracaAudioHost g_praca_audio_host{};

bool hda_pcm_praca_oczekuje() { return g_praca_audio_host.hda.oczekuje(); }
void hda_obsluz_okresowo() { g_praca_audio_host.hda.obsluz(); }
bool ac97_praca_oczekuje() { return g_praca_audio_host.ac97.oczekuje(); }
void ac97_obsluz_okresowo() { g_praca_audio_host.ac97.obsluz(); }
