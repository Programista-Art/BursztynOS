// Host exercises the production partition/GPT/FAT and BSP2 code with a faulting backend.
#include "../../storage_format.h"
#include "../../psf.h"
#include "../../zegar-rtc.h"
#include "../../instalator_format.h"
#include "../../tapety.h"
#include <array>
#include <map>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
using namespace Storage;
void* kmalloc(size_t n){return std::malloc(n);}
void wypisz_log(const char*){}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*){return false;}
bool authorized=true;
bool blokowy_zapis_instalatora(){return authorized;}
struct Fake {
    std::map<uint64_t,std::array<uint8_t,512>> data;
    uint64_t calls=0,sectors=0,fail_after=UINT64_MAX,low=0,high=UINT64_MAX;
    bool flush_fail=false,read_fail=false;
    Device dev;
    explicit Fake(uint64_t n):dev{this,n,512,true,rd,wr,fl}{}
    static bool rd(void*c,uint64_t s,uint32_t n,void*b){auto&f=*(Fake*)c;
        if(f.read_fail||!range(s,n,f.dev.sectors))return false;
        for(unsigned i=0;i<n;++i){auto it=f.data.find(s+i);if(it==f.data.end())std::memset((uint8_t*)b+512*i,0,512);else std::memcpy((uint8_t*)b+512*i,it->second.data(),512);}return true;}
    static bool wr(void*c,uint64_t s,uint32_t n,const void*b){auto&f=*(Fake*)c;
        assert(range(s,n,f.dev.sectors));assert(s>=f.low&&n<=f.high-s);
        if(f.calls++>=f.fail_after)return false;
        f.sectors+=n;for(unsigned i=0;i<n;++i)std::memcpy(f.data[s+i].data(),(const uint8_t*)b+512*i,512);return true;}
    static bool fl(void*c){return !((Fake*)c)->flush_fail;}
};
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(0)

struct MockBlockDriver {
    std::map<uint64_t, std::vector<uint8_t>> storage;
    std::vector<uint32_t> write_chunks;
    std::vector<uint32_t> read_chunks;
    uint32_t sector_bytes = 512;
    uint64_t total_sectors = 1000;
    BlockError inject_read_error = BlockError::Ok;
    BlockError inject_write_error = BlockError::Ok;
    BlockError inject_flush_error = BlockError::Ok;
    BlockError inject_reval_error = BlockError::Ok;
    bool flushed = false;
    Device bdev;

    MockBlockDriver(uint64_t sectors_count, uint32_t sec_size = 512, uint32_t max_transfer = 0)
        : sector_bytes(sec_size), total_sectors(sectors_count) {
        bdev.driver_data = this;
        bdev.ops = &mock_ops;
        bdev.total_sectors = sectors_count;
        bdev.logical_sector_size = sec_size;
        bdev.physical_sector_size = sec_size;
        bdev.max_transfer_sectors = max_transfer;
        bdev.read_only = false;
        bdev.removable = false;
        bdev.device_type = BlockDeviceType::AHCI;
        std::strncpy(bdev.model, "MOCK_DISK_MODEL", sizeof(bdev.model) - 1);
        std::strncpy(bdev.serial, "MOCK_DISK_SERIAL", sizeof(bdev.serial) - 1);
        bdev.write_count = 0;
    }

    static BlockError do_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buf) {
        auto* self = static_cast<MockBlockDriver*>(dev->driver_data);
        if (self->inject_read_error != BlockError::Ok) {
            return self->inject_read_error;
        }
        self->read_chunks.push_back(count);
        uint8_t* dst = static_cast<uint8_t*>(buf);
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t cur_lba = lba + i;
            auto it = self->storage.find(cur_lba);
            if (it != self->storage.end()) {
                std::memcpy(dst + i * self->sector_bytes, it->second.data(), self->sector_bytes);
            } else {
                std::memset(dst + i * self->sector_bytes, 0, self->sector_bytes);
            }
        }
        return BlockError::Ok;
    }

    static BlockError do_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf) {
        auto* self = static_cast<MockBlockDriver*>(dev->driver_data);
        if (self->inject_write_error != BlockError::Ok) {
            return self->inject_write_error;
        }
        self->write_chunks.push_back(count);
        const uint8_t* src = static_cast<const uint8_t*>(buf);
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t cur_lba = lba + i;
            std::vector<uint8_t> block(src + i * self->sector_bytes, src + (i + 1) * self->sector_bytes);
            self->storage[cur_lba] = block;
        }
        return BlockError::Ok;
    }

    static BlockError do_flush(BlockDevice* dev) {
        auto* self = static_cast<MockBlockDriver*>(dev->driver_data);
        if (self->inject_flush_error != BlockError::Ok) {
            return self->inject_flush_error;
        }
        self->flushed = true;
        return BlockError::Ok;
    }

    static BlockError do_revalidate(BlockDevice* dev) {
        auto* self = static_cast<MockBlockDriver*>(dev->driver_data);
        return self->inject_reval_error;
    }

    static const BlockOperations mock_ops;
};

const BlockOperations MockBlockDriver::mock_ops = {
    MockBlockDriver::do_read,
    MockBlockDriver::do_write,
    MockBlockDriver::do_flush,
    MockBlockDriver::do_revalidate
};

static void test_block_device_abstraction() {
    // Test block_range_valid and overflow safety
    CHECK(block_range_valid(0, 100, 100));
    CHECK(!block_range_valid(0, 101, 100));
    CHECK(block_range_valid(99, 1, 100));
    CHECK(!block_range_valid(100, 1, 100));
    CHECK(!block_range_valid(0, 0, 100));
    CHECK(!block_range_valid(UINT64_MAX, 1, 100));
    CHECK(!block_range_valid(UINT64_MAX - 5, 10, 100));
    CHECK(!block_range_valid(50, UINT32_MAX, 100));

    // Test BlockError helpers
    CHECK(block_sukces(BlockError::Ok));
    CHECK(!block_sukces(BlockError::IOError));
    CHECK(!block_sukces(BlockError::InvalidArgument));
    CHECK(std::strcmp(block_error_str(BlockError::Ok), "OK") == 0);
    CHECK(std::strcmp(block_error_str(BlockError::Timeout), "Przekroczenie limitu czasu (timeout)") == 0);

    // Mock device with 1000 sectors of 512 bytes
    MockBlockDriver mock(1000, 512, 0);
    std::vector<uint8_t> wbuf(512 * 4, 0x42);
    std::vector<uint8_t> rbuf(512 * 4, 0x00);

    // 1. Valid write and read
    CHECK(mock.bdev.write(10, 4, wbuf.data()) == BlockError::Ok);
    CHECK(mock.bdev.write_count == 4);
    CHECK(mock.bdev.read(10, 4, rbuf.data()) == BlockError::Ok);
    CHECK(std::memcmp(wbuf.data(), rbuf.data(), 512 * 4) == 0);

    // 2. Read unwritten area yields zeroes
    std::vector<uint8_t> zero_buf(512, 0xFF);
    CHECK(mock.bdev.read(20, 1, zero_buf.data()) == BlockError::Ok);
    for (uint8_t b : zero_buf) CHECK(b == 0);

    // 3. Boundary tests
    // Last valid sector
    CHECK(mock.bdev.write(999, 1, wbuf.data()) == BlockError::Ok);
    CHECK(mock.bdev.read(999, 1, rbuf.data()) == BlockError::Ok);
    // One past the end
    CHECK(mock.bdev.write(1000, 1, wbuf.data()) == BlockError::OutOfRange);
    CHECK(mock.bdev.read(1000, 1, rbuf.data()) == BlockError::OutOfRange);
    // Spanning past the end
    CHECK(mock.bdev.write(998, 3, wbuf.data()) == BlockError::OutOfRange);
    CHECK(mock.bdev.read(998, 3, rbuf.data()) == BlockError::OutOfRange);

    // 4. Overflow tests
    CHECK(mock.bdev.write(UINT64_MAX, 1, wbuf.data()) == BlockError::OutOfRange);
    CHECK(mock.bdev.read(UINT64_MAX, 1, rbuf.data()) == BlockError::OutOfRange);
    CHECK(mock.bdev.write(UINT64_MAX - 2, 5, wbuf.data()) == BlockError::OutOfRange);
    CHECK(mock.bdev.read(UINT64_MAX - 2, 5, rbuf.data()) == BlockError::OutOfRange);

    // 5. Zero-length and null pointer tests
    CHECK(mock.bdev.write(0, 0, wbuf.data()) == BlockError::InvalidArgument);
    CHECK(mock.bdev.read(0, 0, rbuf.data()) == BlockError::InvalidArgument);
    CHECK(mock.bdev.write(0, 1, nullptr) == BlockError::InvalidArgument);
    CHECK(mock.bdev.read(0, 1, nullptr) == BlockError::InvalidArgument);

    // 6. Read-only handling
    mock.bdev.read_only = true;
    CHECK(mock.bdev.write(10, 1, wbuf.data()) == BlockError::Unsupported);
    CHECK(mock.bdev.read(10, 1, rbuf.data()) == BlockError::Ok);
    mock.bdev.read_only = false;

    // 7. Error propagation
    mock.inject_read_error = BlockError::IOError;
    std::memset(rbuf.data(), 0xAA, 512);
    CHECK(mock.bdev.read(10, 1, rbuf.data()) == BlockError::IOError);
    CHECK(rbuf[0] == 0xAA); // Not overwritten
    mock.inject_read_error = BlockError::Ok;

    mock.inject_write_error = BlockError::Timeout;
    CHECK(mock.bdev.write(10, 1, wbuf.data()) == BlockError::Timeout);
    mock.inject_write_error = BlockError::Ok;

    mock.inject_flush_error = BlockError::DeviceUnavailable;
    CHECK(mock.bdev.flush() == BlockError::DeviceUnavailable);
    mock.inject_flush_error = BlockError::Ok;
    CHECK(mock.bdev.flush() == BlockError::Ok);
    CHECK(mock.flushed);

    // 8. Revalidation
    CHECK(mock.bdev.revalidate() == BlockError::Ok);
    mock.inject_reval_error = BlockError::DeviceUnavailable;
    CHECK(mock.bdev.revalidate() == BlockError::DeviceUnavailable);
    mock.inject_reval_error = BlockError::Ok;

    // 9. Chunking / max_transfer_sectors
    MockBlockDriver chunk_mock(100, 512, 4);
    std::vector<uint8_t> chunk_buf(512 * 10, 0x77);
    CHECK(chunk_mock.bdev.write(0, 10, chunk_buf.data()) == BlockError::Ok);
    CHECK(chunk_mock.write_chunks.size() == 3);
    CHECK(chunk_mock.write_chunks[0] == 4);
    CHECK(chunk_mock.write_chunks[1] == 4);
    CHECK(chunk_mock.write_chunks[2] == 2);
    std::vector<uint8_t> chunk_read(512 * 10, 0);
    CHECK(chunk_mock.bdev.read(0, 10, chunk_read.data()) == BlockError::Ok);
    CHECK(chunk_mock.read_chunks.size() == 3);
    CHECK(chunk_mock.read_chunks[0] == 4);
    CHECK(chunk_mock.read_chunks[1] == 4);
    CHECK(chunk_mock.read_chunks[2] == 2);
    CHECK(std::memcmp(chunk_buf.data(), chunk_read.data(), 512 * 10) == 0);

    // 10. Partition relative LBA translation and bounds
    Partition part{&mock.bdev, 200, 50};
    std::vector<uint8_t> p_wbuf(512 * 2, 0x33);
    std::vector<uint8_t> p_rbuf(512 * 2, 0x00);

    // Relative LBA 5 -> physical LBA 205
    CHECK(partition_write(part, 5, 2, p_wbuf.data()) == BlockError::Ok);
    CHECK(mock.storage.count(205) == 1);
    CHECK(mock.storage.count(206) == 1);
    CHECK(mock.storage.count(5) == 0); // Not written at relative address!
    CHECK(partition_read(part, 5, 2, p_rbuf.data()) == BlockError::Ok);
    CHECK(std::memcmp(p_wbuf.data(), p_rbuf.data(), 512 * 2) == 0);

    // Partition boundary checks
    CHECK(partition_write(part, 49, 1, p_wbuf.data()) == BlockError::Ok); // Last sector
    CHECK(partition_write(part, 50, 1, p_wbuf.data()) == BlockError::OutOfRange); // Past end
    CHECK(partition_write(part, 49, 2, p_wbuf.data()) == BlockError::OutOfRange); // Crosses end
    CHECK(partition_read(part, 49, 1, p_rbuf.data()) == BlockError::Ok);
    CHECK(partition_read(part, 50, 1, p_rbuf.data()) == BlockError::OutOfRange);

    // Partition invalid args
    CHECK(partition_write(part, 0, 0, p_wbuf.data()) == BlockError::InvalidArgument);
    CHECK(partition_read(part, 0, 0, p_rbuf.data()) == BlockError::InvalidArgument);
    CHECK(partition_write(part, 0, 1, nullptr) == BlockError::InvalidArgument);
    CHECK(partition_read(part, 0, 1, nullptr) == BlockError::InvalidArgument);

    // Partition overflow protection
    Partition p_ovf{&mock.bdev, UINT64_MAX - 10, 20};
    CHECK(partition_write(p_ovf, 0, 1, p_wbuf.data()) == BlockError::OutOfRange);
    CHECK(partition_read(p_ovf, 0, 1, p_rbuf.data()) == BlockError::OutOfRange);

    // Partition flush error propagation
    mock.flushed = false;
    CHECK(partition_flush(part) == BlockError::Ok);
    CHECK(mock.flushed);
    mock.inject_flush_error = BlockError::IOError;
    CHECK(partition_flush(part) == BlockError::IOError);
    mock.inject_flush_error = BlockError::Ok;

    // 11. Non-512-byte sectors (4096-byte 4Kn)
    MockBlockDriver mock_4kn(200, 4096, 2);
    std::vector<uint8_t> buf_4k(4096 * 3, 0x99);
    CHECK(mock_4kn.bdev.write(10, 3, buf_4k.data()) == BlockError::Ok);
    CHECK(mock_4kn.write_chunks.size() == 2);
    CHECK(mock_4kn.write_chunks[0] == 2);
    CHECK(mock_4kn.write_chunks[1] == 1);
    std::vector<uint8_t> read_4k(4096 * 3, 0x00);
    CHECK(mock_4kn.bdev.read(10, 3, read_4k.data()) == BlockError::Ok);
    CHECK(std::memcmp(buf_4k.data(), read_4k.data(), 4096 * 3) == 0);

    // 4Kn partition translation
    Partition part_4k{&mock_4kn.bdev, 20, 10};
    std::vector<uint8_t> p_buf_4k(4096, 0x88);
    CHECK(partition_write(part_4k, 2, 1, p_buf_4k.data()) == BlockError::Ok);
    CHECK(mock_4kn.storage.count(22) == 1); // 20 + 2
    CHECK(mock_4kn.storage[22].size() == 4096);
    std::vector<uint8_t> p_read_4k(4096, 0);
    CHECK(partition_read(part_4k, 2, 1, p_read_4k.data()) == BlockError::Ok);
    CHECK(std::memcmp(p_buf_4k.data(), p_read_4k.data(), 4096) == 0);

    // 12. Chunking overflow and validation audits
    // Invalid logical sector sizes
    mock.bdev.logical_sector_size = 256; // < 512
    CHECK(mock.bdev.read(0, 1, rbuf.data()) == BlockError::InvalidArgument);
    CHECK(mock.bdev.write(0, 1, wbuf.data()) == BlockError::InvalidArgument);
    mock.bdev.logical_sector_size = 513; // Not power of 2
    CHECK(mock.bdev.read(0, 1, rbuf.data()) == BlockError::InvalidArgument);
    mock.bdev.logical_sector_size = 131072; // > 65536
    CHECK(mock.bdev.read(0, 1, rbuf.data()) == BlockError::InvalidArgument);
    mock.bdev.logical_sector_size = 512; // Restore valid

    // Buffer address wrap-around check
    void* high_buf = reinterpret_cast<void*>(UINTPTR_MAX - 100);
    CHECK(mock.bdev.read(0, 1, high_buf) == BlockError::InvalidArgument);
    CHECK(mock.bdev.write(0, 1, high_buf) == BlockError::InvalidArgument);

    // Saturating write_count
    mock.bdev.write_count = UINT64_MAX - 1;
    CHECK(mock.bdev.write(0, 2, wbuf.data()) == BlockError::Ok);
    CHECK(mock.bdev.write_count == UINT64_MAX); // Saturated, no wrap to 0
}

int main(){
    CHECK(!instalator_potwierdzenie_poprawne("",16));
    CHECK(!instalator_potwierdzenie_poprawne("B",16));
    CHECK(!instalator_potwierdzenie_poprawne("BU",16));
    CHECK(!instalator_potwierdzenie_poprawne("BURSZT",16));
    CHECK(instalator_potwierdzenie_poprawne("BURSZTYN",16));
    CHECK(!instalator_potwierdzenie_poprawne("BURSZTY",16));
    CHECK(!instalator_potwierdzenie_poprawne("bursztyn",16));
    CHECK(!instalator_potwierdzenie_poprawne("BURSZTYNX",16));
    CHECK(!instalator_mozna_zainstalowac(true,true,false,true,false));
    CHECK(!instalator_mozna_zainstalowac(true,true,true,true,false));
    CHECK(!instalator_mozna_zainstalowac(true,true,false,true,true));
    CHECK(instalator_mozna_zainstalowac(true,true,true,true,true));
    CHECK(!instalator_mozna_zainstalowac(false,true,true,true,true));
    CHECK(!instalator_mozna_zainstalowac(true,false,true,true,true));
    CHECK(instalator_mozna_zainstalowac(true,true,true,false,false));
    CHECK(instalator_postep_nowy(0,0,false)==0);
    CHECK(instalator_postep_nowy(45,30,false)==45);
    CHECK(instalator_postep_nowy(99,150,false)==99);
    CHECK(instalator_postep_nowy(99,100,true)==100);
    CHECK(instalator_postep_nowy(63,100,false)==99);
    CHECK(instalator_etap_postepu_poprawny(POSTEP_GPT,POSTEP_ESP));
    CHECK(!instalator_etap_postepu_poprawny(POSTEP_TAPETY,POSTEP_SYSTEM));
    CHECK(instalator_podpostep(45,75,0,13)==45);
    CHECK(instalator_podpostep(45,75,7,13)>=45&&instalator_podpostep(45,75,7,13)<=75);
    CHECK(instalator_podpostep(75,92,12,18)>=75&&instalator_podpostep(75,92,12,18)<=92);
    CHECK(instalator_podpostep(75,92,1,0)==75);
    CHECK(std::strcmp(tapety_komunikat_braku_pakietu(true,true),
        "[TAPETA] Brak tapety.pak w trybie zainstalowanym; uzywam zasobow BSP2.")==0);
    CHECK(std::strstr(tapety_komunikat_braku_pakietu(false,true),"Ostrzezenie")!=nullptr);
    CHECK(crc32("123456789",9)==0xCBF43926);
    Fake disk(4ULL*1024*1024*1024/512);Layout l{};Table t{};uint8_t uuid[16]={1,2,3,4,5,6,7,0x40,0x80,10,11,12,13,14,15,16};
    CHECK(plan(&disk.dev,&l));CHECK(l.esp.start==2048&&l.esp.count==1048576&&l.bsp.start%2048==0&&l.bsp.count==BspMaxBytes/512);
    CHECK(!gpt_read(&disk.dev,&t));CHECK(disk.calls==0);CHECK(!source_iso(&disk.dev));
    Fake small(10000),large((6ULL*1024*1024*1024*1024)/512);Layout big{};
    CHECK(!plan(&small.dev,&big));CHECK(plan(&large.dev,&big));CHECK(big.unallocated>UINT32_MAX);
    CHECK(gpt_write(&large.dev,big,uuid));CHECK(gpt_read(&large.dev,&t));CHECK(large.data.count(large.dev.sectors-1)==1);
    disk.dev.sector_size=4096;CHECK(!plan(&disk.dev,&big));CHECK(!gpt_read(&disk.dev,&t));disk.dev.sector_size=512;
    CHECK(gpt_write(&disk.dev,l,uuid));CHECK(gpt_read(&disk.dev,&t));CHECK(t.count==2&&equal(t.entries[1].type,BspType,16));
    GptInstallState gpt_state{};CHECK(gpt_install_start(&disk.dev,l,uuid,&gpt_state));
    uint32_t gpt_poprzednio=0;
    while(!gpt_state.complete){
        const uint64_t calls_before=disk.calls;
        CHECK(gpt_install_step(&gpt_state));
        CHECK(disk.calls-calls_before<=1);
        CHECK(gpt_state.completed_units==gpt_poprzednio+1||gpt_state.complete);
        CHECK(gpt_state.completed_units<=gpt_state.total_units);
        gpt_poprzednio=gpt_state.completed_units;
    }
    CHECK(gpt_state.completed_units==gpt_state.total_units);CHECK(gpt_read(&disk.dev,&t));
    const auto original=disk.data;uint64_t before=disk.calls;
    disk.data[1][16]^=1;CHECK(!gpt_read(&disk.dev,&t));CHECK(disk.calls==before);disk.data=original;
    disk.data[2][40]^=1;CHECK(!gpt_read(&disk.dev,&t));disk.data=original;
    disk.data[disk.dev.sectors-1][16]^=1;CHECK(!gpt_read(&disk.dev,&t));disk.data=original;
    uint8_t buf[4096]{};
    CHECK(!write(l.bsp,l.bsp.count,1,buf));CHECK(!write(l.bsp,UINT64_MAX,1,buf));CHECK(!write(l.bsp,0,0,buf));CHECK(!write(l.bsp,0,1,nullptr));
    Partition overflow{&disk.dev,UINT64_MAX-5,16};CHECK(!write(overflow,0,1,buf));CHECK(disk.calls==before);
    Layout invalid=l;invalid.bsp.start=UINT64_MAX;CHECK(!gpt_write(&disk.dev,invalid,uuid));CHECK(disk.calls==before);
    BootFile f{buf,4096};Partition missing=l.esp;missing.count=8192;
    CHECK(!fat_install(missing,f,f,f));CHECK(!fat_install(l.esp,{},f,f));CHECK(disk.calls==before);
    disk.fail_after=disk.calls;CHECK(!fat_install(l.esp,f,f,f));disk.fail_after=UINT64_MAX;
    FatInstallState fat_state{};
    CHECK(fat_install_start(l.esp,f,f,f,&fat_state));CHECK(fat_state.total_units>100);
    uint32_t fat_poprzednio=0;
    while(!fat_state.complete){
        CHECK(fat_install_step(&fat_state));
        CHECK(fat_state.completed_units==fat_poprzednio+1);
        CHECK(fat_state.completed_units<=fat_state.total_units);
        fat_poprzednio=fat_state.completed_units;
    }
    CHECK(fat_state.completed_units==fat_state.total_units);
    CHECK(fat_install(l.esp,f,f,f));CHECK(disk.data[l.esp.start][82]=='F');
    CHECK(gpt_write(&disk.dev,l,uuid));CHECK(gpt_read(&disk.dev,&t)); // reinstall GPT
    // Exact production BSP2 formatting, with sentinels immediately outside partition.
    auto&left=disk.data[l.bsp.start-1];left.fill(0xA7);auto&right=disk.data[l.bsp.start+l.bsp.count];right.fill(0x5D);
    const auto outside=disk.data;
    disk.low=l.bsp.start;disk.high=l.bsp.start+l.bsp.count;
    std::vector<uint8_t> ram(16*1024*1024);psf_ustaw_partycje(nullptr);before=disk.calls;
    inicjalizuj_psf(ram.data(),ram.size());CHECK(disk.calls==before);CHECK(psf_czy_gotowy());
    CHECK(utworz_plik("/ram"));std::vector<char> bytes(2*1024*1024,'R');
    CHECK(zapisz_do_pliku("/ram",bytes.data(),bytes.size()));char first=0;uint32_t got=0;
    CHECK(czytaj_fragment_pliku("/ram",0,&first,1,&got)&&first=='R');CHECK(disk.calls==before);
    authorized=false;CHECK(!psf_formatuj_instalacje(l.bsp));authorized=true;
    disk.fail_after=disk.calls;CHECK(!psf_formatuj_instalacje(l.bsp));disk.fail_after=UINT64_MAX;
    CHECK(psf_formatuj_instalacje(l.bsp));CHECK(psf_znacznik_instalacji(uuid,false));CHECK(!complete(l.bsp,uuid));
    CHECK(utworz_plik("/system"));CHECK(zapisz_do_pliku("/system",bytes.data(),bytes.size()));CHECK(psf_weryfikuj_dysk());
    CHECK(czytaj_fragment_pliku("/system",0,&first,1,&got)&&first=='R');
    CHECK(psf_znacznik_instalacji(uuid,true));CHECK(complete(l.bsp,uuid));
    CHECK(psf_weryfikuj_dysk());CHECK(complete(l.bsp,uuid));
    for(const auto&item:disk.data)if(item.first<l.bsp.start||item.first>=l.bsp.start+l.bsp.count){CHECK(outside.count(item.first));CHECK(outside.at(item.first)==item.second);}
    CHECK(left==outside.at(l.bsp.start-1));CHECK(right==outside.at(l.bsp.start+l.bsp.count));
    disk.data[l.bsp.start+l.bsp.count-1][100]^=1;CHECK(!complete(l.bsp,uuid));CHECK(psf_znacznik_instalacji(uuid,true));
    disk.flush_fail=true;CHECK(!psf_flush_cache());disk.flush_fail=false;
    uint64_t format_done=0,format_total=0,format_previous=0;bool format_complete=false;
    CHECK(psf_formatuj_instalacje_start(l.bsp,&format_total));CHECK(format_total>0);
    while(!format_complete){
        const uint64_t calls_before=disk.calls;
        CHECK(psf_formatuj_instalacje_step(&format_done,&format_complete));
        CHECK(disk.calls-calls_before<=1);
        CHECK(format_done==format_previous+1);
        CHECK(format_done<=format_total);
        format_previous=format_done;
    }
    CHECK(format_done==format_total);CHECK(psf_czy_gotowy());
    CHECK(rozmiar_pliku("/system")==0); // no stale metadata after reinstall
    CHECK(utworz_plik("/bad"));disk.fail_after=disk.calls;CHECK(!zapisz_do_pliku("/bad",bytes.data(),bytes.size()));disk.fail_after=UINT64_MAX;

    // Testy A/B: uklad ESP, jadro B, grubenv i sektory stanu rozruchu
    disk.low=0; disk.high=UINT64_MAX;
    EspLayout el{};
    CHECK(esp_odczytaj_uklad(l.esp, &el));
    CHECK(el.jadro_a_lba > l.esp.start && el.jadro_b_lba > el.jadro_a_lba);
    CHECK(el.grubenv_lba > el.jadro_b_lba);

    char potw[16]{}, oczek[16]{}; uint32_t proby = 99;
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "BRAK") == 0 && proby == 0);

    CHECK(esp_zapisz_grubenv(l.esp, "A", "B", 1));
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "B") == 0 && proby == 1);

    std::vector<uint8_t> new_kernel(4096, 0x5A);
    CHECK(esp_zapisz_jadro_slotu(l.esp, 1, new_kernel.data(), uint32_t(new_kernel.size())));
    std::vector<uint8_t> read_kernel(4096, 0);
    uint32_t odczytano_k = 0;
    CHECK(esp_odczytaj_jadro_slotu(l.esp, 1, read_kernel.data(), uint32_t(read_kernel.size()), &odczytano_k));
    CHECK(odczytano_k == 4096 && read_kernel == new_kernel);

    bursztyn_rozruch_stan rstate{};
    rozruch_stan_poczatkowy(&rstate, 0, 1, 0);
    CHECK(rozruch_zapisz_sektory(&disk.dev, &rstate));
    bursztyn_rozruch_stan rread{};
    CHECK(rozruch_odczytaj_sektory(&disk.dev, &rread));
    CHECK(rread.slot_potwierdzony == BURSZTYN_SLOT_A && rread.generacja == 1);

    // Weryfikacja zarezerwowanych sektorow stanu rozruchu i ukladu partycji
    CHECK(rozruch_weryfikuj_uklad_dysku(&disk.dev));
    CHECK(!rozruch_weryfikuj_uklad_dysku(nullptr));
    CHECK(!rozruch_weryfikuj_uklad_dysku(&small.dev));

    test_block_device_abstraction();

    std::printf("Storage/BSP2 host: %u PASS, 0 FAIL (includes outside-partition sentinel, faults, >2 TiB GPT, A/B ESP, BlockDevice abstraction)\n",checks);
}

