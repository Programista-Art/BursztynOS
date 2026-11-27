#pragma once
/*
 * Ochrona drzew systemowych przed zapisem przez zwykle procesy (PZB >= UZYTKOWNIK). Czyste funkcje, testowane na hoscie.
 * Chronione: /system, /jadro, /programy, /gry, pojedyncze *.bur tuz pod "/" oraz /aktualizacje (stan slotow
 * rozruchowych, lista i paczki aktualizacji: zwykla aplikacja nie moze ich podmienic ani uszkodzic - rollback/blokada
 * aktualizacji). Sciezki niekanoniczne ("..", "//", ".") sa odrzucane, wiec aliasy nie obchodza ochrony.
 * /sloty jest fizycznym miejscem /programy i /system (PSF tlumaczy je na /sloty/{a,b}/...), a /sterowniki
 * jest chroniony juz przy usuwaniu drzew - bez nich zapis przez alias /sloty/a/programy/... omijal ochrone.
 */
#include <stddef.h>

namespace OchronaSciezek {

inline bool segment_prefiksu(const char* sciezka, const char* prefiks) {
    if (!sciezka || !prefiks) return false;
    size_t i = 0;
    while (prefiks[i] != '\0') {
        if (sciezka[i] != prefiks[i]) return false;
        ++i;
    }
    return sciezka[i] == '\0' || sciezka[i] == '/';          // "/system2" nie jest dzieckiem "/system"
}

inline bool plik_bur_w_korzeniu(const char* sciezka) {
    if (!sciezka || sciezka[0] != '/') return false;
    size_t n = 1;
    while (sciezka[n] != '\0') { if (sciezka[n] == '/') return false; ++n; }
    return n > 5 && sciezka[n - 4] == '.' && sciezka[n - 3] == 'b' && sciezka[n - 2] == 'u' && sciezka[n - 1] == 'r';
}

inline bool sciezka_kanoniczna(const char* sciezka) {
    if (!sciezka || sciezka[0] != '/') return false;
    if (sciezka[1] == '\0') return true;
    size_t i = 1;
    while (sciezka[i] != '\0') {
        size_t start = i;
        while (sciezka[i] != '\0' && sciezka[i] != '/') ++i;
        const size_t len = i - start;
        if (len == 0) return false;
        if (len == 1 && sciezka[start] == '.') return false;
        if (len == 2 && sciezka[start] == '.' && sciezka[start + 1] == '.') return false;
        if (sciezka[i] == '/') ++i;
    }
    return true;
}

inline bool sciezka_chroniona(const char* sciezka) {
    return segment_prefiksu(sciezka, "/system") || segment_prefiksu(sciezka, "/jadro") ||
           segment_prefiksu(sciezka, "/programy") || segment_prefiksu(sciezka, "/gry") ||
           segment_prefiksu(sciezka, "/aktualizacje") || segment_prefiksu(sciezka, "/sloty") ||
           segment_prefiksu(sciezka, "/sterowniki") || plik_bur_w_korzeniu(sciezka);
}

} // namespace OchronaSciezek
