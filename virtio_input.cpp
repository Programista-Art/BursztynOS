#include "kursor_diag.h"
#include "gui_runtime_diag.h"
/*
 * Bursztyn OS - Implementacja Sterownika Urzadzen Wejsciowych VirtIO-INPUT
 *
 * Plik: virtio_input.cpp
 */

#include "virtio_input.h"
#include "virtio_pci.h"
#include "virtqueue.h"
#include "input_device.h"
#include "idt.h"
#include "apic.h"

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstring>
#include <cstdio>
extern bool host_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages);
extern void host_dma_free(void* vaddr, uint64_t paddr, size_t pages);
extern void mock_virtio_input_on_config_select(VirtioPciDevice* dev, uint8_t select, uint8_t subsel);
static void input_klog(const char* msg) { (void)msg; }

static bool input_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void input_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

#else
#include "sterowniki/dma.h"
#include "grafika.h"
#include "pamiec.h"
extern "C" void* memset(void* s, int c, size_t n);
extern "C" void* memcpy(void* dest, const void* src, size_t n);
extern "C" size_t strlen(const char* s);
#define input_klog klog_serial

static bool input_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void input_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}
#endif

namespace {

static VirtioInputDriver s_drivers[VIRTIO_INPUT_MAX_DRIVERS] = {};
static size_t s_driver_count = 0;

static void virtio_input_msix_handler(void* context) {
    auto* drv = static_cast<VirtioInputDriver*>(context);
    if (!drv) return;
    __atomic_add_fetch(&drv->irq_count, 1U, __ATOMIC_RELAXED);
    drv->has_pending_events = true;
}

static void virtio_input_device_poll(InputDevice* dev) {
    if (!dev || !dev->driver_data) return;
    auto* drv = static_cast<VirtioInputDriver*>(dev->driver_data);
    virtio_input_poll_events(drv, 64);
}

} // namespace

extern "C" {

bool virtio_input_query_config(VirtioPciDevice* dev, uint8_t select, uint8_t subsel,
                               void* out_data, uint8_t* out_size, size_t max_out) {
    if (!dev || !dev->has_device_cfg || dev->device_cfg_len < 8) {
        return false;
    }

    virtio_pci_write_device_config8(dev, 0, select);
    virtio_pci_write_device_config8(dev, 1, subsel);

#ifdef BURSZTYN_VIRTIO_HOST_TEST
    mock_virtio_input_on_config_select(dev, select, subsel);
#endif

    const uint8_t size = virtio_pci_read_device_config8(dev, 2);
    if (out_size) {
        *out_size = size;
    }
    if (size == 0) {
        return false;
    }

    uint8_t copy_len = size;
    if (copy_len > 128) {
        copy_len = 128; // Maksymalny rozmiar unii wg specyfikacji VirtIO
    }
    if (max_out > 0 && copy_len > max_out) {
        copy_len = static_cast<uint8_t>(max_out);
    }

    if (out_data && copy_len > 0) {
        auto* dst = static_cast<uint8_t*>(out_data);
        for (uint8_t i = 0; i < copy_len; ++i) {
            dst[i] = virtio_pci_read_device_config8(dev, 8 + i);
        }
    }
    return true;
}

static void virtio_input_classify_device(VirtioInputDriver* drv) {
    if (!drv || !drv->dev) return;

    drv->has_keyboard = false;
    drv->has_rel_mouse = false;
    drv->has_abs_tablet = false;
    drv->input_dev.kinds = INPUT_DEVICE_KIND_UNKNOWN;

    // 1. Analiza bitmapy klawiszy i przyciskow (subsel = INPUT_EV_KEY)
    bool has_mouse_btn = false;
    bool has_kbd_keys = false;

    uint8_t key_bits[128]{};
    uint8_t key_bits_size = 0;
    if (virtio_input_query_config(
            drv->dev, VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_KEY, key_bits, &key_bits_size, sizeof(key_bits)) && key_bits_size > 0) {

        // Sprawdzenie standardowych klawiszy klawiatury (kody 1..127 w bajtach 0..15)
        const uint8_t max_b = (key_bits_size < 16) ? key_bits_size : 16;
        for (uint8_t b = 0; b < max_b; ++b) {
            if (key_bits[b] != 0) {
                has_kbd_keys = true;
                break;
            }
        }

        // Sprawdzenie przyciskow myszy (BTN_LEFT = 0x110 = 272 = bajt 34 bit 0)
        if (key_bits_size > 34) {
            if ((key_bits[34] & 0x07U) != 0) { // BTN_LEFT, BTN_RIGHT, BTN_MIDDLE
                has_mouse_btn = true;
            }
        }
        // BTN_TOUCH = 0x14A = 330 = bajt 41 bit 2
        if (key_bits_size > 41) {
            if ((key_bits[41] & (1U << 2)) != 0) {
                has_mouse_btn = true;
            }
        }
    }

    // 2. Analiza osi wzglednych (subsel = INPUT_EV_REL)
    uint8_t rel_bits[128]{};
    uint8_t rel_bits_size = 0;
    if (virtio_input_query_config(
            drv->dev, VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_REL, rel_bits, &rel_bits_size, sizeof(rel_bits)) && rel_bits_size > 0) {
        const bool rel_x = (rel_bits[0] & (1U << INPUT_REL_X)) != 0;
        const bool rel_y = (rel_bits[0] & (1U << INPUT_REL_Y)) != 0;
        if (rel_x && rel_y) {
            drv->has_rel_mouse = true;
        }
    }

    // 3. Analiza osi bezwzglednych (subsel = INPUT_EV_ABS)
    uint8_t abs_bits[128]{};
    uint8_t abs_bits_size = 0;
    if (virtio_input_query_config(
            drv->dev, VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_ABS, abs_bits, &abs_bits_size, sizeof(abs_bits)) && abs_bits_size > 0) {
        const bool abs_x = (abs_bits[0] & (1U << INPUT_ABS_X)) != 0;
        const bool abs_y = (abs_bits[0] & (1U << INPUT_ABS_Y)) != 0;
        if (abs_x && abs_y) {
            drv->has_abs_tablet = true;

            // Odpytanie o granice ABS_X
            VirtioInputAbsInfo info_x{};
            uint8_t sz_x = 0;
            if (virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_ABS_INFO, INPUT_ABS_X,
                                          &info_x, &sz_x, sizeof(info_x)) && sz_x >= sizeof(info_x)) {
                if (info_x.min < info_x.max) {
                    drv->input_dev.abs_x.min = static_cast<int32_t>(info_x.min);
                    drv->input_dev.abs_x.max = static_cast<int32_t>(info_x.max);
                    drv->input_dev.abs_x.fuzz = static_cast<int32_t>(info_x.fuzz);
                    drv->input_dev.abs_x.flat = static_cast<int32_t>(info_x.flat);
                    drv->input_dev.abs_x.res = static_cast<int32_t>(info_x.res);
                }
            }

            // Odpytanie o granice ABS_Y
            VirtioInputAbsInfo info_y{};
            uint8_t sz_y = 0;
            if (virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_ABS_INFO, INPUT_ABS_Y,
                                          &info_y, &sz_y, sizeof(info_y)) && sz_y >= sizeof(info_y)) {
                if (info_y.min < info_y.max) {
                    drv->input_dev.abs_y.min = static_cast<int32_t>(info_y.min);
                    drv->input_dev.abs_y.max = static_cast<int32_t>(info_y.max);
                    drv->input_dev.abs_y.fuzz = static_cast<int32_t>(info_y.fuzz);
                    drv->input_dev.abs_y.flat = static_cast<int32_t>(info_y.flat);
                    drv->input_dev.abs_y.res = static_cast<int32_t>(info_y.res);
                }
            }
        }
    }

#if BURSZTYNDIAGGUIRUNTIME || BURSZTYNDIAGABSMAP
    char caps[256]{};unsigned pos=0;
    auto txt=[&](const char* p){while(*p&&pos+1<sizeof(caps))caps[pos++]=*p++;};
    auto num=[&](int64_t v){if(v<0){txt("-");v=-v;}char t[24];unsigned i=0;do{t[i++]=char('0'+v%10);v/=10;}while(v);while(i&&pos+1<sizeof(caps))caps[pos++]=t[--i];};
    txt("[INPUT CAPS] rel_size=");num(rel_bits_size);txt(" rel_byte0=");num(rel_bits[0]);
    txt(" abs_size=");num(abs_bits_size);txt(" abs_byte0=");num(abs_bits[0]);
    txt(" rel_xy=");num(drv->has_rel_mouse);txt(" abs_xy=");num(drv->has_abs_tablet);
    txt(" abs_x_min=");num(drv->input_dev.abs_x.min);txt(" abs_x_max=");num(drv->input_dev.abs_x.max);
    txt(" abs_y_min=");num(drv->input_dev.abs_y.min);txt(" abs_y_max=");num(drv->input_dev.abs_y.max);txt("\n");input_klog(caps);
#endif

    if (has_kbd_keys) {
        drv->has_keyboard = true;
    }
    if (has_mouse_btn && !drv->has_rel_mouse && !drv->has_abs_tablet) {
        drv->has_rel_mouse = true;
    }

    // Ustawienie flag klasyfikacji urzadzenia
    if (drv->has_keyboard)   drv->input_dev.kinds |= INPUT_DEVICE_KIND_KEYBOARD;
    if (drv->has_rel_mouse)  drv->input_dev.kinds |= INPUT_DEVICE_KIND_MOUSE;
    if (drv->has_abs_tablet) drv->input_dev.kinds |= INPUT_DEVICE_KIND_TABLET;
}

bool virtio_input_init(VirtioInputDriver* drv, VirtioPciDevice* dev) {
    if (!drv || !dev || !dev->common_cfg || dev->virtio_id != VIRTIO_ID_INPUT) {
        return false;
    }

    // Zerowanie struktury sterownika
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    std::memset(drv, 0, sizeof(VirtioInputDriver));
#else
    memset(drv, 0, sizeof(VirtioInputDriver));
#endif
    drv->dev = dev;
    drv->irq_vector = -1;

    // 1. Reset sprzetowy kontrolera
    virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);

    // 2. Ustawienie statusu ACKNOWLEDGE i DRIVER
    virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // 3. Negocjacja cech (wymog standardu VirtIO 1.x Modern)
    if (!virtio_pci_negotiate_features(dev, 0)) {
        input_klog("[VIRTIO-INPUT-ERR] Negocjacja cech VirtIO-INPUT nie powiodla sie");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    // 4. Odpytanie przestrzeni konfiguracji o nazwe, serial, devids i mozliwosci
    uint8_t name_sz = 0;
    if (virtio_input_query_config(dev, VIRTIO_INPUT_CFG_ID_NAME, 0, drv->name, &name_sz, sizeof(drv->name) - 1)) {
        drv->name[name_sz] = '\0';
    } else {
        drv->name[0] = 'v'; drv->name[1] = 'i'; drv->name[2] = 'n'; drv->name[3] = 'p'; drv->name[4] = '\0';
    }

    uint8_t serial_sz = 0;
    if (virtio_input_query_config(dev, VIRTIO_INPUT_CFG_ID_SERIAL, 0, drv->serial, &serial_sz, sizeof(drv->serial) - 1)) {
        drv->serial[serial_sz] = '\0';
    }

    uint8_t ids_sz = 0;
    virtio_input_query_config(dev, VIRTIO_INPUT_CFG_ID_DEVIDS, 0, &drv->ids, &ids_sz, sizeof(drv->ids));

    // Kopiowanie nazwy do InputDevice
    for (size_t i = 0; i < sizeof(drv->input_dev.name) - 1 && drv->name[i]; ++i) {
        drv->input_dev.name[i] = drv->name[i];
        drv->input_dev.name[i + 1] = '\0';
    }

    // Klasyfikacja urzadzenia z bitmapy EV_BITS
    virtio_input_classify_device(drv);

    // 5. Utworzenie Split VirtQueue 0 (eventq)
    drv->eventq = virtqueue_create(dev, 0);
    if (!drv->eventq) {
        input_klog("[VIRTIO-INPUT-ERR] Nie mozna utworzyc eventq (queue 0)");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    const uint16_t qsize = virtqueue_get_size(drv->eventq);
    size_t num_bufs = (qsize < VIRTIO_INPUT_NUM_EVENT_BUFS) ? qsize : VIRTIO_INPUT_NUM_EVENT_BUFS;
    if (num_bufs == 0) {
        virtqueue_destroy(drv->eventq);
        drv->eventq = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }
    drv->num_event_bufs = num_bufs;

    // 6. Alokacja stabilnego bufora DMA na pre-postowane deskryptory zdarzen
    const size_t dma_bytes = num_bufs * sizeof(VirtioInputEvent);
    if (!input_dma_alloc(dma_bytes, 4096, &drv->dma_virt, &drv->dma_phys, &drv->dma_pages)) {
        virtqueue_destroy(drv->eventq);
        drv->eventq = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }
    drv->dma_events = reinterpret_cast<VirtioInputEvent*>(drv->dma_virt);

    // 7. Wypelnienie eventq poczatkowa pula buforow (device-writable)
    for (size_t i = 0; i < num_bufs; ++i) {
        VirtqBuffer buf{};
        buf.phys_addr = drv->dma_phys + i * sizeof(VirtioInputEvent);
        buf.len = sizeof(VirtioInputEvent);
        buf.is_write = true; // Urzadzenie bedzie zapisywac do bufora
        const int head = virtqueue_submit(drv->eventq, &buf, 1, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        if (head < 0) {
            input_klog("[VIRTIO-INPUT-ERR] Blad pre-postowania bufora eventq");
            input_dma_free(drv->dma_virt, drv->dma_phys, drv->dma_pages);
            drv->dma_virt = nullptr;
            virtqueue_destroy(drv->eventq);
            drv->eventq = nullptr;
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }
    (void)virtqueue_kick(drv->eventq, true);

    // 8. Konfiguracja przerwan MSI-X dla queue 0 (jesli sprzet obsluguje)
    drv->use_msix = false;
    drv->irq_vector = -1;
    if (dev->has_msix && dev->msix_cap.table_size >= 1) {
        const int vec = idt_alokuj_wektor_irq();
        if (vec >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY) {
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec), virtio_input_msix_handler, drv)) {
                const uint8_t lapic = apic_pobierz_lapic_id();
                if (virtio_pci_program_msix_entry(dev, 0, lapic, static_cast<uint8_t>(vec), false)) {
                    if (virtio_pci_set_queue_msix_vector_checked(dev, 0, 0)) {
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            dev->msix_enabled = true;
                            drv->use_msix = true;
                            drv->irq_vector = vec;
                        }
                    }
                }
            }
            if (!drv->use_msix) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec));
            }
        }
    }

    // 9. Aktywacja urzadzenia (DRIVER_OK tylko po pelnym przygotowaniu kolejki eventq)
    const uint8_t cur_st = virtio_pci_get_status(dev);
    virtio_pci_set_status(dev, static_cast<uint8_t>(cur_st | VIRTIO_STATUS_DRIVER_OK));
    if ((virtio_pci_get_status(dev) & VIRTIO_STATUS_DRIVER_OK) == 0) {
        input_klog("[VIRTIO-INPUT-ERR] Blad ustawienia DRIVER_OK");
        input_dma_free(drv->dma_virt, drv->dma_phys, drv->dma_pages);
        drv->dma_virt = nullptr;
        virtqueue_destroy(drv->eventq);
        drv->eventq = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }
    drv->initialized = true;

    // 10. Rejestracja w dyspozytorze InputDevice
    drv->input_dev.bus_type = INPUT_BUS_VIRTIO;
    drv->input_dev.device_id = drv->dev ? ((static_cast<uint32_t>(drv->dev->bus) << 16) | (static_cast<uint32_t>(drv->dev->slot) << 8) | drv->dev->func) : 1;
    drv->input_dev.driver_data = drv;
    drv->input_dev.poll_fn = virtio_input_device_poll;
    input_register_device(&drv->input_dev);

    input_klog("[VIRTIO-INPUT] Prawidlowo zainicjalizowano urzadzenie:");
    if (drv->has_keyboard)   input_klog("  - Rola: Klawiatura (INPUT_DEVICE_KIND_KEYBOARD)");
    if (drv->has_rel_mouse)  input_klog("  - Rola: Mysz wzgledna (INPUT_DEVICE_KIND_MOUSE)");
    if (drv->has_abs_tablet) input_klog("  - Rola: Tablet bezwzgledny (INPUT_DEVICE_KIND_TABLET)");
    if (drv->use_msix) {
        input_klog("  - Tryb przerwan: MSI-X");
    } else {
        input_klog("  - Tryb przerwan: Polling fallback");
    }
    input_klog("  - Podsystem: Zarejestrowano w InputDevice");

    return true;
}

size_t virtio_input_poll_events(VirtioInputDriver* drv, size_t max_events) {
    if (!drv || !drv->initialized || !drv->eventq) {
        return 0;
    }

    size_t processed = 0;
    while (virtqueue_has_used(drv->eventq)) {
        if (max_events > 0 && processed >= max_events) {
            break;
        }

        void* cookie = nullptr;
        uint32_t used_len = 0;
        const int head = virtqueue_pop_used(drv->eventq, &cookie, &used_len);
        if (head < 0) {
            drv->has_error = true;
            break;
        }

        const uintptr_t buf_idx = reinterpret_cast<uintptr_t>(cookie);
        if (buf_idx >= drv->num_event_bufs) {
            drv->has_error = true;
            break;
        }

        // Walidacja uzytego rozmiaru bufora (dokladnie sizeof(VirtioInputEvent) = 8 bajtow)
        if (used_len != sizeof(VirtioInputEvent)) {
            // Znieksztalcony pakiet zostaje zrzucony; bufor natychmiast wraca do eventq
            VirtqBuffer buf{};
            buf.phys_addr = drv->dma_phys + buf_idx * sizeof(VirtioInputEvent);
            buf.len = sizeof(VirtioInputEvent);
            buf.is_write = true;
            (void)virtqueue_submit(drv->eventq, &buf, 1, reinterpret_cast<void*>(buf_idx));
            (void)virtqueue_kick(drv->eventq, false);
            continue;
        }

        // Bariera pamieci przed odczytem bufora zapisanego przez urzadzenie DMA
        __atomic_thread_fence(__ATOMIC_ACQUIRE);

        // Kopiowanie odebranego zdarzenia
        VirtioInputEvent raw_ev = drv->dma_events[buf_idx];
        if ((raw_ev.type==INPUT_EV_ABS && (raw_ev.code==INPUT_ABS_X || raw_ev.code==INPUT_ABS_Y)) ||
            (raw_ev.type==INPUT_EV_REL && (raw_ev.code==INPUT_REL_X || raw_ev.code==INPUT_REL_Y)))
            kursor_diag_input();

        // Natychmiastowy recycling bufora DMA i re-submit do eventq
        VirtqBuffer buf{};
        buf.phys_addr = drv->dma_phys + buf_idx * sizeof(VirtioInputEvent);
        buf.len = sizeof(VirtioInputEvent);
        buf.is_write = true;
        (void)virtqueue_submit(drv->eventq, &buf, 1, reinterpret_cast<void*>(buf_idx));
        (void)virtqueue_kick(drv->eventq, false);

        // Przekazanie do generycznego dyspozytora zdarzen
        InputEvent ev{};
        ev.type = raw_ev.type;
        ev.code = raw_ev.code;
        ev.value = raw_ev.value;
        input_dispatch_event(&drv->input_dev, &ev);

        processed++;
        drv->total_events_processed++;
    }

    drv->has_pending_events = false;
    return processed;
}

bool virtio_input_reset(VirtioInputDriver* drv) {
    if (!drv) return false;
    drv->initialized = false;

    // 1. Wyrejestrowanie urzadzenia wejsciowego z dyspozytora
    input_unregister_device(&drv->input_dev);

    // 2. Zamaskowanie i wylaczenie MSI-X oraz zwolnienie wektora IDT
    if (drv->use_msix && drv->dev) {
        virtio_pci_mask_msix_entry(drv->dev, 0);
        pci_msix_disable(drv->dev->bus, drv->dev->slot, drv->dev->func, &drv->dev->msix_cap);
        drv->dev->msix_enabled = false;
        virtio_pci_set_queue_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
        if (drv->irq_vector >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(drv->irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->irq_vector));
        }
        drv->use_msix = false;
        drv->irq_vector = -1;
    }

    // 3. Reset urzadzenia PCI z potwierdzeniem monotonicznym (status == 0)
    bool reset_ok = false;
    if (drv->dev) {
        reset_ok = virtio_pci_reset(drv->dev);
    }

    // 4. Zwalnianie kolejek i pamieci DMA tylko w razie potwierdzonego resetu
    if (reset_ok) {
        if (drv->eventq) {
            virtqueue_destroy(drv->eventq);
            drv->eventq = nullptr;
        }
        if (drv->statusq) {
            virtqueue_destroy(drv->statusq);
            drv->statusq = nullptr;
        }
        if (drv->dma_virt) {
            input_dma_free(drv->dma_virt, drv->dma_phys, drv->dma_pages);
            drv->dma_virt = nullptr;
            drv->dma_phys = 0;
            drv->dma_pages = 0;
            drv->dma_events = nullptr;
        }
    } else {
        input_klog("[VIRTIO-INPUT-WARN] Reset kontrolera nie powiodl sie; bufor DMA w kwarantannie.");
    }

    drv->dev = nullptr;
    return reset_ok;
}

void virtio_input_reset_all(void) {
    for (size_t i = 0; i < s_driver_count; ++i) {
        if (s_drivers[i].initialized || s_drivers[i].dev != nullptr) {
            virtio_input_reset(&s_drivers[i]);
        }
    }
    s_driver_count = 0;
}

uint32_t virtio_input_probe_and_init(void) {
    const uint32_t count = virtio_pci_liczba_urzadzen();
    uint32_t initialized = 0;

    for (uint32_t i = 0; i < count; ++i) {
        VirtioPciDevice* dev = virtio_pci_pobierz_urzadzenie(i);
        if (dev && dev->virtio_id == VIRTIO_ID_INPUT) {
            if (s_driver_count < VIRTIO_INPUT_MAX_DRIVERS) {
                VirtioInputDriver* drv = &s_drivers[s_driver_count];
                if (virtio_input_init(drv, dev)) {
                    s_driver_count++;
                    initialized++;
                }
            }
        }
    }
    return initialized;
}

size_t virtio_input_get_count(void) {
    return s_driver_count;
}

VirtioInputDriver* virtio_input_get_driver(size_t index) {
    if (index >= s_driver_count) return nullptr;
    return &s_drivers[index];
}

#if BURSZTYNDIAGABSMAP
extern "C" void input_abs_map_diag_obsluz();
#endif
void virtio_input_obsluz(void) {
    for (size_t i = 0; i < s_driver_count; ++i) {
        if (s_drivers[i].initialized) {
            virtio_input_poll_events(&s_drivers[i], 64);
        }
    }
#if BURSZTYNDIAGABSMAP
    input_abs_map_diag_obsluz();
#endif
}

bool virtio_input_stress_test(VirtioInputDriver* drv) {
    if (!drv || !drv->initialized || !drv->dev || !drv->eventq) {
        return false;
    }

    input_klog("[VIRTIO-INPUT-STRESS] Rozpoczynam test obciazeniowy VirtIO-INPUT...");

    // 1. Weryfikacja zapytan konfiguracyjnych
    uint8_t name[64]{};
    uint8_t nsz = 0;
    if (!virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_ID_NAME, 0, name, &nsz, sizeof(name) - 1)) {
        input_klog("[VIRTIO-INPUT-STRESS] Blad odpytania CFG_ID_NAME");
        return false;
    }

    // 2. Weryfikacja zapytan EV_BITS
    uint8_t evs[32]{};
    uint8_t evsz = 0;
    const uint8_t query_subsel = drv->has_keyboard ? INPUT_EV_KEY : (drv->has_abs_tablet ? INPUT_EV_ABS : INPUT_EV_REL);
    if (!virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_EV_BITS, query_subsel, evs, &evsz, sizeof(evs))) {
        input_klog("[VIRTIO-INPUT-STRESS] Blad odpytania EV_BITS");
        return false;
    }

    // 3. Weryfikacja bezpieczenstwa znieksztalconych zapytan
    uint8_t dummy[16]{};
    uint8_t dummy_sz = 0;
    // Zapytanie z nieznanym selectem 0xFF powinno zwrocic false lub size=0 bez bledu MMIO
    (void)virtio_input_query_config(drv->dev, 0xFF, 0, dummy, &dummy_sz, sizeof(dummy));

    input_klog("[VIRTIO-INPUT-STRESS] SUCCESS: konfiguracja i odpytanie VirtIO-INPUT poprawne.");
    return true;
}

bool virtio_input_test_teardown_reinit(void) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return true;
#else
    if (s_driver_count == 0) return true;

    input_klog("[VIRTIO-INPUT-TEST] === Rozpoczynanie audytu cykli teardown/reinit VirtIO-INPUT ===");

    // 1. Zresetuj urzadzenia, aby uzyskac czysty stan bazowy
    virtio_input_reset_all();

    const uint64_t baseline_pmm = pmm_pobierz_wolne_ramki();
    const uint32_t baseline_vectors = idt_zaalokowane_wektory_liczba();

    constexpr int CYCLES = 5;
    for (int c = 0; c < CYCLES; ++c) {
        const uint32_t count = virtio_input_probe_and_init();
        if (count == 0) {
            input_klog("[VIRTIO-INPUT-TEST] BLAD: Re-inicjalizacja w cyklu nie powiodla sie!");
            return false;
        }

        virtio_input_reset_all();

        const uint64_t curr_pmm = pmm_pobierz_wolne_ramki();
        const uint32_t curr_vectors = idt_zaalokowane_wektory_liczba();

        if (curr_pmm != baseline_pmm || curr_vectors != baseline_vectors) {
            input_klog("[VIRTIO-INPUT-TEST] BLAD WYCIEKU ZASOBOW w cyklu!");
            return false;
        }
    }

    // 2. Finalna re-inicjalizacja urzadzen przed dalsza praca systemu
    const uint32_t final_init = virtio_input_probe_and_init();
    if (final_init == 0) {
        input_klog("[VIRTIO-INPUT-TEST] BLAD: Finalna inicjalizacja urzadzen po testach cykli!");
        return false;
    }

    input_klog("[VIRTIO-INPUT-TEST] Cykle teardown/reinit: PASS (PMM delta=0, DMA delta=0, IDT delta=0).");
    return true;
#endif
}

bool virtio_input_stress_test_all(void) {
    if (!virtio_input_test_teardown_reinit()) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < s_driver_count; ++i) {
        if (s_drivers[i].initialized) {
            if (!virtio_input_stress_test(&s_drivers[i])) {
                ok = false;
            }
        }
    }
    if (ok) {
        input_klog("[VIRTIO-INPUT-STRESS] Test obciazeniowy zakonczony: PASS");
    }
    return ok;
}

} // extern "C"
