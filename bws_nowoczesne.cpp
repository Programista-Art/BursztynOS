/*
 * Addytywne fundamenty BWS 61+: geometria/monitory, natywne SHM, bounded
 * IPC, obserwatory plikow, schowek MIME, kursory i uchwyt TCP/IPv4.
 * Wszystkie wskazniki Ring 3 sa kopiowane przed uzyciem przez podsystemy.
 */

#include "bws_api.h"
#include "bws_nowoczesne.h"
#include "bws_zdarzenia.h"
#include "pzb.h"
#include "scheduler.h"
#include "skladacz_obrazu.h"
#include "grafika.h"
#include "pamiec.h"
#include "heap.h"
#include "psf.h"
#include "tapety.h"
#include "sterowniki/czas/hpet.h"
#include "sterowniki/siec/sockets.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern bool skopiuj_z_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool skopiuj_do_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool skopiuj_string_z_uzytkownika(char*, const char*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void*, size_t);
extern volatile bool ekran_zajety;

extern "C" bool bws_gui_ustaw_kursor_ksztalt(int pid, uint32_t typ);
extern "C" bool bws_gui_ustaw_kursor_custom(
    int pid, const GuiCustomCursorInfo* info, const uint32_t* piksele);

namespace {

class GuardEkranu {
public:
    GuardEkranu() {
        while (__atomic_test_and_set(&ekran_zajety, __ATOMIC_ACQUIRE))
            asm volatile("pause" ::: "memory");
    }
    ~GuardEkranu() { __atomic_clear(&ekran_zajety, __ATOMIC_RELEASE); }
    GuardEkranu(const GuardEkranu&) = delete;
    GuardEkranu& operator=(const GuardEkranu&) = delete;
};

constexpr uint64_t STRONA = 4096ULL;
constexpr uint32_t SHM_OBIEKTOW_MAX = 16;
constexpr uint32_t SHM_MAPOWAN_NA_PROCES = 8;
constexpr uint32_t SHM_STRON_MAX = 512;
constexpr uint64_t SHM_ROZMIAR_MAX = SHM_STRON_MAX * STRONA;
constexpr uint64_t SHM_BAZA = RING3_SHM_BASE;
constexpr uint64_t SHM_SLOT_STRIDE = SHM_ROZMIAR_MAX;
constexpr uint64_t SHM_GLOBAL_MAX = 8ULL * 1024ULL * 1024ULL;

constexpr uint32_t IPC_ENDPOINTOW_MAX = 32;
constexpr uint32_t IPC_KOLEJKA_MAX = 16;
constexpr uint32_t WATCH_MAX = 64;
constexpr size_t SCIEZKA_MAX = 512;
constexpr uint64_t CLIPBOARD_GLOBAL_MAX = 2ULL * 1024ULL * 1024ULL;
constexpr uint32_t SOCKET_IO_MAX = 64U * 1024U;

bool ma_prawo(const proces_t& p, uint64_t prawo) {
    return (p.uprawnienia & static_cast<uint64_t>(PRAWA_ZNANE) & prawo) == prawo;
}

uint64_t utworz_handle(uint32_t indeks, uint32_t generacja) {
    return (static_cast<uint64_t>(generacja) << 32U) |
           static_cast<uint64_t>(indeks + 1U);
}

bool dekoduj_handle(uint64_t handle, uint32_t max,
                    uint32_t* indeks, uint32_t* generacja) {
    const uint32_t low = static_cast<uint32_t>(handle);
    const uint32_t gen = static_cast<uint32_t>(handle >> 32U);
    if (!indeks || !generacja || low == 0 || low > max || gen == 0)
        return false;
    *indeks = low - 1U;
    *generacja = gen;
    return true;
}

void wyzeruj(void* ptr, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; p && i < n; ++i) p[i] = 0;
}

bool kopiuj_string_user(uint64_t adres, char* out, size_t cap) {
    return adres != 0 && out && cap > 1 &&
           skopiuj_string_z_uzytkownika(
               out, reinterpret_cast<const char*>(adres), cap);
}

size_t dlugosc(const char* s, size_t cap) {
    if (!s) return cap;
    for (size_t i = 0; i < cap; ++i) if (s[i] == '\0') return i;
    return cap;
}

uint64_t timestamp() {
    return hpet_dostepny() ? czas_monotoniczny_ns() : scheduler_pobierz_tick();
}

/* --------------------------------------------------------------------- */
/* Monitor / work area                                                   */
/* --------------------------------------------------------------------- */

int32_t work_x = 0;
int32_t work_y = 0;
int32_t work_w = 0;
int32_t work_h = 0;
uint32_t monitor_scale = 1000;

uint64_t monitor_info(proces_t& proces, uint64_t indeks, uint64_t out_user) {
    if (!ma_prawo(proces, PRAWO_GUI)) return 0;
    if (out_user == 0) return indeks == 0 ? 1ULL : 0ULL;
    if (indeks != 0 || !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(out_user), sizeof(GuiMonitorInfo))) return 0;
    const int szer = grafika_pobierz_szerokosc();
    const int wys = grafika_pobierz_wysokosc();
    if (szer <= 0 || wys <= 0) return 0;
    GuiMonitorInfo info{};
    info.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    info.monitor_id = 0;
    info.szer = szer;
    info.wys = wys;
    info.work_x = work_w > 0 && work_h > 0 ? work_x : 0;
    info.work_y = work_w > 0 && work_h > 0 ? work_y : 0;
    info.work_szer = work_w > 0 && work_h > 0 ? work_w : szer;
    info.work_wys = work_w > 0 && work_h > 0 ? work_h : wys;
    info.scale_x1000 = monitor_scale;
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(out_user), &info, sizeof(info)) ? 1ULL : 0ULL;
}

uint64_t ustaw_work_area(proces_t& proces, int32_t x, int32_t y,
                         int32_t w, int32_t h) {
    if (!pzb_ma_prawa(&proces, PRAWO_GUI | PRAWO_SYSTEM_CONFIG) ||
        !pzb_ma_co_najmniej_poziom(&proces, PZB_ZAUFANE) || w <= 0 || h <= 0) return 0;
    const int sw = grafika_pobierz_szerokosc();
    const int sh = grafika_pobierz_wysokosc();
    const int64_t x1 = static_cast<int64_t>(x) + w;
    const int64_t y1 = static_cast<int64_t>(y) + h;
    if (x < 0 || y < 0 || x1 > sw || y1 > sh) return 0;
    work_x = x; work_y = y; work_w = w; work_h = h;
    return 1;
}

/* --------------------------------------------------------------------- */
/* Shared memory                                                         */
/* --------------------------------------------------------------------- */

struct ShmObiekt {
    bool aktywny;
    uint32_t generacja;
    int owner_pid;
    uint32_t flagi;
    uint32_t stron;
    uint64_t rozmiar;
    uint64_t* fizyczne;
    bool dostep[PZB_MAKS_PROCESOW];
    uint32_t mapowania;
    uint32_t wiadomosci;
};

struct ShmMapowanie {
    bool aktywne;
    uint64_t handle;
    uint64_t adres;
    uint64_t rozmiar;
    uint32_t obiekt;
};

ShmObiekt shm[SHM_OBIEKTOW_MAX] = {};
ShmMapowanie* shm_mapowania[PZB_MAKS_PROCESOW] = {};
uint32_t shm_generacje[SHM_OBIEKTOW_MAX] = {};
uint64_t shm_zajete = 0;

ShmObiekt* shm_z_handle(uint64_t handle, uint32_t* indeks_out = nullptr) {
    uint32_t i = 0, gen = 0;
    if (!dekoduj_handle(handle, SHM_OBIEKTOW_MAX, &i, &gen)) return nullptr;
    if (!shm[i].aktywny || shm[i].generacja != gen) return nullptr;
    if (indeks_out) *indeks_out = i;
    return &shm[i];
}

bool shm_ma_dostep(const ShmObiekt& o, int pid) {
    return pid > 0 && pid < static_cast<int>(PZB_MAKS_PROCESOW) && o.dostep[pid];
}

void shm_sprobuj_zwolnic(uint32_t i) {
    if (i >= SHM_OBIEKTOW_MAX) return;
    ShmObiekt& o = shm[i];
    if (!o.aktywny || o.mapowania != 0 || o.wiadomosci != 0) return;
    for (uint32_t pid = 1; pid < PZB_MAKS_PROCESOW; ++pid)
        if (o.dostep[pid]) return;
    for (uint32_t p = 0; p < o.stron; ++p)
        if (o.fizyczne[p]) ZwolnijRamke(reinterpret_cast<void*>(o.fizyczne[p]));
    if (o.fizyczne) kfree(o.fizyczne);
    if (shm_zajete >= o.stron * STRONA) shm_zajete -= o.stron * STRONA;
    const uint32_t gen = o.generacja;
    o = {};
    o.generacja = gen;
}

uint64_t shm_create(proces_t& proces, uint64_t size, uint64_t flags64) {
    if (size == 0 || size > SHM_ROZMIAR_MAX ||
        size > UINT64_MAX - (STRONA - 1ULL)) return 0;
    const uint32_t flags = static_cast<uint32_t>(flags64);
    if (static_cast<uint64_t>(flags) != flags64 ||
        (flags & ~(BWS_SHM_READ | BWS_SHM_WRITE)) != 0 ||
        (flags & BWS_SHM_READ) == 0) return 0;
    const uint64_t rounded = (size + STRONA - 1ULL) & ~(STRONA - 1ULL);
    if (rounded > SHM_GLOBAL_MAX || shm_zajete > SHM_GLOBAL_MAX - rounded)
        return 0;
    uint32_t slot = SHM_OBIEKTOW_MAX;
    for (uint32_t i = 0; i < SHM_OBIEKTOW_MAX; ++i)
        if (!shm[i].aktywny) { slot = i; break; }
    if (slot == SHM_OBIEKTOW_MAX) return 0;

    ShmObiekt nowy{};
    nowy.aktywny = true;
    nowy.owner_pid = aktualny_pid;
    nowy.flagi = flags;
    nowy.rozmiar = size;
    nowy.stron = static_cast<uint32_t>(rounded / STRONA);
    nowy.fizyczne = static_cast<uint64_t*>(
        kmalloc(static_cast<uint64_t>(nowy.stron) * sizeof(uint64_t)));
    if (!nowy.fizyczne) return 0;
    wyzeruj(nowy.fizyczne,
            static_cast<size_t>(nowy.stron) * sizeof(uint64_t));
    nowy.dostep[aktualny_pid] = true;
    uint32_t gen = ++shm_generacje[slot];
    if (gen == 0) gen = ++shm_generacje[slot];
    nowy.generacja = gen;

    for (uint32_t p = 0; p < nowy.stron; ++p) {
        void* frame = ZaalokujRamke();
        if (!frame) {
            for (uint32_t j = 0; j < p; ++j)
                ZwolnijRamke(reinterpret_cast<void*>(nowy.fizyczne[j]));
            kfree(nowy.fizyczne);
            return 0;
        }
        const uint64_t pa = reinterpret_cast<uint64_t>(frame);
        void* kva = FizycznyZakresDoWirtualnego(pa, STRONA);
        if (!kva) {
            ZwolnijRamke(frame);
            for (uint32_t j = 0; j < p; ++j)
                ZwolnijRamke(reinterpret_cast<void*>(nowy.fizyczne[j]));
            kfree(nowy.fizyczne);
            return 0;
        }
        wyzeruj(kva, STRONA);
        nowy.fizyczne[p] = pa;
    }
    shm[slot] = nowy;
    shm_zajete += rounded;
    (void)proces;
    return utworz_handle(slot, gen);
}

uint64_t shm_map(proces_t&, uint64_t handle, uint64_t flags64) {
    uint32_t obj_index = 0;
    ShmObiekt* o = shm_z_handle(handle, &obj_index);
    const uint32_t flags = static_cast<uint32_t>(flags64);
    if (!o || !shm_ma_dostep(*o, aktualny_pid) ||
        static_cast<uint64_t>(flags) != flags64 ||
        (flags & ~(BWS_SHM_READ | BWS_SHM_WRITE)) != 0 ||
        (flags & BWS_SHM_READ) == 0 || (flags & ~o->flagi) != 0) return 0;
    ShmMapowanie*& mapy = shm_mapowania[aktualny_pid];
    if (!mapy) {
        mapy = static_cast<ShmMapowanie*>(kmalloc(
            sizeof(ShmMapowanie) * SHM_MAPOWAN_NA_PROCES));
        if (!mapy) return 0;
        wyzeruj(mapy, sizeof(ShmMapowanie) * SHM_MAPOWAN_NA_PROCES);
    }
    uint32_t slot = SHM_MAPOWAN_NA_PROCES;
    for (uint32_t i = 0; i < SHM_MAPOWAN_NA_PROCES; ++i)
        if (!mapy[i].aktywne) { slot = i; break; }
    if (slot == SHM_MAPOWAN_NA_PROCES) return 0;
    const uint64_t adres = SHM_BAZA + static_cast<uint64_t>(slot) * SHM_SLOT_STRIDE;
    uint32_t mapped = 0;
    for (; mapped < o->stron; ++mapped) {
        const uint64_t va = adres + static_cast<uint64_t>(mapped) * STRONA;
        if (!VmmMapujStroneWspoldzielona(
                reinterpret_cast<void*>(va),
                reinterpret_cast<void*>(o->fizyczne[mapped]),
                (flags & BWS_SHM_WRITE) != 0)) break;
    }
    if (mapped != o->stron) {
        while (mapped != 0) {
            --mapped;
            (void)VmmOdmapujStroneWspoldzielona(reinterpret_cast<void*>(
                adres + static_cast<uint64_t>(mapped) * STRONA));
        }
        return 0;
    }
    mapy[slot] =
        {true, handle, adres, o->rozmiar, obj_index};
    ++o->mapowania;
    return adres;
}

uint64_t shm_unmap(uint64_t adres) {
    if ((adres & (STRONA - 1ULL)) != 0) return 0;
    ShmMapowanie* mapy = shm_mapowania[aktualny_pid];
    if (!mapy) return 0;
    for (uint32_t i = 0; i < SHM_MAPOWAN_NA_PROCES; ++i) {
        ShmMapowanie& m = mapy[i];
        if (!m.aktywne || m.adres != adres) continue;
        ShmObiekt* o = shm_z_handle(m.handle);
        if (!o) return 0;
        uint32_t done = 0;
        for (; done < o->stron; ++done)
            if (!VmmOdmapujStroneWspoldzielona(reinterpret_cast<void*>(
                    adres + static_cast<uint64_t>(done) * STRONA))) break;
        if (done != o->stron) return 0;
        const uint32_t oi = m.obiekt;
        m = {};
        if (o->mapowania) --o->mapowania;
        shm_sprobuj_zwolnic(oi);
        return 1;
    }
    return 0;
}

uint64_t shm_close(uint64_t handle) {
    uint32_t index = 0;
    ShmObiekt* o = shm_z_handle(handle, &index);
    if (!o || !shm_ma_dostep(*o, aktualny_pid)) return 0;
    o->dostep[aktualny_pid] = false;
    shm_sprobuj_zwolnic(index);
    return 1;
}

/* --------------------------------------------------------------------- */
/* IPC                                                                   */
/* --------------------------------------------------------------------- */

struct IpcWiadomosc {
    uint64_t nadawca;
    uint64_t shm_handle;
    uint32_t flagi;
    uint32_t rozmiar;
    uint8_t* dane;
};

struct IpcEndpoint {
    bool aktywny;
    uint32_t generacja;
    int owner_pid;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    IpcWiadomosc* kolejka;
};

IpcEndpoint endpoints[IPC_ENDPOINTOW_MAX] = {};
uint32_t endpoint_generacje[IPC_ENDPOINTOW_MAX] = {};

IpcEndpoint* endpoint_z_handle(uint64_t handle, uint32_t* index = nullptr) {
    uint32_t i = 0, gen = 0;
    if (!dekoduj_handle(handle, IPC_ENDPOINTOW_MAX, &i, &gen)) return nullptr;
    if (!endpoints[i].aktywny || endpoints[i].generacja != gen) return nullptr;
    if (index) *index = i;
    return &endpoints[i];
}

void ipc_zwolnij_ref_wiadomosci(const IpcWiadomosc& m) {
    if (m.dane) kfree(m.dane);
    if ((m.flagi & BWS_IPC_ZAWIERA_SHM) == 0) return;
    uint32_t oi = 0;
    ShmObiekt* o = shm_z_handle(m.shm_handle, &oi);
    if (o && o->wiadomosci) {
        --o->wiadomosci;
        shm_sprobuj_zwolnic(oi);
    }
}

uint64_t ipc_create() {
    for (uint32_t i = 0; i < IPC_ENDPOINTOW_MAX; ++i) {
        if (endpoints[i].aktywny) continue;
        IpcWiadomosc* kolejka = static_cast<IpcWiadomosc*>(
            kmalloc(sizeof(IpcWiadomosc) * IPC_KOLEJKA_MAX));
        if (!kolejka) return 0;
        wyzeruj(kolejka, sizeof(IpcWiadomosc) * IPC_KOLEJKA_MAX);
        uint32_t gen = ++endpoint_generacje[i];
        if (gen == 0) gen = ++endpoint_generacje[i];
        endpoints[i] = {};
        endpoints[i].aktywny = true;
        endpoints[i].generacja = gen;
        endpoints[i].owner_pid = aktualny_pid;
        endpoints[i].kolejka = kolejka;
        return utworz_handle(i, gen);
    }
    return 0;
}

// Staly endpoint jadra korzysta z BWS 69; odpowiedz tylko do wlasciciela.
uint64_t ipc_tapety(const proces_t& proces, uint64_t desc_user) {
    BwsIpcWyslij desc{};
    TapetyZadanie req{};
    if (!skopiuj_z_przestrzeni_uzytkownika(&desc, reinterpret_cast<const void*>(desc_user), sizeof(desc)) ||
        desc.wersja != BWS_NOWOCZESNE_ABI_WERSJA || desc.zarezerwowane || desc.flagi || desc.shm_handle ||
        desc.rozmiar != sizeof(req) || !desc.dane ||
        !skopiuj_z_przestrzeni_uzytkownika(&req, reinterpret_cast<const void*>(desc.dane), sizeof(req))) return 0;
    IpcEndpoint* cel = endpoint_z_handle(req.odpowiedz);
    if (!cel || cel->owner_pid != aktualny_pid || !cel->kolejka || cel->count >= IPC_KOLEJKA_MAX) return 0;
    auto* odpowiedz = static_cast<TapetyOdpowiedz*>(kmalloc(sizeof(TapetyOdpowiedz)));
    if (!odpowiedz) return 0;
    bool ok;
    { GuardEkranu guard; ok = tapety_obsluz(proces, req, odpowiedz); }
    if (!ok) { kfree(odpowiedz); return 0; }
    IpcWiadomosc msg{};
    msg.rozmiar = sizeof(*odpowiedz);
    msg.dane = reinterpret_cast<uint8_t*>(odpowiedz);
    cel->kolejka[cel->head] = msg;
    cel->head = (cel->head + 1U) % IPC_KOLEJKA_MAX;
    ++cel->count;
    bws_zdarzenie zd{};
    zd.typ = BWS_ZDARZENIE_IPC_READY;
    zd.x = static_cast<int32_t>(req.odpowiedz);
    zd.y = static_cast<int32_t>(req.odpowiedz >> 32U);
    zd.timestamp = timestamp();
    (void)scheduler_dodaj_zdarzenie(aktualny_pid, &zd);
    return 1;
}

uint64_t ipc_send(uint64_t endpoint_handle, uint64_t desc_user) {
    IpcEndpoint* e = endpoint_z_handle(endpoint_handle);
    if (!e || !scheduler_pid_uzytkownika(e->owner_pid) || desc_user == 0 ||
        e->count >= IPC_KOLEJKA_MAX) return 0;
    BwsIpcWyslij desc{};
    if (!skopiuj_z_przestrzeni_uzytkownika(
            &desc, reinterpret_cast<const void*>(desc_user), sizeof(desc)) ||
        desc.wersja != BWS_NOWOCZESNE_ABI_WERSJA ||
        desc.rozmiar > BWS_IPC_MAX_DANYCH ||
        (desc.flagi & ~BWS_IPC_ZAWIERA_SHM) != 0 ||
        (desc.rozmiar != 0 && desc.dane == 0)) return 0;

    ShmObiekt* przekazywany = nullptr;
    if ((desc.flagi & BWS_IPC_ZAWIERA_SHM) != 0) {
        przekazywany = shm_z_handle(desc.shm_handle);
        if (!przekazywany || !shm_ma_dostep(*przekazywany, aktualny_pid))
            return 0;
    } else if (desc.shm_handle != 0) return 0;

    IpcWiadomosc msg{};
    msg.nadawca = static_cast<uint64_t>(aktualny_pid);
    msg.shm_handle = desc.shm_handle;
    msg.flagi = desc.flagi;
    msg.rozmiar = desc.rozmiar;
    if (desc.rozmiar != 0) {
        msg.dane = static_cast<uint8_t*>(kmalloc(desc.rozmiar));
        if (!msg.dane || !skopiuj_z_przestrzeni_uzytkownika(
                msg.dane, reinterpret_cast<const void*>(desc.dane),
                desc.rozmiar)) {
            if (msg.dane) kfree(msg.dane);
            return 0;
        }
    }
    e->kolejka[e->head] = msg;
    e->head = (e->head + 1U) % IPC_KOLEJKA_MAX;
    ++e->count;
    if (przekazywany) ++przekazywany->wiadomosci;
    bws_zdarzenie gotowe{};
    gotowe.typ = BWS_ZDARZENIE_IPC_READY;
    gotowe.x = static_cast<int32_t>(endpoint_handle);
    gotowe.y = static_cast<int32_t>(endpoint_handle >> 32U);
    gotowe.timestamp = timestamp();
    (void)scheduler_dodaj_zdarzenie(e->owner_pid, &gotowe);
    return 1;
}

uint64_t ipc_receive(uint64_t endpoint_handle, uint64_t out_user) {
    IpcEndpoint* e = endpoint_z_handle(endpoint_handle);
    if (!e || e->owner_pid != aktualny_pid || e->count == 0 || out_user == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(out_user), sizeof(BwsIpcOdbierz))) return 0;
    const IpcWiadomosc& msg = e->kolejka[e->tail];
    BwsIpcOdbierz out{};
    out.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    out.flagi = msg.flagi;
    out.nadawca_pid = msg.nadawca;
    out.shm_handle = msg.shm_handle;
    out.rozmiar = msg.rozmiar;
    for (uint32_t i = 0; i < msg.rozmiar; ++i) out.dane[i] = msg.dane[i];
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(out_user), &out, sizeof(out))) return 0;

    if ((msg.flagi & BWS_IPC_ZAWIERA_SHM) != 0) {
        uint32_t oi = 0;
        ShmObiekt* o = shm_z_handle(msg.shm_handle, &oi);
        if (o) {
            o->dostep[aktualny_pid] = true;
            if (o->wiadomosci) --o->wiadomosci;
            shm_sprobuj_zwolnic(oi);
        }
    }
    if (msg.dane) kfree(msg.dane);
    e->kolejka[e->tail] = {};
    e->tail = (e->tail + 1U) % IPC_KOLEJKA_MAX;
    --e->count;
    return 1;
}

uint64_t ipc_close(uint64_t handle) {
    uint32_t index = 0;
    IpcEndpoint* e = endpoint_z_handle(handle, &index);
    if (!e || e->owner_pid != aktualny_pid) return 0;
    while (e->count != 0) {
        ipc_zwolnij_ref_wiadomosci(e->kolejka[e->tail]);
        e->tail = (e->tail + 1U) % IPC_KOLEJKA_MAX;
        --e->count;
    }
    const uint32_t gen = e->generacja;
    IpcWiadomosc* kolejka = e->kolejka;
    *e = {};
    e->generacja = gen;
    kfree(kolejka);
    (void)index;
    return 1;
}

/* --------------------------------------------------------------------- */
/* File watching                                                         */
/* --------------------------------------------------------------------- */

struct Watcher {
    bool aktywny;
    uint32_t generacja;
    int owner_pid;
    uint32_t maska;
    char* sciezka;
};

Watcher watchers[WATCH_MAX] = {};
uint32_t watcher_generacje[WATCH_MAX] = {};

bool sciezka_pasuje(const char* baza, const char* zmiana) {
    if (!baza || !zmiana) return false;
    size_t i = 0;
    while (baza[i] && baza[i] == zmiana[i]) ++i;
    if (baza[i] != '\0') return false;
    return zmiana[i] == '\0' || zmiana[i] == '/' ||
           (i == 1 && baza[0] == '/');
}

uint64_t watch_dispatch(proces_t& proces, uint64_t op, uint64_t arg,
                        uint64_t mask64) {
    if (!ma_prawo(proces, PRAWO_PLIKI_CZYTAJ)) return 0;
    if (op == BWS_FILE_WATCH_DODAJ) {
        const uint32_t mask = static_cast<uint32_t>(mask64);
        if (static_cast<uint64_t>(mask) != mask64 || mask == 0 ||
            (mask & ~BWS_FILE_WATCH_WSZYSTKO) != 0) return 0;
        char path[SCIEZKA_MAX] = {};
        if (!kopiuj_string_user(arg, path, sizeof(path)) || path[0] != '/')
            return 0;
        psf_metadane meta{};
        if (!pobierz_metadane_tworu(path, &meta) ||
            (meta.pzb_dostepny && proces.poziom_zaufania > meta.poziom_pzb))
            return 0;
        for (uint32_t i = 0; i < WATCH_MAX; ++i) {
            if (watchers[i].aktywny) continue;
            uint32_t gen = ++watcher_generacje[i];
            if (gen == 0) gen = ++watcher_generacje[i];
            watchers[i] = {};
            watchers[i].aktywny = true;
            watchers[i].generacja = gen;
            watchers[i].owner_pid = aktualny_pid;
            watchers[i].maska = mask;
            const size_t n = dlugosc(path, sizeof(path));
            watchers[i].sciezka = static_cast<char*>(kmalloc(n + 1U));
            if (!watchers[i].sciezka) {
                watchers[i] = {};
                return 0;
            }
            for (size_t j = 0; j <= n; ++j) watchers[i].sciezka[j] = path[j];
            return utworz_handle(i, gen);
        }
        return 0;
    }
    if (op == BWS_FILE_WATCH_USUN) {
        uint32_t i = 0, gen = 0;
        if (!dekoduj_handle(arg, WATCH_MAX, &i, &gen) ||
            !watchers[i].aktywny || watchers[i].generacja != gen ||
            watchers[i].owner_pid != aktualny_pid) return 0;
        const uint32_t old_gen = watchers[i].generacja;
        char* stara_sciezka = watchers[i].sciezka;
        watchers[i] = {};
        watchers[i].generacja = old_gen;
        kfree(stara_sciezka);
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* Clipboard MIME                                                        */
/* --------------------------------------------------------------------- */

struct ClipboardFormat {
    bool aktywny;
    char mime[BWS_CLIPBOARD_MIME_MAX];
    uint8_t* dane;
    uint32_t rozmiar;
};

ClipboardFormat clipboard[BWS_CLIPBOARD_FORMATOW_MAX] = {};
uint64_t clipboard_bytes = 0;

bool mime_poprawny(const char* mime) {
    const size_t n = dlugosc(mime, BWS_CLIPBOARD_MIME_MAX);
    if (n < 3 || n >= BWS_CLIPBOARD_MIME_MAX) return false;
    bool slash = false;
    for (size_t i = 0; i < n; ++i) {
        const char c = mime[i];
        if (c == '/') slash = true;
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '.' || c == '+')) return false;
    }
    return slash;
}

int clipboard_find(const char* mime) {
    for (uint32_t i = 0; i < BWS_CLIPBOARD_FORMATOW_MAX; ++i) {
        if (!clipboard[i].aktywny) continue;
        size_t j = 0;
        while (j < BWS_CLIPBOARD_MIME_MAX &&
               clipboard[i].mime[j] == mime[j] && mime[j]) ++j;
        if (j < BWS_CLIPBOARD_MIME_MAX && clipboard[i].mime[j] == mime[j])
            return static_cast<int>(i);
    }
    return -1;
}

bool clipboard_mime_user(uint64_t ptr, char out[BWS_CLIPBOARD_MIME_MAX]) {
    return kopiuj_string_user(ptr, out, BWS_CLIPBOARD_MIME_MAX) &&
           mime_poprawny(out);
}

uint64_t clipboard_enum(proces_t& p, uint64_t index64, uint64_t out_user,
                        uint64_t cap64) {
    if (!ma_prawo(p, PRAWO_GUI) || !pzb_ma_co_najmniej_poziom(&p, PZB_UZYTKOWNIK) ||
        index64 >= BWS_CLIPBOARD_FORMATOW_MAX ||
        cap64 == 0 || cap64 > BWS_CLIPBOARD_MIME_MAX || out_user == 0) return 0;
    uint32_t seen = 0;
    ClipboardFormat* found = nullptr;
    for (uint32_t i = 0; i < BWS_CLIPBOARD_FORMATOW_MAX; ++i)
        if (clipboard[i].aktywny && seen++ == index64) { found = &clipboard[i]; break; }
    if (!found) return 0;
    const size_t n = dlugosc(found->mime, sizeof(found->mime));
    if (n + 1U > cap64 || !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(out_user), static_cast<size_t>(cap64))) return 0;
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(out_user), found->mime, n + 1U) ? 1ULL : 0ULL;
}

uint64_t clipboard_size(proces_t& p, uint64_t mime_user, uint64_t out_user) {
    if (!ma_prawo(p, PRAWO_GUI) || !pzb_ma_co_najmniej_poziom(&p, PZB_UZYTKOWNIK) ||
        out_user == 0) return 0;
    char mime[BWS_CLIPBOARD_MIME_MAX] = {};
    if (!clipboard_mime_user(mime_user, mime)) return 0;
    const int i = clipboard_find(mime);
    if (i < 0) return 0;
    const uint64_t size = clipboard[i].rozmiar;
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(out_user), &size, sizeof(size)) ? 1ULL : 0ULL;
}

uint64_t clipboard_read(proces_t& p, uint64_t mime_user, uint64_t out_user,
                        uint64_t cap64, uint64_t bytes_user) {
    if (!ma_prawo(p, PRAWO_GUI) || !pzb_ma_co_najmniej_poziom(&p, PZB_UZYTKOWNIK) ||
        cap64 > BWS_CLIPBOARD_FORMAT_MAX ||
        bytes_user == 0 || !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bytes_user), sizeof(uint64_t))) return 0;
    char mime[BWS_CLIPBOARD_MIME_MAX] = {};
    if (!clipboard_mime_user(mime_user, mime)) return 0;
    const int i = clipboard_find(mime);
    if (i < 0 || cap64 < clipboard[i].rozmiar ||
        (clipboard[i].rozmiar != 0 && out_user == 0)) return 0;
    if (clipboard[i].rozmiar != 0 &&
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(out_user), clipboard[i].rozmiar)) return 0;
    if (clipboard[i].rozmiar != 0 && !skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(out_user), clipboard[i].dane,
            clipboard[i].rozmiar)) return 0;
    const uint64_t n = clipboard[i].rozmiar;
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(bytes_user), &n, sizeof(n)) ? 1ULL : 0ULL;
}

uint64_t clipboard_set(proces_t& p, uint64_t mime_user, uint64_t data_user,
                       uint64_t size64) {
    if (!ma_prawo(p, PRAWO_GUI) || !pzb_ma_co_najmniej_poziom(&p, PZB_UZYTKOWNIK) ||
        size64 > BWS_CLIPBOARD_FORMAT_MAX ||
        (size64 != 0 && data_user == 0)) return 0;
    char mime[BWS_CLIPBOARD_MIME_MAX] = {};
    if (!clipboard_mime_user(mime_user, mime)) return 0;
    int index = clipboard_find(mime);
    if (index < 0) for (uint32_t i = 0; i < BWS_CLIPBOARD_FORMATOW_MAX; ++i)
        if (!clipboard[i].aktywny) { index = static_cast<int>(i); break; }
    if (index < 0) return 0;
    const uint64_t old = clipboard[index].aktywny ? clipboard[index].rozmiar : 0;
    if (clipboard_bytes - old > CLIPBOARD_GLOBAL_MAX - size64) return 0;
    uint8_t* nowe = nullptr;
    if (size64 != 0) {
        nowe = static_cast<uint8_t*>(kmalloc(size64));
        if (!nowe || !skopiuj_z_przestrzeni_uzytkownika(
                nowe, reinterpret_cast<const void*>(data_user), size64)) {
            if (nowe) kfree(nowe);
            return 0;
        }
    }
    ClipboardFormat& f = clipboard[index];
    uint8_t* stare = f.dane;
    f = {};
    f.aktywny = true;
    f.dane = nowe;
    f.rozmiar = static_cast<uint32_t>(size64);
    const size_t n = dlugosc(mime, sizeof(mime));
    for (size_t j = 0; j <= n; ++j) f.mime[j] = mime[j];
    clipboard_bytes = clipboard_bytes - old + size64;
    if (stare) kfree(stare);
    return 1;
}

/* --------------------------------------------------------------------- */
/* Socket TCP/IPv4 (obecny stos ma jedna sesje globalna)                 */
/* --------------------------------------------------------------------- */

struct SocketStan {
    bool aktywny;
    bool polaczony;
    uint32_t generacja;
    int owner_pid;
};

SocketStan socket_stan{};
uint32_t socket_generacja = 0;

bool socket_handle_ok(uint64_t handle) {
    uint32_t i = 0, gen = 0;
    return dekoduj_handle(handle, 1, &i, &gen) && i == 0 &&
           socket_stan.aktywny && socket_stan.generacja == gen &&
           socket_stan.owner_pid == aktualny_pid;
}

uint64_t socket_create(proces_t& p, uint64_t typ) {
    if (!ma_prawo(p, PRAWO_SIEC) || typ != BWS_SOCKET_TCP_IPV4 ||
        socket_stan.aktywny || tcp_gniazdo_otwarte()) return 0;
    uint32_t gen = ++socket_generacja;
    if (gen == 0) gen = ++socket_generacja;
    socket_stan = {true, false, gen, aktualny_pid};
    return utworz_handle(0, gen);
}

uint64_t socket_connect(proces_t& p, uint64_t handle, uint64_t ip_user,
                        uint64_t port64) {
    if (!ma_prawo(p, PRAWO_SIEC) || !socket_handle_ok(handle) ||
        socket_stan.polaczony || port64 == 0 || port64 > 65535 || ip_user == 0)
        return 0;
    uint8_t ip[4] = {};
    if (!skopiuj_z_przestrzeni_uzytkownika(
            ip, reinterpret_cast<const void*>(ip_user), sizeof(ip))) return 0;
    if (!tcp_gniazdo_polacz(ip, static_cast<uint16_t>(port64))) return 0;
    socket_stan.polaczony = true;
    return 1;
}

uint64_t socket_io_result(uint64_t out_user, uint32_t status, uint32_t bytes) {
    if (out_user == 0) return 0;
    BwsIoResult out{BWS_NOWOCZESNE_ABI_WERSJA, status, bytes, 0};
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(out_user), &out, sizeof(out)) ? 1ULL : 0ULL;
}

uint64_t socket_send(proces_t& p, uint64_t handle, uint64_t data_user,
                     uint64_t len64, uint64_t result_user) {
    if (!ma_prawo(p, PRAWO_SIEC) || !socket_handle_ok(handle) ||
        !socket_stan.polaczony || len64 > SOCKET_IO_MAX ||
        (len64 != 0 && data_user == 0) || result_user == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(result_user), sizeof(BwsIoResult))) return 0;
    if (len64 == 0) return socket_io_result(result_user, BWS_IO_OK, 0);
    uint8_t* buf = static_cast<uint8_t*>(kmalloc(len64));
    if (!buf) return 0;
    if (!skopiuj_z_przestrzeni_uzytkownika(
            buf, reinterpret_cast<const void*>(data_user), len64)) {
        kfree(buf); return 0;
    }
    const int sent = tcp_gniazdo_wyslij(buf, static_cast<uint32_t>(len64));
    kfree(buf);
    return sent < 0 ? socket_io_result(result_user, BWS_IO_BLAD, 0) :
        socket_io_result(result_user, BWS_IO_OK, static_cast<uint32_t>(sent));
}

uint64_t socket_recv(proces_t& p, uint64_t handle, uint64_t data_user,
                     uint64_t cap64, uint64_t result_user) {
    if (!ma_prawo(p, PRAWO_SIEC) || !socket_handle_ok(handle) ||
        !socket_stan.polaczony || cap64 == 0 || cap64 > SOCKET_IO_MAX ||
        data_user == 0 || result_user == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(result_user), sizeof(BwsIoResult)) ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(data_user), static_cast<size_t>(cap64)))
        return 0;
    uint8_t* buf = static_cast<uint8_t*>(kmalloc(cap64));
    if (!buf) return 0;
    const int got = tcp_gniazdo_odbierz(buf, static_cast<uint32_t>(cap64));
    if (got > 0 && !skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(data_user), buf, static_cast<size_t>(got))) {
        kfree(buf); return 0;
    }
    kfree(buf);
    if (got > 0) return socket_io_result(
        result_user, BWS_IO_OK, static_cast<uint32_t>(got));
    if (got == 0) return socket_io_result(result_user, BWS_IO_EOF, 0);
    if (got == SOCKETS_TCP_BRAK_DANYCH)
        return socket_io_result(result_user, BWS_IO_WOULD_BLOCK, 0);
    return socket_io_result(result_user, BWS_IO_BLAD, 0);
}

uint64_t socket_close(proces_t& p, uint64_t handle) {
    if (!ma_prawo(p, PRAWO_SIEC) || !socket_handle_ok(handle)) return 0;
    if (socket_stan.polaczony) tcp_gniazdo_zamknij();
    socket_stan = {};
    return 1;
}

} // namespace

void bws_ipc_inicjalizuj_tapety() {
    // Wywolanie jednorazowe przed uruchomieniem pierwszego procesu Ring 3.
    endpoints[0] = {};
    endpoints[0].aktywny = true;
    endpoints[0].generacja = endpoint_generacje[0] = 1;
    endpoints[0].owner_pid = 0;
}


/* Stary HTTP i nowe gniazdo korzystaja obecnie z tej samej pojedynczej
 * sesji stosu TCP. Dispatcher uzywa tego predykatu, aby nigdy nie pozwolic
 * im nadpisac sobie stanu protokolu. */
extern "C" bool bws_socket_rezerwuje_stos_tcp() {
    return socket_stan.aktywny;
}

extern "C" void bws_powiadom_zmiane_pliku(const char* sciezka,
                                            uint32_t akcja) {
    if (!sciezka || (akcja & BWS_FILE_WATCH_WSZYSTKO) == 0) return;
    for (uint32_t i = 0; i < WATCH_MAX; ++i) {
        const Watcher& w = watchers[i];
        if (!w.aktywny || (w.maska & akcja) == 0 ||
            !scheduler_pid_uzytkownika(w.owner_pid) ||
            !sciezka_pasuje(w.sciezka, sciezka)) continue;
        const proces_t& odbiorca = tablica_procesow[w.owner_pid];
        if (odbiorca.pid != static_cast<uint64_t>(w.owner_pid) ||
            !ma_prawo(odbiorca, PRAWO_PLIKI_CZYTAJ)) continue;
        psf_metadane meta{};
        if (pobierz_metadane_tworu(sciezka, &meta) && meta.pzb_dostepny &&
            odbiorca.poziom_zaufania > meta.poziom_pzb) continue;
        bws_zdarzenie e{};
        e.typ = BWS_ZDARZENIE_FILE_WATCH;
        const uint64_t handle = utworz_handle(i, w.generacja);
        e.x = static_cast<int32_t>(handle);
        e.y = static_cast<int32_t>(handle >> 32U);
        e.kod = akcja;
        e.timestamp = timestamp();
        (void)scheduler_dodaj_zdarzenie(w.owner_pid, &e);
    }
}

extern "C" void bws_nowoczesne_posprzataj_proces(int pid, bool aktywny_cr3) {
    if (pid <= 0 || pid >= static_cast<int>(PZB_MAKS_PROCESOW)) return;

    for (uint32_t i = 0; i < IPC_ENDPOINTOW_MAX; ++i) {
        IpcEndpoint& e = endpoints[i];
        if (!e.aktywny || e.owner_pid != pid) continue;
        while (e.count) {
            ipc_zwolnij_ref_wiadomosci(e.kolejka[e.tail]);
            e.tail = (e.tail + 1U) % IPC_KOLEJKA_MAX;
            --e.count;
        }
        const uint32_t gen = e.generacja;
        IpcWiadomosc* kolejka = e.kolejka;
        e = {}; e.generacja = gen;
        kfree(kolejka);
    }
    for (uint32_t i = 0; i < WATCH_MAX; ++i)
        if (watchers[i].aktywny && watchers[i].owner_pid == pid) {
            const uint32_t gen = watchers[i].generacja;
            char* sciezka = watchers[i].sciezka;
            watchers[i] = {}; watchers[i].generacja = gen;
            kfree(sciezka);
        }

    ShmMapowanie* mapy = shm_mapowania[pid];
    for (uint32_t i = 0; mapy && i < SHM_MAPOWAN_NA_PROCES; ++i) {
        ShmMapowanie& m = mapy[i];
        if (!m.aktywne) continue;
        ShmObiekt* o = shm_z_handle(m.handle);
        if (aktywny_cr3 && o) for (uint32_t p = 0; p < o->stron; ++p)
            (void)VmmOdmapujStroneWspoldzielona(reinterpret_cast<void*>(
                m.adres + static_cast<uint64_t>(p) * STRONA));
        const uint32_t oi = m.obiekt;
        m = {};
        if (o && o->mapowania) --o->mapowania;
        shm_sprobuj_zwolnic(oi);
    }
    if (mapy) {
        kfree(mapy);
        shm_mapowania[pid] = nullptr;
    }
    for (uint32_t i = 0; i < SHM_OBIEKTOW_MAX; ++i) {
        if (!shm[i].aktywny) continue;
        shm[i].dostep[pid] = false;
        shm_sprobuj_zwolnic(i);
    }

    if (socket_stan.aktywny && socket_stan.owner_pid == pid) {
        if (socket_stan.polaczony) tcp_gniazdo_zamknij();
        socket_stan = {};
    }
}

extern "C" uint64_t bws_nowoczesne_obsluz(
    uint64_t nr, proces_t* proces, uint64_t a1, uint64_t a2,
    uint64_t a3, uint64_t a4
) {
    if (!proces) return 0;
    switch (nr) {
        case BWS_GUI_GEOMETRIA: {
            if (!ma_prawo(*proces, PRAWO_GUI)) return 0;
            const int x = static_cast<int32_t>(a1 >> 32U);
            const int y = static_cast<int32_t>(a1);
            const int w = static_cast<int32_t>(a2 >> 32U);
            const int h = static_cast<int32_t>(a2);
            GuardEkranu guard;
            return zmien_geometrie_warstwy(aktualny_pid, x, y, w, h) ? 1ULL : 0ULL;
        }
        case BWS_GUI_MONITOR_INFO:
            return monitor_info(*proces, a1, a2);
        case BWS_GUI_USTAW_WORK_AREA:
            return ustaw_work_area(*proces, static_cast<int32_t>(a1 >> 32U),
                static_cast<int32_t>(a1), static_cast<int32_t>(a2 >> 32U),
                static_cast<int32_t>(a2));
        case BWS_SHM_CREATE: return shm_create(*proces, a1, a2);
        case BWS_SHM_MAP: return shm_map(*proces, a1, a2);
        case BWS_SHM_UNMAP: return shm_unmap(a1);
        case BWS_SHM_CLOSE: return shm_close(a1);
        case BWS_IPC_CREATE: return ipc_create();
        case BWS_IPC_SEND: return a1 == TAPETY_IPC ? ipc_tapety(*proces, a2) : ipc_send(a1, a2);
        case BWS_IPC_RECEIVE: return ipc_receive(a1, a2);
        case BWS_IPC_CLOSE: return ipc_close(a1);
        case BWS_FILE_WATCH: return watch_dispatch(*proces, a1, a2, a3);
        case BWS_CLIPBOARD_ENUM: return clipboard_enum(*proces, a1, a2, a3);
        case BWS_CLIPBOARD_SIZE: return clipboard_size(*proces, a1, a2);
        case BWS_CLIPBOARD_READ:
            return clipboard_read(*proces, a1, a2, a3, a4);
        case BWS_CLIPBOARD_SET:
            return clipboard_set(*proces, a1, a2, a3);
        case BWS_GUI_CURSOR_SHAPE: {
            if (!ma_prawo(*proces, PRAWO_GUI)) return 0;
            GuardEkranu guard;
            return bws_gui_ustaw_kursor_ksztalt(
                aktualny_pid, static_cast<uint32_t>(a1)) ? 1ULL : 0ULL;
        }
        case BWS_GUI_CURSOR_CUSTOM: {
            if (!ma_prawo(*proces, PRAWO_GUI) || a1 == 0) return 0;
            GuiCustomCursorInfo info{};
            if (!skopiuj_z_przestrzeni_uzytkownika(
                    &info, reinterpret_cast<const void*>(a1), sizeof(info)) ||
                info.wersja != BWS_NOWOCZESNE_ABI_WERSJA || info.szer == 0 ||
                info.wys == 0 || info.szer > GUI_CURSOR_CUSTOM_MAX_W ||
                info.wys > GUI_CURSOR_CUSTOM_MAX_H || info.hotspot_x >= info.szer ||
                info.hotspot_y >= info.wys || info.piksele == 0 ||
                (info.format != GUI_CURSOR_BGRA8888 &&
                 info.format != GUI_CURSOR_RGBA8888)) return 0;
            uint64_t pixels = 0, bytes = 0;
            if (__builtin_mul_overflow(static_cast<uint64_t>(info.szer),
                    static_cast<uint64_t>(info.wys), &pixels) ||
                __builtin_mul_overflow(pixels, 4ULL, &bytes) || bytes > 16384ULL)
                return 0;
            uint32_t* copy = static_cast<uint32_t*>(kmalloc(bytes));
            if (!copy) return 0;
            bool ok = skopiuj_z_przestrzeni_uzytkownika(
                copy, reinterpret_cast<const void*>(info.piksele), bytes);
            if (ok) {
                GuardEkranu guard;
                ok = bws_gui_ustaw_kursor_custom(aktualny_pid, &info, copy);
            }
            kfree(copy);
            return ok ? 1ULL : 0ULL;
        }
        case BWS_SOCKET_CREATE: return socket_create(*proces, a1);
        case BWS_SOCKET_CONNECT: return socket_connect(*proces, a1, a2, a3);
        case BWS_SOCKET_SEND: return socket_send(*proces, a1, a2, a3, a4);
        case BWS_SOCKET_RECV: return socket_recv(*proces, a1, a2, a3, a4);
        case BWS_SOCKET_CLOSE: return socket_close(*proces, a1);
        default: return UINT64_MAX;
    }
}
