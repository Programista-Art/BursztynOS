#include "blokowy.h"
#include "pamiec.h"
#include "grafika.h"
#include "instalator.h"
#include "hpet.h"

namespace {
struct FilterContext {
    Storage::Device* underlying;
    uint32_t id;
};

FilterContext filter_contexts[32]{};
Storage::Device devices[32]{};
uint32_t registered_device_count = 0;
Storage::Table table; // duże struktury poza 16 KiB stosem jadra
int installation_target = -1;
Storage::Device* dysk_glowny = nullptr;

static BlockError filter_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buf) {
    if (!dev || !dev->driver_data) return BlockError::DeviceUnavailable;
    auto* fc = static_cast<FilterContext*>(dev->driver_data);
    if (!fc->underlying) return BlockError::DeviceUnavailable;
    return fc->underlying->read(lba, count, buf);
}

static BlockError filter_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf) {
    if (!dev || !dev->driver_data) return BlockError::DeviceUnavailable;
    auto* fc = static_cast<FilterContext*>(dev->driver_data);
    if (!fc->underlying) return BlockError::DeviceUnavailable;
    if (instalator_tryb()) {
        if (installation_target < 0 || fc->id != static_cast<uint32_t>(installation_target)) {
            return BlockError::DeviceUnavailable;
        }
    }
    BlockError err = fc->underlying->write(lba, count, buf);
    if (err == BlockError::Ok) {
        dev->write_count = fc->underlying->write_count;
    }
    return err;
}

static BlockError filter_flush(BlockDevice* dev) {
    if (!dev || !dev->driver_data) return BlockError::DeviceUnavailable;
    auto* fc = static_cast<FilterContext*>(dev->driver_data);
    if (!fc->underlying) return BlockError::DeviceUnavailable;
    return fc->underlying->flush();
}

static BlockError filter_revalidate(BlockDevice* dev) {
    if (!dev || !dev->driver_data) return BlockError::DeviceUnavailable;
    auto* fc = static_cast<FilterContext*>(dev->driver_data);
    if (!fc->underlying) return BlockError::DeviceUnavailable;
    BlockError err = fc->underlying->revalidate();
    if (err == BlockError::Ok) {
        dev->total_sectors = fc->underlying->total_sectors;
        dev->logical_sector_size = fc->underlying->logical_sector_size;
        dev->physical_sector_size = fc->underlying->physical_sector_size;
        dev->max_transfer_sectors = fc->underlying->max_transfer_sectors;
        dev->read_only = fc->underlying->read_only;
        dev->removable = fc->underlying->removable;
        dev->device_type = fc->underlying->device_type;
        dev->write_count = fc->underlying->write_count;
        for (size_t k = 0; k < sizeof(dev->model) && k < sizeof(fc->underlying->model); ++k) {
            dev->model[k] = fc->underlying->model[k];
        }
        dev->model[sizeof(dev->model) - 1] = '\0';
        for (size_t k = 0; k < sizeof(dev->serial) && k < sizeof(fc->underlying->serial); ++k) {
            dev->serial[k] = fc->underlying->serial[k];
        }
        dev->serial[sizeof(dev->serial) - 1] = '\0';
        dev->sync_state();
    }
    return err;
}

static const BlockOperations blokowy_filtered_ops = {
    filter_read,
    filter_write,
    filter_flush,
    filter_revalidate
};

static bool filter_legacy_read(void* ctx, uint64_t lba, uint32_t count, void* buf) {
    auto* fc = static_cast<FilterContext*>(ctx);
    if (!fc || !fc->underlying) return false;
    return fc->underlying->read(lba, count, buf) == BlockError::Ok;
}

static bool filter_legacy_write(void* ctx, uint64_t lba, uint32_t count, const void* buf) {
    auto* fc = static_cast<FilterContext*>(ctx);
    if (!fc || !fc->underlying) return false;
    if (instalator_tryb()) {
        if (installation_target < 0 || fc->id != static_cast<uint32_t>(installation_target)) {
            return false;
        }
    }
    return fc->underlying->write(lba, count, buf) == BlockError::Ok;
}

static bool filter_legacy_flush(void* ctx) {
    auto* fc = static_cast<FilterContext*>(ctx);
    if (!fc || !fc->underlying) return false;
    return fc->underlying->flush() == BlockError::Ok;
}
} // namespace

bool blokowy_zarejestruj_dysk(Storage::Device* dev) {
    if (!dev || registered_device_count >= 32) return false;
    // Odrzucamy urządzenia uszkodzone, częściowo zainicjalizowane lub o zerowej pojemności/sektorze:
    if (dev->total_sectors == 0 || dev->logical_sector_size == 0 || !dev->can_read()) {
        return false;
    }
    uint32_t id = registered_device_count;
    filter_contexts[id].underlying = dev;
    filter_contexts[id].id = id;

    Storage::Device& d = devices[id];
    d = {};
    d.driver_data = &filter_contexts[id];
    d.ops = &blokowy_filtered_ops;
    d.total_sectors = dev->total_sectors;
    d.logical_sector_size = dev->logical_sector_size;
    d.physical_sector_size = dev->physical_sector_size;
    d.max_transfer_sectors = dev->max_transfer_sectors;
    d.read_only = dev->read_only;
    d.removable = dev->removable;
    d.device_type = dev->device_type;
    d.write_count = dev->write_count;
    for (size_t k = 0; k < sizeof(d.model) && k < sizeof(dev->model); ++k) {
        d.model[k] = dev->model[k];
    }
    d.model[sizeof(d.model) - 1] = '\0';
    for (size_t k = 0; k < sizeof(d.serial) && k < sizeof(dev->serial); ++k) {
        d.serial[k] = dev->serial[k];
    }
    d.serial[sizeof(d.serial) - 1] = '\0';
    d.ctx = &filter_contexts[id];
    d.sectors = dev->total_sectors;
    d.sector_size = dev->logical_sector_size;
    d.writable = !dev->read_only;
    d.legacy_read = filter_legacy_read;
    d.legacy_write = filter_legacy_write;
    d.legacy_flush = filter_legacy_flush;
    d.licz_io = false;   // telemetria liczona raz, na urzadzeniu sprzetowym (block_device.h: BlockIoStats)
    d.sync_state();

    registered_device_count++;
    return true;
}

static uint64_t zegar_io_ns() { return hpet_dostepny() ? czas_monotoniczny_ns() : 0; }

void blokowy_inicjalizuj() {
    g_block_io_clock_ns = zegar_io_ns;
    registered_device_count = 0;
    dysk_glowny = nullptr;
    installation_target = -1;
}

uint32_t blokowy_liczba_dyskow() {
    return registered_device_count;
}

Storage::Device* blokowy_dysk(uint32_t i) {
    return i < registered_device_count ? &devices[i] : nullptr;
}

const Storage::Device* blokowy_dysk_sprzetowy(uint32_t id) {
    return id < registered_device_count ? filter_contexts[id].underlying : nullptr;
}

bool blokowy_sprawdz_tozsamosc(uint32_t id) {
    auto* dev = blokowy_dysk(id);
    if (!dev) return false;
    return dev->revalidate() == BlockError::Ok;
}

uint64_t blokowy_licznik_zapisow(uint32_t id) {
    if (id == UINT32_MAX) {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < registered_device_count; ++i) {
            uint64_t w = filter_contexts[i].underlying ? filter_contexts[i].underlying->write_count : devices[i].write_count;
            if (sum <= UINT64_MAX - w) sum += w;
            else sum = UINT64_MAX;
        }
        return sum;
    }
    if (id < registered_device_count) {
        return filter_contexts[id].underlying ? filter_contexts[id].underlying->write_count : devices[id].write_count;
    }
    return 0;
}

bool blokowy_znajdz_bsp(uint32_t ordinal, Storage::Partition* out) {
    if (!out) return false;
    uint32_t found = 0;
    for (uint32_t d = 0; d < registered_device_count; ++d) {
        auto* dev = blokowy_dysk(d);
        if (!Storage::gpt_read(dev, &table)) continue;
        for (uint32_t i = 0; i < table.count; ++i) {
            const auto& e = table.entries[i];
            if (!Storage::equal(e.type, Storage::BspType, 16)) continue;
            Storage::Partition p{dev, e.start, e.count};
            if (!Storage::complete(p, table.uuid)) {
                klog_serial("[BSP] Pomijam nieukonczona/uszkodzona instalacje.");
                continue;
            }
            if (found++ == ordinal) {
                dysk_glowny = dev;
                *out = p;
                klog_serial("[BSP] Wybrano kompletny wolumin GPT (kolejnosc HBA/port/wpis).");
                return true;
            }
        }
    }
    return false;
}

bool blokowy_znajdz_esp(uint32_t ordinal, Storage::Partition* out) {
    if (!out) return false;
    uint32_t found = 0;
    for (uint32_t d = 0; d < registered_device_count; ++d) {
        auto* dev = blokowy_dysk(d);
        if (!Storage::gpt_read(dev, &table)) continue;
        for (uint32_t i = 0; i < table.count; ++i) {
            const auto& e = table.entries[i];
            if (!Storage::equal(e.type, Storage::EspType, 16)) continue;
            Storage::Partition p{dev, e.start, e.count};
            if (found++ == ordinal) {
                *out = p;
                return true;
            }
        }
    }
    return false;
}

bool blokowy_esp_systemowy(Storage::Partition* out) {
    if (!out) return false;
    Storage::Device* dev = dysk_glowny ? dysk_glowny : blokowy_dysk(0);
    if (!dev || !Storage::gpt_read(dev, &table)) return false;
    for (uint32_t i = 0; i < table.count; ++i) {
        const auto& e = table.entries[i];
        if (Storage::equal(e.type, Storage::EspType, 16)) {
            *out = Storage::Partition{dev, e.start, e.count};
            return true;
        }
    }
    return false;
}

Storage::Device* blokowy_dysk_systemowy() {
    return dysk_glowny ? dysk_glowny : blokowy_dysk(0);
}

void blokowy_zezwol_instalatorowi(uint32_t id, bool enabled) {
    installation_target = enabled ? int(id) : -1;
}

bool blokowy_zapis_instalatora() {
    return installation_target >= 0;
}

