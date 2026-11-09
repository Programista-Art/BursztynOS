/*
 * Bursztyn OS - Abstrakcja Urzadzenia Blokowego (BlockDevice API)
 *
 * Wspolny interfejs blokowy dla urzadzen masowych w jadrze Bursztyn OS.
 * Umozliwia obsluge sterownikow AHCI/SATA, VirtIO-BLK, NVMe oraz nosnikow
 * ramdyskowych bez zaleznosci wyzszych warstw (GPT, partycje, BSP2/PSF)
 * od konkretnego protokolu sprzetowego.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Jawne kody bledow operacji wejscia/wyjscia warstwy blokowej.
 */
enum class BlockError : int32_t {
    Ok = 0,
    InvalidArgument = -1,
    OutOfRange = -2,
    Unsupported = -3,
    IOError = -4,
    Timeout = -5,
    DeviceUnavailable = -6,
    ResourceFailure = -7,
};

inline constexpr bool block_sukces(BlockError err) noexcept {
    return err == BlockError::Ok;
}

inline const char* block_error_str(BlockError err) noexcept {
    switch (err) {
        case BlockError::Ok: return "OK";
        case BlockError::InvalidArgument: return "Nieprawidlowy argument";
        case BlockError::OutOfRange: return "Zakres poza granicami nosnika";
        case BlockError::Unsupported: return "Operacja nieobslugiwana";
        case BlockError::IOError: return "Blad wejscia/wyjscia (I/O)";
        case BlockError::Timeout: return "Przekroczenie limitu czasu (timeout)";
        case BlockError::DeviceUnavailable: return "Urzadzenie niedostepne";
        case BlockError::ResourceFailure: return "Blad zasobow/DMA";
        default: return "Nieznany blad blokowy";
    }
}

/*
 * Typ urzadzenia blokowego dla celow polityki instalatora i diagnostyki.
 */
enum class BlockDeviceType : uint32_t {
    Unknown = 0,
    AHCI = 1,
    VirtIO = 2,
    NVMe = 3,
    Ramdisk = 4,
};

/*
 * Sprawdza, czy zakres [start, start + count) miesci sie w granicach capacity
 * bez zadnego ryzyka przepelnienia calkowitoliczbowego (integer overflow).
 */
inline constexpr bool block_range_valid(uint64_t start, uint64_t count, uint64_t capacity) noexcept {
    if (count == 0) return false;
    if (start >= capacity) return false;
    if (count > capacity - start) return false;
    return true;
}

struct BlockDevice;

/*
 * Tabela operacji niskopoziomowych sterownika urzadzenia blokowego.
 */
struct BlockOperations {
    BlockError (*read)(BlockDevice* dev, uint64_t lba, uint32_t count, void* buf);
    BlockError (*write)(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf);
    BlockError (*flush)(BlockDevice* dev);
    BlockError (*revalidate)(BlockDevice* dev);
};

/*
 * Glowna struktura urzadzenia blokowego (BlockDevice).
 */
struct BlockDevice {
    union {
        void* driver_data;
        void* ctx;
    };
    const BlockOperations* ops;

    union {
        uint64_t total_sectors;
        uint64_t sectors;
    };
    union {
        uint32_t logical_sector_size;
        uint32_t sector_size;
    };
    uint32_t physical_sector_size;
    uint32_t max_transfer_sectors;

    bool read_only;
    bool removable;
    BlockDeviceType device_type;

    char model[41];
    char serial[21];

    uint64_t write_count;

    /*
     * Pola zgodnosci wstecznej z istniejacym Storage::Device (dla testow hostowych i istniejacego ABI):
     */
    bool writable;
    bool (*legacy_read)(void* ctx, uint64_t lba, uint32_t count, void* buf);
    bool (*legacy_write)(void* ctx, uint64_t lba, uint32_t count, const void* buf);
    bool (*legacy_flush)(void* ctx);

    constexpr BlockDevice()
        : driver_data(nullptr), ops(nullptr),
          total_sectors(0),
          logical_sector_size(512),
          physical_sector_size(512), max_transfer_sectors(0),
          read_only(false), removable(false),
          device_type(BlockDeviceType::Unknown),
          model{0}, serial{0}, write_count(0),
          writable(true),
          legacy_read(nullptr), legacy_write(nullptr), legacy_flush(nullptr) {}

    BlockDevice(void* c, uint64_t s, uint32_t sz, bool w,
                bool (*rd)(void*, uint64_t, uint32_t, void*),
                bool (*wr)(void*, uint64_t, uint32_t, const void*),
                bool (*fl)(void*))
        : driver_data(c), ops(nullptr),
          total_sectors(s),
          logical_sector_size(sz),
          physical_sector_size(sz), max_transfer_sectors(0),
          read_only(!w), removable(false),
          device_type(BlockDeviceType::Unknown),
          model{0}, serial{0}, write_count(0),
          writable(w),
          legacy_read(rd), legacy_write(wr), legacy_flush(fl) {}

    void sync_state() {
        if (!writable && !read_only) read_only = true;
        if (read_only && writable) writable = false;
    }

    bool can_read() const {
        return (ops && ops->read != nullptr) || legacy_read != nullptr;
    }

    bool can_write() const {
        return !read_only && writable && ((ops && ops->write != nullptr) || legacy_write != nullptr);
    }

    BlockError read(uint64_t lba, uint32_t count, void* buf);
    BlockError write(uint64_t lba, uint32_t count, const void* buf);
    BlockError flush();
    BlockError revalidate();
};

inline BlockError BlockDevice::read(uint64_t lba, uint32_t count, void* buf) {
    if (!buf || count == 0) return BlockError::InvalidArgument;
    if (total_sectors == 0 || lba >= total_sectors || static_cast<uint64_t>(count) > total_sectors - lba) {
        return BlockError::OutOfRange;
    }
    uint32_t sec_sz = logical_sector_size ? logical_sector_size : 512;
    if (sec_sz < 512 || (sec_sz & (sec_sz - 1)) != 0 || sec_sz > 65536) {
        return BlockError::InvalidArgument;
    }
    if (static_cast<uint64_t>(count) > (static_cast<uint64_t>(SIZE_MAX) / sec_sz)) {
        return BlockError::InvalidArgument;
    }
    const size_t total_bytes = static_cast<size_t>(count) * sec_sz;
    const uintptr_t buf_addr = reinterpret_cast<uintptr_t>(buf);
    if (buf_addr > UINTPTR_MAX - total_bytes) {
        return BlockError::InvalidArgument;
    }

    if (ops && ops->read) {
        uint32_t limit = max_transfer_sectors;
        if (limit == 0 || count <= limit) {
            return ops->read(this, lba, count, buf);
        }
        uint8_t* ptr = static_cast<uint8_t*>(buf);
        uint32_t done = 0;
        while (done < count) {
            uint32_t chunk = (count - done > limit) ? limit : (count - done);
            size_t byte_offset = static_cast<size_t>(done) * sec_sz;
            BlockError err = ops->read(this, lba + done, chunk, ptr + byte_offset);
            if (err != BlockError::Ok) return err;
            done += chunk;
        }
        return BlockError::Ok;
    }
    if (legacy_read) {
        return legacy_read(driver_data, lba, count, buf) ? BlockError::Ok : BlockError::IOError;
    }
    return BlockError::Unsupported;
}

inline BlockError BlockDevice::write(uint64_t lba, uint32_t count, const void* buf) {
    if (!buf || count == 0) return BlockError::InvalidArgument;
    if (read_only || !writable) return BlockError::Unsupported;
    if (total_sectors == 0 || lba >= total_sectors || static_cast<uint64_t>(count) > total_sectors - lba) {
        return BlockError::OutOfRange;
    }
    uint32_t sec_sz = logical_sector_size ? logical_sector_size : 512;
    if (sec_sz < 512 || (sec_sz & (sec_sz - 1)) != 0 || sec_sz > 65536) {
        return BlockError::InvalidArgument;
    }
    if (static_cast<uint64_t>(count) > (static_cast<uint64_t>(SIZE_MAX) / sec_sz)) {
        return BlockError::InvalidArgument;
    }
    const size_t total_bytes = static_cast<size_t>(count) * sec_sz;
    const uintptr_t buf_addr = reinterpret_cast<uintptr_t>(buf);
    if (buf_addr > UINTPTR_MAX - total_bytes) {
        return BlockError::InvalidArgument;
    }

    if (ops && ops->write) {
        uint32_t limit = max_transfer_sectors;
        if (limit == 0 || count <= limit) {
            BlockError err = ops->write(this, lba, count, buf);
            if (err == BlockError::Ok) {
                if (write_count <= UINT64_MAX - count) write_count += count;
                else write_count = UINT64_MAX;
            }
            return err;
        }
        const uint8_t* ptr = static_cast<const uint8_t*>(buf);
        uint32_t done = 0;
        while (done < count) {
            uint32_t chunk = (count - done > limit) ? limit : (count - done);
            size_t byte_offset = static_cast<size_t>(done) * sec_sz;
            BlockError err = ops->write(this, lba + done, chunk, ptr + byte_offset);
            if (err != BlockError::Ok) return err;
            if (write_count <= UINT64_MAX - chunk) write_count += chunk;
            else write_count = UINT64_MAX;
            done += chunk;
        }
        return BlockError::Ok;
    }
    if (legacy_write) {
        bool ok = legacy_write(driver_data, lba, count, buf);
        if (ok) {
            if (write_count <= UINT64_MAX - count) write_count += count;
            else write_count = UINT64_MAX;
        }
        return ok ? BlockError::Ok : BlockError::IOError;
    }
    return BlockError::Unsupported;
}

inline BlockError BlockDevice::flush() {
    if (ops && ops->flush) {
        return ops->flush(this);
    }
    if (legacy_flush) {
        return legacy_flush(driver_data) ? BlockError::Ok : BlockError::IOError;
    }
    return BlockError::Unsupported;
}

inline BlockError BlockDevice::revalidate() {
    if (ops && ops->revalidate) {
        return ops->revalidate(this);
    }
    if (total_sectors == 0) {
        return BlockError::DeviceUnavailable;
    }
    return BlockError::Ok;
}

