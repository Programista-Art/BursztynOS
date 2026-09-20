/*
 * Bursztyn OS - Sterownik sprzetowego generatora liczb losowych VirtIO-RNG (VirtIO 1.x)
 *
 * Plik: virtio_rng.h
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "virtio_pci.h"
#include "virtqueue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maksymalny rozmiar pojedynczego bufora DMA na jedno zadanie entropii w sterowniku (64 B).
 * Dzieki temu narzut DMA jest minimalny, a wieksze zadania sa dzielone na bezpieczne fragmenty.
 */
constexpr size_t VIRTIO_RNG_BUFFER_SIZE = 64U;

/*
 * Domyslny timeout w milisekundach dla pojedynczej transakcji entropii (deadline monotoniczny).
 */
constexpr uint64_t VIRTIO_RNG_DEFAULT_TIMEOUT_MS = 100ULL;

/*
 * Wtorna ochrona petli (spin count guard) zapobiegajaca zawieszeniu CPU
 * w sytuacji niedostepnosci zegara monotonicznego (wczesny bootloader / brak HPET).
 */
constexpr uint32_t VIRTIO_RNG_MAX_SPINS_GUARD = 10000000U;

/*
 * Stan urzadzenia i sterownika VirtIO-RNG:
 */
struct VirtioRngDriver {
    VirtioPciDevice* dev;
    VirtQueue*       vq;
    void*            dma_virtual;
    uint64_t         dma_physical;
    size_t           dma_pages;
    size_t           buffer_size;
    bool             initialized;
    bool             has_error;
    uint64_t         total_bytes_read;
    uint64_t         total_requests;
};

/*
 * Bezpieczne, nieoptymalizowalne przez kompilator zerowanie pamieci z danymi wrazliwymi.
 */
void virtio_rng_secure_zero(void* ptr, size_t len);

/*
 * Inicjalizuje sterownik dla wykrytego urzadzenia modern VirtIO-RNG PCI.
 * Weryfikuje typ urzadzenia, tworzy Split VirtQueue 0, alokuje stabilny bufor DMA
 * i po zakonczeniu konfiguracji ustawia flage DRIVER_OK.
 */
bool virtio_rng_init(VirtioPciDevice* dev);

/*
 * Skanuje wykryte urzadzenia PCI VirtIO w poszukiwaniu VirtIO-RNG i inicjalizuje pierwsze dostepne.
 */
bool virtio_rng_probe_and_init(void);

/*
 * Zwraca true, jezeli sterownik VirtIO-RNG jest aktywny i gotowy do pobierania entropii.
 */
bool virtio_rng_is_ready(void);

/*
 * Pobiera do max_len bajtow entropii sprzetowej z VirtIO-RNG do bufora dst.
 * Wykonuje bounded wait z limitem czasu monotonicznego (deadline timeout_ms).
 * Zwraca rzeczywista liczbe pobranych bajtow (0 w razie bledu lub timeoutu).
 * W przypadku czesciowego wypelnienia zeruje pozostala czesc bufora dst.
 */
size_t virtio_rng_read_bytes(void* dst, size_t max_len, uint64_t timeout_ms);

/*
 * Wygodny helper uzywajacy domyslnego timeoutu (100 ms).
 */
size_t virtio_rng_read(void* dst, size_t max_len);

/*
 * Zatrzymuje urzadzenie z potwierdzonym resetem sprzetowym (status == 0),
 * a nastepnie zwalnia struktury VirtQueue, bufor DMA i resetuje urzadzenie w transporcie PCI.
 * Jezeli urzadzenie nie zresetuje sie poprawnie, pamiec DMA NIE jest zwalniana (kwarantanna).
 */
bool virtio_rng_reset(void);

/*
 * Pobiera wskaznik do aktywnego sterownika VirtIO-RNG (uzywane glownie w testach).
 */
VirtioRngDriver* virtio_rng_get_driver(void);

/*
 * Test obciazeniowy QEMU / host: conajmniej 1000 transakcji z weryfikacja danych,
 * braku uszkodzen kolejki oraz zerowej delty PMM/DMA po resetach.
 */
bool virtio_rng_stress_test(VirtioPciDevice* dev);

/*
 * Polskie aliasy ABI zgodne ze stylem Bursztyn OS:
 */
static inline bool virtio_rng_zainicjalizuj(VirtioPciDevice* dev) {
    return virtio_rng_init(dev);
}
static inline bool virtio_rng_czy_gotowy(void) {
    return virtio_rng_is_ready();
}
static inline size_t virtio_rng_odczytaj(void* dst, size_t len) {
    return virtio_rng_read(dst, len);
}
static inline bool virtio_rng_zresetuj(void) {
    return virtio_rng_reset();
}
static inline void virtio_rng_bezpieczne_zerowanie(void* ptr, size_t len) {
    virtio_rng_secure_zero(ptr, len);
}

#ifdef __cplusplus
}
#endif
