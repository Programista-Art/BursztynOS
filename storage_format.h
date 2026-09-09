#pragma once
#include <stdint.h>
#include <stddef.h>

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
bool source_iso(Device*); // konserwatywnie chroni każdy nośnik ISO9660
struct BootFile { const uint8_t* data; uint32_t size; };
bool fat_install(const Partition&,BootFile efi,BootFile kernel,BootFile config);
bool marker(const Partition&,const uint8_t uuid[16],uint64_t fs_bytes,bool complete);
bool complete(const Partition&,const uint8_t uuid[16]);
}
