#include "utf8.h"

namespace brb {

namespace {

bool kontynuacja(uint8_t b) { return (b & 0xC0u) == 0x80u; }

// Dekoduje jeden znak. Zwraca liczbe zuzytych bajtow (>= 1). *poprawny = false dla bledu.
size_t dekoduj(const uint8_t* s, size_t n, uint32_t* znak, bool* poprawny) {
    const uint8_t b0 = s[0];
    *poprawny = false;
    *znak = ZNAK_ZASTEPCZY;
    if (b0 < 0x80u) { *znak = b0; *poprawny = true; return 1; }
    size_t potrzebne;
    uint32_t wartosc;
    uint32_t minimum;
    if (b0 >= 0xC2u && b0 <= 0xDFu)      { potrzebne = 2; wartosc = b0 & 0x1Fu; minimum = 0x80u; }
    else if (b0 >= 0xE0u && b0 <= 0xEFu) { potrzebne = 3; wartosc = b0 & 0x0Fu; minimum = 0x800u; }
    else if (b0 >= 0xF0u && b0 <= 0xF4u) { potrzebne = 4; wartosc = b0 & 0x07u; minimum = 0x10000u; }
    else return 1;  // 0x80..0xC1 (kontynuacja albo overlong 2-bajtowy) i 0xF5..0xFF
    size_t i = 1;
    for (; i < potrzebne; ++i) {
        if (i >= n || !kontynuacja(s[i])) return i;  // urwana sekwencja: pomin poprawny prefiks
        wartosc = (wartosc << 6) | (s[i] & 0x3Fu);
    }
    if (wartosc < minimum || wartosc > 0x10FFFFu || (wartosc >= 0xD800u && wartosc <= 0xDFFFu)) return i;
    *znak = wartosc;
    *poprawny = true;
    return i;
}

}  // namespace

size_t dlugosc_tekstu(const char* tekst, size_t maksimum) {
    if (!tekst) return 0;
    size_t n = 0;
    while (n < maksimum && tekst[n] != '\0') ++n;
    return n;
}

WynikUtf8 utf8_na_ucs2(const char* zrodlo, size_t dlugosc, char16_t* cel, size_t pojemnosc) {
    WynikUtf8 w{};
    const bool zrodlo_puste = !zrodlo || dlugosc == 0 || zrodlo[0] == '\0';
    if (!cel || pojemnosc == 0) {
        w.obciete = !zrodlo_puste;
        return w;
    }
    cel[0] = 0;
    if (zrodlo_puste) return w;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(zrodlo);
    size_t poz = 0;
    // Pojemnosc uzytkowa: pojemnosc - 1 (miejsce na NUL). Petla jest ograniczona przez dlugosc zrodla.
    while (poz < dlugosc && s[poz] != 0) {
        uint32_t znak;
        bool poprawny;
        // Bajt 0 konczy tekst takze wewnatrz sekwencji: ograniczamy n do pierwszego zera.
        size_t dostepne = dlugosc - poz;
        for (size_t k = 1; k < dostepne && k < 4; ++k) {
            if (s[poz + k] == 0) { dostepne = k; break; }
        }
        const size_t zuzyte = dekoduj(s + poz, dostepne, &znak, &poprawny);
        char16_t jednostki[2];
        size_t ile = 1;
        if (!poprawny) {
            ++w.bledne;
            jednostki[0] = ZNAK_ZASTEPCZY;
        } else if (znak > 0xFFFFu) {
            ++w.poza_bmp;
            jednostki[0] = ZNAK_ZASTEPCZY;
        } else if (znak == u'\n') {
            jednostki[0] = u'\r';
            jednostki[1] = u'\n';
            ile = 2;
        } else if ((znak < 0x20u && znak != u'\t') || znak == 0x7Fu || (znak >= 0x80u && znak < 0xA0u)) {
            ++w.sterujace;
            jednostki[0] = ZNAK_ZASTEPCZY;
        } else {
            jednostki[0] = static_cast<char16_t>(znak);
        }
        if (w.zapisane + ile > pojemnosc - 1) {  // brak miejsca na caly znak + NUL
            w.obciete = true;
            break;
        }
        for (size_t k = 0; k < ile; ++k) cel[w.zapisane++] = jednostki[k];
        poz += zuzyte;
    }
    cel[w.zapisane] = 0;
    return w;
}

size_t transliteruj_ascii(char16_t* tekst, size_t pojemnosc) {
    if (!tekst) return 0;
    struct Para { char16_t z; char16_t na; };
    static constexpr Para tablica[] = {
        {u'ą', u'a'}, {u'ć', u'c'}, {u'ę', u'e'}, {u'ł', u'l'}, {u'ń', u'n'}, {u'ó', u'o'}, {u'ś', u's'},
        {u'ź', u'z'}, {u'ż', u'z'}, {u'Ą', u'A'}, {u'Ć', u'C'}, {u'Ę', u'E'}, {u'Ł', u'L'}, {u'Ń', u'N'},
        {u'Ó', u'O'}, {u'Ś', u'S'}, {u'Ź', u'Z'}, {u'Ż', u'Z'},
    };
    size_t zmienione = 0;
    for (size_t i = 0; i < pojemnosc && tekst[i] != 0; ++i) {
        const char16_t c = tekst[i];
        if (c <= 0x7Eu) continue;
        char16_t nowy = ZNAK_ZASTEPCZY;
        for (const Para& p : tablica) {
            if (p.z == c) { nowy = p.na; break; }
        }
        tekst[i] = nowy;
        ++zmienione;
    }
    return zmienione;
}

}  // namespace brb
