/*
 * BTP - Bursztynowa TaPeta, rozszerzenie .btp. Samodzielny dekoder BTP1.
 * Bez integracji z jadrem, systemem plikow lub wyborem tapety.
 *
 * Format pliku (little-endian):
 *   0: magic "BTP1" (4 bajty), 4: szerokosc u32, 8: wysokosc u32,
 *   12: bpp u8 (32), 13: kompresja u8 (0 = RAW, 1 = RLE).
 * RAW: dokladnie szerokosc * wysokosc * 4 bajty B, G, R, A, bez paddingu.
 * Kazdy pakiet RLE ma dokladnie 5 bajtow: liczba (1..255), B, G, R, A.
 * Wiersze biegna od gory, piksele od lewej. Pakiet moze przecinac wiersz.
 */

#include "tapetybtp.h"

namespace {

// Limit osi zgodny z MAKS_WYMIAR_WARSTWY w skladacz_obrazu.cpp oraz
// MAX_W / MAX_H przegladarki obrazow: do 8192 * 8192 pikseli (takze 8K).
constexpr uint32_t BTPMAKSWYMIAR = 8192U;
constexpr size_t BTPROZMIARPAKIETU = 5U;

// Wywolywane dopiero po sprawdzeniu obecnosci wszystkich czterech bajtow.
// Struktura powyzej opisuje ABI; nigdy nie rzutujemy na nia bufora pliku.
uint32_t btpczytaju32le(const uint8_t* dane) noexcept {
    return static_cast<uint32_t>(dane[0]) |
           (static_cast<uint32_t>(dane[1]) << 8U) |
           (static_cast<uint32_t>(dane[2]) << 16U) |
           (static_cast<uint32_t>(dane[3]) << 24U);
}

uint16_t btpczytaju16le(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint64_t btpczytaju64le(const uint8_t* p) noexcept {
    return static_cast<uint64_t>(btpczytaju32le(p)) |
           (static_cast<uint64_t>(btpczytaju32le(p + 4)) << 32);
}
uint32_t btpcrcbyte(uint32_t crc, uint8_t byte) noexcept {
    crc ^= byte;
    for (unsigned j = 0; j < 8; ++j)
        crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    return crc;
}
uint32_t btpcrc(const uint8_t* p, size_t n) noexcept {
    uint32_t crc = ~0U;
    for (size_t i = 0; i < n; ++i) crc = btpcrcbyte(crc, p[i]);
    return ~crc;
}

// Chunks are contiguous, independent and <=128 KiB. The token scan checks
// every read and every output length before a decoder can touch its target.
BtpWynik btpv2validate(const uint8_t* file, size_t size, BtpInfo* info) noexcept {
    if (size < 72) return BTPBLADZAMALYPLIK;
    if (file[0] != 'B' || file[1] != 'T' || file[2] != 'P' || file[3] != '2') return BTPBLADMAGIC;
    if (btpczytaju16le(file + 4) != 2 || btpczytaju16le(file + 6) != 72 ||
        file[16] != 1 || file[17] != 1 || btpczytaju16le(file + 18) ||
        btpczytaju32le(file + 68)) return BTPBLADV2;
    const uint32_t w = btpczytaju32le(file + 8), h = btpczytaju32le(file + 12);
    if (!w || !h || w > BTPMAKSWYMIAR || h > BTPMAKSWYMIAR) return BTPBLADWYMIAROW;
    const uint64_t raw = static_cast<uint64_t>(w) * h * 4U;
    if (btpczytaju64le(file + 20) != raw) return BTPBLADV2;
    const uint32_t count = btpczytaju32le(file + 36), chunk = btpczytaju32le(file + 40);
    if (chunk < 4 || chunk > 131072 || (chunk & 3) ||
        count != (static_cast<uint32_t>(raw) + chunk - 1U) / chunk) return BTPBLADV2;
    const uint64_t table = btpczytaju64le(file + 44), payload = btpczytaju64le(file + 52);
    if (table != 72 || payload != table + static_cast<uint64_t>(count) * 20 ||
        payload > size || btpczytaju64le(file + 28) != size - payload) return BTPBLADV2;
    if (btpcrc(file + static_cast<size_t>(payload), size - static_cast<size_t>(payload)) !=
        btpczytaju32le(file + 64)) return BTPBLADV2;
    size_t cursor = static_cast<size_t>(payload);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* entry = file + 72 + static_cast<size_t>(i) * 20;
        const uint64_t off = btpczytaju64le(entry);
        const uint32_t packed = btpczytaju32le(entry + 8), decoded = btpczytaju32le(entry + 12);
        const uint64_t remain = raw - static_cast<uint64_t>(i) * chunk;
        if (off != cursor || !packed || packed > size - cursor ||
            decoded != (remain < chunk ? remain : chunk) || entry[16] > 1 ||
            entry[17] > 2 || btpczytaju16le(entry + 18)) return BTPBLADV2;
        if (entry[16] == 0) {
            if (packed != decoded || entry[17]) return BTPBLADV2;
        } else {
            size_t src = cursor, end = cursor + packed, produced = 0;
            while (src < end) {
                const uint8_t token = file[src++];
                const unsigned kind = token >> 6;
                const unsigned low = token & 63U;
                size_t len = low + (kind == 0 ? 1U : kind == 2 ? 2U : 3U);
                if (kind && low == 63U) {
                    if (end - src < 2) return BTPBLADV2;
                    len = btpczytaju16le(file + src);
                    src += 2;
                    if (len < (kind == 1 ? 66U : kind == 2 ? 65U : 66U)) return BTPBLADV2;
                }
                const size_t output = kind == 2 ? len * 4 : len;
                const size_t input = kind == 0 ? len : kind == 1 ? 1U : kind == 2 ? 4U : 2U;
                if (output > decoded - produced || input > end - src) return BTPBLADV2;
                if (kind == 3) {
                    const uint16_t distance = btpczytaju16le(file + src);
                    if (!distance || distance > produced) return BTPBLADV2;
                }
                produced += output;
                src += input;
            }
            if (produced != decoded) return BTPBLADV2;
        }
        cursor += packed;
    }
    if (cursor != size) return BTPBLADV2;
    *info = BtpInfo{w, h, 2};
    return BTPOK;
}

uint8_t& btpv2byte(uint32_t* dst, size_t offset, uint32_t width, uint32_t stride) noexcept {
    if (width == stride) return reinterpret_cast<uint8_t*>(dst)[offset];
    const size_t rowbytes = static_cast<size_t>(width) * 4;
    return reinterpret_cast<uint8_t*>(dst)[offset / rowbytes * static_cast<size_t>(stride) * 4 + offset % rowbytes];
}
BtpWynik btpv2decode(const uint8_t* file, uint32_t* dst, uint32_t width,
                      uint32_t stride, size_t size) noexcept {
    const uint32_t count = btpczytaju32le(file + 36), chunk = btpczytaju32le(file + 40);
    size_t global = 0;
    uint32_t crc = ~0U;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* entry = file + 72 + static_cast<size_t>(i) * 20;
        const size_t start = static_cast<size_t>(btpczytaju64le(entry));
        const size_t end = start + btpczytaju32le(entry + 8);
        const size_t expected = btpczytaju32le(entry + 12);
        size_t pos = start, produced = 0;
        if (entry[16] == 0) {
            while (pos < end) {
                const uint8_t byte = file[pos++];
                btpv2byte(dst, global + produced++, width, stride) = byte;
            }
        } else while (pos < end) {
            const uint8_t token = file[pos++];
            const unsigned kind = token >> 6;
            const unsigned low = token & 63U;
            size_t len = low + (kind == 0 ? 1U : kind == 2 ? 2U : 3U);
            if (kind && low == 63U) {
                len = btpczytaju16le(file + pos);
                pos += 2;
            }
            const size_t total = kind == 2 ? len * 4 : len;
            uint16_t distance = 0;
            if (kind == 3) { distance = btpczytaju16le(file + pos); pos += 2; }
            for (size_t j = 0; j < total; ++j) {
                const uint8_t byte = kind == 0 ? file[pos + j] :
                    kind == 1 ? file[pos] : kind == 2 ? file[pos + (j & 3)] :
                    btpv2byte(dst, global + produced - distance, width, stride);
                btpv2byte(dst, global + produced++, width, stride) = byte;
            }
            if (kind == 0) pos += len;
            else if (kind == 1) ++pos;
            else if (kind == 2) pos += 4;
        }
        if (produced != expected) return BTPBLADV2;
        const size_t distance = entry[17] == 1 ? 4U :
                                entry[17] == 2 ? static_cast<size_t>(width) * 4 : 0U;
        if (distance) for (size_t j = distance; j < produced; ++j) {
            uint8_t& byte = btpv2byte(dst, global + j, width, stride);
            byte = static_cast<uint8_t>(byte + btpv2byte(dst, global + j - distance, width, stride));
        }
        for (size_t j = 0; j < produced; ++j)
            crc = btpcrcbyte(crc, btpv2byte(dst, global + j, width, stride));
        global += produced;
    }
    (void)chunk; (void)size;
    return (~crc == btpczytaju32le(file + 60)) ? BTPOK : BTPBLADV2;
}

/*
 * Audyt: grafika.cpp (PostawPiksel, wczytaj_tapete_z_dysku) oraz
 * skladacz_obrazu.h/.cpp (SKLADACZ_MASKA_KOLORU_RGB, zloz_snapshot_region).
 * Natywny kolor XRGB ma R w bitach 23..16, G w 15..8, B w 7..0.
 * Gorny bajt jest markerem widocznosci, nie kanalem mieszania alfa.
 * Zachowujemy A: A=255 daje 0xFFRRGGBB, w tym widoczna czern 0xFF000000.
 * Zgodnie z regula DOKLADNEGO zera tylko A=R=G=B=0 jest przezroczyste.
 * A=0 przy niezerowym RGB nadal oznacza widoczny kolor; brak alpha blending.
 */
uint32_t btpczytajkolor(const uint8_t* dane) noexcept {
    const uint32_t b = dane[0];
    const uint32_t g = dane[1];
    const uint32_t r = dane[2];
    const uint32_t a = dane[3];
    return (a << 24U) | (r << 16U) | (g << 8U) | b;
}

// Wspolny przebieg walidacji i rysowania; geometria i bufory juz sprawdzone.
// Pierwszy przebieg bada tylko pakiety, bez petli po pikselach i bez zapisu.
BtpWynik btpdekodujrle(
    const uint8_t* danepliku,
    size_t rozmiarpliku,
    uint32_t* framebuffer,
    uint32_t szerokosc,
    size_t liczbapikseli,
    size_t stridepikseli,
    bool rysuj
) noexcept {
    size_t pozycja = sizeof(BtpNaglowek);
    size_t pozostalo = liczbapikseli;
    size_t wiersz = 0;
    uint32_t x = 0;

    while (pozostalo != 0) {
        // pozycja <= rozmiarpliku: nie dodajemy 5 przed kontrola zakresu.
        if (rozmiarpliku - pozycja < BTPROZMIARPAKIETU)
            return BTPBLADRLE;

        const uint32_t liczba = danepliku[pozycja];
        if (liczba == 0 || static_cast<size_t>(liczba) > pozostalo)
            return BTPBLADRLE;

        if (rysuj) {
            const uint32_t kolor = btpczytajkolor(danepliku + pozycja + 1U);
            for (uint32_t i = 0; i < liczba; ++i) {
                // Przezroczystosc zachowuje dotychczasowy piksel celu.
                if (kolor != 0)
                    framebuffer[wiersz + x] = kolor;

                ++x;
                if (x == szerokosc) {
                    x = 0;
                    wiersz += stridepikseli;
                }
            }
        }

        pozostalo -= liczba;
        pozycja += BTPROZMIARPAKIETU;
    }

    // BTP1 nie definiuje stopki ani paddingu: odrzucamy takze dodatkowe pakiety.
    return pozycja == rozmiarpliku ? BTPOK : BTPBLADRLE;
}

} // namespace

BtpWynik btpnaglowek(const uint8_t* danepliku, size_t rozmiarpliku,
                       BtpInfo* info) noexcept {
    if (!danepliku || !info) return BTPBLADARGUMENTU;
    if (rozmiarpliku >= 4 && danepliku[0] == 'B' && danepliku[1] == 'T' &&
        danepliku[2] == 'P' && danepliku[3] == '2') {
        if (rozmiarpliku < 72) return BTPBLADZAMALYPLIK;
        const uintptr_t address = reinterpret_cast<uintptr_t>(danepliku);
        if (rozmiarpliku - 1 > UINTPTR_MAX - address) return BTPBLADPRZEPELNIENIA;
        const uint32_t w = btpczytaju32le(danepliku + 8), h = btpczytaju32le(danepliku + 12);
        if (!w || !h || w > BTPMAKSWYMIAR || h > BTPMAKSWYMIAR) return BTPBLADWYMIAROW;
        if (btpczytaju16le(danepliku + 4) != 2 || btpczytaju16le(danepliku + 6) != 72 ||
            danepliku[16] != 1 || danepliku[17] != 1 || btpczytaju64le(danepliku + 20) != static_cast<uint64_t>(w) * h * 4)
            return BTPBLADV2;
        *info = BtpInfo{w, h, 2};
        return BTPOK;
    }
    if (rozmiarpliku < sizeof(BtpNaglowek))
        return BTPBLADZAMALYPLIK;

    const uintptr_t adrespliku = reinterpret_cast<uintptr_t>(danepliku);
    if (rozmiarpliku - 1U > UINTPTR_MAX - adrespliku)
        return BTPBLADPRZEPELNIENIA;

    if (danepliku[0] != 'B' || danepliku[1] != 'T' ||
        danepliku[2] != 'P' || danepliku[3] != '1')
        return BTPBLADMAGIC;

    const uint32_t szerokosc = btpczytaju32le(danepliku + 4U);
    const uint32_t wysokosc = btpczytaju32le(danepliku + 8U);
    if (szerokosc == 0 || wysokosc == 0)
        return BTPBLADWYMIAROW;
    if (danepliku[12] != 32U)
        return BTPBLADBPP;
    const uint8_t kompresja = danepliku[13];
    if (kompresja != 0U && kompresja != 1U)
        return BTPBLADKOMPRESJI;

    if (static_cast<size_t>(szerokosc) > SIZE_MAX / wysokosc)
        return BTPBLADPRZEPELNIENIA;
    const size_t liczbapikseli = static_cast<size_t>(szerokosc) * wysokosc;
    if (liczbapikseli > SIZE_MAX / sizeof(uint32_t))
        return BTPBLADPRZEPELNIENIA;

    if (szerokosc > BTPMAKSWYMIAR || wysokosc > BTPMAKSWYMIAR)
        return BTPBLADWYMIAROW;
    *info = BtpInfo{szerokosc, wysokosc, kompresja};
    return BTPOK;
}

BtpWynik btpsprawdzplik(const uint8_t* danepliku, size_t rozmiarpliku, BtpInfo* info) noexcept {
    if (danepliku && rozmiarpliku >= 4 && danepliku[0] == 'B' && danepliku[1] == 'T' &&
        danepliku[2] == 'P' && danepliku[3] == '2') {
        BtpInfo h{};
        const BtpWynik result = btpnaglowek(danepliku, rozmiarpliku, &h);
        if (result != BTPOK) return result;
        if (!info) return BTPBLADARGUMENTU;
        return btpv2validate(danepliku, rozmiarpliku, info);
    }
    BtpInfo h{};
    const BtpWynik naglowek = btpnaglowek(danepliku, rozmiarpliku, &h);
    if (naglowek != BTPOK) return naglowek;
    if (!info) return BTPBLADARGUMENTU;
    const uint32_t szerokosc=h.szerokosc, wysokosc=h.wysokosc;
    const uint8_t kompresja=h.kompresja;
    const size_t liczbapikseli=static_cast<size_t>(szerokosc)*wysokosc;
    const size_t payload = rozmiarpliku - sizeof(BtpNaglowek);
    if (kompresja == 0U) {
        if (payload != liczbapikseli * sizeof(uint32_t)) return BTPBLADRAW;
    } else {
        if (payload % BTPROZMIARPAKIETU || payload / BTPROZMIARPAKIETU > liczbapikseli)
            return BTPBLADRLE;
        const BtpWynik wynik = btpdekodujrle(danepliku, rozmiarpliku, nullptr,
            szerokosc, liczbapikseli, 0, false);
        if (wynik != BTPOK) return wynik;
    }
    *info = BtpInfo{szerokosc, wysokosc, kompresja};
    return BTPOK;
}

/*
 * Rysuje w (0, 0), bez skalowania, obcinania i czyszczenia reszty celu.
 * stridepikseli: odleglosc poczatkow wierszy w uint32_t, NIE w bajtach.
 * Dla GOP / HAL grafiki nalezy podac pitch / sizeof(uint32_t), po kontroli
 * podzielnosci pitch przez 4. Nigdy nie zakladamy pitch == szerokosc * 4.
 * rozmiarfb: rzeczywista dostepna pojemnosc celu W BAJTACH, obejmujaca
 * co najmniej stridepikseli * wysokoscfb * sizeof(uint32_t), wraz z paddingiem.
 *
 * Kontrakt wywolujacego: oba bufory sa poprawnie zmapowane na podane rozmiary;
 * cel jest zapisywalnym buforem uint32_t (RAM / backbuffer, nie MMIO).
 * Dane pliku musza pozostac niezmienne przez CALE wywolanie, a dostep do
 * buforow musi byc synchronizowany przez wywolujacego. Funkcja nie sprawdza
 * mapowania stron; nie jest interfejsem przyjmujacym wskazniki z Ring 3.
 * Nakladanie zakresow jest odrzucane, aby zapis nie mogl zmienic wejscia.
 *
 * Pelny rozmiar RAW / wszystkie pakiety RLE sprawdzamy przed zapisem.
 * BTP2 sprawdza strukture i CRC payloadu przed zapisem, ale CRC rozkodowanych
 * pikseli dopiero po rozkodowaniu. Wywolujacy musi dekodowac do bufora
 * roboczego i publikowac obraz tylko po BTPOK. Drugi przebieg RLE nadal
 * kontroluje granice pakietow i liczbe pikseli.
 * Bez alokacji, bufora dekompresji, rekurencji i zaleznosci od runtime.
 */
BtpWynik btpwczytajirysuj(
    const uint8_t* danepliku,
    size_t rozmiarpliku,
    uint32_t* framebuffer,
    uint32_t szerokoscfb,
    uint32_t wysokoscfb,
    uint32_t stridepikseli,
    size_t rozmiarfb
) noexcept {
    if (!danepliku || !framebuffer)
        return BTPBLADARGUMENTU;
    if (reinterpret_cast<uintptr_t>(framebuffer) % alignof(uint32_t) != 0)
        return BTPBLADARGUMENTU;
    BtpInfo info{};
    const BtpWynik sprawdzony = btpnaglowek(danepliku, rozmiarpliku, &info);
    if (sprawdzony != BTPOK) return sprawdzony;
    if (szerokoscfb == 0 || wysokoscfb == 0) return BTPBLADWYMIAROW;
    const uint32_t szerokosc = info.szerokosc, wysokosc = info.wysokosc;
    const uint8_t kompresja = info.kompresja;
    const size_t liczbapikseli = static_cast<size_t>(szerokosc) * wysokosc;
    const uintptr_t adrespliku = reinterpret_cast<uintptr_t>(danepliku);

    if (szerokosc > BTPMAKSWYMIAR || wysokosc > BTPMAKSWYMIAR ||
        szerokosc > szerokoscfb || wysokosc > wysokoscfb)
        return BTPBLADWYMIAROW;
    if (stridepikseli < szerokoscfb)
        return BTPBLADBUFORA;

    if (static_cast<size_t>(stridepikseli) > SIZE_MAX / wysokoscfb)
        return BTPBLADPRZEPELNIENIA;
    const size_t pikselefb = static_cast<size_t>(stridepikseli) * wysokoscfb;
    if (pikselefb > SIZE_MAX / sizeof(uint32_t))
        return BTPBLADPRZEPELNIENIA;
    const size_t potrzebnebajty = pikselefb * sizeof(uint32_t);
    if (rozmiarfb < potrzebnebajty)
        return BTPBLADBUFORA;

    const uintptr_t adresfb = reinterpret_cast<uintptr_t>(framebuffer);
    if (potrzebnebajty - 1U > UINTPTR_MAX - adresfb)
        return BTPBLADPRZEPELNIENIA;
    const uintptr_t koniecpliku = adrespliku + (rozmiarpliku - 1U);
    const uintptr_t koniecfb = adresfb + (potrzebnebajty - 1U);
    if (adrespliku <= koniecfb && adresfb <= koniecpliku)
        return BTPBLADARGUMENTU;

    const BtpWynik payload = btpsprawdzplik(danepliku, rozmiarpliku, &info);
    if (payload != BTPOK) return payload;
    if (kompresja == 2U)
        return btpv2decode(danepliku, framebuffer, szerokosc, stridepikseli, rozmiarpliku);
    const size_t rozmiardanych = rozmiarpliku - sizeof(BtpNaglowek);
    if (kompresja == 0U) {
        // Mnozenie zostalo sprawdzone powyzej. Wymagamy dokladnego rozmiaru
        // przed pierwszym odczytem payloadu i przed jakimkolwiek zapisem.
        if (rozmiardanych != liczbapikseli * sizeof(uint32_t))
            return BTPBLADRAW;

        size_t pozycja = sizeof(BtpNaglowek);
        size_t wiersz = 0;
        for (uint32_t y = 0; y < wysokosc; ++y) {
            for (uint32_t x = 0; x < szerokosc; ++x) {
                const uint32_t kolor = btpczytajkolor(danepliku + pozycja);
                if (kolor != 0)
                    framebuffer[wiersz + x] = kolor;
                pozycja += sizeof(uint32_t);
            }
            wiersz += stridepikseli;
        }
        return BTPOK;
    }

    return btpdekodujrle(
        danepliku, rozmiarpliku, framebuffer, szerokosc,
        liczbapikseli, stridepikseli, true);
}
