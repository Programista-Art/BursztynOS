#pragma once

#include <stdint.h>
#include <stddef.h>

namespace BursztynUI {

// GCC/Clang zapewniaja 128-bitowe typy calkowite na docelowym x86_64.
// Uzywamy ich jawnie do geometrii duzych zakresow bez przepelnien.
__extension__ typedef __int128 SzerokaLiczba;
__extension__ typedef unsigned __int128 SzerokaLiczbaBezZnaku;

struct Punkt {
    int x{0};
    int y{0};

    constexpr Punkt() = default;
    constexpr Punkt(int _x, int _y) : x(_x), y(_y) {}

    constexpr Punkt operator+(const Punkt& o) const { return {x + o.x, y + o.y}; }
    constexpr Punkt operator-(const Punkt& o) const { return {x - o.x, y - o.y}; }
    constexpr bool operator==(const Punkt& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Punkt& o) const { return !(*this == o); }
};

struct Rozmiar {
    static constexpr int NIEOKRESLONY = -1;

    int szerokosc{0};
    int wysokosc{0};

    constexpr Rozmiar() = default;
    constexpr Rozmiar(int sz, int wys)
        : szerokosc(sz), wysokosc(wys) {}

    static constexpr Rozmiar Nieokreslony() { return Rozmiar(NIEOKRESLONY, NIEOKRESLONY); }
    static constexpr Rozmiar Zero() { return Rozmiar(0, 0); }

    constexpr bool czy_okreslona_szerokosc() const { return szerokosc >= 0; }
    constexpr bool czy_okreslona_wysokosc() const { return wysokosc >= 0; }
    constexpr bool czy_nieokreslona_szerokosc() const { return szerokosc < 0; }
    constexpr bool czy_nieokreslona_wysokosc() const { return wysokosc < 0; }
    constexpr bool czy_nieokreslony() const { return szerokosc < 0 || wysokosc < 0; }
    constexpr bool czy_calkowicie_nieokreslony() const { return szerokosc < 0 && wysokosc < 0; }

    constexpr Rozmiar bezpieczny_fizyczny() const {
        return Rozmiar(szerokosc < 0 ? 0 : szerokosc, wysokosc < 0 ? 0 : wysokosc);
    }

    constexpr bool operator==(const Rozmiar& o) const {
        return szerokosc == o.szerokosc && wysokosc == o.wysokosc;
    }
    constexpr bool operator!=(const Rozmiar& o) const { return !(*this == o); }
};

struct Marginesy {
    int lewy{0};
    int gorny{0};
    int prawy{0};
    int dolny{0};

    constexpr Marginesy() = default;
    constexpr Marginesy(int wszystkie)
        : lewy(wszystkie), gorny(wszystkie), prawy(wszystkie), dolny(wszystkie) {}
    constexpr Marginesy(int poziome, int pionowe)
        : lewy(poziome), gorny(pionowe), prawy(poziome), dolny(pionowe) {}
    constexpr Marginesy(int l, int g, int p, int d)
        : lewy(l), gorny(g), prawy(p), dolny(d) {}

    constexpr int poziomo() const { return lewy + prawy; }
    constexpr int pionowo() const { return gorny + dolny; }
};

using Grubosc = Marginesy;

struct Prostokat {
    int x{0};
    int y{0};
    int szerokosc{0};
    int wysokosc{0};

    constexpr Prostokat() = default;
    constexpr Prostokat(int _x, int _y, int _sz, int _wys)
        : x(_x), y(_y), szerokosc(_sz < 0 ? 0 : _sz), wysokosc(_wys < 0 ? 0 : _wys) {}
    constexpr Prostokat(Punkt pt, Rozmiar rz)
        : x(pt.x), y(pt.y), szerokosc(rz.szerokosc < 0 ? 0 : rz.szerokosc), wysokosc(rz.wysokosc < 0 ? 0 : rz.wysokosc) {}

    constexpr bool operator==(const Prostokat& o) const {
        return x == o.x && y == o.y && szerokosc == o.szerokosc && wysokosc == o.wysokosc;
    }
    constexpr bool operator!=(const Prostokat& o) const { return !(*this == o); }

    constexpr int prawy() const { return x + szerokosc; }
    constexpr int dolny() const { return y + wysokosc; }
    constexpr Punkt pozycja() const { return {x, y}; }
    constexpr Rozmiar rozmiar() const { return {szerokosc, wysokosc}; }

    constexpr bool czy_pusty() const { return szerokosc <= 0 || wysokosc <= 0; }

    constexpr bool zawiera(int px, int py) const {
        return px >= x && px < x + szerokosc && py >= y && py < y + wysokosc;
    }
    constexpr bool zawiera(Punkt pt) const { return zawiera(pt.x, pt.y); }

    constexpr bool przecina(const Prostokat& o) const {
        return !(x + szerokosc <= o.x || o.x + o.szerokosc <= x ||
                 y + wysokosc <= o.y || o.y + o.wysokosc <= y);
    }

    Prostokat przeciecie(const Prostokat& o) const {
        if (!przecina(o)) return {0, 0, 0, 0};
        int nx = x > o.x ? x : o.x;
        int ny = y > o.y ? y : o.y;
        int nr = (x + szerokosc) < (o.x + o.szerokosc) ? (x + szerokosc) : (o.x + o.szerokosc);
        int nb = (y + wysokosc) < (o.y + o.wysokosc) ? (y + wysokosc) : (o.y + o.wysokosc);
        return {nx, ny, nr - nx, nb - ny};
    }

    Prostokat suma(const Prostokat& o) const {
        if (czy_pusty()) return o;
        if (o.czy_pusty()) return *this;
        int nx = x < o.x ? x : o.x;
        int ny = y < o.y ? y : o.y;
        int nr = (x + szerokosc) > (o.x + o.szerokosc) ? (x + szerokosc) : (o.x + o.szerokosc);
        int nb = (y + wysokosc) > (o.y + o.wysokosc) ? (y + wysokosc) : (o.y + o.wysokosc);
        return {nx, ny, nr - nx, nb - ny};
    }

    Prostokat zmniejszony(Marginesy m) const {
        return {x + m.lewy, y + m.gorny,
                szerokosc - m.poziomo() < 0 ? 0 : szerokosc - m.poziomo(),
                wysokosc - m.pionowo() < 0 ? 0 : wysokosc - m.pionowo()};
    }

    Prostokat powiekszony(Marginesy m) const {
        return {x - m.lewy, y - m.gorny,
                szerokosc + m.poziomo(),
                wysokosc + m.pionowo()};
    }
};

enum class WyrownaniePoziome {
    Poczatek,
    Srodek,
    Koniec,
    Rozciagnij
};

enum class WyrownaniePionowe {
    Poczatek,
    Srodek,
    Koniec,
    Rozciagnij
};

enum class Orientacja {
    Pozioma, // Wiersz
    Pionowa  // Kolumna
};

enum class StanWyboru {
    Niezaznaczone = 0,
    Zaznaczone = 1,
    Nieokreslone = 2
};

template <typename T>
constexpr T przytnij(T wartosc, T min_wart, T max_wart) {
    return (wartosc < min_wart) ? min_wart : ((wartosc > max_wart) ? max_wart : wartosc);
}

} // namespace BursztynUI
