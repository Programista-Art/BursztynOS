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
/*
 * Przewijanie MP3 (zastepuje drmp3_calculate_seek_points, ktore przy PIERWSZYM przewinieciu synchronicznie
 * przechodzilo caly plik dwa razy i dwa razy dekodowalo go od poczatku do biezacej pozycji - kilka sekund bez
 * pompowania PCM, czyli wielosekundowe przerwy w dzwieku).
 *
 * Indeks: punkty drmp3_seek_point co `krok_` ramek MPEG, budowane PRZYROSTOWO drugim egzemplarzem dr_mp3 (wlasny
 * Czytnik), ktory tylko parsuje naglowki i informacje boczne ramek (drmp3_decode_next_frame_ex z pcm == NULL: bez
 * syntezy). Praca: praca_w_tle(budzet ramek) z petli silnika, gdy kolejka PCM jest pelna, oraz ograniczone
 * dociagniecie w szukaj() (MAKS_RAMEK_SYNCHRONICZNIE). Pamiec: MAKS_PUNKTOW (stala tablica); po zapelnieniu co
 * drugi punkt jest usuwany, a krok podwajany - indeks pokrywa kazdy plik w ograniczonej pamieci.
 * Punkt dla ramki i (i >= 2, i % krok == 0): pozycja bajtowa ramki i-2, indeks PCM poczatku ramki i, 2 ramki MPEG
 * i 1 ramka PCM (dlugosc ramki i-1) do odrzucenia - jak punkty dr_mp3: ramka i-2 (bez syntezy) laduje rezerwuar bitow,
 * ramka i-1 odtwarza stan nakladania IMDCT/syntezy i jest odrzucana, probki od ramki i sa identyczne jak przy
 * dekodowaniu od poczatku.
 *
 * szukaj(cel):
 *  - cel w zasiegu indeksu (albo plik w calosci zaindeksowany): DOKLADNIE - skok do najblizszego wczesniejszego
 *    punktu i dekodowanie do celu (<= krok_ ramek), probki identyczne jak przy dekodowaniu od poczatku,
 *  - inaczej PRZYBLIZONE: bajt z tablicy TOC Xing (gdy poprawna) albo liniowo (CBR / brak TOC), synchronizacja
 *    na najblizszej ramce za tym bajtem, pozycja = cel. Blad pozycji wynika z dokladnosci TOC/przeplywnosci.
 * Koszt szukaj(): O(krok_) dekodowanych ramek + O(MAKS_RAMEK_SYNCHRONICZNIE) parsowanych naglowkow; nigdy pelny skan.
 */
constexpr uint32_t MAKS_PUNKTOW = 2048;
constexpr uint32_t KROK_POCZATKOWY = 8;            // >= 8: omija przypadek punktow gestszych niz co 3 ramki w dr_mp3
constexpr uint32_t MAKS_RAMEK_SYNCHRONICZNIE = 1536;   // naglowki parsowane w jednym szukaj() (~37 s przy 1152 probkach)
constexpr uint32_t MAKS_BAJTOW_SKANU_WYWOLANIA = 4U * 1024U * 1024U;   // ochrona przed petla na smieciach
constexpr uint32_t MAKS_RAMEK_DO_PRZODU = 128;     // dokladne dekodowanie od ostatniego punktu (~3 s audio), gdy cel tuz za indeksem

class DekoderMp3 : public Dekoder {
public:
    ~DekoderMp3() override { zamknij(); }

    bool otworz(const DostepDoPlikow& dostep, const char* sciezka) override {
        zamknij();
        blad_[0] = '\0';
        pozycja_ = 0;
        szukanie_ = Szukanie::Brak;
        dostep_ = dostep;
        kopiuj_tekst(sciezka_, sizeof(sciezka_), sciezka ? sciezka : "");
        if (!czytnik_.otworz(dostep, sciezka)) { ustaw_blad("Nie można otworzyć pliku."); return false; }
        // Walidacja zawartosci (dwie spojne ramki Layer III), czas z Xing/VBRI albo oszacowanie CBR.
        const Mp3::Blad b = Mp3::analizuj(czytnik_, info_, nullptr, nullptr, 0);
        if (b != Mp3::Blad::OK) { ustaw_blad(Mp3::opis(b)); return false; }
        czytnik_.ustaw_pozycje(0);
        mp3_ = nowy_drmp3(this, &DekoderMp3::odczyt, &DekoderMp3::przesun, &DekoderMp3::pozycja_cb, &bez_budzetu_);
        if (!mp3_) { ustaw_blad("Nie można zainicjować dekodera MP3."); return false; }
        zainicjowany_ = true;
        if (mp3_->channels < 1 || mp3_->channels > 2 || mp3_->sampleRate < 8000 || mp3_->sampleRate > 48000) {
            ustaw_blad("Nieobsługiwany format strumienia MP3.");
            return false;
        }
        p_.czestotliwosc = mp3_->sampleRate;
        p_.kanaly = uint16_t(mp3_->channels);
        p_.przeplywnosc = info_.przeplywnosc;
        if (mp3_->totalPCMFrameCount != DRMP3_UINT64_MAX) {
            const uint64_t opoz = uint64_t(mp3_->delayInPCMFrames) + mp3_->paddingInPCMFrames;
            p_.ramki = mp3_->totalPCMFrameCount > opoz ? mp3_->totalPCMFrameCount - opoz : 0;
        } else {
            p_.ramki = info_.czas_ms * uint64_t(p_.czestotliwosc) / 1000ULL;   // oszacowanie CBR
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

    bool praca_w_tle(uint32_t budzet) override {
        if (!zainicjowany_ || ma_blad() || skan_koniec_) return false;
        indeksuj(budzet);
        return !skan_koniec_;
    }
    uint64_t zaindeksowane_ramki() const override { return skan_pcm_; }
    bool indeks_kompletny() const override { return skan_koniec_; }

    bool szukaj(uint64_t ramka) override {
        if (!zainicjowany_ || ma_blad()) return false;
        bez_budzetu_ = true;
        bool ok;
        if (ramka == 0) {
            ok = drmp3_seek_to_pcm_frame(mp3_, 0) != 0;
            szukanie_ = Szukanie::Dokladne;
        } else {
            if (!skan_koniec_ && skan_pcm_ <= ramka) {
                // ograniczone dociagniecie indeksu (tylko naglowki); krotkie i srednie pliki koncza tu skan
                uint32_t zostalo = MAKS_RAMEK_SYNCHRONICZNIE;
                while (zostalo && !skan_koniec_ && skan_pcm_ <= ramka) {
                    const uint32_t porcja = zostalo < 256 ? zostalo : 256;
                    if (!indeksuj(porcja)) break;
                    zostalo -= porcja;
                }
            }
            const bool w_zasiegu = skan_koniec_ || skan_pcm_ > ramka ||
                                   (liczba_ && ramka - punkty_[liczba_ - 1].pcmFrameIndex <=
                                                   uint64_t(MAKS_RAMEK_DO_PRZODU) * probki_na_ramke());
            if (w_zasiegu) {
                (void)drmp3_bind_seek_table(mp3_, liczba_, liczba_ ? punkty_ : nullptr);
                ok = drmp3_seek_to_pcm_frame(mp3_, ramka) != 0;
                szukanie_ = Szukanie::Dokladne;
            } else if (ramka > probki_na_ramke()) {
                ok = przybliz(ramka);
                szukanie_ = Szukanie::Przyblizone;
            } else {
                (void)drmp3_bind_seek_table(mp3_, liczba_, liczba_ ? punkty_ : nullptr);
                ok = drmp3_seek_to_pcm_frame(mp3_, ramka) != 0;   // cel w pierwszej ramce: od poczatku
                szukanie_ = Szukanie::Dokladne;
            }
        }
        bez_budzetu_ = false;
        if (!ok) { ustaw_blad("Nie można przewinąć pliku MP3."); return false; }
        pozycja_ = mp3_->currentPCMFrame;
        return true;
    }

    FormatAudio format() const override { return FormatAudio::MP3; }

private:
    static drmp3* nowy_drmp3(void* ctx, drmp3_read_proc rd, drmp3_seek_proc sk, drmp3_tell_proc tl, bool* bez_budzetu) {
        auto* m = static_cast<drmp3*>(alokuj(sizeof(drmp3)));
        if (!m) return nullptr;
        mp3_zeruj(m, sizeof(drmp3));
        drmp3_allocation_callbacks a{};
        a.onMalloc = mp3_alokuj;
        a.onFree = mp3_zwolnij;
        a.onRealloc = nullptr;            // dr_mp3 emuluje realloc przez malloc + kopiowanie + free
        if (bez_budzetu) *bez_budzetu = true;
        const bool ok = drmp3_init(m, rd, sk, tl, nullptr, ctx, &a) != 0;
        if (bez_budzetu) *bez_budzetu = false;
        if (!ok) { zwolnij(m); return nullptr; }
        return m;
    }
    uint32_t probki_na_ramke() const { return p_.czestotliwosc >= 32000 ? 1152 : 576; }

    // Jeden krok skanu: do `budzet` ramek MPEG. false = nic nie zrobiono (koniec albo blad inicjalizacji).
    bool indeksuj(uint32_t budzet) {
        if (skan_koniec_) return false;
        if (!skan_) {
            if (!punkty_) {
                punkty_ = static_cast<drmp3_seek_point*>(alokuj(sizeof(drmp3_seek_point) * MAKS_PUNKTOW));
                if (!punkty_) { skan_koniec_ = true; return false; }
            }
            if (!czytnik_skanu_.otworz(dostep_, sciezka_)) { skan_koniec_ = true; return false; }
            skan_ = nowy_drmp3(this, &DekoderMp3::odczyt_skanu, &DekoderMp3::przesun_skanu, &DekoderMp3::pozycja_skanu, nullptr);
            if (!skan_ || !drmp3_seek_to_start_of_stream(skan_)) { skan_koniec_ = true; return false; }
            skan_pcm_ = 0; skan_ramka_ = 0; liczba_ = 0; krok_ = KROK_POCZATKOWY;
            // Punkt 0: poczatek strumienia (za ID3v2 i ramka Xing). Bez niego dr_mp3 dla celu przed pierwszym
            // punktem skacze do bajtu 0 PLIKU (tag ID3, ramka Xing dekodowana jak dzwiek).
            drmp3_seek_point& p0 = punkty_[liczba_++];
            p0.seekPosInBytes = skan_->streamStartOffset; p0.pcmFrameIndex = 0;
            p0.mp3FramesToDiscard = 0; p0.pcmFramesToDiscard = 0;
        }
        skan_bajty_ = 0;
        bool cos = false;
        for (uint32_t i = 0; i < budzet; ++i) {
            if (skan_bajty_ > MAKS_BAJTOW_SKANU_WYWOLANIA) break;    // smieci miedzy ramkami: dokonczymy pozniej
            const uint64_t bajt = skan_->streamCursor - skan_->dataSize;
            const drmp3_uint32 n = drmp3_decode_next_frame_ex(skan_, nullptr, nullptr, nullptr);
            if (n == 0) { skan_koniec_ = true; break; }
            cos = true;
            hist_[0] = hist_[1]; hist_[1] = hist_[2];
            hist_[2] = {bajt, skan_pcm_};
            skan_pcm_ += n;
            const uint64_t nr = skan_ramka_++;
            if (nr >= 2 && nr % krok_ == 0) dodaj_punkt(hist_[0].bajt, hist_[2].pcm, uint32_t(hist_[2].pcm - hist_[1].pcm));
        }
        if (skan_koniec_) zwolnij_skan();
        return cos;
    }
    void dodaj_punkt(uint64_t bajt, uint64_t pcm, uint32_t odrzuc_pcm) {
        if (liczba_ == MAKS_PUNKTOW) {
            uint32_t j = 1;                                  // punkt 0 (poczatek strumienia) zostaje zawsze
            for (uint32_t i = 2; i < liczba_; i += 2) punkty_[j++] = punkty_[i];
            liczba_ = j;
            krok_ *= 2;
            if (skan_ramka_ - 1 < 2 || (skan_ramka_ - 1) % krok_ != 0) return;
        }
        drmp3_seek_point& p = punkty_[liczba_++];
        p.seekPosInBytes = bajt;
        p.pcmFrameIndex = pcm;
        p.mp3FramesToDiscard = 2;
        p.pcmFramesToDiscard = drmp3_uint16(odrzuc_pcm);   // <= 1152
    }
    // Skok przyblizony: TOC Xing albo liniowo; ramka znaleziona za szacowanym bajtem dostaje indeks `ramka`.
    bool przybliz(uint64_t ramka) {
        const uint64_t start = mp3_->streamStartOffset;
        uint64_t koniec = czytnik_.rozmiar();
        if (koniec <= start + 4) return false;
        uint64_t dane = koniec - start;
        if (info_.bajty_xing && info_.bajty_xing <= dane) dane = info_.bajty_xing;
        const uint64_t calosc = p_.ramki ? p_.ramki : 1;
        // ulamek czasu w promilach procenta (0..100000) - bez liczb zmiennoprzecinkowych w sciezce przewijania
        uint64_t u = ramka >= calosc ? 99999 : ramka * 100000ULL / calosc;
        uint64_t bajt_rel;
        if (info_.ma_toc) {
            const uint32_t idx = uint32_t(u / 1000);
            const uint64_t a = info_.toc[idx], b = idx < 99 ? info_.toc[idx + 1] : 256;
            const uint64_t q = a * 1000 + (b - a) * (u % 1000);     // * 1/256000 strumienia
            bajt_rel = dane / 256000ULL * q + (dane % 256000ULL) * q / 256000ULL;
        } else {
            bajt_rel = dane / 100000ULL * u + (dane % 100000ULL) * u / 100000ULL;
        }
        uint64_t bajt = start + bajt_rel;
        if (bajt >= koniec - 4) bajt = koniec - 4;
        if (!drmp3__on_seek_64(mp3_, bajt, DRMP3_SEEK_SET)) return false;
        drmp3_reset(mp3_);
        if (drmp3_decode_next_frame_ex(mp3_, nullptr, nullptr, nullptr) == 0) return false;   // synchronizacja
        drmp3_seek_point punkt{};
        punkt.seekPosInBytes = mp3_->streamCursor - mp3_->dataSize;   // poczatek ramki za zsynchronizowana
        // jak w indeksie: ramka za synchronizacja laduje rezerwuar, nastepna (spf probek) odtwarza stan syntezy
        // i jest odrzucana; pierwsza probka wyjscia dostaje indeks `ramka`
        const uint32_t spf = probki_na_ramke();
        punkt.pcmFrameIndex = ramka;
        punkt.mp3FramesToDiscard = 2;
        punkt.pcmFramesToDiscard = drmp3_uint16(spf);
        (void)drmp3_bind_seek_table(mp3_, 1, &punkt);
        const bool ok = drmp3_seek_to_pcm_frame(mp3_, ramka) != 0;
        (void)drmp3_bind_seek_table(mp3_, liczba_, liczba_ ? punkty_ : nullptr);   // punkt lokalny nie zostaje
        return ok;
    }

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
    static drmp3_bool32 przesun_w(Czytnik& c, int offset, drmp3_seek_origin origin) {
        int64_t cel = 0;
        if (origin == DRMP3_SEEK_SET) cel = offset;
        else if (origin == DRMP3_SEEK_CUR) cel = int64_t(c.pozycja()) + offset;
        else cel = int64_t(c.rozmiar()) + offset;
        if (cel < 0 || uint64_t(cel) > c.rozmiar()) return DRMP3_FALSE;
        return c.ustaw_pozycje(uint64_t(cel)) ? DRMP3_TRUE : DRMP3_FALSE;
    }
    static drmp3_bool32 przesun(void* u, int offset, drmp3_seek_origin origin) {
        return przesun_w(static_cast<DekoderMp3*>(u)->czytnik_, offset, origin);
    }
    static drmp3_bool32 pozycja_cb(void* u, drmp3_int64* kursor) {
        auto* d = static_cast<DekoderMp3*>(u);
        if (!kursor) return DRMP3_FALSE;
        *kursor = drmp3_int64(d->czytnik_.pozycja());
        return DRMP3_TRUE;
    }
    static size_t odczyt_skanu(void* u, void* bufor, size_t n) {
        auto* d = static_cast<DekoderMp3*>(u);
        if (n > 0x7FFFFFFF) n = 0x7FFFFFFF;
        const uint32_t got = d->czytnik_skanu_.czytaj(bufor, uint32_t(n));
        d->skan_bajty_ += got;
        return got;
    }
    static drmp3_bool32 przesun_skanu(void* u, int offset, drmp3_seek_origin origin) {
        return przesun_w(static_cast<DekoderMp3*>(u)->czytnik_skanu_, offset, origin);
    }
    static drmp3_bool32 pozycja_skanu(void* u, drmp3_int64* kursor) {
        auto* d = static_cast<DekoderMp3*>(u);
        if (!kursor) return DRMP3_FALSE;
        *kursor = drmp3_int64(d->czytnik_skanu_.pozycja());
        return DRMP3_TRUE;
    }
    void zwolnij_skan() {
        if (skan_) { drmp3_uninit(skan_); zwolnij(skan_); skan_ = nullptr; }
        czytnik_skanu_.zamknij();
    }
    void zamknij() {
        if (mp3_) {
            if (zainicjowany_) drmp3_uninit(mp3_);
            zwolnij(mp3_);
            mp3_ = nullptr;
        }
        zwolnij_skan();
        if (punkty_) { zwolnij(punkty_); punkty_ = nullptr; }
        liczba_ = 0; krok_ = KROK_POCZATKOWY; skan_pcm_ = 0; skan_ramka_ = 0; skan_koniec_ = false;
        zainicjowany_ = false;
    }

    struct Wpis { uint64_t bajt, pcm; };
    DostepDoPlikow dostep_{};
    char sciezka_[MAKS_SCIEZKA]{};
    Mp3::Info info_{};
    Czytnik czytnik_;
    Czytnik czytnik_skanu_;
    drmp3* mp3_ = nullptr;
    drmp3* skan_ = nullptr;
    drmp3_seek_point* punkty_ = nullptr;
    uint32_t liczba_ = 0, krok_ = KROK_POCZATKOWY;
    uint64_t skan_pcm_ = 0, skan_ramka_ = 0, skan_bajty_ = 0;
    Wpis hist_[3]{};
    bool zainicjowany_ = false, skan_koniec_ = false, bez_budzetu_ = false, przekroczony_budzet_ = false;
    uint32_t przeczytane_w_wywolaniu_ = 0;
};

} // namespace

Dekoder* utworz_dekoder_mp3() { return new DekoderMp3(); }

} // namespace Grajek
