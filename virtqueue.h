/*
 * Bursztyn OS - Wspolna warstwa Split VirtQueue (VirtIO 1.x)
 *
 * Plik: virtqueue.h
 *
 * Implementacja uniwersalnej kolejki Split VirtQueue zgodnej ze standardem
 * VirtIO 1.x (VirtIO 1.0/1.1/1.2 sec 2.6).
 *
 * Zapewnia:
 *   - uklad i wyrownanie struktur: Descriptor Table (16 B), Available Ring (2 B), Used Ring (4 B/64 B),
 *   - weryfikacje rozmiaru kolejki zgloszonego przez urzadzenie (wymog potegi 2, max 32768),
 *   - 64-bitowe fizyczne adresy DMA (queue_desc, queue_driver, queue_device),
 *   - bezpieczne zarzadzanie deskryptorami (wykrywanie wyczerpania, ochrona przed double-free / double-completion),
 *   - obsluge lancuchow deskryptorow (direct chains) z buforami device-readable i device-writable,
 *   - naturalna obsluge zawijania 16-bitowych indeksow avail->idx i used->idx,
 *   - bariery pamieciowe (Store-Store przed avail idx, Store-Load mfence przed powiadomieniem,
 *     Load-Load przed odczytem wpisow used, bariera spojnosci DMA przed zwrotem bufora),
 *   - pelny rollback w razie niepowodzenia inicjalizacji kolejki.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "virtio_pci.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. FLAGE I STALE STRUKTUR VIRTQUEUE (VirtIO 1.0 sec 2.6)
 * ========================================================================= */

#define VRING_DESC_F_NEXT     0x0001U
#define VRING_DESC_F_WRITE    0x0002U
#define VRING_DESC_F_INDIRECT 0x0004U

#define VRING_AVAIL_F_NO_INTERRUPT 0x0001U
#define VRING_USED_F_NO_NOTIFY     0x0001U

#define VIRTQUEUE_MAX_QUEUE_SIZE 32768U

/* =========================================================================
 * 2. STRUKTURY PIERSCIENIA SPLIT VIRTQUEUE (VirtIO 1.0 sec 2.6)
 * ========================================================================= */

#pragma pack(push, 1)

/*
 * Wpis w tablicy deskryptorow (16 bajtow):
 */
struct VirtqDesc {
    uint64_t addr;  /* Fizyczny 64-bitowy adres DMA bufora */
    uint32_t len;   /* Dlugosc bufora w bajtach */
    uint16_t flags; /* VRING_DESC_F_* */
    uint16_t next;  /* Indeks nastepnego deskryptora w lancuchu */
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

/*
 * Pierscien buforow udostepnionych przez sterownik (Available Ring):
 */
struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; /* ring[queue_size], nastepnie used_event jesli wynegocjowano */
};

/*
 * Wpis w pierscieniu zakonczonych zadan (Used Ring element, 8 bajtow):
 */
struct VirtqUsedElem {
    uint32_t id;  /* Indeks pierwszego deskryptora w zwroconym lancuchu */
    uint32_t len; /* Liczba bajtow zapisana przez urzadzenie */
};

/*
 * Pierscien zadan ukonczonych przez urzadzenie (Used Ring):
 */
struct VirtqUsed {
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[]; /* ring[queue_size], nastepnie avail_event jesli wynegocjowano */
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#pragma pack(pop)

static_assert(sizeof(VirtqDesc) == 16, "VirtqDesc rozmiar != 16");
static_assert(sizeof(VirtqUsedElem) == 8, "VirtqUsedElem rozmiar != 8");

/* =========================================================================
 * 3. STRUKTURY DANYCH STEROWNIKA DLA TRANSKACJI I KOLEJKI
 * ========================================================================= */

/*
 * Opis pojedynczego bufora DMA w lancuchu scatter-gather:
 */
struct VirtqBuffer {
    uint64_t phys_addr; /* 64-bitowy fizyczny adres DMA */
    uint32_t len;       /* Dlugosc bufora */
    bool     is_write;  /* true = device-writable, false = device-readable */
};

/*
 * Stan lokalny pojedynczego deskryptora:
 */
struct VirtQueueDescState {
    void*    cookie;
    uint32_t total_buf_len;
    uint16_t chain_len;
    bool     is_allocated;
    bool     is_chain_head;
};

/*
 * Glowna struktura zarzadzania kolejka VirtQueue:
 */
struct VirtQueue {
    VirtioPciDevice* dev;
    uint16_t queue_index;
    uint16_t queue_size;
    uint16_t queue_notify_off;

    /* Bufor DMA zawierajacy struktury pierscieni: */
    void*    dma_virtual;
    uint64_t dma_physical;
    size_t   dma_size;
    size_t   dma_pages;

    /* Wskazniki wirtualne do struktur pierscienia: */
    volatile VirtqDesc*  desc;
    volatile VirtqAvail* avail;
    volatile VirtqUsed*  used;

    /* Fizyczne 64-bitowe adresy przekazane urzadzeniu: */
    uint64_t desc_phys;
    uint64_t driver_phys;
    uint64_t device_phys;

    /* Indeks ostatnio przetworzonego elementu w used ring: */
    uint16_t last_used_idx;

    /* Zarzadzanie pula wolnych deskryptorow: */
    uint16_t free_head;
    uint16_t num_free;
    uint16_t* free_desc_next;
    VirtQueueDescState* desc_state;

    bool initialized;
};

/* =========================================================================
 * 4. PUBLICZNE API VIRTQUEUE
 * ========================================================================= */

/*
 * Alokuje, inicjalizuje i wlacza kolejke Split VirtQueue dla zadanego indeksu.
 * Odczytuje i waliduje rozmiar kolejki z rejestrow VirtIO transportu.
 * Zwraca wskaznik do VirtQueue lub nullptr w razie bledu.
 * W razie niepowodzenia zwalnia wszystkie alokacje i pozostawia kolejke wylaczona.
 * NIE ustawia flaqi DRIVER_OK w urzadzeniu.
 */
VirtQueue* virtqueue_create(VirtioPciDevice* dev, uint16_t queue_index);

/*
 * Zwalnia zasoby kolejki, wylacza kolejke w kontrolerze i zwalnia pamiec DMA.
 */
void virtqueue_destroy(VirtQueue* vq);

/*
 * Zglasza lancuch buforow do kolejki.
 * bufs: tablica buforow VirtqBuffer
 * num_bufs: liczba buforow w lancuchu (1 .. queue_size)
 * cookie: token przekazany przez wywolujacego, zwracany przy zakonczeniu
 * Zwraca: indeks pierwszego deskryptora (head >= 0) w razie sukcesu,
 *         kod ujemny w razie bledu (-1 = bledne parametry, -2 = brak deskryptorow, -3 = bledny adres fizyczny).
 */
int virtqueue_submit(VirtQueue* vq, const VirtqBuffer* bufs, uint32_t num_bufs, void* cookie);

/*
 * Wysyla powiadomienie do urzadzenia (kick).
 * Jesli force == false, powiadomienie jest wysylane tylko wtedy, gdy urzadzenie nie ustawilo
 * flagi VRING_USED_F_NO_NOTIFY.
 * Zwraca true, jesli powiadomienie zostalo fizycznie wyslane do rejestru notify.
 */
bool virtqueue_kick(VirtQueue* vq, bool force);

/*
 * Sprawdza, czy w pierscieniu used ring znajduja sie nieprzetworzone zakonczenia.
 */
bool virtqueue_has_used(const VirtQueue* vq);

/*
 * Pobiera kolejne zakonczone zadanie z pierscienia used ring.
 * out_cookie: wskaznik do zapisania tokenu powiazanego z zadaniem
 * out_len: wskaznik do zapisania liczby bajtow zapisanych przez urzadzenie
 * Zwraca: indeks deskryptora glowy (>= 0) w razie sukcesu,
 *         -1 jesli brak zadan,
 *         -2 w razie uszkodzenia indeksu (id >= queue_size),
 *         -3 w razie zduplikowanego zakonczenia / niepoprawnej glowy lancucha,
 *         -4 w razie niezgodnosci dlugosci lub uszkodzenia lancucha.
 */
int virtqueue_pop_used(VirtQueue* vq, void** out_cookie, uint32_t* out_len);

uint16_t virtqueue_get_size(const VirtQueue* vq);
uint16_t virtqueue_get_num_free(const VirtQueue* vq);

/*
 * Testowa transakcja sprzetowa VirtIO-RNG (uzywana przez jadro w QEMU do walidacji):
 */
bool virtqueue_test_rng(VirtioPciDevice* dev);

/*
 * Polskie aliasy ABI zgodne ze stylem Bursztyn OS:
 */
static inline VirtQueue* virtqueue_utworz(VirtioPciDevice* dev, uint16_t indeks) {
    return virtqueue_create(dev, indeks);
}

static inline void virtqueue_zniszcz(VirtQueue* vq) {
    virtqueue_destroy(vq);
}

static inline int virtqueue_dodaj(VirtQueue* vq, const VirtqBuffer* bufs, uint32_t n, void* cookie) {
    return virtqueue_submit(vq, bufs, n, cookie);
}

static inline bool virtqueue_powiadom(VirtQueue* vq, bool force) {
    return virtqueue_kick(vq, force);
}

static inline bool virtqueue_czy_gotowe(const VirtQueue* vq) {
    return virtqueue_has_used(vq);
}

static inline int virtqueue_pobierz_uzyty(VirtQueue* vq, void** cookie, uint32_t* len) {
    return virtqueue_pop_used(vq, cookie, len);
}

#ifdef __cplusplus
}
#endif
