/*
 * BTP - Bursztynowa TaPeta, rozszerzenie .btp. Samodzielny dekoder BTP1.
 * Etap 1: bez integracji z jadrem, systemem plikow lub wyborem tapety.
 *
 * Format pliku (little-endian):
 *   0: magic "BTP1" (4 bajty), 4: szerokosc u32, 8: wysokosc u32,
 *   12: bpp u8 (32), 13: kompresja u8 (1 = RLE).
 * Kazdy pakiet RLE ma dokladnie 5 bajtow: liczba (1..255), B, G, R, A.
 * Wiersze biegna od gory, piksele od lewej. Pakiet moze przecinac wiersz.
 */

#include <stdint.h>
#include <stddef.h>

struct BtpNaglowek {
    uint8_t magic[4];
    uint32_t szerokosc;
    uint32_t wysokosc;
    uint8_t bpp;
    uint8_t kompresja;
} __attribute__((packed));

static_assert(sizeof(BtpNaglowek) == 14, "BTP1: naglowek ma 14 bajtow");
static_assert(alignof(BtpNaglowek) == 1, "BTP1: naglowek bez wyrownania");
static_assert(offsetof(BtpNaglowek, magic) == 0, "BTP1: offset magic");
static_assert(offsetof(BtpNaglowek, szerokosc) == 4, "BTP1: offset szerokosci");
static_assert(offsetof(BtpNaglowek, wysokosc) == 8, "BTP1: offset wysokosci");
static_assert(offsetof(BtpNaglowek, bpp) == 12, "BTP1: offset bpp");
static_assert(offsetof(BtpNaglowek, kompresja) == 13, "BTP1: offset kompresji");

enum BtpWynik : int32_t {
    BTPOK = 0,
    BTPBLADARGUMENTU = -1,       // Null, zle wyrownanie lub nakladanie buforow.
    BTPBLADZAMALYPLIK = -2,     // Niepelny naglowek.
    BTPBLADMAGIC = -3,
    BTPBLADWYMIAROW = -4,       // Zero, limit lub obraz wiekszy od celu.
    BTPBLADBPP = -5,
    BTPBLADKOMPRESJI = -6,      // Takze tryb surowy (0): nieobslugiwany.
    BTPBLADRLE = -7,            // Zly pakiet, niepelny obraz lub nadmiar danych.
    BTPBLADPRZEPELNIENIA = -8,  // Rozmiar lub zakres adresow niereprezentowalny.
    BTPBLADBUFORA = -9          // Za maly stride lub pojemnosc celu.
};

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
 * Wszystkie pakiety sa sprawdzane przed pierwszym zapisem. Przy zachowaniu
 * kontraktu blad pozostawia cel bez zmian, a BTPOK oznacza caly poprawny obraz.
 * Drugi przebieg nadal kontroluje granice pakietow i liczbe pikseli.
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
    if (szerokosc == 0 || wysokosc == 0 ||
        szerokoscfb == 0 || wysokoscfb == 0)
        return BTPBLADWYMIAROW;
    if (danepliku[12] != 32U)
        return BTPBLADBPP;
    if (danepliku[13] != 1U)
        return BTPBLADKOMPRESJI;

    if (static_cast<size_t>(szerokosc) > SIZE_MAX / wysokosc)
        return BTPBLADPRZEPELNIENIA;
    const size_t liczbapikseli = static_cast<size_t>(szerokosc) * wysokosc;
    if (liczbapikseli > SIZE_MAX / sizeof(uint32_t))
        return BTPBLADPRZEPELNIENIA;

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

    // Kazdy pakiet daje co najmniej jeden piksel; liczba pakietow jest
    // ograniczona liczba pikseli, a ta limitem wymiarow. Brak dlugich skanow
    // nadmiarowego payloadu, nawet gdy wywolujacy dostarczy olbrzymi plik.
    const size_t rozmiardanych = rozmiarpliku - sizeof(BtpNaglowek);
    if (rozmiardanych % BTPROZMIARPAKIETU != 0 ||
        rozmiardanych / BTPROZMIARPAKIETU > liczbapikseli)
        return BTPBLADRLE;

    const BtpWynik wynik = btpdekodujrle(
        danepliku, rozmiarpliku, framebuffer, szerokosc,
        liczbapikseli, stridepikseli, false);
    if (wynik != BTPOK)
        return wynik;

    return btpdekodujrle(
        danepliku, rozmiarpliku, framebuffer, szerokosc,
        liczbapikseli, stridepikseli, true);
}
