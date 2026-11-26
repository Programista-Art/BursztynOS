/*
 * Grajek - dekoder MP3 (MPEG-1/2/2.5 Layer III) na bazie dr_mp3 (public domain / MIT-0, bazuje na minimp3, CC0).
 *
 * To jedyna jednostka Grajka kompilowana z SSE2 (-msse2 -mfpmath=sse), tak jak silnik czcionek BSF i Husarz:
 * minimp3 liczy w float. Jadro zapisuje stan FPU/SSE przy przelaczaniu procesow (jadro/arch/fpu).
 *
 * Konfiguracja dr_mp3 dla Ring 3: bez stdio i libc (wlasne kopiowanie/zerowanie/alokator z limitem), bez asercji
 * (bledy wejscia sa zwracane, nie przerywaja procesu). Wejscie jest czytane strumieniowo przez Czytnik
 * (okno 64 KiB) - pliku nigdy nie wczytuje sie w calosci. Ochrona przed petlami na uszkodzonych danych: jedno
 * wywolanie dekoduj() moze przeczytac najwyzej BUDZET_BAJTOW_WYWOLANIA bajtow bez wyprodukowania probek;
 * przekroczenie konczy strumien z bledem "uszkodzony plik".
 */
#include "dekoder.h"
#include "pamiec.h"

namespace {
void* mp3_alokuj(size_t n, void*) { return Grajek::alokuj(n); }
void mp3_zwolnij(void* p, void*) { Grajek::zwolnij(p); }
void mp3_przenies(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    if (d == s || n == 0) return;
    if (d < s) { for (size_t i = 0; i < n; ++i) d[i] = s[i]; }
    else { for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1]; }
}
void mp3_zeruj(void* p, size_t n) { auto* d = static_cast<uint8_t*>(p); for (size_t i = 0; i < n; ++i) d[i] = 0; }
}

// Ring 3 jest freestanding: naglowek intrinsics SSE dolacza mm_malloc.h (malloc/free z libc). Grajek nie uzywa
// _mm_malloc, wiec blokujemy ten naglowek jego wlasnym straznikiem.
#define _MM_MALLOC_H_INCLUDED 1
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DRMP3_API static
#define DRMP3_ASSERT(expression) ((void)0)
#define DRMP3_COPY_MEMORY(dst, src, sz) Grajek::kopiuj_bajty((dst), (src), (sz))
#define DRMP3_ZERO_MEMORY(p, sz) mp3_zeruj((p), (sz))
#define DRMP3_MOVE_MEMORY(dst, src, sz) mp3_przenies((dst), (src), (sz))
#define DRMP3_MALLOC(sz) Grajek::alokuj((sz))
#define DRMP3_REALLOC(p, sz) ((void)(p), (void)(sz), nullptr)
#define DRMP3_FREE(p) Grajek::zwolnij((p))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#include "zewnetrzne/dr_mp3.h"
#pragma GCC diagnostic pop

namespace Grajek {
namespace {

constexpr uint32_t BUDZET_BAJTOW_WYWOLANIA = 1024U * 1024U;
constexpr uint32_t MAKS_PUNKTOW_SZUKANIA = 1024;

class DekoderMp3 : public Dekoder {
public:
    ~DekoderMp3() override { zamknij(); }

    bool otworz(const DostepDoPlikow& dostep, const char* sciezka) override {
        zamknij();
        blad_[0] = '\0';
        pozycja_ = 0;
        if (!czytnik_.otworz(dostep, sciezka)) { ustaw_blad("Nie można otworzyć pliku."); return false; }
        // Walidacja zawartosci (dwie spojne ramki Layer III), czas z Xing/VBRI albo oszacowanie CBR.
        Mp3::Info info{};
        const Mp3::Blad b = Mp3::analizuj(czytnik_, info, nullptr, nullptr, 0);
        if (b != Mp3::Blad::OK) { ustaw_blad(Mp3::opis(b)); return false; }
        czytnik_.ustaw_pozycje(0);
        mp3_ = static_cast<drmp3*>(alokuj(sizeof(drmp3)));
        if (!mp3_) { ustaw_blad("Brak pamięci dla dekodera MP3."); return false; }
        mp3_zeruj(mp3_, sizeof(drmp3));
        drmp3_allocation_callbacks a{};
        a.onMalloc = mp3_alokuj;
        a.onFree = mp3_zwolnij;
        a.onRealloc = nullptr;            // dr_mp3 emuluje realloc przez malloc + kopiowanie + free
        bez_budzetu_ = true;
        const bool ok = drmp3_init(mp3_, &DekoderMp3::odczyt, &DekoderMp3::przesun, &DekoderMp3::pozycja_cb,
                                   nullptr, this, &a) != 0;
        bez_budzetu_ = false;
        if (!ok) { zwolnij(mp3_); mp3_ = nullptr; ustaw_blad("Nie można zainicjować dekodera MP3."); return false; }
        zainicjowany_ = true;
        if (mp3_->channels < 1 || mp3_->channels > 2 || mp3_->sampleRate < 8000 || mp3_->sampleRate > 48000) {
            ustaw_blad("Nieobsługiwany format strumienia MP3.");
            return false;
        }
        p_.czestotliwosc = mp3_->sampleRate;
        p_.kanaly = uint16_t(mp3_->channels);
        p_.przeplywnosc = info.przeplywnosc;
        if (mp3_->totalPCMFrameCount != DRMP3_UINT64_MAX) {
            const uint64_t opoz = uint64_t(mp3_->delayInPCMFrames) + mp3_->paddingInPCMFrames;
            p_.ramki = mp3_->totalPCMFrameCount > opoz ? mp3_->totalPCMFrameCount - opoz : 0;
        } else {
            p_.ramki = info.czas_ms * uint64_t(p_.czestotliwosc) / 1000ULL;   // oszacowanie CBR
        }
        return true;
    }

    uint32_t dekoduj(int16_t* wy, uint32_t ramki) override {
        if (!zainicjowany_ || ma_blad() || !wy || !ramki) return 0;
        if (ramki > 4096) ramki = 4096;
        przeczytane_w_wywolaniu_ = 0;
        przekroczony_budzet_ = false;
        const drmp3_uint64 n = drmp3_read_pcm_frames_s16(mp3_, ramki, wy);
        if (przekroczony_budzet_ && n == 0) { ustaw_blad("Uszkodzony plik MP3 (brak poprawnych ramek)."); return 0; }
        if (czytnik_.blad() && n == 0) { ustaw_blad("Błąd odczytu pliku MP3."); return 0; }
        if (n > ramki) return 0;
        pozycja_ += n;
        // Dlugosc szacowana (CBR bez Xing) moze byc zanizona: zawsze >= pozycja.
        if (p_.ramki && pozycja_ > p_.ramki) p_.ramki = pozycja_;
        return uint32_t(n);
    }

    bool szukaj(uint64_t ramka) override {
        if (!zainicjowany_ || ma_blad()) return false;
        bez_budzetu_ = true;
        // dr_mp3 0.7.4: punkt szukania lezacy w poczatkowym oknie DRMP3_SEEK_LEADING_MP3_FRAMES+1 ramek (gdy punkty
        // sa gesciej niz co 3 ramki) zglasza indeks PCM o jedna ramke za wczesnie. Odstep >= 8 ramek MPEG omija ten
        // przypadek; krotkie pliki (< 2 punktow) sa przewijane dokladnie metoda "od poczatku" (brute force).
        const uint64_t ramki_mpeg = p_.ramki / (p_.czestotliwosc >= 32000 ? 1152 : 576);
        const uint64_t chciane = ramki_mpeg / 8 < MAKS_PUNKTOW_SZUKANIA ? ramki_mpeg / 8 : MAKS_PUNKTOW_SZUKANIA;
        if (!tabela_ && !bez_tabeli_ && chciane >= 2) {
            punkty_ = static_cast<drmp3_seek_point*>(alokuj(sizeof(drmp3_seek_point) * MAKS_PUNKTOW_SZUKANIA));
            if (punkty_) {
                drmp3_uint32 liczba = drmp3_uint32(chciane);
                if (drmp3_calculate_seek_points(mp3_, &liczba, punkty_) && liczba > 0 &&
                    drmp3_bind_seek_table(mp3_, liczba, punkty_))
                    tabela_ = true;
            }
            if (!tabela_) bez_tabeli_ = true;        // nie powtarzaj kosztownego skanu przy kolejnych przewinieciach
        }
        const bool ok = drmp3_seek_to_pcm_frame(mp3_, ramka) != 0;
        bez_budzetu_ = false;
        if (!ok) { ustaw_blad("Nie można przewinąć pliku MP3."); return false; }
        pozycja_ = mp3_->currentPCMFrame;
        return true;
    }

    FormatAudio format() const override { return FormatAudio::MP3; }

private:
    static size_t odczyt(void* u, void* bufor, size_t n) {
        auto* d = static_cast<DekoderMp3*>(u);
        if (!d->bez_budzetu_) {
            if (d->przeczytane_w_wywolaniu_ >= BUDZET_BAJTOW_WYWOLANIA) { d->przekroczony_budzet_ = true; return 0; }
            const uint32_t zostalo = BUDZET_BAJTOW_WYWOLANIA - d->przeczytane_w_wywolaniu_;
            if (n > zostalo) n = zostalo;
        }
        if (n > 0x7FFFFFFF) n = 0x7FFFFFFF;
        const uint32_t got = d->czytnik_.czytaj(bufor, uint32_t(n));
        d->przeczytane_w_wywolaniu_ += got;
        return got;
    }
    static drmp3_bool32 przesun(void* u, int offset, drmp3_seek_origin origin) {
        auto* d = static_cast<DekoderMp3*>(u);
        int64_t cel = 0;
        if (origin == DRMP3_SEEK_SET) cel = offset;
        else if (origin == DRMP3_SEEK_CUR) cel = int64_t(d->czytnik_.pozycja()) + offset;
        else cel = int64_t(d->czytnik_.rozmiar()) + offset;
        if (cel < 0 || uint64_t(cel) > d->czytnik_.rozmiar()) return DRMP3_FALSE;
        return d->czytnik_.ustaw_pozycje(uint64_t(cel)) ? DRMP3_TRUE : DRMP3_FALSE;
    }
    static drmp3_bool32 pozycja_cb(void* u, drmp3_int64* kursor) {
        auto* d = static_cast<DekoderMp3*>(u);
        if (!kursor) return DRMP3_FALSE;
        *kursor = drmp3_int64(d->czytnik_.pozycja());
        return DRMP3_TRUE;
    }
    void zamknij() {
        if (mp3_) {
            if (zainicjowany_) drmp3_uninit(mp3_);
            zwolnij(mp3_);
            mp3_ = nullptr;
        }
        if (punkty_) { zwolnij(punkty_); punkty_ = nullptr; }
        tabela_ = false;
        bez_tabeli_ = false;
        zainicjowany_ = false;
    }

    Czytnik czytnik_;
    drmp3* mp3_ = nullptr;
    drmp3_seek_point* punkty_ = nullptr;
    bool zainicjowany_ = false, tabela_ = false, bez_tabeli_ = false, bez_budzetu_ = false, przekroczony_budzet_ = false;
    uint32_t przeczytane_w_wywolaniu_ = 0;
};

} // namespace

Dekoder* utworz_dekoder_mp3() { return new DekoderMp3(); }

} // namespace Grajek
