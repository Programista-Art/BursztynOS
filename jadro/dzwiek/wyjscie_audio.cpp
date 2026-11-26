/*
 * Bursztyn OS - warstwa wyjscia audio: jedno urzadzenie PCM, wybrane raz przy starcie.
 * Brak automatycznego przelaczania w trakcie pracy; brak urzadzenia = PCM_DEVICE (bez cichego sukcesu).
 */
#include "wyjscie_audio.h"
#include "pcm_aplikacji.h"
#include "sterowniki/dzwiek/hda_pcm_aplikacji.h"
#include "sterowniki/dzwiek/ac97.h"

namespace {
WyjscieAudio::Wybor g_wybor;
inline WyjscieAudio::Urzadzenie aktywne() { return g_wybor.aktywne(); }
}

bool wyjscie_audio_ustal(const WyjscieAudio::Dostepnosc& d) { return g_wybor.ustaw(d); }
WyjscieAudio::Urzadzenie wyjscie_audio_aktywne() { return aktywne(); }
WyjscieAudio::Dostepnosc wyjscie_audio_dostepnosc() { return g_wybor.dostepnosc(); }

void wyjscie_audio_glosnosc_info(HdaGlosnoscInfo* out) {
    if (aktywne() == WyjscieAudio::Urzadzenie::Ac97) { ac97_glosnosc_info(out); return; }
    hda_glosnosc_info(out);   // HDA albo brak urzadzenia: HDA zglasza urzadzenie=false
}
uint32_t wyjscie_audio_glosnosc_ustaw(uint32_t procent) {
    if (aktywne() == WyjscieAudio::Urzadzenie::Ac97) return ac97_glosnosc_ustaw(procent);
    return hda_glosnosc_ustaw(procent);
}
uint32_t wyjscie_audio_wyciszenie_ustaw(bool wyciszony) {
    if (aktywne() == WyjscieAudio::Urzadzenie::Ac97) return ac97_wyciszenie_ustaw(wyciszony);
    return hda_wyciszenie_ustaw(wyciszony);
}

namespace PcmAplikacji {
using WyjscieAudio::Urzadzenie;
int64_t open(Pcm::Owner o, const BwsPcmFormat& f) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::open(o, f);
        case Urzadzenie::Ac97: return Ac97Pcm::open(o, f);
        default: return PCM_DEVICE;
    }
}
int64_t write(Pcm::Owner o, uint64_t h, const int16_t* p, uint32_t n, uint64_t copy_ns) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::write(o, h, p, n, copy_ns);
        case Urzadzenie::Ac97: return Ac97Pcm::write(o, h, p, n);
        default: return PCM_DEVICE;
    }
}
int64_t query(Pcm::Owner o, uint64_t h, BwsPcmState& s) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::query(o, h, s);
        case Urzadzenie::Ac97: return Ac97Pcm::query(o, h, s);
        default: return PCM_DEVICE;
    }
}
int64_t statistics(Pcm::Owner o, uint64_t h, BwsPcmStats& s) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::statistics(o, h, s);
        case Urzadzenie::Ac97: return Ac97Pcm::statistics(o, h, s);
        default: return PCM_DEVICE;
    }
}
int64_t gain(Pcm::Owner o, uint64_t h, uint64_t v, uint64_t m) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::gain(o, h, v, m);
        case Urzadzenie::Ac97: return Ac97Pcm::gain(o, h, v, m);
        default: return PCM_DEVICE;
    }
}
int64_t close(Pcm::Owner o, uint64_t h) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::close(o, h);
        case Urzadzenie::Ac97: return Ac97Pcm::close(o, h);
        default: return PCM_DEVICE;
    }
}
int64_t finish(Pcm::Owner o, uint64_t h) {
    switch (aktywne()) {
        case Urzadzenie::Hda: return HdaPcmAplikacji::finish(o, h);
        case Urzadzenie::Ac97: return Ac97Pcm::finish(o, h);
        default: return PCM_DEVICE;
    }
}
} // namespace PcmAplikacji

// C linkage: planista wola ten hak przez slaby symbol extern "C".
extern "C" bool pcm_posprzataj_proces(uint32_t pid, uint64_t generation) {
    switch (aktywne()) {
        case WyjscieAudio::Urzadzenie::Hda: return hda_pcm_posprzataj_proces(pid, generation);
        case WyjscieAudio::Urzadzenie::Ac97: return Ac97Pcm::posprzataj(pid, generation);
        default: return true;
    }
}
extern "C" bool pcm_diag_stream(uint32_t pid, uint64_t generation, BwsPcmStats& out) {
    switch (aktywne()) {
        case WyjscieAudio::Urzadzenie::Hda: return hda_pcm_diag_stream(pid, generation, out);
        case WyjscieAudio::Urzadzenie::Ac97: return Ac97Pcm::diag_stream(pid, generation, out);
        default: return false;
    }
}
extern "C" bool pcm_diag_starvation(uint32_t index, Pcm::Starvation& out) {
    switch (aktywne()) {
        case WyjscieAudio::Urzadzenie::Hda: return hda_pcm_diag_starvation(index, out);
        case WyjscieAudio::Urzadzenie::Ac97: return Ac97Pcm::diag_starvation(index, out);
        default: return false;
    }
}
