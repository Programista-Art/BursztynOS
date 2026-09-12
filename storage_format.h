#pragma once
#include <stdint.h>
#include <stddef.h>
#include "aktualizacja_rozruch.h"

namespace Storage {
constexpr uint32_t Sector = 512;
constexpr uint64_t MiB = 1024*1024;
constexpr uint64_t BspMaxBytes = 2ULL*1024*MiB; // konserwatywny limit pierwszego wydania
constexpr uint64_t EspBytes = 512*MiB;
struct Device {
    void* ctx;
    uint64_t sectors;
    uint32_t sector_size;
    bool writable;
    bool (*read)(void*,uint64_t,uint32_t,void*);
    bool (*write)(void*,uint64_t,uint32_t,const void*);
    bool (*flush)(void*);
};
struct Partition { Device* device; uint64_t start, count; };
struct Layout { Partition esp, bsp; uint64_t unallocated; };
struct Entry { uint8_t type[16],uuid[16]; uint64_t start,count; };
struct Table { uint8_t uuid[16]; uint32_t count; Entry entries[128]; };

struct FormatterWorkspace {
    uint8_t write_verify_check[4096];
    uint8_t gpt_h[512];
    uint8_t gpt_b[512];
    uint8_t gpt_sector[512];
    uint8_t gpt_backup[512];
    uint8_t gpt_install_z[4096];
    uint8_t fat_install_b[4096];
    uint8_t esp_boot_dir[4096];
    uint8_t esp_grub_dir[4096];
    uint8_t esp_slot_blk[4096];
    uint8_t esp_slot_boot_dir[4096];
    uint8_t esp_write_env[1024];
    uint8_t esp_read_env[1024];
    Table rozruch_table;
    uint8_t rozruch_h[512];
};

class GuardStorageFormatter {
public:
    GuardStorageFormatter();
    ~GuardStorageFormatter();
    bool aktywny() const { return aktywny_; }
    GuardStorageFormatter(const GuardStorageFormatter&) = delete;
    GuardStorageFormatter& operator=(const GuardStorageFormatter&) = delete;
private:
    bool aktywny_;
    bool pierwotny_;
};

extern const uint8_t BspType[16];
extern const uint8_t EspType[16];
bool range(uint64_t start,uint64_t count,uint64_t capacity);
bool read(const Partition&,uint64_t,uint32_t,void*);
bool write(const Partition&,uint64_t,uint32_t,const void*);
bool flush(const Partition&);
bool write_verify(const Partition&,uint64_t,uint32_t,const void*);
uint32_t crc32(const void*,size_t);
bool equal(const void*,const void*,size_t);
bool plan(Device*,Layout*);
bool gpt_read(Device*,Table*);
bool gpt_write(Device*,const Layout&,const uint8_t uuid[16]);
struct GptInstallState {
    Device* device;
    Layout layout;
    uint8_t uuid[16];
    uint32_t phase;
    uint32_t index;
    uint32_t entries_crc;
    uint32_t completed_units;
    uint32_t total_units;
    bool failed;
    bool complete;
};
bool gpt_install_start(Device*,const Layout&,const uint8_t uuid[16],GptInstallState*);
bool gpt_install_step(GptInstallState*);
bool source_iso(Device*); // konserwatywnie chroni każdy nośnik ISO9660

struct BootFile { const uint8_t* data; uint32_t size; };
struct FatInstallState {
    Partition partition;
    BootFile files[5];
    uint32_t fat_sectors;
    uint32_t data_start;
    uint32_t clusters;
    uint32_t file_start[5];
    uint32_t file_clusters[5];
    uint32_t next_cluster;
    uint32_t phase;
    uint32_t index;
    uint32_t file_index;
    uint32_t boot_crc_offset;
    uint32_t boot_crc_state;
    uint32_t completed_units;
    uint32_t total_units;
    bool failed;
    bool complete;
};
bool fat_install_start(const Partition&,BootFile efi,BootFile kernel,
                       BootFile config,FatInstallState*);
bool fat_install_step(FatInstallState*);
bool fat_install(const Partition&,BootFile efi,BootFile kernel,BootFile config);
bool marker(const Partition&,const uint8_t uuid[16],uint64_t fs_bytes,bool complete);
bool complete(const Partition&,const uint8_t uuid[16]);

struct EspLayout {
    uint64_t data_start_lba;
    uint32_t cluster_size_sectors;
    uint64_t jadro_a_lba;
    uint32_t jadro_a_clusters;
    uint32_t jadro_a_max_bytes;
    uint32_t jadro_a_size;
    uint64_t jadro_b_lba;
    uint32_t jadro_b_clusters;
    uint32_t jadro_b_max_bytes;
    uint32_t jadro_b_size;
    uint64_t grubenv_lba;
    uint64_t dirent_boot_lba;
    uint32_t dirent_jadro_a_offset;
    uint32_t dirent_jadro_b_offset;
};

// Formalnie zarezerwowane sektory stanu rozruchu Bursztyn OS (Bursztyn Boot State reserved sectors)
constexpr uint64_t BootStateSector0 = ROZRUCH_SEKTOR_KOPIA_0;
constexpr uint64_t BootStateSector1 = ROZRUCH_SEKTOR_KOPIA_1;
constexpr uint64_t BootStateSectorCount = ROZRUCH_SEKTORY_LICZBA;
constexpr uint64_t EspMinStartSector = ROZRUCH_ESP_MIN_START_LBA;

bool esp_odczytaj_uklad(const Partition& esp, EspLayout* out);
bool esp_zapisz_jadro_slotu(const Partition& esp, uint8_t slot, const uint8_t* dane, uint32_t rozmiar);
bool esp_odczytaj_jadro_slotu(const Partition& esp, uint8_t slot, uint8_t* bufor, uint32_t maks_rozmiar, uint32_t* odczytano);
bool esp_zapisz_grubenv(const Partition& esp, const char* potwierdzony, const char* oczekujacy, uint32_t proby);
bool esp_odczytaj_grubenv(const Partition& esp, char* potwierdzony, char* oczekujacy, uint32_t* proby);
bool rozruch_zapisz_sektory(Device* d, const bursztyn_rozruch_stan* s);
bool rozruch_odczytaj_sektory(Device* d, bursztyn_rozruch_stan* s);
bool rozruch_weryfikuj_uklad_dysku(Device* d);
}

