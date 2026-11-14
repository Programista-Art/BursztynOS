// Narzedzie testowe hosta: podglad_jpeg_cli plik.jpg wyjscie.ppm [budzet_MiB] [maks_px]
#include "../obrazy/podglad_jpeg.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
static uint32_t czytaj(void* ctx, uint64_t off, uint8_t* dst, uint32_t n) {
    FILE* f = static_cast<FILE*>(ctx);
    if (fseek(f, static_cast<long>(off), SEEK_SET)) return 0;
    return static_cast<uint32_t>(fread(dst, 1, n, f));
}
static size_t g_aktualne = 0, g_szczyt = 0;
static void* alok(size_t n) { void* p = calloc(1, n + 16); if (!p) return nullptr; *static_cast<size_t*>(p) = n; g_aktualne += n; if (g_aktualne > g_szczyt) g_szczyt = g_aktualne; return static_cast<char*>(p) + 16; }
static void zwol(void* p) { if (!p) return; char* b = static_cast<char*>(p) - 16; g_aktualne -= *reinterpret_cast<size_t*>(b); free(b); }
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f);
    PodgladZrodlo z{czytaj, f, static_cast<uint64_t>(sz)};
    PodgladOpcje o{ (argc > 3 ? strtoull(argv[3], 0, 10) : 48) * 1024ULL * 1024ULL, argc > 4 ? static_cast<uint32_t>(strtoul(argv[4], 0, 10)) : 4200000U, 8192, 64ULL * 1024 * 1024 };
    BladObrazu b;
    timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    PodgladJpeg* d = podglad_jpeg_otworz(z, o, alok, zwol, &b);
    if (!d) { printf("BLAD otworz %d\n", static_cast<int>(b)); return 3; }
    PodgladStan s; while ((s = podglad_jpeg_krok(d, 8)) == PodgladStan::Dalej) {}
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (s != PodgladStan::Gotowy) { printf("BLAD krok %d\n", static_cast<int>(podglad_jpeg_blad(d))); podglad_jpeg_zamknij(d, false); return 4; }
    const PodgladWynik& w = podglad_jpeg_wynik(d);
    printf("OK %ux%u -> %ux%u skala=%u prog=%d szczyt=%llu ms=%ld\n", w.szer_zrodla, w.wys_zrodla, w.szer, w.wys, w.skala, w.progresywny,
           (unsigned long long)w.szczyt_bajtow, (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
    FILE* out = fopen(argv[2], "wb");
    fprintf(out, "P6\n%u %u\n255\n", w.szer, w.wys);
    for (uint32_t i = 0; i < w.szer * w.wys; ++i) { uint32_t p = w.bgra[i]; fputc((p >> 16) & 255, out); fputc((p >> 8) & 255, out); fputc(p & 255, out); }
    fclose(out);
    podglad_jpeg_zamknij(d, false);
    fclose(f);
    if (g_aktualne) { printf("WYCIEK %zu\n", g_aktualne); return 5; }
    return 0;
}
