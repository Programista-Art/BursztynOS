/*
 * BRB - testy hosta Fazy 2 (make test-brb-host; ASan + UBSan).
 *
 * Zakres: uklad ABI (GUID-y, rozmiary), konwencja wywolan ms_abi (argumenty w rejestrach i na stosie,
 * wyrownanie stosu, shadow space, rejestry nieulotne), operacje pamieci, UTF-8 -> UCS-2, log szeregowy
 * na modelu UART 16550, walidacja tablic firmware, pelny przebieg brb::uruchom na modelu firmware
 * (konsola, klawiatura, zdarzenia, Stall, LoadedImage) z bledami wstrzykiwanymi w kazdym punkcie.
 *
 * Model firmware udostepnia te same struktury i wskazniki funkcji (ms_abi), z ktorych korzysta kod
 * produkcyjny. Kazda atrapa sprawdza argumenty (This, GUID, typy zdarzen, czasy) i ma stan - nie zwraca
 * "zawsze sukcesu". Zaden test nie wykonuje instrukcji in/out (Makefile sprawdza to w deasemblacji).
 */
#include "rozruch/brb/uefi/program.h"
#include "rozruch/brb/uefi/uefi_typy.h"
#include "rozruch/brb/wspolne/log_szeregowy.h"
#include "rozruch/brb/wspolne/pamiec_podstawowa.h"
#include "rozruch/brb/wspolne/utf8.h"
#include "rozruch/brb/wspolne/wersja.h"
#include "testy/brb_elf_fixture.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" char __executable_start[];
extern "C" char _end[];

namespace {

int g_sprawdzenia = 0, g_bledy = 0;
#define SPRAWDZ(w)                                                                     \
    do {                                                                               \
        ++g_sprawdzenia;                                                               \
        if (!(w)) {                                                                    \
            ++g_bledy;                                                                 \
            std::printf("  BLAD %s:%d: %s\n", __FILE__, __LINE__, #w);                 \
        }                                                                              \
    } while (0)

std::string na_utf8(const std::u16string& s) {
    std::string w;
    for (char16_t c : s) {
        if (c < 0x80) w += char(c);
        else if (c < 0x800) { w += char(0xC0 | (c >> 6)); w += char(0x80 | (c & 0x3F)); }
        else { w += char(0xE0 | (c >> 12)); w += char(0x80 | ((c >> 6) & 0x3F)); w += char(0x80 | (c & 0x3F)); }
    }
    return w;
}

// =====================================================================================================
// Model UART 16550 (COM1) - dostep przez brb::PortyWeWy, bez instrukcji in/out.
// =====================================================================================================
struct ModelUart {
    enum Tryb { Sprawny, Brak, ScrUszkodzony, NadajnikZawieszony, NadajnikPadaPo } tryb = Sprawny;
    uint8_t lcr = 0x03, dll = 0x01, dlm = 0x00, scr = 0x00, ier = 0, mcr = 0, fcr = 0;
    std::string wyslane;
    std::vector<std::pair<uint16_t, uint8_t>> zapisy;
    uint64_t odczyty_lsr = 0;
    size_t pada_po = 0;   // NadajnikPadaPo: THRE gasnie na stale po tylu bajtach
    static uint8_t czytaj(void* k, uint16_t port) {
        auto* u = static_cast<ModelUart*>(k);
        if (u->tryb == Brak) return 0xFF;
        const bool dlab = u->lcr & 0x80;
        switch (port - 0x3F8) {
            case 0: return dlab ? u->dll : 0;
            case 1: return dlab ? u->dlm : u->ier;
            case 3: return u->lcr;
            case 4: return u->mcr;
            case 5: {
                ++u->odczyty_lsr;
                if (u->tryb == NadajnikZawieszony) return 0x00;
                if (u->tryb == NadajnikPadaPo && u->wyslane.size() >= u->pada_po) return 0x00;
                return 0x60;
            }
            case 7: return u->tryb == ScrUszkodzony ? 0x00 : u->scr;
            default: return 0;
        }
    }
    static void zapisz(void* k, uint16_t port, uint8_t v) {
        auto* u = static_cast<ModelUart*>(k);
        u->zapisy.push_back({port, v});
        if (u->tryb == Brak) return;
        const bool dlab = u->lcr & 0x80;
        switch (port - 0x3F8) {
            case 0: if (dlab) u->dll = v; else u->wyslane += char(v); break;
            case 1: if (dlab) u->dlm = v; else u->ier = v; break;
            case 2: u->fcr = v; break;
            case 3: u->lcr = v; break;
            case 4: u->mcr = v; break;
            case 7: u->scr = v; break;
            default: break;
        }
    }
    brb::PortyWeWy porty() { return brb::PortyWeWy{czytaj, zapisz, this}; }
};

// =====================================================================================================
// Model firmware UEFI
// =====================================================================================================
struct ModelFirmware {
    EFI_SYSTEM_TABLE st{};
    EFI_BOOT_SERVICES bs{};
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL wy{};
    SIMPLE_TEXT_OUTPUT_MODE tryb{};
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL we{};
    EFI_LOADED_IMAGE_PROTOCOL obraz{};
    int uchwyt_obrazu = 0, uchwyt_konsoli = 0, zdarzenie_klawisza = 0;
    int zdarzenia_zegara[8]{};
    // Konsola
    std::u16string ekran;
    int wywolania_output = 0;
    int output_blad_od = -1;          // od tego wywolania OutputString zwraca blad
    EFI_STATUS status_output_blad = EFI_DEVICE_ERROR;
    bool ostrzezenie_glifu = false;
    EFI_STATUS status_test_string = EFI_SUCCESS;
    std::u16string ostatni_test_string;
    EFI_STATUS status_clear = EFI_SUCCESS, status_atrybut = EFI_SUCCESS;
    std::vector<UINTN> atrybuty;
    int clear_wywolania = 0;
    // Klawiatura: kolejne wyniki ReadKeyStroke (po wyczerpaniu: EFI_NOT_READY)
    std::vector<EFI_STATUS> klawisze;
    size_t indeks_klawisza = 0;
    int read_wywolania = 0;
    // Zdarzenia
    EFI_STATUS status_create = EFI_SUCCESS, status_set_timer = EFI_SUCCESS, status_wait = EFI_SUCCESS;
    std::vector<UINTN> wyniki_wait;   // indeks zwracany przez kolejne WaitForEvent (po wyczerpaniu: 1 = zegar)
    size_t indeks_wait = 0;
    int utworzone = 0, zamkniete = 0, anulowane = 0, wait_wywolania = 0;
    UINT64 ostatni_trigger = 0;
    bool zle_argumenty = false;       // dowolna atrapa wykryla bledne argumenty
    bool zly_stos = false;            // wyrownanie stosu przy wejsciu do atrapy
    // Stall
    UINT64 suma_stall_us = 0;
    int stall_wywolania = 0;
    // HandleProtocol / LoadedImage
    EFI_STATUS status_handle = EFI_SUCCESS;
    bool obraz_null = false;
    int handle_wywolania = 0;
    // Faza 3: system plikow woluminu obrazu BRB (uchwyt_urzadzenia = LoadedImage->DeviceHandle)
    int uchwyt_urzadzenia = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL sfs{};
    EFI_FILE_PROTOCOL korzen{}, plik{};
    EFI_STATUS status_sfs = EFI_SUCCESS;          // HandleProtocol(SFS)
    EFI_STATUS status_open_volume = EFI_SUCCESS;
    EFI_STATUS status_open = EFI_SUCCESS;         // != SUCCESS: Open zwraca ten status
    bool plik_istnieje = true;
    bool katalog = false;
    std::vector<uint8_t> dane_pliku;
    bool rozmiar_zgloszony_ustawiony = false;
    UINT64 rozmiar_zgloszony = 0;                 // FileSize w EFI_FILE_INFO (domyslnie = dane_pliku.size())
    EFI_STATUS status_info = EFI_SUCCESS;
    UINTN info_wymagany = 0;                      // != 0: GetInfo zada takiego bufora (EFI_BUFFER_TOO_SMALL)
    UINTN info_zwracany_rozmiar = 0;              // != 0: nadpisanie *BufferSize po sukcesie
    UINT64 info_pole_size = 0;                    // != 0: nadpisanie pola Size
    bool info_bez_nul = false;
    UINTN maks_porcja = 0;                        // 0 = Read oddaje tyle, o ile poproszono
    int read_blad_od = -1;                        // od tego wywolania Read zwraca status_read_blad
    EFI_STATUS status_read_blad = EFI_DEVICE_ERROR;
    bool read_zawyza = false;                     // Read zglasza wiecej bajtow niz bufor
    UINT64 pozycja = 0;
    int read_wywolania_pliku = 0, otwarcia_plikow = 0, zamkniecia_plikow = 0;
    std::u16string ostatnia_sciezka;
    UINT64 ostatni_tryb = 0, ostatnie_atrybuty = ~0ull;
    // AllocatePages/FreePages
    EFI_STATUS status_alloc = EFI_SUCCESS;
    std::vector<std::pair<EFI_PHYSICAL_ADDRESS, UINTN>> przydzialy;   // aktywne (AllocateAnyPages: wskazniki hosta)
    int przydzialy_lacznie = 0, zwolnienia = 0;
    bool zle_zwolnienie = false;
    // Faza 4A: symulowana pamiec fizyczna [fiz_baza, fiz_baza + fiz.size()) dla AllocateAddress. Test nigdy nie
    // dereferencjonuje prawdziwego adresu fizycznego - tlumacz zwraca wskaznik tylko w tym zakresie.
    UINT64 fiz_baza = 0x100000;
    std::vector<uint8_t> fiz;
    std::vector<std::pair<UINT64, UINT64>> zajete;                    // [poczatek, koniec) - pamiec firmware
    std::vector<std::pair<EFI_PHYSICAL_ADDRESS, UINTN>> przydzialy_fiz;  // aktywne przydzialy AllocateAddress
    int adres_wywolania = 0, adres_przydzialy_lacznie = 0, adres_zwolnienia = 0;
    int adres_odmow_od = -1;                      // od tego wywolania AllocateAddress -> EFI_NOT_FOUND
    int adres_zly_przy = -1;                      // przy tym wywolaniu zwroc adres + 0x1000 (zly adres)
    int free_fiz_blad_przy = -1;                  // przy tym zwolnieniu AllocateAddress -> EFI_DEVICE_ERROR
    int free_fiz_wywolania = 0;
    int any_wywolania = 0, any_blad_przy = -1;   // AllocateAnyPages: wstrzykniecie EFI_OUT_OF_RESOURCES
    // GetMemoryMap
    UINTN mapa_krok = 48;
    UINT32 mapa_wersja = 1;
    bool mapa_pierwsze_sukces = false;            // zapytanie o rozmiar zwraca EFI_SUCCESS (niezgodne ze spec.)
    UINT32 mapa_wzrost = 0;                       // dodatkowe deskryptory przy kazdym wywolaniu (zmiana mapy)
    int mapa_blad_od = -1;                        // od tego wywolania -> EFI_DEVICE_ERROR
    int mapa_wywolania = 0;
    UINTN mapa_klucz = 0x100;

    void zainicjuj();
    void ustaw_jadro(const std::vector<uint8_t>& d) { dane_pliku = d; }
};

ModelFirmware* M = nullptr;
const EFI_GUID GUID_LI = EFI_LOADED_IMAGE_PROTOCOL_GUID;

}  // namespace

// Atrapy firmware i wstawki wejsciowe maja zasieg globalny i linkage C: wstawki asemblerowe odwoluja sie do
// nich po nazwie. Korzystaja z modelu (M) z przestrzeni anonimowej tej samej jednostki kompilacji.
// Kontrola granicy wywolania firmware: tablice firmware wskazuja na wstawki asemblerowe "stub_<atrapa>",
// ktore PRZED wykonaniem atrapy (1) sprawdzaja wyrownanie stosu: przy wejsciu RSP mod 16 == 8 (ABI x64),
// (2) zapisuja wzorzec 0xA5 w 32 B shadow space [RSP+8, RSP+40) - obszar, ktory wywolujacy (kod BRB) MUSI
// zarezerwowac; gdyby go nie zarezerwowal, zapis zniszczylby jego dane (wykrywane przez testy wyniku i ASan),
// (3) skacza do atrapy (ten sam adres powrotu, te same rejestry argumentow). Atrapa moze potem uzywac shadow
// space na wlasne potrzeby (GCC przechowuje tam argumenty) - dlatego wzorzec nie moze byc zapisywany z jej
// wnetrza (blad wykryty w Fazie 3: zapis z wnetrza nadpisywal zachowane argumenty atrapy).
extern "C" { volatile unsigned char g_zly_stos_stub = 0; }
#define BRB_STUB(nazwa)                                                                           \
    asm(".text\n.globl stub_" #nazwa "\n.type stub_" #nazwa ",@function\nstub_" #nazwa ":\n"         \
        "    mov %rsp, %r11\n    and $15, %r11\n    cmp $8, %r11\n    je 1f\n"                      \
        "    movb $1, g_zly_stos_stub(%rip)\n1:\n"                                                 \
        "    movabs $0xA5A5A5A5A5A5A5A5, %r11\n"                                                    \
        "    mov %r11, 8(%rsp)\n    mov %r11, 16(%rsp)\n    mov %r11, 24(%rsp)\n    mov %r11, 32(%rsp)\n" \
        "    jmp " #nazwa "\n.size stub_" #nazwa ", .-stub_" #nazwa "\n");                         \
    extern "C" decltype(nazwa) stub_##nazwa

extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* t, const CHAR16* s) {
    if (t != &M->wy || !s) M->zle_argumenty = true;
    const int n = M->wywolania_output++;
    if (M->output_blad_od >= 0 && n >= M->output_blad_od) return M->status_output_blad;
    for (size_t i = 0; s && s[i] && i < 4096; ++i) M->ekran += s[i];
    return M->ostrzezenie_glifu ? EFI_WARN_UNKNOWN_GLYPH : EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_test_string(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* t, const CHAR16* s) {
    if (t != &M->wy || !s) M->zle_argumenty = true;
    M->ostatni_test_string.clear();
    for (size_t i = 0; s && s[i] && i < 256; ++i) M->ostatni_test_string += s[i];
    return M->status_test_string;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_set_attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* t, UINTN a) {
    if (t != &M->wy || a > 0x7F) M->zle_argumenty = true;
    M->atrybuty.push_back(a);
    return M->status_atrybut;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* t) {
    if (t != &M->wy) M->zle_argumenty = true;
    ++M->clear_wywolania;
    return M->status_clear;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_read_key(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* t, EFI_INPUT_KEY* k) {
    if (t != &M->we || !k) M->zle_argumenty = true;
    ++M->read_wywolania;
    if (M->indeks_klawisza >= M->klawisze.size()) return EFI_NOT_READY;
    const EFI_STATUS s = M->klawisze[M->indeks_klawisza++];
    if (s == EFI_SUCCESS) { k->ScanCode = 0; k->UnicodeChar = u'x'; }
    return s;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_create_event(UINT32 typ, EFI_TPL tpl, EFI_EVENT_NOTIFY f, VOID* ctx, EFI_EVENT* wynik) {
    // Piaty argument (wynik) jest przekazywany na stosie ZA shadow space - sprawdza poprawnosc ms_abi.
    if (typ != EVT_TIMER || tpl != TPL_CALLBACK || f != nullptr || ctx != nullptr || !wynik) M->zle_argumenty = true;
    if (M->status_create != EFI_SUCCESS) return M->status_create;
    if (M->utworzone >= 8) return EFI_OUT_OF_RESOURCES;
    *wynik = &M->zdarzenia_zegara[M->utworzone++];
    return EFI_SUCCESS;
}
bool zdarzenie_zegara(EFI_EVENT e) {
    for (int i = 0; i < M->utworzone; ++i) if (e == &M->zdarzenia_zegara[i]) return true;
    return false;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_set_timer(EFI_EVENT e, EFI_TIMER_DELAY typ, UINT64 czas) {
    if (!zdarzenie_zegara(e)) M->zle_argumenty = true;
    if (typ == TimerCancel) { ++M->anulowane; return EFI_SUCCESS; }
    if (typ != TimerRelative) M->zle_argumenty = true;
    M->ostatni_trigger = czas;
    return M->status_set_timer;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_wait(UINTN n, EFI_EVENT* zd, UINTN* indeks) {
    ++M->wait_wywolania;
    if (n != 2 || !zd || !indeks || zd[0] != &M->zdarzenie_klawisza || !zdarzenie_zegara(zd[1])) M->zle_argumenty = true;
    if (M->status_wait != EFI_SUCCESS) return M->status_wait;
    *indeks = M->indeks_wait < M->wyniki_wait.size() ? M->wyniki_wait[M->indeks_wait++] : 1;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_close(EFI_EVENT e) {
    if (!zdarzenie_zegara(e)) M->zle_argumenty = true;
    ++M->zamkniete;
    return EFI_SUCCESS;
}
const EFI_GUID GUID_SFS_T = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
const EFI_GUID GUID_INFO_T = EFI_FILE_INFO_ID;

extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_handle_protocol(EFI_HANDLE h, const EFI_GUID* g, VOID** wynik) {
    ++M->handle_wywolania;
    if (!g || !wynik) { M->zle_argumenty = true; return EFI_INVALID_PARAMETER; }
    if (std::memcmp(g, &GUID_LI, sizeof(EFI_GUID)) == 0) {
        if (h != &M->uchwyt_obrazu) { M->zle_argumenty = true; return EFI_INVALID_PARAMETER; }
        if (M->status_handle != EFI_SUCCESS) return M->status_handle;
        *wynik = M->obraz_null ? nullptr : &M->obraz;
        return EFI_SUCCESS;
    }
    if (std::memcmp(g, &GUID_SFS_T, sizeof(EFI_GUID)) == 0) {
        // BRB moze pytac wylacznie o urzadzenie, z ktorego zostal zaladowany (brak przeszukiwania dyskow).
        if (h != &M->uchwyt_urzadzenia) { M->zle_argumenty = true; return EFI_UNSUPPORTED; }
        if (M->status_sfs != EFI_SUCCESS) return M->status_sfs;
        *wynik = &M->sfs;
        return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_open_volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* t, EFI_FILE_PROTOCOL** korzen) {
    if (t != &M->sfs || !korzen) M->zle_argumenty = true;
    if (M->status_open_volume != EFI_SUCCESS) return M->status_open_volume;
    ++M->otwarcia_plikow;
    *korzen = &M->korzen;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_file_open(EFI_FILE_PROTOCOL* t, EFI_FILE_PROTOCOL** nowy, const CHAR16* nazwa, UINT64 tryb, UINT64 atr) {
    // Piaty argument (Attributes) na stosie za shadow space.
    if (t != &M->korzen || !nowy || !nazwa) { M->zle_argumenty = true; return EFI_INVALID_PARAMETER; }
    M->ostatnia_sciezka.clear();
    for (size_t i = 0; nazwa[i] && i < 256; ++i) M->ostatnia_sciezka += nazwa[i];
    M->ostatni_tryb = tryb;
    M->ostatnie_atrybuty = atr;
    if (tryb != EFI_FILE_MODE_READ || atr != 0) M->zle_argumenty = true;   // tylko odczyt
    if (M->status_open != EFI_SUCCESS) return M->status_open;
    if (!M->plik_istnieje || M->ostatnia_sciezka != u"\\boot\\system_operacyjny.bin") return EFI_NOT_FOUND;
    ++M->otwarcia_plikow;
    M->pozycja = 0;
    *nowy = &M->plik;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_file_close(EFI_FILE_PROTOCOL* t) {
    if (t != &M->korzen && t != &M->plik) M->zle_argumenty = true;
    ++M->zamkniecia_plikow;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_file_read(EFI_FILE_PROTOCOL* t, UINTN* rozmiar, VOID* bufor) {
    if (t != &M->plik || !rozmiar || (!bufor && *rozmiar)) { M->zle_argumenty = true; return EFI_INVALID_PARAMETER; }
    const int n = M->read_wywolania_pliku++;
    if (M->read_blad_od >= 0 && n >= M->read_blad_od) return M->status_read_blad;
    const UINT64 dostepne = M->dane_pliku.size() - M->pozycja;
    UINTN ile = *rozmiar;
    if (M->maks_porcja && ile > M->maks_porcja) ile = M->maks_porcja;
    if (ile > dostepne) ile = UINTN(dostepne);
    if (ile) std::memcpy(bufor, M->dane_pliku.data() + M->pozycja, ile);   // ASan: zapis w granicach bufora BRB
    M->pozycja += ile;
    *rozmiar = M->read_zawyza ? *rozmiar + 16 : ile;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_file_get_info(EFI_FILE_PROTOCOL* t, const EFI_GUID* typ, UINTN* rozmiar, VOID* bufor) {
    if (t != &M->plik || !typ || !rozmiar || std::memcmp(typ, &GUID_INFO_T, sizeof(EFI_GUID)) != 0) {
        M->zle_argumenty = true;
        return EFI_UNSUPPORTED;
    }
    if (M->status_info != EFI_SUCCESS) return M->status_info;
    static const char16_t nazwa[] = u"system_operacyjny.bin";
    const UINTN potrzebne = EFI_FILE_INFO_STALA_CZESC + sizeof(nazwa);
    const UINTN wymagane = M->info_wymagany ? M->info_wymagany : potrzebne;
    if (*rozmiar < wymagane) { *rozmiar = wymagane; return EFI_BUFFER_TOO_SMALL; }
    auto* b = static_cast<uint8_t*>(bufor);
    std::memset(b, 0, potrzebne);
    const UINT64 size = M->info_pole_size ? M->info_pole_size : potrzebne;
    const UINT64 fsize = M->rozmiar_zgloszony_ustawiony ? M->rozmiar_zgloszony : M->dane_pliku.size();
    const UINT64 atr = M->katalog ? EFI_FILE_DIRECTORY : 0x20 /* EFI_FILE_ARCHIVE */;
    std::memcpy(b + offsetof(EFI_FILE_INFO, Size), &size, 8);
    std::memcpy(b + offsetof(EFI_FILE_INFO, FileSize), &fsize, 8);
    std::memcpy(b + offsetof(EFI_FILE_INFO, PhysicalSize), &fsize, 8);
    std::memcpy(b + offsetof(EFI_FILE_INFO, Attribute), &atr, 8);
    std::memcpy(b + EFI_FILE_INFO_STALA_CZESC, nazwa, sizeof(nazwa));
    if (M->info_bez_nul) for (UINTN o = EFI_FILE_INFO_STALA_CZESC; o < potrzebne; ++o) b[o] = 'A';
    *rozmiar = M->info_zwracany_rozmiar ? M->info_zwracany_rozmiar : potrzebne;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_allocate_pages(EFI_ALLOCATE_TYPE typ, EFI_MEMORY_TYPE pamiec, UINTN strony, EFI_PHYSICAL_ADDRESS* adres) {
    if (typ == AllocateAddress) {
        // Faza 4A: strony docelowe jadra (EfiLoaderData), wylacznie w symulowanej pamieci fizycznej.
        if (pamiec != EfiLoaderData || strony == 0 || !adres || (*adres & 0xFFF)) { M->zle_argumenty = true; return EFI_INVALID_PARAMETER; }
        const int n = M->adres_wywolania++;
        const UINT64 a = *adres, k = a + UINT64(strony) * 4096;
        if (M->adres_odmow_od >= 0 && n >= M->adres_odmow_od) return EFI_NOT_FOUND;
        if (a < M->fiz_baza || k > M->fiz_baza + M->fiz.size() || k < a) return EFI_NOT_FOUND;
        for (auto& z : M->zajete) if (a < z.second && z.first < k) return EFI_NOT_FOUND;
        for (auto& z : M->przydzialy_fiz) if (a < z.first + z.second * 4096 && z.first < k) return EFI_NOT_FOUND;
        const UINT64 wynik = (M->adres_zly_przy == n) ? a + 0x1000 : a;
        M->przydzialy_fiz.push_back({wynik, strony});
        ++M->adres_przydzialy_lacznie;
        *adres = wynik;
        return EFI_SUCCESS;
    }
    // Bufory tymczasowe (plik jadra, mapa pamieci): dowolny adres, EfiLoaderData.
    if (typ != AllocateAnyPages || pamiec != EfiLoaderData || strony == 0 || strony > 16384 + 1 || !adres) {
        M->zle_argumenty = true;
        return EFI_INVALID_PARAMETER;
    }
    if (M->status_alloc != EFI_SUCCESS) return M->status_alloc;
    if (M->any_blad_przy == M->any_wywolania++) return EFI_OUT_OF_RESOURCES;
    void* p = std::aligned_alloc(4096, strony * 4096);
    if (!p) return EFI_OUT_OF_RESOURCES;
    std::memset(p, 0xCC, strony * 4096);
    *adres = reinterpret_cast<uintptr_t>(p);
    M->przydzialy.push_back({*adres, strony});
    ++M->przydzialy_lacznie;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_free_pages(EFI_PHYSICAL_ADDRESS adres, UINTN strony) {
    for (size_t i = 0; i < M->przydzialy_fiz.size(); ++i) {
        if (M->przydzialy_fiz[i].first == adres) {
            if (M->przydzialy_fiz[i].second != strony) M->zle_zwolnienie = true;
            if (M->free_fiz_blad_przy == M->free_fiz_wywolania++) return EFI_DEVICE_ERROR;   // strony zostaja zajete
            M->przydzialy_fiz.erase(M->przydzialy_fiz.begin() + long(i));
            ++M->adres_zwolnienia;
            return EFI_SUCCESS;
        }
    }
    for (size_t i = 0; i < M->przydzialy.size(); ++i) {
        if (M->przydzialy[i].first == adres) {
            if (M->przydzialy[i].second != strony) M->zle_zwolnienie = true;
            std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(adres)));
            M->przydzialy.erase(M->przydzialy.begin() + long(i));
            ++M->zwolnienia;
            return EFI_SUCCESS;
        }
    }
    M->zle_zwolnienie = true;
    return EFI_NOT_FOUND;
}
// GetMemoryMap: mapa budowana ze stanu modelu (strony symulacji: wolna / zajeta przez firmware / ladowarka),
// plus wpis powyzej 4 GiB i wpis typu OEM; krok deskryptora i wersja konfigurowalne; mapa moze rosnac.
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_get_memory_map(UINTN* rozmiar, EFI_MEMORY_DESCRIPTOR* mapa, UINTN* klucz,
                                                                    UINTN* krok, UINT32* wersja) {
    if (!rozmiar || !klucz || !krok || !wersja) { M->zle_argumenty = true; return EFI_INVALID_PARAMETER; }
    const int n = M->mapa_wywolania++;
    if (M->mapa_blad_od >= 0 && n >= M->mapa_blad_od) return EFI_DEVICE_ERROR;
    struct W { uint32_t typ; UINT64 p, strony; };
    std::vector<W> w;
    w.push_back({0, 0, 0x100});
    const UINT64 strony_sym = M->fiz.size() / 4096;
    auto typ_strony = [&](UINT64 a) -> uint32_t {
        for (auto& z : M->zajete) if (a >= z.first && a < z.second) return 4;
        for (auto& z : M->przydzialy_fiz) if (a >= z.first && a < z.first + z.second * 4096) return 2;
        return 7;
    };
    for (UINT64 i = 0; i < strony_sym;) {
        const UINT64 a = M->fiz_baza + i * 4096;
        const uint32_t t = typ_strony(a);
        UINT64 j = i + 1;
        while (j < strony_sym && typ_strony(M->fiz_baza + j * 4096) == t) ++j;
        w.push_back({t, a, j - i});
        i = j;
    }
    w.push_back({7, 0x100000000ull, 0x10000});
    w.push_back({0x70000001u, 0x200000000ull, 4});
    for (UINT32 i = 0; i < M->mapa_wzrost * UINT32(n + 1); ++i) w.push_back({7, 0x300000000ull + UINT64(i) * 0x1000, 1});
    const UINTN potrzebne = w.size() * M->mapa_krok;
    *krok = M->mapa_krok;
    *wersja = M->mapa_wersja;
    *klucz = ++M->mapa_klucz;
    if (n == 0 && M->mapa_pierwsze_sukces) { *rozmiar = 0; return EFI_SUCCESS; }
    if (*rozmiar < potrzebne || !mapa) { *rozmiar = potrzebne; return EFI_BUFFER_TOO_SMALL; }
    auto* b = reinterpret_cast<uint8_t*>(mapa);
    std::memset(b, 0x5A, potrzebne);   // dopelnienie deskryptora (krok > 40) celowo niezerowe
    for (size_t i = 0; i < w.size(); ++i) {
        uint8_t* d = b + i * M->mapa_krok;
        const uint64_t zero = 0;
        std::memcpy(d, &w[i].typ, 4);
        std::memcpy(d + 8, &w[i].p, 8);
        std::memcpy(d + 16, &zero, 8);
        std::memcpy(d + 24, &w[i].strony, 8);
        const uint64_t atr = 0xF;
        std::memcpy(d + 32, &atr, 8);
    }
    *rozmiar = potrzebne;
    return EFI_SUCCESS;
}
extern "C" __attribute__((used)) EFI_STATUS EFIAPI m_stall(UINTN us) {
    ++M->stall_wywolania;
    M->suma_stall_us += us;
    return EFI_SUCCESS;
}

BRB_STUB(m_output);
BRB_STUB(m_test_string);
BRB_STUB(m_set_attribute);
BRB_STUB(m_clear);
BRB_STUB(m_read_key);
BRB_STUB(m_create_event);
BRB_STUB(m_set_timer);
BRB_STUB(m_wait);
BRB_STUB(m_close);
BRB_STUB(m_handle_protocol);
BRB_STUB(m_stall);
BRB_STUB(m_open_volume);
BRB_STUB(m_file_open);
BRB_STUB(m_file_close);
BRB_STUB(m_file_read);
BRB_STUB(m_file_get_info);
BRB_STUB(m_allocate_pages);
BRB_STUB(m_free_pages);
BRB_STUB(m_get_memory_map);

namespace {

void ModelFirmware::zainicjuj() {
    st.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    st.Hdr.Revision = EFI_REWIZJA(2, 70);
    st.Hdr.HeaderSize = sizeof(EFI_SYSTEM_TABLE);
    st.ConOut = &wy;
    st.ConsoleOutHandle = &uchwyt_konsoli;
    st.ConIn = &we;
    st.BootServices = &bs;
    bs.Hdr.Signature = EFI_BOOT_SERVICES_SIGNATURE;
    bs.Hdr.Revision = EFI_REWIZJA(2, 70);
    bs.Hdr.HeaderSize = sizeof(EFI_BOOT_SERVICES);
    bs.CreateEvent = stub_m_create_event;
    bs.SetTimer = stub_m_set_timer;
    bs.WaitForEvent = stub_m_wait;
    bs.CloseEvent = stub_m_close;
    bs.HandleProtocol = stub_m_handle_protocol;
    bs.Stall = stub_m_stall;
    wy.OutputString = stub_m_output;
    wy.TestString = stub_m_test_string;
    wy.SetAttribute = stub_m_set_attribute;
    wy.ClearScreen = stub_m_clear;
    wy.Mode = &tryb;
    we.ReadKeyStroke = stub_m_read_key;
    we.WaitForKey = &zdarzenie_klawisza;
    obraz.Revision = EFI_LOADED_IMAGE_PROTOCOL_REVISION;
    obraz.SystemTable = &st;
    obraz.ImageBase = __executable_start;
    obraz.ImageSize = UINT64(_end - __executable_start);
    obraz.DeviceHandle = &uchwyt_urzadzenia;
    bs.AllocatePages = stub_m_allocate_pages;
    bs.FreePages = stub_m_free_pages;
    bs.GetMemoryMap = stub_m_get_memory_map;
    fiz.assign(0x100000, 0xCC);   // 1 MiB symulacji od 1 MiB (syntetyczne jadro: 0x100000-0x104000)
    sfs.Revision = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
    sfs.OpenVolume = stub_m_open_volume;
    for (EFI_FILE_PROTOCOL* p : {&korzen, &plik}) {
        p->Revision = EFI_FILE_PROTOCOL_REVISION2;
        p->Open = stub_m_file_open;
        p->Close = stub_m_file_close;
        p->Read = stub_m_file_read;
        p->GetInfo = stub_m_file_get_info;
    }
    dane_pliku = fixture::jadro_minimalne().b;
}

struct Przebieg {
    ModelFirmware fw;
    ModelUart uart;
    brb::LogSzeregowy log;
    brb::StanProgramu stan;
    EFI_STATUS status = 0;
    Przebieg() { fw.zainicjuj(); g_zly_stos_stub = 0; }
    // Tlumacz Fazy 4A: tylko symulowana pamiec fizyczna modelu.
    static uint8_t* tlumacz(void* k, uint64_t adres, uint64_t dl) {
        auto* f = static_cast<ModelFirmware*>(k);
        if (adres < f->fiz_baza || dl > f->fiz.size() || adres - f->fiz_baza > f->fiz.size() - dl) return nullptr;
        return f->fiz.data() + (adres - f->fiz_baza);
    }
    brb::DostepFizyczny pamiec() { return brb::DostepFizyczny{tlumacz, &fw}; }
    // Domyslnie w testach: polskie znaki wlaczone (sprawdza sciezke UTF-8 -> UCS-2 i TestString).
    // Zachowanie produkcyjne (transliteracja) sprawdza test_przebieg_domyslny_ascii.
    EFI_STATUS uruchom(const brb::Ustawienia& u = {3, 100, 2, true}) {
        M = &fw;
        log.inicjalizuj(uart.porty());
        status = brb::uruchom(&fw.uchwyt_obrazu, &fw.st, log, u, pamiec(), &stan);
        M = nullptr;
        return status;
    }
    std::string ekran() const { return na_utf8(fw.ekran); }
    bool w_logu(const char* t) const { return uart.wyslane.find(t) != std::string::npos; }
    bool na_ekranie(const char* t) const { return ekran().find(t) != std::string::npos; }
    void sprawdz_sprzatanie() {
        SPRAWDZ(fw.zamkniete == fw.utworzone);   // kazde zdarzenie zamkniete
        SPRAWDZ(!fw.zle_argumenty);
        SPRAWDZ(!fw.zly_stos && g_zly_stos_stub == 0);
        SPRAWDZ(fw.otwarcia_plikow == fw.zamkniecia_plikow);   // kazdy uchwyt pliku/woluminu zamkniety
        SPRAWDZ(fw.przydzialy.empty() && !fw.zle_zwolnienie);  // kazdy bufor zwolniony, z wlasciwa liczba stron
        SPRAWDZ(fw.przydzialy_fiz.empty());                    // kazdy przydzial AllocateAddress zwolniony
    }
    ~Przebieg() {
        for (auto& p : fw.przydzialy) std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(p.first)));
    }
};

// =====================================================================================================
// Testy
// =====================================================================================================
void test_abi_guid() {
    std::printf("[test] ABI: GUID-y i rozmiary\n");
    // Bajty w pamieci (little-endian) wg UEFI 2.10: 9.1 i 12.3/12.4.
    static const uint8_t li[16] = {0xA1, 0x31, 0x1B, 0x5B, 0x62, 0x95, 0xD2, 0x11, 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B};
    static const uint8_t ti[16] = {0xC1, 0x77, 0x74, 0x38, 0xC7, 0x69, 0xD2, 0x11, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B};
    static const uint8_t to[16] = {0xC2, 0x77, 0x74, 0x38, 0xC7, 0x69, 0xD2, 0x11, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B};
    const EFI_GUID g1 = EFI_LOADED_IMAGE_PROTOCOL_GUID, g2 = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,
                   g3 = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
    SPRAWDZ(std::memcmp(&g1, li, 16) == 0);
    SPRAWDZ(std::memcmp(&g2, ti, 16) == 0);
    SPRAWDZ(std::memcmp(&g3, to, 16) == 0);
    const uint64_t sys = EFI_SYSTEM_TABLE_SIGNATURE, bs = EFI_BOOT_SERVICES_SIGNATURE;
    SPRAWDZ(std::memcmp(&sys, "IBI SYST", 8) == 0);
    SPRAWDZ(std::memcmp(&bs, "BOOTSERV", 8) == 0);
    SPRAWDZ(EFI_ERROR(EFI_TIMEOUT) && !EFI_ERROR(EFI_WARN_UNKNOWN_GLYPH) && EFI_TIMEOUT == 0x8000000000000012ULL);
    SPRAWDZ(EFI_INCOMPATIBLE_VERSION == 0x8000000000000019ULL && EFI_NOT_READY == 0x8000000000000006ULL);
    SPRAWDZ(brb::MIN_ROZMIAR_USLUG == 0x100);
}

// Wywolanie funkcji ms_abi z wartosciami kontrolnymi w rejestrach nieulotnych ms_abi; po powrocie
// sprawdzamy, czy zostaly zachowane (RBX, RSI, RDI, R12-R15) i czy wywolanie mialo wyrownany stos.
extern "C" uint64_t brb_test_wywolaj_ms(void* funkcja, void* a1, void* a2, uint64_t* rejestry);
asm(R"(
    .text
    .globl brb_test_wywolaj_ms
    .type brb_test_wywolaj_ms, @function
brb_test_wywolaj_ms:
    push %rbp
    mov %rsp, %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    push %rcx
    sub $32, %rsp
    mov %rdi, %rax
    mov %rsi, %rcx
    movabs $0x1111111111111111, %rbx
    movabs $0x2222222222222222, %rsi
    movabs $0x3333333333333333, %rdi
    movabs $0x4444444444444444, %r12
    movabs $0x5555555555555555, %r13
    movabs $0x6666666666666666, %r14
    movabs $0x7777777777777777, %r15
    call *%rax
    mov 32(%rsp), %rcx
    mov %rbx, 0(%rcx)
    mov %rsi, 8(%rcx)
    mov %rdi, 16(%rcx)
    mov %r12, 24(%rcx)
    mov %r13, 32(%rcx)
    mov %r14, 40(%rcx)
    mov %r15, 48(%rcx)
    add $40, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
    .size brb_test_wywolaj_ms, .-brb_test_wywolaj_ms
)");

// Odpowiednik efi_main (ta sama postac: EFIAPI, wywolanie kodu SysV), z modelem UART zamiast portow.
ModelUart* g_uart_wejscia = nullptr;
extern "C" EFI_STATUS EFIAPI wejscie_testowe(EFI_HANDLE obraz, EFI_SYSTEM_TABLE* tablica) {
    brb::LogSzeregowy log;
    log.inicjalizuj(g_uart_wejscia->porty());
    const brb::DostepFizyczny pamiec{[](void* k, uint64_t a, uint64_t d) -> uint8_t* {
        auto* f = static_cast<ModelFirmware*>(k);
        if (a < f->fiz_baza || d > f->fiz.size() || a - f->fiz_baza > f->fiz.size() - d) return nullptr;
        return f->fiz.data() + (a - f->fiz_baza);
    }, M};
    return brb::uruchom(obraz, tablica, log, brb::Ustawienia{2, 100, 1, false}, pamiec);
}

void test_abi_wejscie_ms() {
    std::printf("[test] ABI: wejscie ms_abi (RCX/RDX, rejestry nieulotne, stos)\n");
    Przebieg p;
    p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};  // oproznianie (NOT_READY), potem klawisz po pobudzeniu
    p.fw.wyniki_wait = {0};
    M = &p.fw;
    g_uart_wejscia = &p.uart;
    uint64_t r[7] = {};
    const uint64_t s = brb_test_wywolaj_ms(reinterpret_cast<void*>(&wejscie_testowe), &p.fw.uchwyt_obrazu, &p.fw.st, r);
    M = nullptr;
    SPRAWDZ(s == EFI_SUCCESS);                     // RCX/RDX dotarly jako ImageHandle/SystemTable
    SPRAWDZ(r[0] == 0x1111111111111111ULL);       // RBX
    SPRAWDZ(r[1] == 0x2222222222222222ULL);       // RSI (nieulotny w ms_abi, ulotny w SysV)
    SPRAWDZ(r[2] == 0x3333333333333333ULL);       // RDI (j.w.)
    SPRAWDZ(r[3] == 0x4444444444444444ULL && r[4] == 0x5555555555555555ULL);
    SPRAWDZ(r[5] == 0x6666666666666666ULL && r[6] == 0x7777777777777777ULL);
    SPRAWDZ(p.uart.wyslane.find("[BRB] etap 1: wejscie UEFI") != std::string::npos);
    p.sprawdz_sprzatanie();
}

void test_pamiec() {
    std::printf("[test] pamiec: memcpy/memmove/memset/memcmp\n");
    uint8_t wzor[96], a[96], b[96];
    for (int i = 0; i < 96; ++i) wzor[i] = uint8_t(i * 7 + 3);
    // Puste zakresy: zadnych odczytow ani zapisow (nullptr dozwolone przy n == 0 w brb_*).
    SPRAWDZ(brb_memcpy(a, wzor, 0) == a);
    SPRAWDZ(brb_memmove(a, wzor, 0) == a);
    SPRAWDZ(brb_memset(a, 0, 0) == a);
    SPRAWDZ(brb_memcmp(a, b, 0) == 0);
    // Wszystkie przesuniecia 0..7 i dlugosci 0..67 (niewyrownane bufory), porownanie z referencja bajtowa.
    for (int src = 0; src < 8; ++src)
        for (int dst = 0; dst < 8; ++dst)
            for (int n = 0; n <= 67; ++n) {
                std::memset(a, 0xEE, sizeof a);
                SPRAWDZ(brb_memcpy(a + dst, wzor + src, size_t(n)) == a + dst);
                bool ok = true;
                for (int i = 0; i < 96; ++i) {
                    const uint8_t oczek = (i >= dst && i < dst + n) ? wzor[src + i - dst] : 0xEE;
                    ok = ok && a[i] == oczek;
                }
                SPRAWDZ(ok);
            }
    // memmove z nakladaniem w obu kierunkach, kazda odleglosc 0..16.
    for (int odl = 0; odl <= 16; ++odl)
        for (int n = 0; n <= 64; n += 7) {
            std::memcpy(a, wzor, 96);
            std::memcpy(b, wzor, 96);
            brb_memmove(a + odl, a, size_t(n));     // cel za zrodlem
            std::memmove(b + odl, b, size_t(n));
            SPRAWDZ(std::memcmp(a, b, 96) == 0);
            std::memcpy(a, wzor, 96);
            std::memcpy(b, wzor, 96);
            brb_memmove(a, a + odl, size_t(n));     // cel przed zrodlem
            std::memmove(b, b + odl, size_t(n));
            SPRAWDZ(std::memcmp(a, b, 96) == 0);
        }
    // memset: wartosc obcinana do bajtu, granice nienaruszone.
    std::memset(a, 0x11, sizeof a);
    brb_memset(a + 3, 0x1FF, 50);
    SPRAWDZ(a[2] == 0x11 && a[3] == 0xFF && a[52] == 0xFF && a[53] == 0x11);
    // memcmp: znak wyniku wg bajtow bez znaku.
    const uint8_t x[3] = {1, 0x80, 3}, y[3] = {1, 0x7F, 3};
    SPRAWDZ(brb_memcmp(x, y, 3) > 0 && brb_memcmp(y, x, 3) < 0 && brb_memcmp(x, x, 3) == 0);
    SPRAWDZ(brb_memcmp(x, y, 1) == 0);
    // Bufory na stercie dokladnie o zadanym rozmiarze: ASan wykryje kazde wyjscie poza zakres.
    for (size_t n = 1; n <= 33; ++n) {
        std::vector<uint8_t> z(n, 0x5A), c(n, 0);
        brb_memcpy(c.data(), z.data(), n);
        brb_memmove(c.data(), c.data() + (n > 1 ? 1 : 0), n - (n > 1 ? 1 : 0));
        brb_memset(c.data(), 7, n);
        SPRAWDZ(brb_memcmp(c.data(), c.data(), n) == 0 && c[n - 1] == 7);
    }
}

std::u16string konwertuj(const char* s, size_t n, size_t pojemnosc, brb::WynikUtf8* w = nullptr) {
    std::vector<char16_t> bufor(pojemnosc ? pojemnosc : 1, u'#');
    const brb::WynikUtf8 r = brb::utf8_na_ucs2(s, n, pojemnosc ? bufor.data() : nullptr, pojemnosc);
    if (w) *w = r;
    if (!pojemnosc) return u"";
    if (r.zapisane >= pojemnosc || bufor[r.zapisane] != 0) return u"<BRAK NUL>";
    return std::u16string(bufor.data(), r.zapisane);
}

void test_utf8() {
    std::printf("[test] UTF-8 -> UCS-2\n");
    brb::WynikUtf8 w{};
    const char pl[] = "Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ";
    SPRAWDZ(konwertuj(pl, sizeof(pl) - 1, 64, &w) == u"Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ");
    SPRAWDZ(w.bledne == 0 && !w.obciete);
    SPRAWDZ(konwertuj("a\nb", 3, 8) == u"a\r\nb");
    SPRAWDZ(konwertuj("a\tb", 3, 8) == u"a\tb");
    struct Zly { const char* s; size_t n; const char16_t* oczek; uint32_t bledne; };
    const Zly zle[] = {
        {"\x80", 1, u"?", 1},                      // samotna kontynuacja
        {"\xC0\x80", 2, u"??", 2},                 // overlong 2 B (C0 zawsze bledny)
        {"\xC1\xBF", 2, u"??", 2},
        {"\xE0\x80\x80", 3, u"?", 1},              // overlong 3 B
        {"\xF0\x80\x80\x80", 4, u"?", 1},          // overlong 4 B
        {"\xED\xA0\x80", 3, u"?", 1},              // surogat U+D800
        {"\xED\xBF\xBF", 3, u"?", 1},              // surogat U+DFFF
        {"\xF4\x90\x80\x80", 4, u"?", 1},          // > U+10FFFF
        {"\xF5\x80\x80\x80", 4, u"????", 4},       // niedozwolony bajt wiodacy
        {"\xFF", 1, u"?", 1},
        {"\xE2\x82", 2, u"?", 1},                  // urwana na koncu bufora
        {"\xE2\x82" "A", 3, u"?A", 1},             // urwana, nastepny znak zachowany
        {"\xC4" "A\xC4\x85", 4, u"?Aą", 1},
    };
    for (const Zly& z : zle) {
        SPRAWDZ(konwertuj(z.s, z.n, 16, &w) == z.oczek);
        SPRAWDZ(w.bledne == z.bledne);
    }
    // NUL wewnatrz sekwencji konczy tekst (bez czytania za NUL).
    char z_nulem[] = {'\xC4', '\0', '\x85'};
    SPRAWDZ(konwertuj(z_nulem, 3, 8, &w) == u"?" && w.bledne == 1);
    // Znak spoza BMP (U+1F600) i sterujace.
    SPRAWDZ(konwertuj("\xF0\x9F\x98\x80", 4, 8, &w) == u"?" && w.poza_bmp == 1 && w.bledne == 0);
    SPRAWDZ(konwertuj("a\x01\x7F" "b\xC2\x85", 6, 8, &w) == u"a??b?" && w.sterujace == 3);
    // Pojemnosc: 0 (nic nie zapisane), 1 (tylko NUL), dokladne dopasowanie, CR LF nie jest dzielone.
    SPRAWDZ(konwertuj("abc", 3, 0, &w) == u"" && w.obciete);
    SPRAWDZ(konwertuj("abc", 3, 1, &w) == u"" && w.obciete);
    SPRAWDZ(konwertuj("abc", 3, 4, &w) == u"abc" && !w.obciete);
    SPRAWDZ(konwertuj("abcd", 4, 4, &w) == u"abc" && w.obciete);
    SPRAWDZ(konwertuj("ab\n", 3, 4, &w) == u"ab" && w.obciete);  // brak miejsca na CR LF + NUL
    SPRAWDZ(konwertuj("ąę", 4, 2, &w) == u"ą" && w.obciete);
    SPRAWDZ(konwertuj(nullptr, 5, 4, &w) == u"" && !w.obciete);
    SPRAWDZ(konwertuj("", 0, 4, &w) == u"" && !w.obciete);
    // Dokladny bufor na stercie: ASan.
    for (size_t cap = 1; cap <= 12; ++cap) (void)konwertuj("Łódź ąę\n", 12, cap);
    // Transliteracja.
    char16_t t[] = u"Zażółć Gęślą Jaźń ŁÓDŹ €";
    SPRAWDZ(brb::transliteruj_ascii(t, sizeof(t) / 2) == 13);
    SPRAWDZ(std::u16string(t) == u"Zazolc Gesla Jazn LODZ ?");
    SPRAWDZ(brb::transliteruj_ascii(nullptr, 4) == 0);
    SPRAWDZ(brb::dlugosc_tekstu("abc", 2) == 2 && brb::dlugosc_tekstu(nullptr, 9) == 0);
}

void test_log_szeregowy() {
    std::printf("[test] log szeregowy: model UART 16550\n");
    {   // Port skonfigurowany przez firmware: bez zmiany predkosci, DLAB przywrocony.
        ModelUart u;
        u.lcr = 0x03; u.dll = 0x01; u.scr = 0x42;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        SPRAWDZ(log.stan() == brb::StanPortu::Gotowy && !log.zainicjowal_uart());
        SPRAWDZ(u.dll == 0x01 && u.lcr == 0x03 && u.scr == 0x42);
        log.linia("[BRB] test ąę\x01");
        log.hex(0x1234ABCDull);
        log.znak(' ');
        log.dziesietnie(0);
        log.znak(' ');
        log.dziesietnie(18446744073709551615ull);
        SPRAWDZ(u.wyslane == "[BRB] test ?????\r\n0x000000001234ABCD 0 18446744073709551615");
    }
    {   // Port nieskonfigurowany (dzielnik 0): konfiguracja jak serial_inicjalizuj() jadra (38400 8N1).
        ModelUart u;
        u.lcr = 0x00; u.dll = 0; u.dlm = 0;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        SPRAWDZ(log.zainicjowal_uart() && u.dll == 0x03 && u.dlm == 0 && u.lcr == 0x03 && u.fcr == 0xC7 && u.mcr == 0x0B);
    }
    {   // Brak urzadzenia (LSR = 0xFF): zadnych zapisow poza testem.
        ModelUart u;
        u.tryb = ModelUart::Brak;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        SPRAWDZ(log.stan() == brb::StanPortu::Nieobecny && u.zapisy.empty());
        log.linia("nic");
        SPRAWDZ(u.zapisy.empty() && log.wyslane() == 0);
    }
    {   // Rejestr SCR nie dziala (brak UART 16450+).
        ModelUart u;
        u.tryb = ModelUart::ScrUszkodzony;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        SPRAWDZ(log.stan() == brb::StanPortu::Nieobecny);
        log.linia("nic");
        SPRAWDZ(u.wyslane.empty());
    }
    {   // Nadajnik zawieszony: jeden ograniczony cykl oczekiwania, potem port wylaczony (bez blokady).
        ModelUart u;
        u.tryb = ModelUart::NadajnikZawieszony;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        log.linia("dlugi tekst, ktory nie moze zablokowac programu");
        SPRAWDZ(log.stan() == brb::StanPortu::Wylaczony);
        SPRAWDZ(u.odczyty_lsr <= brb::LogSzeregowy::LIMIT_PROB_NADAJNIKA + 2);
        SPRAWDZ(u.wyslane.empty());
    }
    {   // Nadajnik pada w trakcie: wyslane bajty do awarii, potem cisza.
        ModelUart u;
        u.tryb = ModelUart::NadajnikPadaPo;
        u.pada_po = 5;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        log.tekst("0123456789");
        SPRAWDZ(u.wyslane == "01234" && log.stan() == brb::StanPortu::Wylaczony);
    }
    {   // Brak funkcji portow.
        brb::LogSzeregowy log;
        log.inicjalizuj(brb::PortyWeWy{nullptr, nullptr, nullptr});
        SPRAWDZ(log.stan() == brb::StanPortu::Nieobecny);
        log.linia("x");
    }
}

void test_walidacja_firmware() {
    std::printf("[test] walidacja tablic firmware\n");
    struct Przypadek {
        const char* nazwa;
        void (*psuj)(ModelFirmware&);
        EFI_STATUS status;
        brb::BladFirmware blad;
    };
    const Przypadek lista[] = {
        {"poprawna", [](ModelFirmware&) {}, EFI_SUCCESS, brb::BladFirmware::Brak},
        {"sygnatura ST", [](ModelFirmware& f) { f.st.Hdr.Signature ^= 1; }, EFI_INCOMPATIBLE_VERSION, brb::BladFirmware::ZlaSygnaturaTablicy},
        {"HeaderSize ST", [](ModelFirmware& f) { f.st.Hdr.HeaderSize = 112; }, EFI_INCOMPATIBLE_VERSION, brb::BladFirmware::ZaMalaTablica},
        {"rewizja 1.10", [](ModelFirmware& f) { f.st.Hdr.Revision = EFI_REWIZJA(1, 10); }, EFI_INCOMPATIBLE_VERSION, brb::BladFirmware::ZaStaraRewizja},
        {"brak ConOut", [](ModelFirmware& f) { f.st.ConOut = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakKonsoliWyjscia},
        {"brak OutputString", [](ModelFirmware& f) { f.wy.OutputString = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakKonsoliWyjscia},
        {"brak BootServices", [](ModelFirmware& f) { f.st.BootServices = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakUslugRozruchowych},
        {"sygnatura BS", [](ModelFirmware& f) { f.bs.Hdr.Signature = 0; }, EFI_INCOMPATIBLE_VERSION, brb::BladFirmware::ZlaSygnaturaUslug},
        {"HeaderSize BS", [](ModelFirmware& f) { f.bs.Hdr.HeaderSize = 0xF8; }, EFI_INCOMPATIBLE_VERSION, brb::BladFirmware::ZaMaleUslugi},
        {"brak CreateEvent", [](ModelFirmware& f) { f.bs.CreateEvent = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakFunkcjiUslug},
        {"brak WaitForEvent", [](ModelFirmware& f) { f.bs.WaitForEvent = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakFunkcjiUslug},
        {"brak Stall", [](ModelFirmware& f) { f.bs.Stall = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakFunkcjiUslug},
        {"brak HandleProtocol", [](ModelFirmware& f) { f.bs.HandleProtocol = nullptr; }, EFI_UNSUPPORTED, brb::BladFirmware::BrakFunkcjiUslug},
    };
    for (const Przypadek& p : lista) {
        ModelFirmware f;
        f.zainicjuj();
        p.psuj(f);
        const brb::WynikWalidacji w = brb::waliduj_firmware(&f.uchwyt_obrazu, &f.st);
        if (w.status != p.status || w.blad != p.blad) std::printf("  przypadek: %s\n", p.nazwa);
        SPRAWDZ(w.status == p.status && w.blad == p.blad);
    }
    ModelFirmware f;
    f.zainicjuj();
    SPRAWDZ(brb::waliduj_firmware(nullptr, &f.st).blad == brb::BladFirmware::BrakUchwytuObrazu);
    SPRAWDZ(brb::waliduj_firmware(&f.uchwyt_obrazu, nullptr).status == EFI_INVALID_PARAMETER);
    // ConIn nie jest wymagane przez walidacje.
    f.st.ConIn = nullptr;
    SPRAWDZ(brb::waliduj_firmware(&f.uchwyt_obrazu, &f.st).status == EFI_SUCCESS);
    SPRAWDZ(std::strcmp(brb::nazwa_statusu(EFI_TIMEOUT), "EFI_TIMEOUT") == 0);
    SPRAWDZ(std::strcmp(brb::nazwa_statusu(EFI_BLAD(99)), "inny blad") == 0);
    SPRAWDZ(std::strcmp(brb::opis_bledu_firmware(brb::BladFirmware::ZaStaraRewizja), "rewizja UEFI ponizej 2.0") == 0);
}

void test_obraz() {
    std::printf("[test] kontrola zaladowanego obrazu i relokacji\n");
    ModelFirmware f;
    f.zainicjuj();
    SPRAWDZ(brb::sprawdz_obraz(&f.obraz, &f.st));
    EFI_LOADED_IMAGE_PROTOCOL z = f.obraz;
    z.SystemTable = nullptr;
    SPRAWDZ(!brb::sprawdz_obraz(&z, &f.st));
    z = f.obraz; z.Revision = 0x0FFF;
    SPRAWDZ(!brb::sprawdz_obraz(&z, &f.st));
    z = f.obraz; z.ImageSize = 16;                       // kod BRB poza zgloszonym zakresem
    SPRAWDZ(!brb::sprawdz_obraz(&z, &f.st));
    z = f.obraz; z.ImageBase = _end;                     // obraz "gdzie indziej"
    SPRAWDZ(!brb::sprawdz_obraz(&z, &f.st));
    z = f.obraz; z.ImageSize = ~0ull;                    // przepelnienie baza + rozmiar
    SPRAWDZ(!brb::sprawdz_obraz(&z, &f.st));
    z = f.obraz; z.ImageBase = nullptr;
    SPRAWDZ(!brb::sprawdz_obraz(&z, &f.st));
    SPRAWDZ(!brb::sprawdz_obraz(nullptr, &f.st));
}

void test_przebieg_klawisz() {
    std::printf("[test] uruchom: klawisz przez WaitForEvent\n");
    Przebieg p;
    p.fw.klawisze = {EFI_SUCCESS, EFI_SUCCESS, EFI_NOT_READY, EFI_SUCCESS};  // 2 stare klawisze w buforze
    p.fw.wyniki_wait = {0};
    SPRAWDZ(p.uruchom() == EFI_SUCCESS);
    SPRAWDZ(p.stan.klawisz == brb::WynikKlawisza::Klawisz && !p.stan.transliteracja && p.stan.relokacja_ok);
    const std::string oczekiwany =
        "Bursztynowy Program Rozruchowy\r\n"
        "BRB wersja rozwojowa 0.1.0\r\n"
        "Architektura x86-64\r\n"
        "Tryb rozruchu UEFI\r\n"
        "\r\n"
        "Jądro: \\boot\\system_operacyjny.bin (12416 B)\r\n"
        "Weryfikacja ELF64: poprawna, segmenty: 3, wejście: 0x101010\r\n"
        "Plan ładowania: 0x100000-0x104000 (16 KiB).\r\n"
        "Faza 4A: segmenty załadowane i zweryfikowane (stron: 4), pamięć zwolniona.\r\n"
        "Jądro nie zostało uruchomione.\r\n"
        "Inicjalizacja programu rozruchowego zakończona.\r\n"
        "Naciśnij dowolny klawisz, aby powrócić do oprogramowania układowego.\r\n"
        "(Automatyczny powrót po 3 s bez naciśnięcia klawisza.)\r\n"
        "Powrót do oprogramowania układowego.\r\n";
    SPRAWDZ(p.ekran() == oczekiwany);
    if (p.ekran() != oczekiwany) std::printf("  ekran:\n%s\n", p.ekran().c_str());
    // Zadnych twierdzen o zaladowaniu/uruchomieniu jadra (Faza 3 tylko weryfikuje i planuje).
    SPRAWDZ(p.ekran().find("uruchomiono") == std::string::npos && p.ekran().find("załadowano") == std::string::npos);
    SPRAWDZ(p.stan.status_jadra == EFI_SUCCESS && p.stan.segmenty == 3 && p.stan.bufor_zwolniony);
    SPRAWDZ(p.fw.przydzialy_lacznie == 2 && p.fw.zwolnienia == 2);   // bufor pliku + bufor migawki mapy
    SPRAWDZ(p.fw.ostatnia_sciezka == u"\\boot\\system_operacyjny.bin" && p.fw.ostatni_tryb == EFI_FILE_MODE_READ &&
            p.fw.ostatnie_atrybuty == 0);
    SPRAWDZ(p.w_logu("[BRB] etap 2: wolumin rozruchowy otwarty"));
    SPRAWDZ(p.w_logu("[BRB] plik: \\boot\\system_operacyjny.bin rozmiar=12416 B (metadane poprawne)"));
    SPRAWDZ(p.w_logu("[BRB] etap 3: ELF64 poprawny (ET_EXEC, x86-64), PT_LOAD=3 wejscie=0x0000000000101010"));
    SPRAWDZ(p.w_logu("[BRB] plan: seg2 ph=2 plik=0x0000000000003000 fiz=0x0000000000102000 wirt=0x0000000000102000 "
                     "z_pliku=0x0000000000000080 pamiec=0x0000000000002000 zera=0x0000000000001F80 "
                     "wyr=0x0000000000001000 prawa=RW- strony=0x0000000000102000-0x0000000000104000 (2)"));
    SPRAWDZ(p.w_logu("[BRB] plan: multiboot2 plik=0x0000000000001000 dlugosc=48 architektura=0 tagi=2 segment=0"));
    SPRAWDZ(p.w_logu("[BRB] etap 3: plan ladowania gotowy (zweryfikowany opis ladowania)"));
    SPRAWDZ(p.stan.status_4a == EFI_SUCCESS && p.stan.etap_4a == brb::EtapFazy4A::ZasobyZwolnione && p.stan.strony_4a == 4);
    SPRAWDZ(p.fw.adres_przydzialy_lacznie == 3 && p.fw.adres_zwolnienia == 3);
    SPRAWDZ(p.w_logu("[BRB] 4A: obraz zweryfikowany bajt po bajcie"));
    SPRAWDZ(p.w_logu("[BRB] plik: bufor tymczasowy zwolniony"));
    SPRAWDZ(p.fw.clear_wywolania == 1);
    SPRAWDZ(p.fw.atrybuty.size() >= 2 && p.fw.atrybuty[0] == EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK));
    SPRAWDZ(p.fw.ostatni_test_string == u"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ");
    SPRAWDZ(p.fw.ostatni_trigger == 3ull * 10000000ull);
    SPRAWDZ(p.fw.utworzone == 1 && p.fw.anulowane == 1 && p.fw.wait_wywolania == 1 && p.fw.stall_wywolania == 0);
    SPRAWDZ(p.fw.handle_wywolania == 2);   // LoadedImage + Simple File System (tylko urzadzenie obrazu BRB)
    SPRAWDZ(p.w_logu("[BRB] etap 1: wejscie UEFI\r\n"));
    SPRAWDZ(p.w_logu("[BRB] kontrola firmware: OK rewizja=2.70 tablica=120 uslugi=376 B"));
    SPRAWDZ(p.w_logu(" relokacja=OK"));
    SPRAWDZ(p.w_logu("[BRB] konsola: gotowa, polskie znaki=tak"));
    SPRAWDZ(p.w_logu("[BRB] klawiatura: oczekiwanie (WaitForEvent, limit 3 s)"));
    SPRAWDZ(p.w_logu("[BRB] klawiatura: klawisz odebrany"));
    SPRAWDZ(p.w_logu("[BRB] zakonczenie: powrot do firmware status=0x0000000000000000 (EFI_SUCCESS)"));
    p.sprawdz_sprzatanie();
}

// Zachowanie produkcyjne (USTAWIENIA_DOMYSLNE: polskie_znaki_konsoli = false). Firmware, ktore "klamie"
// w TestString (jak OVMF 2024.02: EFI_SUCCESS dla kazdego znaku), nie moze wymusic polskich liter.
void test_przebieg_domyslny_ascii() {
    std::printf("[test] uruchom: ustawienia domyslne - transliteracja ASCII niezaleznie od TestString\n");
    Przebieg p;
    p.fw.status_test_string = EFI_SUCCESS;   // firmware twierdzi, ze ma wszystkie glify
    p.fw.wyniki_wait = {0};
    p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
    brb::Ustawienia u = brb::USTAWIENIA_DOMYSLNE;
    u.limit_czasu_s = 3;
    SPRAWDZ(!brb::USTAWIENIA_DOMYSLNE.polskie_znaki_konsoli && brb::USTAWIENIA_DOMYSLNE.limit_czasu_s == 60);
    SPRAWDZ(p.uruchom(u) == EFI_SUCCESS && p.stan.transliteracja);
    SPRAWDZ(p.fw.ostatni_test_string.empty());   // TestString nie jest nawet pytany
    SPRAWDZ(p.na_ekranie("Inicjalizacja programu rozruchowego zakonczona.\r\n"));
    SPRAWDZ(p.na_ekranie("Nacisnij dowolny klawisz, aby powrocic do oprogramowania ukladowego.\r\n"));
    SPRAWDZ(p.na_ekranie("(Automatyczny powrot po 3 s bez nacisniecia klawisza.)\r\n"));
    SPRAWDZ(p.na_ekranie("Powrot do oprogramowania ukladowego.\r\n"));
    for (char16_t c : p.fw.ekran) SPRAWDZ(c >= 0x20 ? c <= 0x7E : (c == u'\r' || c == u'\n'));
    SPRAWDZ(p.w_logu("[BRB] konsola: transliteracja ASCII (czcionka firmware nie gwarantuje polskich liter)"));
    SPRAWDZ(p.w_logu("[BRB] konsola: gotowa, polskie znaki=transliteracja ASCII"));
    p.sprawdz_sprzatanie();
}

void test_przebieg_limit_czasu() {
    std::printf("[test] uruchom: limit czasu, pobudzenia bez klawisza\n");
    {
        Przebieg p;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_TIMEOUT);
        SPRAWDZ(p.stan.klawisz == brb::WynikKlawisza::LimitCzasu);
        SPRAWDZ(p.na_ekranie("Upłynął limit czasu. Powrót do oprogramowania układowego."));
        SPRAWDZ(p.w_logu("(EFI_TIMEOUT)"));
        p.sprawdz_sprzatanie();
    }
    {   // Zdarzenie klawisza bez klawisza (NOT_READY) - nieskonczenie: petla jest ograniczona.
        Przebieg p;
        p.fw.wyniki_wait = std::vector<UINTN>(100, 0);
        SPRAWDZ(p.uruchom() == EFI_TIMEOUT);
        SPRAWDZ(p.fw.wait_wywolania == 16);
        p.sprawdz_sprzatanie();
    }
    {   // Kilka pustych pobudzen, potem klawisz.
        Przebieg p;
        p.fw.wyniki_wait = {0, 0, 0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_NOT_READY, EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.fw.wait_wywolania == 3);
        p.sprawdz_sprzatanie();
    }
}

void test_przebieg_awaryjny() {
    std::printf("[test] uruchom: tryb awaryjny bez zdarzen (Stall, ograniczony)\n");
    {   // CreateEvent nieudane -> Stall; brak klawisza -> limit czasu po dokladnie limit/krok krokach.
        Przebieg p;
        p.fw.status_create = EFI_OUT_OF_RESOURCES;
        SPRAWDZ(p.uruchom({3, 100, 1, true}) == EFI_TIMEOUT);
        SPRAWDZ(p.stan.tryb_awaryjny_klawiatury);
        SPRAWDZ(p.fw.stall_wywolania == 30 && p.fw.suma_stall_us == 3000000);
        SPRAWDZ(p.w_logu("tryb awaryjny Stall"));
        p.sprawdz_sprzatanie();
    }
    {   // SetTimer nieudane -> zdarzenie zamkniete, Stall, klawisz po 4 krokach.
        Przebieg p;
        p.fw.status_set_timer = EFI_DEVICE_ERROR;
        p.fw.klawisze = {EFI_NOT_READY, EFI_NOT_READY, EFI_NOT_READY, EFI_NOT_READY, EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS);
        SPRAWDZ(p.fw.utworzone == 1 && p.fw.zamkniete == 1 && p.fw.stall_wywolania == 4);
        p.sprawdz_sprzatanie();
    }
    {   // WaitForEvent nieudane -> zegar anulowany i zamkniety, tryb awaryjny.
        Przebieg p;
        p.fw.status_wait = EFI_INVALID_PARAMETER;
        p.fw.klawisze = {EFI_NOT_READY, EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.stan.tryb_awaryjny_klawiatury);
        SPRAWDZ(p.fw.anulowane == 1 && p.fw.zamkniete == 1);
        p.sprawdz_sprzatanie();
    }
    {   // Brak WaitForKey -> bez CreateEvent, tryb awaryjny.
        Przebieg p;
        p.fw.we.WaitForKey = nullptr;
        SPRAWDZ(p.uruchom({1, 250, 1, true}) == EFI_TIMEOUT);
        SPRAWDZ(p.fw.utworzone == 0 && p.fw.stall_wywolania == 4);
    }
}

void test_przebieg_bledy() {
    std::printf("[test] uruchom: bledy konsoli, klawiatury, LoadedImage, tablic\n");
    {   // OutputString zawodzi przy pierwszej linii: blad zwrocony, log kontrolowany.
        Przebieg p;
        p.fw.output_blad_od = 0;
        SPRAWDZ(p.uruchom() == EFI_DEVICE_ERROR);
        SPRAWDZ(p.w_logu("[BRB] blad kontrolowany: konsola: OutputString status=0x8000000000000007 (EFI_DEVICE_ERROR)"));
        SPRAWDZ(p.fw.utworzone == 0);
    }
    {   // OutputString zawodzi w polowie ekranu.
        Przebieg p;
        p.fw.output_blad_od = 4;
        SPRAWDZ(p.uruchom() == EFI_DEVICE_ERROR && p.fw.wywolania_output == 5);
    }
    {   // Ostrzezenie o glifach nie jest bledem.
        Przebieg p;
        p.fw.ostrzezenie_glifu = true;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_TIMEOUT && p.stan.ostrzezenia_glifow == 13);
    }
    {   // Brak polskich glifow (TestString -> EFI_UNSUPPORTED): transliteracja na ekranie.
        Przebieg p;
        p.fw.status_test_string = EFI_UNSUPPORTED;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_TIMEOUT && p.stan.transliteracja);
        SPRAWDZ(p.na_ekranie("Inicjalizacja programu rozruchowego zakonczona.\r\n"));
        SPRAWDZ(p.na_ekranie("Nacisnij dowolny klawisz, aby powrocic do oprogramowania ukladowego."));
        for (char16_t c : p.fw.ekran) SPRAWDZ(c < 0x80);
        SPRAWDZ(p.w_logu("transliteracja ASCII"));
    }
    {   // Brak TestString: ostroznie - transliteracja.
        Przebieg p;
        p.fw.wy.TestString = nullptr;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_TIMEOUT && p.stan.transliteracja);
    }
    {   // ClearScreen i SetAttribute zawodza: kontynuacja.
        Przebieg p;
        p.fw.status_clear = EFI_DEVICE_ERROR;
        p.fw.status_atrybut = EFI_UNSUPPORTED;
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.w_logu("ClearScreen nieudane"));
    }
    {   // ReadKeyStroke: blad urzadzenia podczas oprozniania bufora.
        Przebieg p;
        p.fw.klawisze = {EFI_DEVICE_ERROR};
        SPRAWDZ(p.uruchom() == EFI_DEVICE_ERROR && p.fw.utworzone == 0);
        SPRAWDZ(p.na_ekranie("Błąd klawiatury UEFI."));
    }
    {   // ReadKeyStroke: blad urzadzenia po pobudzeniu.
        Przebieg p;
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_DEVICE_ERROR};
        SPRAWDZ(p.uruchom() == EFI_DEVICE_ERROR);
        p.sprawdz_sprzatanie();
    }
    {   // Ciagly strumien klawiszy podczas oprozniania: ograniczone do 32 odczytow, potem oczekiwanie.
        Przebieg p;
        p.fw.klawisze = std::vector<EFI_STATUS>(40, EFI_SUCCESS);
        p.fw.wyniki_wait = {0};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.fw.read_wywolania == 33);
        p.sprawdz_sprzatanie();
    }
    {   // Brak ConIn: komunikat, ograniczone oczekiwanie, EFI_UNSUPPORTED.
        Przebieg p;
        p.fw.st.ConIn = nullptr;
        SPRAWDZ(p.uruchom({60, 100, 2, true}) == EFI_UNSUPPORTED);
        SPRAWDZ(p.fw.stall_wywolania == 20 && p.fw.suma_stall_us == 2000000);
        SPRAWDZ(p.na_ekranie("Klawiatura UEFI jest niedostępna."));
    }
    {   // HandleProtocol zawodzi.
        Przebieg p;
        p.fw.status_handle = EFI_UNSUPPORTED;
        SPRAWDZ(p.uruchom() == EFI_UNSUPPORTED);
        SPRAWDZ(p.w_logu("[BRB] blad kontrolowany: LoadedImage"));
        SPRAWDZ(p.na_ekranie("Blad: brak protokolu EFI_LOADED_IMAGE"));
    }
    {   // HandleProtocol zwraca sukces z pustym wskaznikiem.
        Przebieg p;
        p.fw.obraz_null = true;
        SPRAWDZ(p.uruchom() == EFI_NOT_FOUND);
    }
    {   // LoadedImage niespojne (inna tablica systemowa) -> EFI_LOAD_ERROR, bez ekranu powitalnego.
        Przebieg p;
        p.fw.obraz.SystemTable = nullptr;
        SPRAWDZ(p.uruchom() == EFI_LOAD_ERROR && !p.stan.relokacja_ok);
        SPRAWDZ(!p.na_ekranie("Inicjalizacja programu"));
        SPRAWDZ(p.w_logu("relokacja=BLAD"));
    }
    {   // Zla sygnatura tablicy systemowej: konsola NIE jest uzywana.
        Przebieg p;
        p.fw.st.Hdr.Signature = 0;
        SPRAWDZ(p.uruchom() == EFI_INCOMPATIBLE_VERSION);
        SPRAWDZ(p.fw.wywolania_output == 0 && p.fw.handle_wywolania == 0);
        SPRAWDZ(p.w_logu("kontrola firmware: BLAD - zla sygnatura tablicy systemowej"));
    }
    {   // Zla tablica uslug: konsola (zweryfikowana wczesniej) pokazuje komunikat ASCII, zadnych wywolan uslug.
        Przebieg p;
        p.fw.bs.Hdr.HeaderSize = 8;
        SPRAWDZ(p.uruchom() == EFI_INCOMPATIBLE_VERSION);
        SPRAWDZ(p.na_ekranie("Blad: oprogramowanie ukladowe UEFI nie spelnia wymagan programu rozruchowego."));
        SPRAWDZ(p.fw.handle_wywolania == 0 && p.fw.stall_wywolania == 0);
    }
    {   // Brak tablicy systemowej.
        ModelUart u;
        brb::LogSzeregowy log;
        log.inicjalizuj(u.porty());
        const brb::DostepFizyczny brak_pamieci{[](void*, uint64_t, uint64_t) -> uint8_t* { return nullptr; }, nullptr};
        SPRAWDZ(brb::uruchom(reinterpret_cast<EFI_HANDLE>(1), nullptr, log, brb::USTAWIENIA_DOMYSLNE, brak_pamieci) == EFI_INVALID_PARAMETER);
        SPRAWDZ(u.wyslane.find("brak tablicy systemowej") != std::string::npos);
    }
    {   // Brak COM1: program dziala normalnie.
        Przebieg p;
        p.uart.tryb = ModelUart::Brak;
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.uart.wyslane.empty());
    }
}

// Faza 3: kazda sciezka bledu warstwy plikow UEFI - status, kod BRB, komunikat, sprzatanie zasobow.
void test_jadro_plik() {
    std::printf("[test] Faza 3: odczyt pliku jadra przez UEFI (bledy i sprzatanie)\n");
    struct P {
        const char* nazwa;
        void (*ustaw)(ModelFirmware&);
        EFI_STATUS status;            // status zwrocony do firmware po klawiszu
        brb::BladPliku blad;
        int alokacje;                 // oczekiwana liczba AllocatePages
    };
    const std::vector<uint8_t> wzor = fixture::jadro_minimalne().b;
    const UINT64 n = wzor.size();
    (void)n;
    const P lista[] = {
        {"brak pliku", [](ModelFirmware& f) { f.plik_istnieje = false; }, EFI_NOT_FOUND, brb::BladPliku::BrakPliku, 0},
        {"plik pusty", [](ModelFirmware& f) { f.dane_pliku.clear(); }, EFI_LOAD_ERROR, brb::BladPliku::PustyPlik, 0},
        {"katalog", [](ModelFirmware& f) { f.katalog = true; }, EFI_LOAD_ERROR, brb::BladPliku::Katalog, 0},
        {"plik > 64 MiB (metadane)", [](ModelFirmware& f) { f.rozmiar_zgloszony_ustawiony = true; f.rozmiar_zgloszony = brb::ELF_MAKS_ROZMIAR_PLIKU + 1; },
         EFI_BAD_BUFFER_SIZE, brb::BladPliku::ZaDuzyPlik, 0},
        {"rozmiar 2^64-1 (metadane)", [](ModelFirmware& f) { f.rozmiar_zgloszony_ustawiony = true; f.rozmiar_zgloszony = ~0ull; },
         EFI_BAD_BUFFER_SIZE, brb::BladPliku::ZaDuzyPlik, 0},
        {"brak systemu plikow", [](ModelFirmware& f) { f.status_sfs = EFI_UNSUPPORTED; }, EFI_UNSUPPORTED, brb::BladPliku::BrakSystemuPlikow, 0},
        {"brak DeviceHandle", [](ModelFirmware& f) { f.obraz.DeviceHandle = nullptr; }, EFI_UNSUPPORTED, brb::BladPliku::BrakSystemuPlikow, 0},
        {"OpenVolume blad", [](ModelFirmware& f) { f.status_open_volume = EFI_DEVICE_ERROR; }, EFI_DEVICE_ERROR, brb::BladPliku::OtwarcieWoluminu, 0},
        {"Open blad urzadzenia", [](ModelFirmware& f) { f.status_open = EFI_DEVICE_ERROR; }, EFI_DEVICE_ERROR, brb::BladPliku::OtwarciePliku, 0},
        {"GetInfo blad", [](ModelFirmware& f) { f.status_info = EFI_DEVICE_ERROR; }, EFI_DEVICE_ERROR, brb::BladPliku::Metadane, 0},
        {"GetInfo zada 600 B", [](ModelFirmware& f) { f.info_wymagany = 600; }, EFI_LOAD_ERROR, brb::BladPliku::Metadane, 0},
        {"GetInfo rozmiar < stala czesc", [](ModelFirmware& f) { f.info_zwracany_rozmiar = 40; }, EFI_LOAD_ERROR, brb::BladPliku::NiespojneMetadane, 0},
        {"GetInfo rozmiar > bufor", [](ModelFirmware& f) { f.info_zwracany_rozmiar = 4096; }, EFI_LOAD_ERROR, brb::BladPliku::NiespojneMetadane, 0},
        {"pole Size > zwrocony rozmiar", [](ModelFirmware& f) { f.info_pole_size = 300; }, EFI_LOAD_ERROR, brb::BladPliku::NiespojneMetadane, 0},
        {"nazwa bez NUL", [](ModelFirmware& f) { f.info_bez_nul = true; }, EFI_LOAD_ERROR, brb::BladPliku::NiespojneMetadane, 0},
        {"brak pamieci", [](ModelFirmware& f) { f.status_alloc = EFI_OUT_OF_RESOURCES; }, EFI_OUT_OF_RESOURCES, brb::BladPliku::Alokacja, 0},
        {"blad odczytu (1. Read)", [](ModelFirmware& f) { f.read_blad_od = 0; }, EFI_DEVICE_ERROR, brb::BladPliku::Odczyt, 1},
        {"blad odczytu w sondzie konca", [](ModelFirmware& f) { f.read_blad_od = 1; }, EFI_DEVICE_ERROR, brb::BladPliku::Odczyt, 1},
        {"przedwczesny koniec pliku", [](ModelFirmware& f) { f.rozmiar_zgloszony_ustawiony = true; f.rozmiar_zgloszony = f.dane_pliku.size() + 100; },
         EFI_LOAD_ERROR, brb::BladPliku::PrzedwczesnyKoniec, 1},
        {"plik dluzszy niz metadane", [](ModelFirmware& f) { f.rozmiar_zgloszony_ustawiony = true; f.rozmiar_zgloszony = f.dane_pliku.size() - 100; },
         EFI_LOAD_ERROR, brb::BladPliku::PlikDluzszyNizZgloszono, 1},
        {"porcje po 1 B (brak postepu w limicie)", [](ModelFirmware& f) { f.maks_porcja = 1; }, EFI_LOAD_ERROR, brb::BladPliku::ZaDuzoOdczytow, 1},
        {"Read zglasza wiecej niz bufor", [](ModelFirmware& f) { f.read_zawyza = true; }, EFI_LOAD_ERROR, brb::BladPliku::NiespojnyOdczyt, 1},
    };
    for (const P& c : lista) {
        Przebieg p;
        c.ustaw(p.fw);
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
        const EFI_STATUS s = p.uruchom();
        if (s != c.status || p.stan.blad_pliku != c.blad)
            std::printf("  przypadek '%s': status=0x%llx blad=%d\n", c.nazwa, (unsigned long long)s, int(p.stan.blad_pliku));
        SPRAWDZ(s == c.status && p.stan.blad_pliku == c.blad && p.stan.status_jadra == c.status);
        SPRAWDZ(p.fw.przydzialy_lacznie == c.alokacje);
        SPRAWDZ(p.na_ekranie("Błąd pliku jądra (BRB-E2"));
        SPRAWDZ(p.w_logu("[BRB] blad pliku: BRB-E2"));
        SPRAWDZ(!p.w_logu("etap 3:"));
        SPRAWDZ(p.na_ekranie("Naciśnij dowolny klawisz"));   // program kontynuuje do kontrolowanego powrotu
        p.sprawdz_sprzatanie();
    }
    {   // Odczyt porcjami po 512 B (dopuszczalne skrocone odczyty firmware): sukces, wiele wywolan Read.
        Przebieg p;
        p.fw.maks_porcja = 512;
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.stan.status_jadra == EFI_SUCCESS);
        SPRAWDZ(p.fw.read_wywolania_pliku == 25 + 1);   // 12416 B / 512 = 24,25 -> 25 porcji + sonda konca
        p.sprawdz_sprzatanie();
    }
    {   // Niepoprawny ELF (zla sygnatura): plik odczytany, bufor zwolniony, EFI_LOAD_ERROR, kod BRB-E103.
        Przebieg p;
        p.fw.dane_pliku[0] = 0;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_LOAD_ERROR && p.stan.blad_elf == brb::BladElf::ZlaSygnatura && p.stan.bufor_zwolniony);
        SPRAWDZ(p.w_logu("[BRB] blad ELF: BRB-E103 brak sygnatury ELF"));
        SPRAWDZ(p.na_ekranie("Błąd weryfikacji ELF64 (BRB-E103): brak sygnatury ELF."));
        SPRAWDZ(p.fw.przydzialy_lacznie == 1 && p.fw.zwolnienia == 1);
        p.sprawdz_sprzatanie();
    }
    {   // Segment W+X: indeks naglowka programu w logu.
        Przebieg p;
        fixture::Obraz o{p.fw.dane_pliku};
        o.w32(fixture::ph(1) + fixture::P_FLAGS, 7);
        p.fw.dane_pliku = o.b;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_LOAD_ERROR && p.stan.blad_elf == brb::BladElf::ZapisIWykonanie);
        SPRAWDZ(p.w_logu("segment jednoczesnie zapisywalny i wykonywalny ph=1"));
        p.sprawdz_sprzatanie();
    }
    {   // Niepoprawny protokol SFS (rewizja 0): bez wywolywania OpenVolume.
        Przebieg p;
        p.fw.sfs.Revision = 0;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_UNSUPPORTED && p.stan.blad_pliku == brb::BladPliku::ZlyProtokol);
        SPRAWDZ(p.fw.otwarcia_plikow == 0);
        p.sprawdz_sprzatanie();
    }
    {   // Niepoprawny protokol korzenia (brak Read): korzen NIE jest wywolywany (takze Close) - patrz plik_jadra.cpp.
        Przebieg p;
        p.fw.korzen.Read = nullptr;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_UNSUPPORTED && p.stan.blad_pliku == brb::BladPliku::ZlyProtokol);
        SPRAWDZ(p.fw.otwarcia_plikow == 1 && p.fw.zamkniecia_plikow == 0 && p.fw.przydzialy.empty());
    }
    {   // Niepoprawny protokol otwartego pliku (rewizja 0): korzen zamkniety, plik nie wywolywany.
        Przebieg p;
        p.fw.plik.Revision = 0;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_UNSUPPORTED && p.fw.otwarcia_plikow == 2 && p.fw.zamkniecia_plikow == 1);
    }
    {   // Limit czasu przy poprawnym jadrze: status Fazy 2 (EFI_TIMEOUT) zachowany.
        Przebieg p;
        p.fw.wyniki_wait = {1};
        SPRAWDZ(p.uruchom() == EFI_TIMEOUT && p.stan.status_jadra == EFI_SUCCESS);
        p.sprawdz_sprzatanie();
    }
}

// T-ELF-PROD w pelnym przebiegu programu: prawdziwe jadro produkcyjne przez model firmware UEFI.
void test_jadro_produkcyjne(const char* sciezka) {
    FILE* f = std::fopen(sciezka, "rb");
    if (!f) { std::printf("[test] jadro produkcyjne: NIE URUCHOMIONO (brak pliku %s)\n", sciezka); return; }
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + k);
    std::fclose(f);
    std::printf("[test] jadro produkcyjne przez model UEFI: %s (%zu B)\n", sciezka, d.size());
    {   // Wariant A: zakres 1-16 MiB wolny w modelu -> pelna Faza 4A na prawdziwych bajtach jadra.
        Przebieg p;
        p.fw.dane_pliku = d;
        p.fw.maks_porcja = 65536;   // realistyczne porcje
        p.fw.fiz.assign(0xF00000, 0xCC);
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
        const bool ok_a = p.uruchom() == EFI_SUCCESS && p.stan.status_jadra == EFI_SUCCESS && p.stan.segmenty == 4;
        if (!ok_a) {
            const size_t i = p.uart.wyslane.find("[BRB] 4A");
            std::printf("  wariant A: status=0x%llx log 4A:\n%s\n", (unsigned long long)p.status,
                        i == std::string::npos ? "(brak)" : p.uart.wyslane.substr(i + 1400, 2500).c_str());
        }
        SPRAWDZ(ok_a);
        SPRAWDZ(p.w_logu("[BRB] plan: seg3 ph=3 plik=0x00000000001AB000 fiz=0x00000000002AA000"));
        SPRAWDZ(p.w_logu("wejscie_wirt=0x00000000001193E2 wejscie_fiz=0x00000000001193E2 seg_wejscia=1"));
        SPRAWDZ(p.na_ekranie("Plan ładowania: 0x100000-0xD6F000 (12732 KiB)."));
        SPRAWDZ(p.na_ekranie("Faza 4A: segmenty załadowane i zweryfikowane (stron: 3183), pamięć zwolniona."));
        SPRAWDZ(p.w_logu("[BRB] 4A: zakres 3 przydzielony fiz=0x00000000002AA000-0x0000000000D6F000 strony=2757"));
        SPRAWDZ(p.w_logu("[BRB] 4A: segmenty skopiowane: bajty_z_pliku=9328944 bss_wyzerowane=3699152"));
        SPRAWDZ(p.w_logu("[BRB] 4A: weryfikacja seg3 fiz=0x00000000002AA000 z_pliku=7592768 pamiec=11291920 bss=3699152 dane=OK bss=OK"));
        SPRAWDZ(p.fw.adres_przydzialy_lacznie == 4 && p.fw.adres_zwolnienia == 4);
        p.sprawdz_sprzatanie();
    }
    {   // Wariant B: uklad jak w OVMF (ACPI NVS / BS data od 0x800000) -> blokada, wycofanie 3 zakresow.
        Przebieg p;
        p.fw.dane_pliku = d;
        p.fw.maks_porcja = 1024 * 1024;
        p.fw.fiz.assign(0xF00000, 0xCC);
        p.fw.zajete = {{0x800000, 0x808000}, {0x810000, 0x1000000}};
        p.fw.wyniki_wait = {0};
        p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS};
        SPRAWDZ(p.uruchom() == EFI_NOT_FOUND && p.stan.blad_4a == brb::BladPrzydzialu::ZakresZajety);
        SPRAWDZ(p.w_logu("[BRB] blad fazy 4A: BRB-E506 pamiec wymagana przez jadro jest zajeta zakres=3"));
        SPRAWDZ(p.w_logu("[BRB] 4A: wycofano zakresow=3 bledy_sprzatania=0"));
        SPRAWDZ(p.fw.adres_przydzialy_lacznie == 3 && p.fw.adres_zwolnienia == 3);
        SPRAWDZ(p.fw.fiz[0x800000 - 0x100000] == 0xCC);   // pamiec firmware nietknieta
        p.sprawdz_sprzatanie();
    }
}

// Faza 4A na poziomie UEFI: przydzial transakcyjny, wycofanie, zle adresy, bledy zwolnienia, mapa pamieci.
void test_faza4a() {
    std::printf("[test] Faza 4A: przydzial AllocateAddress, wycofanie, mapa pamieci, sprzatanie\n");
    auto przygotuj = [](Przebieg& p) { p.fw.wyniki_wait = {0}; p.fw.klawisze = {EFI_NOT_READY, EFI_SUCCESS}; };
    auto bez_zapisu_obcego = [](const Przebieg& p) {
        // Model: wszystkie przydzialy zwolnione; bajty strony zajetej przez firmware nienaruszone.
        for (auto& z : p.fw.zajete)
            for (UINT64 a = z.first; a < z.second && a - p.fw.fiz_baza < p.fw.fiz.size(); ++a)
                if (p.fw.fiz[a - p.fw.fiz_baza] != 0xCC) return false;
        return true;
    };
    {   // Odmowa przed pierwszym przydzialem (zajeta strona .boot).
        Przebieg p;
        przygotuj(p);
        p.fw.zajete = {{0x100000, 0x101000}};
        SPRAWDZ(p.uruchom() == EFI_NOT_FOUND && p.stan.blad_4a == brb::BladPrzydzialu::ZakresZajety);
        SPRAWDZ(p.fw.adres_przydzialy_lacznie == 0 && p.w_logu("wycofano zakresow=0"));
        SPRAWDZ(bez_zapisu_obcego(p) && p.na_ekranie("Błąd fazy 4A (BRB-E506)"));
        SPRAWDZ(p.na_ekranie("Jądro nie zostało uruchomione."));
        p.sprawdz_sprzatanie();
    }
    {   // Odmowa po pierwszym udanym przydziale.
        Przebieg p;
        przygotuj(p);
        p.fw.zajete = {{0x101000, 0x102000}};
        SPRAWDZ(p.uruchom() == EFI_NOT_FOUND && p.fw.adres_przydzialy_lacznie == 1 && p.fw.adres_zwolnienia == 1);
        SPRAWDZ(p.w_logu("wycofano zakresow=1") && bez_zapisu_obcego(p));
        p.sprawdz_sprzatanie();
    }
    {   // Odmowa po wielu udanych przydzialach (wstrzykniecie w 3. wywolaniu).
        Przebieg p;
        przygotuj(p);
        p.fw.adres_odmow_od = 2;
        SPRAWDZ(p.uruchom() == EFI_NOT_FOUND && p.fw.adres_przydzialy_lacznie == 2 && p.fw.adres_zwolnienia == 2);
        SPRAWDZ(p.w_logu("wycofano zakresow=2"));
        SPRAWDZ(p.w_logu("[BRB] mapa: zakres 2 "));   // dowody z mapy po wycofaniu
        p.sprawdz_sprzatanie();
    }
    {   // Firmware zwraca inny adres niz zadany: przydzial jest wlasnoscia BRB i zostaje zwolniony.
        Przebieg p;
        przygotuj(p);
        p.fw.adres_zly_przy = 1;
        SPRAWDZ(p.uruchom() == EFI_LOAD_ERROR && p.stan.blad_4a == brb::BladPrzydzialu::ZlyAdresZwrocony);
        SPRAWDZ(p.fw.adres_przydzialy_lacznie == 2 && p.fw.adres_zwolnienia == 2 && p.w_logu("zwrocil inny adres="));
        p.sprawdz_sprzatanie();
    }
    {   // FreePages zawodzi przy zwalnianiu po udanej weryfikacji: blad raportowany, brak podwojnego zwolnienia.
        Przebieg p;
        przygotuj(p);
        p.fw.free_fiz_blad_przy = 0;   // pierwsze zwolnienie (zakres 2, kolejnosc odwrotna)
        SPRAWDZ(p.uruchom() == EFI_DEVICE_ERROR && p.stan.blad_4a == brb::BladPrzydzialu::SprzatanieNieudane);
        SPRAWDZ(p.stan.bledy_sprzatania_4a == 1 && p.fw.free_fiz_wywolania == 3 && p.fw.adres_zwolnienia == 2);
        SPRAWDZ(p.fw.przydzialy_fiz.size() == 1);   // porzucony (firmware go nie zwolnilo), nie zwalniany ponownie
        SPRAWDZ(p.w_logu("zakres porzucony, bez ponownego zwalniania"));
        SPRAWDZ(p.na_ekranie("Błąd fazy 4A (BRB-E510): zwolnienie pamieci nieudane."));
        p.fw.przydzialy_fiz.clear();
        p.sprawdz_sprzatanie();
    }
    {   // FreePages zawodzi podczas wycofania: pozostale zakresy nadal zwalniane.
        Przebieg p;
        przygotuj(p);
        p.fw.adres_odmow_od = 2;
        p.fw.free_fiz_blad_przy = 0;
        SPRAWDZ(p.uruchom() == EFI_NOT_FOUND && p.fw.free_fiz_wywolania == 2 && p.fw.adres_zwolnienia == 1);
        SPRAWDZ(p.w_logu("wycofano zakresow=1 bledy_sprzatania=1"));
    }
    {   // Mapa: niestandardowy krok deskryptora (56 i 64 B) - poprawna praca.
        for (UINTN krok : {56u, 64u}) {
            Przebieg p;
            przygotuj(p);
            p.fw.mapa_krok = krok;
            SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.stan.status_4a == EFI_SUCCESS);
            SPRAWDZ(p.w_logu(krok == 56 ? "deskryptor=56 B" : "deskryptor=64 B"));
            p.sprawdz_sprzatanie();
        }
    }
    {   // Mapa rosnie raz (zapas 16 deskryptorow wystarcza) i rosnie zawsze (limit prob).
        Przebieg p;
        przygotuj(p);
        p.fw.mapa_wzrost = 4;
        SPRAWDZ(p.uruchom() == EFI_SUCCESS && p.w_logu("proby=1"));
        p.sprawdz_sprzatanie();
        Przebieg q;
        przygotuj(q);
        q.fw.mapa_wzrost = 40;   // kazde wywolanie dodaje wiecej niz zapas
        SPRAWDZ(q.uruchom() == EFI_LOAD_ERROR && q.stan.blad_4a == brb::BladPrzydzialu::MapaPamieci);
        SPRAWDZ(q.w_logu("mapa pamieci zmienia sie przy kazdej probie") && q.fw.adres_przydzialy_lacznie == 0);
        q.sprawdz_sprzatanie();
    }
    {   // Mapa: zla wersja deskryptora, krok 44 B, pierwsze wywolanie z EFI_SUCCESS, blad firmware, brak funkcji.
        struct M4 { void (*u)(ModelFirmware&); EFI_STATUS st; const char* log; };
        const M4 lista[] = {
            {[](ModelFirmware& f) { f.mapa_wersja = 2; }, EFI_LOAD_ERROR, "nieobslugiwana wersja deskryptora"},
            {[](ModelFirmware& f) { f.mapa_krok = 44; }, EFI_LOAD_ERROR, "migawka mapy pamieci niepoprawna"},
            {[](ModelFirmware& f) { f.mapa_pierwsze_sukces = true; }, EFI_DEVICE_ERROR, "nieoczekiwany status zapytania o rozmiar mapy"},
            {[](ModelFirmware& f) { f.mapa_blad_od = 1; }, EFI_DEVICE_ERROR, "GetMemoryMap zwrocil blad"},
            {[](ModelFirmware& f) { f.bs.GetMemoryMap = nullptr; }, EFI_UNSUPPORTED, "brak uslug pamieci firmware"},
        };
        for (const M4& c : lista) {
            Przebieg p;
            przygotuj(p);
            c.u(p.fw);
            const EFI_STATUS s = p.uruchom();
            if (s != c.st || !p.w_logu(c.log)) std::printf("  mapa: oczekiwano '%s', status=0x%llx\n", c.log, (unsigned long long)s);
            SPRAWDZ(s == c.st && p.stan.blad_4a == brb::BladPrzydzialu::MapaPamieci && p.w_logu(c.log));
            SPRAWDZ(p.fw.adres_przydzialy_lacznie == 0);   // bez mapy brak przydzialu
            p.sprawdz_sprzatanie();
        }
    }
    {   // Brak pamieci na bufor mapy (AllocatePages AnyPages zawodzi dopiero po wczytaniu pliku).
        Przebieg p;
        przygotuj(p);
        p.fw.any_blad_przy = 1;   // 0 = bufor pliku jadra, 1 = bufor migawki mapy
        SPRAWDZ(p.uruchom() == EFI_OUT_OF_RESOURCES && p.stan.blad_4a == brb::BladPrzydzialu::MapaPamieci);
        SPRAWDZ(p.w_logu("brak pamieci na bufor mapy") && p.fw.adres_przydzialy_lacznie == 0);
        SPRAWDZ(p.w_logu("[BRB] plik: bufor tymczasowy zwolniony"));
        p.sprawdz_sprzatanie();
    }
}

// Bezposrednie wywolania orkiestratora: konflikty z obrazem BRB i buforem zrodla, brak funkcji.
void test_faza4a_konflikty() {
    std::printf("[test] Faza 4A: konflikty z zywymi zasobami BRB (bez przydzialu)\n");
    const std::vector<uint8_t> d = fixture::jadro_minimalne().b;
    brb::PlanLadowania plan;
    SPRAWDZ(brb::zbuduj_plan_ladowania(d.data(), d.size(), brb::POLITYKA_JADRA_BURSZTYN, &plan).blad == brb::BladElf::Brak);
    std::vector<uint8_t> kopia(d);
    const brb::BuforJadra bufor{reinterpret_cast<uintptr_t>(kopia.data()), (d.size() + 4095) / 4096, d.size()};
    {
        Przebieg p;
        M = &p.fw;
        EFI_LOADED_IMAGE_PROTOCOL obraz = p.fw.obraz;
        obraz.ImageBase = reinterpret_cast<VOID*>(uintptr_t(0x101800));   // obraz BRB "w" zakresie .text jadra
        obraz.ImageSize = 0x1000;
        brb::RaportFazy4A r;
        p.log.inicjalizuj(p.uart.porty());
        SPRAWDZ(brb::faza4a_zaladuj_i_zweryfikuj(&p.fw.bs, &obraz, bufor, plan, p.pamiec(), p.log, &r) == EFI_LOAD_ERROR);
        SPRAWDZ(r.blad == brb::BladPrzydzialu::KonfliktZObrazemBrb && r.zakres_bledu == 1 && p.fw.adres_wywolania == 0);
        M = nullptr;
    }
    {
        Przebieg p;
        M = &p.fw;
        const brb::BuforJadra w_celu{0x102000, 4, d.size()};   // bufor zrodla "na" zakresie .data
        brb::RaportFazy4A r;
        p.log.inicjalizuj(p.uart.porty());
        SPRAWDZ(brb::faza4a_zaladuj_i_zweryfikuj(&p.fw.bs, &p.fw.obraz, w_celu, plan, p.pamiec(), p.log, &r) == EFI_LOAD_ERROR);
        SPRAWDZ(r.blad == brb::BladPrzydzialu::KonfliktZBuforemZrodla && r.zakres_bledu == 2 && p.fw.adres_wywolania == 0);
        SPRAWDZ(p.uart.wyslane.find("BRB-E504") != std::string::npos);
        M = nullptr;
    }
    {
        Przebieg p;
        M = &p.fw;
        p.fw.bs.AllocatePages = nullptr;
        brb::RaportFazy4A r;
        p.log.inicjalizuj(p.uart.porty());
        SPRAWDZ(brb::faza4a_zaladuj_i_zweryfikuj(&p.fw.bs, &p.fw.obraz, bufor, plan, p.pamiec(), p.log, &r) == EFI_UNSUPPORTED);
        SPRAWDZ(r.blad == brb::BladPrzydzialu::BrakFunkcji);
        SPRAWDZ(brb::faza4a_zaladuj_i_zweryfikuj(&p.fw.bs, &p.fw.obraz, bufor, plan, p.pamiec(), p.log, nullptr) == EFI_INVALID_PARAMETER);
        M = nullptr;
    }
    {   // Plan niezgodny z buforem (np. podmieniony bajt po walidacji): brak zapisu, zakresy zwolnione.
        Przebieg p;
        M = &p.fw;
        std::vector<uint8_t> zmieniony(d);
        zmieniony[0x2010] ^= 0xFF;   // bajt .text - plan ELF identyczny, ale niezgodnosc wykryje weryfikacja? nie: plan ten sam
        brb::PlanLadowania inny = plan;
        inny.segmenty[1].bajty_z_pliku -= 1;   // plan rozni sie od ponownie wyliczonego
        const brb::BuforJadra b2{reinterpret_cast<uintptr_t>(zmieniony.data()), (d.size() + 4095) / 4096, d.size()};
        brb::RaportFazy4A r;
        p.log.inicjalizuj(p.uart.porty());
        SPRAWDZ(brb::faza4a_zaladuj_i_zweryfikuj(&p.fw.bs, &p.fw.obraz, b2, inny, p.pamiec(), p.log, &r) == EFI_LOAD_ERROR);
        SPRAWDZ(r.blad == brb::BladPrzydzialu::Ladowanie && r.blad_ladowania == brb::BladLadowania::NiespojnyPlan);
        SPRAWDZ(p.fw.przydzialy_fiz.empty() && p.fw.adres_zwolnienia == p.fw.adres_przydzialy_lacznie);
        for (uint8_t b : p.fw.fiz) if (b != 0xCC) { SPRAWDZ(false); break; }   // zero zapisow
        M = nullptr;
    }
    for (int k = 0; k <= int(brb::BladPrzydzialu::SprzatanieNieudane); ++k)
        SPRAWDZ(std::strcmp(brb::opis_bledu_przydzialu(brb::BladPrzydzialu(k)), "nieznany blad fazy 4A") != 0);
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("BRB - testy hosta Fazy 2 (wersja %s)\n", BRB_WERSJA_TEKST);
    test_abi_guid();
    test_abi_wejscie_ms();
    test_pamiec();
    test_utf8();
    test_log_szeregowy();
    test_walidacja_firmware();
    test_obraz();
    test_przebieg_klawisz();
    test_przebieg_domyslny_ascii();
    test_przebieg_limit_czasu();
    test_przebieg_awaryjny();
    test_przebieg_bledy();
    test_jadro_plik();
    test_faza4a();
    test_faza4a_konflikty();
    if (argc > 1) test_jadro_produkcyjne(argv[1]);
    else std::printf("[test] jadro produkcyjne przez model UEFI: NIE URUCHOMIONO (nie podano pliku)\n");
    std::printf("BRB testy hosta: sprawdzenia=%d bledy=%d\n", g_sprawdzenia, g_bledy);
    return g_bledy == 0 ? 0 : 1;
}
