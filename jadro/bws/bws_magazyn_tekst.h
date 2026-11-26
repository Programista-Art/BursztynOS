#pragma once
// Polskie opisy BWS 111 dla aplikacji (Eksplorator, Grajek). Czyste funkcje, bez alokacji, testowane na hoscie.
#include "bws_magazyn.h"

namespace MagazynTekst {

inline size_t dopisz(char* cel, size_t poj, size_t p, const char* t) {
    while (t && *t && p + 1 < poj) cel[p++] = *t++;
    if (poj) cel[p < poj ? p : poj - 1] = '\0';
    return p;
}
// 1024-krotne jednostki, jedna cyfra po przecinku (np. "1,5 GB"). Bez liczb zmiennoprzecinkowych.
inline size_t dopisz_rozmiar(char* cel, size_t poj, size_t p, uint64_t b) {
    static const char* const J[] = {"B", "KB", "MB", "GB", "TB"};
    uint32_t j = 0;
    uint64_t calosc = b, dziesiate = 0;
    while (calosc >= 1024 && j < 4) { dziesiate = ((calosc % 1024) * 10) / 1024; calosc /= 1024; ++j; }
    char t[24]; int n = 0;
    uint64_t v = calosc;
    do { t[n++] = char('0' + v % 10); v /= 10; } while (v && n < 20);
    while (n && p + 1 < poj) cel[p++] = t[--n];
    if (j) { p = dopisz(cel, poj, p, ","); if (p + 1 < poj) cel[p++] = char('0' + dziesiate); }
    p = dopisz(cel, poj, p, " ");
    return dopisz(cel, poj, p, J[j]);
}
inline bool trwaly(const BwsMagazynV1& m) { return (m.flagi & BWS_MAG_GOTOWY) && (m.flagi & BWS_MAG_TRWALY); }
// "Wolne: 1,2 GB z 2,0 GB (dysk trwały)" albo "... (RAM: pliki znikną po restarcie)".
inline void opis_wolnego(char* cel, size_t poj, const BwsMagazynV1& m) {
    if (!poj) return;
    cel[0] = '\0';
    if (!(m.flagi & BWS_MAG_GOTOWY)) { (void)dopisz(cel, poj, 0, "System plików niedostępny."); return; }
    size_t p = dopisz(cel, poj, 0, "Wolne: ");
    p = dopisz_rozmiar(cel, poj, p, m.wolne_bajty);
    p = dopisz(cel, poj, p, " z ");
    p = dopisz_rozmiar(cel, poj, p, m.pojemnosc_bajty);
    (void)dopisz(cel, poj, p, trwaly(m) ? " (dysk trwały)" : " (RAM: pliki znikną po restarcie)");
}

} // namespace MagazynTekst
