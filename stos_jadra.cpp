/*
 * Bursztyn OS - Implementacja Architektury i Ochrony Stosow Jadra Ring 0
 */

#include "stos_jadra.h"
#include "pamiec.h"
#include "pzb.h"

extern "C" uint8_t* tss_pobierz_ist_df_baza();

namespace {

constexpr uint16_t PORT_COM1 = 0x3F8;

static inline void serial_outb(uint16_t port, uint8_t wartosc) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc), "Nd"(port)
        : "memory"
    );
}

void serial_wypisz(const char* tekst) {
    if (!tekst) return;
    for (size_t i = 0; tekst[i] != '\0'; ++i) {
        serial_outb(PORT_COM1, static_cast<uint8_t>(tekst[i]));
    }
}

void uint_do_hex_str(uint64_t wartosc, char* bufor) {
    if (!bufor) return;
    static constexpr char CYFRY[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; --i) {
        bufor[i] = CYFRY[wartosc & 0xFULL];
        wartosc >>= 4;
    }
    bufor[16] = '\0';
}

void uint_do_dec_str(uint64_t wartosc, char* bufor, size_t pojemnosc) {
    if (!bufor || pojemnosc == 0) return;
    if (wartosc == 0) {
        if (pojemnosc > 1) { bufor[0] = '0'; bufor[1] = '\0'; }
        else { bufor[0] = '\0'; }
        return;
    }
    char tmp[24] = {};
    size_t n = 0;
    while (wartosc > 0 && n < sizeof(tmp)) {
        tmp[n++] = static_cast<char>('0' + (wartosc % 10ULL));
        wartosc /= 10ULL;
    }
    size_t out = 0;
    while (n > 0 && out + 1 < pojemnosc) {
        bufor[out++] = tmp[--n];
    }
    bufor[out] = '\0';
}

bool g_stosy_zainicjalizowane = false;
StosMetryki g_metryki[PZB_MAKS_PROCESOW]{};

} // namespace

bool stos_jadra_inicjalizuj() {
    if (g_stosy_zainicjalizowane) return true;

    // Utworzenie galezi PML4 dla stosow jadra przed pierwszym startem procesu Ring 3.
    // Mapujemy tymczasowa ramke pod adresem pierwszego slotu i natychmiast ja odmapowujemy.
    void* pa = ZaalokujRamke();
    if (!pa) {
        serial_wypisz("[STACK] BLAD: Brak ramki dla inicjalizacji galezi PML4 stosow!\n");
        return false;
    }

    const uint64_t test_va = BAZA_STOSOW_JADRA + ROZMIAR_GUARD_PAGE_BAJTY;
    if (!VmmMapujNowaStroneJadra(reinterpret_cast<void*>(test_va), pa)) {
        ZwolnijRamke(pa);
        serial_wypisz("[STACK] BLAD: Nie udalo sie zmapowac galezi PML4 stosow!\n");
        return false;
    }

    void* zwrocone_pa = VmmOdmapujStroneJadra(reinterpret_cast<void*>(test_va));
    if (zwrocone_pa) {
        ZwolnijRamke(zwrocone_pa);
    }

    // Ochrona awaryjnego stosu IST1 dla Double Fault:
    // Odmapowujemy pierwsza strone (4 KiB) stosu awaryjnego, aby stanowila guard page (P=0).
    uint8_t* ist_df_baza = tss_pobierz_ist_df_baza();
    if (ist_df_baza) {
        (void)VmmOdmapujStroneJadra(ist_df_baza);
        *reinterpret_cast<uint64_t*>(ist_df_baza + ROZMIAR_GUARD_PAGE_BAJTY) = KERNEL_STACK_CANARY;
    }

    g_stosy_zainicjalizowane = true;

    // Przydziel chroniony slot stosu dla PID 0 (jadro / watek idle)
    stos_jadra_przydziel(0, nullptr, nullptr);

    serial_wypisz("[STACK] Podsystem chronionych stosow Ring 0 zainicjalizowany.\n");
    return true;
}

bool stos_jadra_przydziel(int pid, uint64_t* out_baza, uint64_t* out_szczyt) {
    if (pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW)) return false;

    if (!g_stosy_zainicjalizowane) {
        if (!stos_jadra_inicjalizuj()) return false;
    }

    const uint64_t slot_va = BAZA_STOSOW_JADRA + static_cast<uint64_t>(pid) * ROZMIAR_SLOTU_PROCESU_BAJTY;
    const uint64_t stack_base = slot_va + ROZMIAR_GUARD_PAGE_BAJTY;
    const uint64_t stack_top = stack_base + ROZMIAR_STOSU_JADRA_BAJTY;

    // Alokujemy 16 ramek fizycznych
    void* ramki[ROZMIAR_STOSU_JADRA_STRONY] = {};
    for (size_t i = 0; i < ROZMIAR_STOSU_JADRA_STRONY; ++i) {
        ramki[i] = ZaalokujRamke();
        if (!ramki[i]) {
            for (size_t j = 0; j < i; ++j) ZwolnijRamke(ramki[j]);
            serial_wypisz("[STACK] BLAD: Brak ramek fizycznych dla stosu jadra!\n");
            return false;
        }
    }

    // Mapujemy 16 stron (64 KiB) jako supervisor RW
    for (size_t i = 0; i < ROZMIAR_STOSU_JADRA_STRONY; ++i) {
        void* va = reinterpret_cast<void*>(stack_base + i * ROZMIAR_STRONY_STOSU);
        if (!VmmMapujNowaStroneJadra(va, ramki[i])) {
            for (size_t j = 0; j < i; ++j) {
                void* old_va = reinterpret_cast<void*>(stack_base + j * ROZMIAR_STRONY_STOSU);
                void* old_pa = VmmOdmapujStroneJadra(old_va);
                if (old_pa) ZwolnijRamke(old_pa);
            }
            for (size_t j = i; j < ROZMIAR_STOSU_JADRA_STRONY; ++j) {
                ZwolnijRamke(ramki[j]);
            }
            serial_wypisz("[STACK] BLAD: Nie udalo sie zmapowac stosu jadra!\n");
            return false;
        }
    }

    // Guard page na slot_va [0x0000..0x0FFF] pozostaje unmapped (P=0)
    // Ustawiamy kanarek na dnie zmapowanego stosu
    *reinterpret_cast<uint64_t*>(stack_base) = KERNEL_STACK_CANARY;

    // Wypelniamy reszte stosu znakiem wodnym 0xCC (poza kanarkiem)
    uint8_t* ptr = reinterpret_cast<uint8_t*>(stack_base + sizeof(uint64_t));
    const size_t bajty_znaku = ROZMIAR_STOSU_JADRA_BAJTY - sizeof(uint64_t);
    for (size_t i = 0; i < bajty_znaku; ++i) {
        ptr[i] = STACK_WATERMARK_BYTE;
    }

    g_metryki[pid].stack_base = stack_base;
    g_metryki[pid].stack_top = stack_top;
    g_metryki[pid].current_rsp = stack_top;
    g_metryki[pid].min_rsp = stack_top;
    g_metryki[pid].max_used = 0;
    g_metryki[pid].remaining_margin = ROZMIAR_STOSU_JADRA_BAJTY;

    if (out_baza) *out_baza = stack_base;
    if (out_szczyt) *out_szczyt = stack_top;
    return true;
}

void stos_jadra_zwolnij(int pid) {
    if (pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW)) return;

    const uint64_t slot_va = BAZA_STOSOW_JADRA + static_cast<uint64_t>(pid) * ROZMIAR_SLOTU_PROCESU_BAJTY;
    const uint64_t stack_base = slot_va + ROZMIAR_GUARD_PAGE_BAJTY;

    if (g_metryki[pid].stack_base != 0) {
        if (!stos_weryfikuj_kanarka(pid)) {
            serial_wypisz("\n[STACK] Wykryto uszkodzenie kanarka stosu jadra podczas zwalniania!\n");
        }
    }

    for (size_t i = 0; i < ROZMIAR_STOSU_JADRA_STRONY; ++i) {
        void* va = reinterpret_cast<void*>(stack_base + i * ROZMIAR_STRONY_STOSU);
        void* pa = VmmOdmapujStroneJadra(va);
        if (pa) {
            ZwolnijRamke(pa);
        }
    }

    g_metryki[pid] = {};
}

bool stos_czy_adres_w_guard_page(uint64_t va, int pid, uint64_t* out_base, uint64_t* out_top) {
    if (pid >= 0 && pid < static_cast<int>(PZB_MAKS_PROCESOW)) {
        const uint64_t slot_va = BAZA_STOSOW_JADRA + static_cast<uint64_t>(pid) * ROZMIAR_SLOTU_PROCESU_BAJTY;
        const uint64_t stack_base = slot_va + ROZMIAR_GUARD_PAGE_BAJTY;
        const uint64_t stack_top = stack_base + ROZMIAR_STOSU_JADRA_BAJTY;
        if (va >= slot_va && va < stack_base) {
            if (out_base) *out_base = stack_base;
            if (out_top) *out_top = stack_top;
            return true;
        }
    }
    for (int p = 0; p < static_cast<int>(PZB_MAKS_PROCESOW); ++p) {
        const uint64_t slot_va = BAZA_STOSOW_JADRA + static_cast<uint64_t>(p) * ROZMIAR_SLOTU_PROCESU_BAJTY;
        const uint64_t stack_base = slot_va + ROZMIAR_GUARD_PAGE_BAJTY;
        const uint64_t stack_top = stack_base + ROZMIAR_STOSU_JADRA_BAJTY;
        if (va >= slot_va && va < stack_base) {
            if (out_base) *out_base = stack_base;
            if (out_top) *out_top = stack_top;
            return true;
        }
    }
    return false;
}

bool stos_weryfikuj_kanarka(int pid) {
    if (pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW)) return true;
    if (g_metryki[pid].stack_base == 0) return true;
    const uint64_t* ptr = reinterpret_cast<const uint64_t*>(g_metryki[pid].stack_base);
    return (*ptr == KERNEL_STACK_CANARY);
}

void stos_probkuj_rsp(int pid, uint64_t rsp) {
    if (pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW)) return;
    if (g_metryki[pid].stack_base == 0) return;
    g_metryki[pid].current_rsp = rsp;
    if (rsp >= g_metryki[pid].stack_base && rsp < g_metryki[pid].min_rsp) {
        g_metryki[pid].min_rsp = rsp;
    }
}

StosMetryki stos_zmierz_zuzycie(int pid) {
    if (pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW) || g_metryki[pid].stack_base == 0) {
        return {};
    }
    const uint64_t base = g_metryki[pid].stack_base;
    const uint64_t top = g_metryki[pid].stack_top;

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(base + sizeof(uint64_t));
    const size_t limit = ROZMIAR_STOSU_JADRA_BAJTY - sizeof(uint64_t);
    uint64_t lowest_written = top;

    for (size_t i = 0; i < limit; ++i) {
        if (ptr[i] != STACK_WATERMARK_BYTE) {
            lowest_written = reinterpret_cast<uint64_t>(&ptr[i]);
            break;
        }
    }

    if (lowest_written < g_metryki[pid].min_rsp) {
        g_metryki[pid].min_rsp = lowest_written;
    }

    if (g_metryki[pid].min_rsp < top) {
        g_metryki[pid].max_used = top - g_metryki[pid].min_rsp;
    } else {
        g_metryki[pid].max_used = 0;
    }

    g_metryki[pid].remaining_margin = (top > base + g_metryki[pid].max_used) ?
        (top - base - g_metryki[pid].max_used) : 0;

    constexpr uint64_t PROG_OSTRZEZENIA = (ROZMIAR_STOSU_JADRA_BAJTY * 3ULL) / 4ULL;
    if (g_metryki[pid].max_used > PROG_OSTRZEZENIA) {
        serial_wypisz("\n[STACK WARNING] Zuzycie stosu jadra dla PID ");
        char buf[16] = {};
        uint_do_dec_str(static_cast<uint64_t>(pid), buf, sizeof(buf));
        serial_wypisz(buf);
        serial_wypisz(" przekroczylo 75%!\n");
    }

    return g_metryki[pid];
}

void stos_raportuj_sciezke(const char* nazwa_sciezki, int pid) {
    if (!nazwa_sciezki || pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW)) return;
    StosMetryki m = stos_zmierz_zuzycie(pid);
    if (m.stack_base == 0) return;

    char buf[32] = {};
    char hex[17] = {};

    serial_wypisz("[STACK METRICS] ");
    serial_wypisz(nazwa_sciezki);
    serial_wypisz(": pid=");
    uint_do_dec_str(static_cast<uint64_t>(pid), buf, sizeof(buf));
    serial_wypisz(buf);

    serial_wypisz(" base=0x");
    uint_do_hex_str(m.stack_base, hex);
    serial_wypisz(hex);

    serial_wypisz(" top=0x");
    uint_do_hex_str(m.stack_top, hex);
    serial_wypisz(hex);

    serial_wypisz(" min_rsp=0x");
    uint_do_hex_str(m.min_rsp, hex);
    serial_wypisz(hex);

    serial_wypisz(" used=");
    uint_do_dec_str(m.max_used, buf, sizeof(buf));
    serial_wypisz(buf);
    serial_wypisz(" B margin=");
    uint_do_dec_str(m.remaining_margin, buf, sizeof(buf));
    serial_wypisz(buf);
    serial_wypisz(" B\n");
}

StosMetryki stos_pobierz_metryki(int pid) {
    if (pid >= 0 && pid < static_cast<int>(PZB_MAKS_PROCESOW)) {
        return g_metryki[pid];
    }
    return {};
}

[[noreturn]] void stos_wykonaj_test_przepelnienia(int pid) {
    serial_wypisz("[STACK TEST] Rozpoczynam kontrolowany test przekroczenia guard page...\n");
    if (pid < 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW) || g_metryki[pid].stack_base == 0) {
        serial_wypisz("[STACK TEST] Brak aktywnego stosu dla wskazanego PID!\n");
        while (true) asm volatile("cli; hlt");
    }

    // Dotykamy adresu wewnatrz unmapped guard page (stack_base - 8)
    volatile uint64_t* guard_ptr = reinterpret_cast<volatile uint64_t*>(g_metryki[pid].stack_base - 8);
    *guard_ptr = 0xDEADBEEFCAFEULL;

    while (true) asm volatile("cli; hlt");
}
