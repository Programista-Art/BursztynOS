#pragma once
/*
 * Bursztyn OS - AC97: silnik PCM dla BWS 110.
 *
 * Uzywa wspolnego miksera jadra (jadro/dzwiek/mikser_pcm.h, ta sama klasa co sterownik HDA) i przekazuje
 * zmiksowane okresy kontrolerowi AC97 (ac97_rdzen.h). Semantyka BWS 110 jest identyczna jak dla HDA:
 * te same kody bledow, preroll 2 okresow, promocja strumieni po limicie, statystyki per strumien,
 * sprzatanie przy zakonczeniu procesu. Aktywny jest tylko silnik urzadzenia wybranego przy starcie
 * (jadro/dzwiek/wyjscie_audio.h), wiec strumien nigdy nie trafia do dwoch urzadzen.
 *
 * Wywolujacy serializuje WSZYSTKIE metody (blokada sterownika w ac97.cpp). Brak alokacji.
 */
#include "ac97_rdzen.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include "jadro/bws/pcm_api.h"

namespace Ac97 {

constexpr uint64_t BEZCZYNNOSC_NS = 256000000ULL;   // jak HDA: stop DMA 256 ms po zamknieciu ostatniego strumienia
constexpr uint32_t PREROLL_RAMEK = 2 * OKRES_RAMEK;
static_assert(OKRES_RAMEK <= Pcm::MAX_MIX_FRAMES, "okres AC97 miesci sie w jednym miksie");

struct StatystykiPcm {
    uint64_t otwarcia, zamkniecia, usuniete_przy_zakonczeniu, start_przedwczesny, okresy_brakow;
    uint64_t ramki_brakujace, bledy_startu, bledy_obslugi, stopy_bezczynnosci;
};

template <class P>
class SilnikPcm {
public:
    using HakPlanisty = void (*)(Pcm::Owner, BwsPcmStats&);
    using HakOtwarcia = void (*)(uint32_t pid);

    explicit SilnikPcm(Kontroler<P>& k) : k_(k) {}
    SilnikPcm(const SilnikPcm&) = delete;
    SilnikPcm& operator=(const SilnikPcm&) = delete;

    void ustaw_haki(HakPlanisty planista, HakOtwarcia otwarcie) { planista_ = planista; otwarcie_ = otwarcie; }
    const StatystykiPcm& statystyki_silnika() const { return s_; }
    uint32_t klienci() const { return klienci_; }
    Pcm::Mixer& mikser() { return mikser_; }

    static int64_t wynik(Pcm::Result rc) {
        switch (rc) {
            case Pcm::OK: return PCM_OK;
            case Pcm::UNSUPPORTED: return PCM_UNSUPPORTED;
            case Pcm::LIMIT: return PCM_LIMIT;
            case Pcm::BAD_HANDLE: return PCM_HANDLE;
            case Pcm::FULL: return PCM_FULL;
            default: return PCM_INVALID;
        }
    }

    int64_t open(Pcm::Owner owner, const BwsPcmFormat& fmt) {
        if (fmt.version != BWS_PCM_V1 || fmt.size != sizeof(fmt) || fmt.reserved) return PCM_INVALID;
        if (!k_.gotowy()) return PCM_DEVICE;
        uint64_t h = 0;
        const auto rc = mikser_.open(owner, {fmt.rate, fmt.channels, fmt.encoding}, &h, PREROLL_RAMEK);
        if (rc != Pcm::OK) return wynik(rc);
        ++klienci_; ++s_.otwarcia; bezczynny_od_ = 0;
        if (otwarcie_) otwarcie_(owner.pid);
        return static_cast<int64_t>(h);
    }
    int64_t write(Pcm::Owner owner, uint64_t h, const int16_t* probki, uint32_t ramki, uint64_t teraz) {
        if (!k_.gotowy()) return PCM_DEVICE;
        uint32_t przyjete = 0;
        const auto rc = mikser_.write(owner, h, probki, ramki, &przyjete, teraz);
        if (rc == Pcm::OK) (void)mikser_.write_timing(owner, h, teraz, 0);
        return rc == Pcm::OK ? static_cast<int64_t>(przyjete) : wynik(rc);
    }
    int64_t query(Pcm::Owner owner, uint64_t h, BwsPcmState& st) {
        Pcm::Status s{};
        const auto rc = mikser_.status(owner, h, &s);
        if (rc != Pcm::OK) return wynik(rc);
        st = {BWS_PCM_V1, sizeof(st), s.queued, s.capacity, s.volume, s.muted, s.channels, Pcm::RATE};
        return PCM_OK;
    }
    int64_t statistics(Pcm::Owner owner, uint64_t h, BwsPcmStats& out) {
        const auto rc = mikser_.statistics(owner, h, out);
        if (rc == Pcm::OK && planista_) planista_(owner, out);
        return wynik(rc);
    }
    int64_t gain(Pcm::Owner owner, uint64_t h, uint64_t vol, uint64_t mute) {
        if (vol > 100 || mute > 1) return PCM_INVALID;
        return wynik(mikser_.gain(owner, h, static_cast<uint32_t>(vol), static_cast<uint32_t>(mute)));
    }
    int64_t close(Pcm::Owner owner, uint64_t h, uint64_t teraz) {
        archiwizuj(owner, h);
        const auto rc = mikser_.close(owner, h);
        if (rc == Pcm::OK) {
            ++s_.zamkniecia;
            if (klienci_ && !--klienci_) bezczynny_od_ = teraz ? teraz : 1;
        }
        return wynik(rc);
    }
    int64_t finish(Pcm::Owner owner, uint64_t h) { return wynik(mikser_.finish(owner, h)); }
    void posprzataj(Pcm::Owner owner, uint64_t teraz) {
        biezacy_ = this;
        mikser_.archive_owner(owner, [](Pcm::Owner o, uint64_t h) { biezacy_->archiwizuj(o, h); });
        const uint32_t usuniete = mikser_.cleanup(owner);
        klienci_ = usuniete <= klienci_ ? klienci_ - usuniete : 0;
        s_.usuniete_przy_zakonczeniu += usuniete;
        if (usuniete && !klienci_) bezczynny_od_ = teraz ? teraz : 1;
    }
    bool statystyki_koncowe(uint32_t pid, uint64_t gen, BwsPcmStats& out) const {
        for (uint32_t i = 0; i < archiwum_n_; ++i)
            if (archiwum_[i].owner.pid == pid && archiwum_[i].owner.generation == gen) { out = archiwum_[i].stats; return true; }
        return false;
    }
    bool glod(uint32_t i, Pcm::Starvation& out) const { return mikser_.starvation(i, out); }

    // Praca odroczona: start po przygotowaniu danych, uzupelnianie DMA, stop po bezczynnosci.
    void obsluz(uint64_t teraz) {
        if (!k_.gotowy()) return;
        if (k_.odtwarza()) {
            s_.start_przedwczesny += mikser_.promote_timed_out(teraz, 2 * OKRES_NS);
            if (!klienci_ && bezczynny_od_ && teraz - bezczynny_od_ >= BEZCZYNNOSC_NS) {
                if (k_.zatrzymaj()) { bezczynny_od_ = 0; ++s_.stopy_bezczynnosci; }
                return;
            }
            if (k_.obsluz(teraz) != Blad::Brak) ++s_.bledy_obslugi;
            return;
        }
        if (!klienci_ || !mikser_.has_pcm()) { gotowosc_od_ = 0; return; }
        const auto poziomy = mikser_.queue_levels();
        const bool gotowe = mikser_.has_ready_pcm() &&
                            (!poziomy.expecting || poziomy.minimum >= WYPRZEDZENIE * OKRES_RAMEK);
        if (!gotowe) {
            if (!gotowosc_od_) gotowosc_od_ = teraz ? teraz : 1;
            const uint64_t laska = poziomy.expecting > 1 ? 2 * OKRES_NS : OKRES_NS;
            if (teraz - gotowosc_od_ < laska) return;
            const uint32_t p = mikser_.promote_all_nonempty();
            s_.start_przedwczesny += p ? p : 1;
        }
        gotowosc_od_ = 0;
        teraz_ = teraz;
        if (!k_.start(&SilnikPcm::zrodlo, this, teraz)) ++s_.bledy_startu;
    }

private:
    // Dokladnie raz na okres: render + zatwierdzenie w mikserze (bez duplikatow i pominiec ramek).
    static bool zrodlo(void* ctx, int16_t* probki, uint32_t ramki) {
        auto* self = static_cast<SilnikPcm*>(ctx);
        auto* wyj = reinterpret_cast<Pcm::Frame*>(probki);
        if (self->mikser_.preview(wyj, ramki) != Pcm::OK) {
            for (uint32_t i = 0; i < ramki * 2; ++i) probki[i] = 0;
            return false;
        }
        const uint32_t brak = self->mikser_.consume(ramki, self->teraz_);
        if (brak) { ++self->s_.okresy_brakow; self->s_.ramki_brakujace += brak; }
        for (uint32_t i = 0; i < ramki * 2; ++i) if (probki[i]) return true;
        return false;
    }
    void archiwizuj(Pcm::Owner owner, uint64_t h) {
        if (archiwum_n_ == Pcm::MAX_STREAMS) return;
        BwsPcmStats st{};
        if (mikser_.statistics(owner, h, st) == Pcm::OK) {
            if (planista_) planista_(owner, st);
            archiwum_[archiwum_n_++] = {owner, st};
        }
    }
    struct Archiwum { Pcm::Owner owner; BwsPcmStats stats; };

    Kontroler<P>& k_;
    Pcm::Mixer mikser_;
    StatystykiPcm s_{};
    Archiwum archiwum_[Pcm::MAX_STREAMS]{};
    uint32_t archiwum_n_ = 0, klienci_ = 0;
    uint64_t bezczynny_od_ = 0, gotowosc_od_ = 0, teraz_ = 0;
    HakPlanisty planista_ = nullptr;
    HakOtwarcia otwarcie_ = nullptr;
    static inline SilnikPcm* biezacy_ = nullptr;
};

} // namespace Ac97
