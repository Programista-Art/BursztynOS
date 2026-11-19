/*
 * Bursztyn OS - testy hosta telemetrii urzadzen (etap 1: dyski, siec, grafika; BWS 105).
 * Kompilacja: make -j1 test-telemetria-urzadzen-host (ASan + UBSan).
 *
 * Testuje prawdziwy kod: BlockDevice::read/write/flush (block_device.h), rejestr NetworkDevice
 * (network_device.cpp), budowe odpowiedzi (urzadzenia_migawka.h) i model Menedzera zadan (urzadzenia.h).
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "block_device.h"
#include "network_device.h"
#include "urzadzenia_migawka.h"
#define BURSZTYN_ROZLICZANIE_IO_HOST 1
#include "rozliczanie_io.h"
#include "../programy/menedzer_zadan/urzadzenia.h"

static int g_bledy = 0, g_ok = 0;
#define CHECK(x) do { if (x) ++g_ok; else { ++g_bledy; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

/* ---------------------------------------------------------------- stos sieciowy (mock) */
static uint32_t g_rx_stosu = 0;
void obsluz_pakiet_sieciowy(uint8_t*, uint16_t) { ++g_rx_stosu; }

/* ---------------------------------------------------------------- dysk (mock sterownika) */
struct MockDysk {
    uint32_t wywolania = 0, blad_na = 0xFFFFFFFF;   // numer wywolania (od 1), ktore zwraca blad
    BlockError blad = BlockError::IOError;
};
static BlockError mock_rw(BlockDevice* d, uint64_t, uint32_t, const void*) {
    auto* m = static_cast<MockDysk*>(d->driver_data);
    ++m->wywolania;
    return m->wywolania == m->blad_na ? m->blad : BlockError::Ok;
}
static BlockError mock_read(BlockDevice* d, uint64_t l, uint32_t c, void* b) { return mock_rw(d, l, c, b); }
static BlockError mock_write(BlockDevice* d, uint64_t l, uint32_t c, const void* b) { return mock_rw(d, l, c, b); }
static BlockError mock_flush(BlockDevice* d) { return mock_rw(d, 0, 0, nullptr); }
static const BlockOperations g_mock_ops = {mock_read, mock_write, mock_flush, nullptr};

static BlockDevice nowy_dysk(MockDysk* m, BlockDeviceType typ = BlockDeviceType::VirtIO) {
    BlockDevice d;
    d.driver_data = m; d.ops = &g_mock_ops; d.total_sectors = 1000; d.logical_sector_size = 512;
    d.max_transfer_sectors = 8; d.device_type = typ;
    return d;
}

/* Filtr jak w blokowy.cpp: osobne BlockDevice z licz_io=false wolajace urzadzenie sprzetowe. */
static BlockError filtr_read(BlockDevice* d, uint64_t l, uint32_t c, void* b) { return static_cast<BlockDevice*>(d->driver_data)->read(l, c, b); }
static BlockError filtr_write(BlockDevice* d, uint64_t l, uint32_t c, const void* b) { return static_cast<BlockDevice*>(d->driver_data)->write(l, c, b); }
static const BlockOperations g_filtr_ops = {filtr_read, filtr_write, nullptr, nullptr};

static uint64_t g_zegar = 0;
static uint64_t zegar_testowy() { g_zegar += 1000; return g_zegar; }

static void testy_dysku() {
    static uint8_t buf[64 * 512];
    g_block_io_clock_ns = nullptr;
    {   // pojedyncze polecenie, potem podzial na porcje (20 sektorow / max 8 = 3 polecenia)
        MockDysk m; BlockDevice d = nowy_dysk(&m);
        CHECK(d.read(0, 4, buf) == BlockError::Ok);
        CHECK(d.io.read_ops == 1 && d.io.read_bytes == 2048 && d.io.read_errors == 0);
        CHECK(d.read(0, 20, buf) == BlockError::Ok);
        CHECK(m.wywolania == 4 && d.io.read_ops == 4 && d.io.read_bytes == 2048 + 20 * 512);
        CHECK(d.io.timed_read_ops == 0 && d.io.read_ns == 0);   // brak zegara = brak pomiaru czasu
    }
    {   // blad w drugiej porcji: pierwsza porcja udana (liczona), druga = blad, trzecia nie wyslana
        MockDysk m; m.blad_na = 2; BlockDevice d = nowy_dysk(&m);
        CHECK(d.read(0, 20, buf) == BlockError::IOError);
        CHECK(m.wywolania == 2 && d.io.read_ops == 1 && d.io.read_bytes == 8 * 512 && d.io.read_errors == 1);
    }
    {   // zapis + czesciowy zapis + timeout
        MockDysk m; m.blad_na = 3; m.blad = BlockError::Timeout; BlockDevice d = nowy_dysk(&m);
        CHECK(d.write(0, 8, buf) == BlockError::Ok);
        CHECK(d.write(0, 16, buf) == BlockError::Timeout);
        CHECK(d.io.write_ops == 2 && d.io.write_bytes == 16 * 512 && d.io.write_errors == 1 && d.io.read_ops == 0);
    }
    {   // odrzucenie przed sterownikiem: nie ma polecenia, nic nie jest liczone
        MockDysk m; BlockDevice d = nowy_dysk(&m);
        CHECK(d.read(990, 20, buf) == BlockError::OutOfRange);
        CHECK(d.read(0, 0, buf) == BlockError::InvalidArgument);
        CHECK(d.read(0, 1, nullptr) == BlockError::InvalidArgument);
        d.read_only = true; d.writable = false;
        CHECK(d.write(0, 1, buf) == BlockError::Unsupported);
        CHECK(m.wywolania == 0 && d.io.read_ops == 0 && d.io.read_errors == 0 && d.io.write_ops == 0 && d.io.write_errors == 0);
    }
    {   // flush
        MockDysk m; m.blad_na = 2; BlockDevice d = nowy_dysk(&m);
        CHECK(d.flush() == BlockError::Ok && d.flush() == BlockError::IOError);
        CHECK(d.io.flushes == 1 && d.io.flush_errors == 1);
    }
    {   // brak podwojnego liczenia: filtr (licz_io=false) -> sprzet
        MockDysk m; BlockDevice hw = nowy_dysk(&m);
        BlockDevice f; f.driver_data = &hw; f.ops = &g_filtr_ops; f.total_sectors = hw.total_sectors; f.licz_io = false;
        CHECK(f.read(0, 20, buf) == BlockError::Ok && f.write(0, 4, buf) == BlockError::Ok);
        CHECK(hw.io.read_ops == 3 && hw.io.read_bytes == 20 * 512 && hw.io.write_ops == 1 && hw.io.write_bytes == 2048);
        CHECK(f.io.read_ops == 0 && f.io.read_bytes == 0 && f.io.write_ops == 0);
    }
    {   // zegar: czas od wyslania do zakonczenia kazdego polecenia, takze nieudanego
        g_zegar = 0; g_block_io_clock_ns = zegar_testowy;
        MockDysk m; m.blad_na = 3; BlockDevice d = nowy_dysk(&m);
        CHECK(d.read(0, 16, buf) == BlockError::Ok);            // 2 polecenia po 1000 ns
        CHECK(d.write(0, 1, buf) == BlockError::IOError);       // 1 polecenie (blad) - czas tez
        CHECK(d.io.timed_read_ops == 2 && d.io.read_ns == 2000 && d.io.timed_write_ops == 1 && d.io.write_ns == 1000);
        g_block_io_clock_ns = nullptr;
    }
    {   // nasycenie zamiast zawiniecia
        MockDysk m; BlockDevice d = nowy_dysk(&m);
        d.io.read_bytes = UINT64_MAX - 100; d.io.read_ops = UINT64_MAX;
        CHECK(d.read(0, 1, buf) == BlockError::Ok);
        CHECK(d.io.read_bytes == UINT64_MAX && d.io.read_ops == UINT64_MAX);
    }
    {   // legacy (bez ops)
        BlockDevice d(nullptr, 100, 512, true,
                      [](void*, uint64_t, uint32_t, void*) { return true; },
                      [](void*, uint64_t, uint32_t, const void*) { return false; }, nullptr);
        CHECK(d.read(0, 2, buf) == BlockError::Ok && d.write(0, 2, buf) == BlockError::IOError);
        CHECK(d.io.read_ops == 1 && d.io.read_bytes == 1024 && d.io.write_errors == 1 && d.io.write_ops == 0);
    }
}

static void testy_migawki_dysku() {
    MockDysk m; BlockDevice hw = nowy_dysk(&m, BlockDeviceType::AHCI);
    const char model[] = "QEMU HARDDISK                           ";
    std::memcpy(hw.model, model, sizeof(hw.model) - 1); hw.model[sizeof(hw.model) - 1] = 0;
    static uint8_t buf[4096];
    CHECK(hw.read(0, 2, buf) == BlockError::Ok);
    BlockDevice f; f.total_sectors = 1000; f.licz_io = false; f.device_type = BlockDeviceType::AHCI;
    BwsDyskInfoV1 o{};
    UrzadzeniaMigawka::dysk(o, 3, f, &hw, true);
    CHECK(o.id == 3 && o.typ == BWS_DYSK_AHCI && o.sektory == 1000 && o.rozmiar_sektora == 512);
    CHECK((o.flagi & BWS_DYSK_ONLINE) && (o.flagi & BWS_DYSK_SYSTEMOWY) && (o.flagi & BWS_DYSK_MODEL_URZADZENIA));
    CHECK(std::strcmp(o.model, "QEMU HARDDISK") == 0);       // dopelnienie spacjami obciete
    CHECK(o.bajty_odczytane == 1024 && o.operacje_odczytu == 1);
    // VirtIO: napis sterownika nie jest modelem urzadzenia
    MockDysk m2; BlockDevice v = nowy_dysk(&m2, BlockDeviceType::VirtIO);
    std::strcpy(v.model, "VirtIO Block Disk");
    UrzadzeniaMigawka::dysk(o, 0, v, &v, false);
    CHECK(o.typ == BWS_DYSK_VIRTIO && !(o.flagi & BWS_DYSK_MODEL_URZADZENIA) && o.model[0] == 0 && !(o.flagi & BWS_DYSK_SYSTEMOWY));
    // sterownik po resecie (pojemnosc 0) = offline; brak sterownika = brak licznikow
    v.total_sectors = 0;
    UrzadzeniaMigawka::dysk(o, 0, v, &v, false);
    CHECK(!(o.flagi & BWS_DYSK_ONLINE));
    UrzadzeniaMigawka::dysk(o, 1, f, nullptr, false);
    CHECK(!(o.flagi & BWS_DYSK_ONLINE) && o.bajty_odczytane == 0);
    // znaki sterujace w modelu nie przechodza do ABI
    hw.model[2] = '\n';
    UrzadzeniaMigawka::dysk(o, 3, f, &hw, false);
    CHECK(o.model[2] == '?');
}

/* ---------------------------------------------------------------- siec */
struct MockKarta { NetError wynik = NetError::Ok; NetLinkState lacze = NetLinkState::Up; };
static NetError mock_tx(NetworkDevice* d, const void*, size_t len) {
    auto* k = static_cast<MockKarta*>(d->driver_data);
    // jak e1000/virtio-net: liczone po zakonczeniu, w sterowniku (jedno miejsce)
    if (k->wynik == NetError::Ok) { d->stats.tx_packets++; d->stats.tx_bytes += len; }
    else { d->stats.tx_errors++; if (k->wynik == NetError::QueueFull) d->stats.tx_dropped++; }
    return k->wynik;
}
static NetLinkState mock_lacze(const NetworkDevice* d) { return static_cast<const MockKarta*>(d->driver_data)->lacze; }
static const NetDeviceOps g_net_ops = {mock_tx, nullptr, mock_lacze, nullptr};

static void przygotuj_karte(NetworkDevice& d, MockKarta& k, const char* nazwa, uint8_t ostatni, NetDeviceType typ) {
    d = NetworkDevice{};
    d.ops = &g_net_ops; d.driver_data = &k; d.device_type = typ;
    std::strncpy(d.name, nazwa, sizeof(d.name) - 1);
    const uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, ostatni};
    std::memcpy(d.mac, mac, 6);
}

static void testy_sieci() {
    CHECK(net_get_device_count() == 0 && net_get_primary_device() == nullptr);   // zero interfejsow
    static NetworkDevice a, b, c; static MockKarta ka, kb, kc;
    przygotuj_karte(a, ka, "eth0", 1, NetDeviceType::E1000);
    przygotuj_karte(b, kb, "virtio-net0", 2, NetDeviceType::VirtIO);
    przygotuj_karte(c, kc, "virtio-net1", 3, NetDeviceType::VirtIO);
    CHECK(net_register_device(&a));
    CHECK(net_get_device_count() == 1 && a.registration_id == 1);
    CHECK(net_register_device(&b) && net_register_device(&c));
    CHECK(net_get_device_count() == 3 && b.registration_id == 2 && c.registration_id == 3);
    CHECK(!net_register_device(&a) && a.registration_id == 1);   // duplikat odrzucony, id bez zmian

    // RX: liczone raz (warstwa NetworkDevice), ramki bledne jako bledy+odrzucone, bez przekazania do stosu
    uint8_t ramka[1518] = {};
    const uint32_t stos0 = g_rx_stosu;
    net_receive_frame(&b, ramka, 60);
    net_receive_frame(&b, ramka, 1514);
    net_receive_frame(&b, ramka, 10);
    net_receive_frame(&b, nullptr, 60);
    CHECK(b.stats.rx_packets == 2 && b.stats.rx_bytes == 1574 && b.stats.rx_errors == 2 && b.stats.rx_dropped == 2);
    CHECK(g_rx_stosu - stos0 == 2);
    // TX: sukces, pelna kolejka, ramka za duza (odrzucona przed sterownikiem)
    CHECK(a.transmit(ramka, 60) == NetError::Ok);
    ka.wynik = NetError::QueueFull;
    CHECK(a.transmit(ramka, 60) == NetError::QueueFull);
    CHECK(a.transmit(ramka, 1600) == NetError::BufferTooLarge);
    CHECK(a.stats.tx_packets == 1 && a.stats.tx_bytes == 60 && a.stats.tx_errors == 1 && a.stats.tx_dropped == 1);

    // migawka interfejsu
    BwsSiecInfoV1 o{};
    UrzadzeniaMigawka::siec(o, b, false, b.get_link_state());
    CHECK(o.id == 2 && o.typ == BWS_SIEC_VIRTIO && std::strcmp(o.nazwa, "virtio-net0") == 0 && o.mtu == 1500);
    CHECK((o.flagi & BWS_SIEC_AKTYWNA) && (o.flagi & BWS_SIEC_LACZE_ZNANE) && (o.flagi & BWS_SIEC_LACZE) && (o.flagi & BWS_SIEC_MAC));
    CHECK(!(o.flagi & BWS_SIEC_GLOWNA) && !(o.flagi & BWS_SIEC_PREDKOSC) && o.predkosc_mbps == 0);   // predkosci nie zmyslamy
    CHECK(o.rx_pakiety == 2 && o.rx_bajty == 1574 && o.rx_bledy == 2 && o.mac[5] == 2);
    kb.lacze = NetLinkState::Down;
    UrzadzeniaMigawka::siec(o, b, true, b.get_link_state());
    CHECK((o.flagi & BWS_SIEC_LACZE_ZNANE) && !(o.flagi & BWS_SIEC_LACZE) && (o.flagi & BWS_SIEC_GLOWNA));
    UrzadzeniaMigawka::siec(o, b, false, NetLinkState::Unknown);
    CHECK(!(o.flagi & BWS_SIEC_LACZE_ZNANE));

    // znikniecie i ponowna rejestracja: indeksy sie przesuwaja, id nie; nowa rejestracja = nowe id
    net_unregister_device(&a);
    CHECK(net_get_device_count() == 2 && net_get_device_by_index(0) == &b && b.registration_id == 2 && !a.active);
    CHECK(net_get_primary_device() == &b);
    CHECK(net_register_device(&a) && a.registration_id == 4);
    net_unregister_device(&a); net_unregister_device(&b); net_unregister_device(&c);
    CHECK(net_get_device_count() == 0);
}

/* ---------------------------------------------------------------- grafika */
static void testy_grafiki() {
    UrzadzeniaMigawka::ZrodloGrafiki z{};
    z.backend = 2; z.szerokosc = 1280; z.wysokosc = 720; z.bpp = 32; z.pitch = 5120; z.zegar = true;
    z.klatki = 100; z.czas_skladania_ns = 5000; z.kopie_bajty = 4096;
    z.virtio = true; z.polecenia_gpu = 7; z.transfery = 3; z.pci_vendor = 0x1AF4;   // sterownik obecny, ale nieaktywny
    BwsGrafikaInfoV1 o{};
    UrzadzeniaMigawka::grafika(o, z);
    CHECK(o.backend == 2 && o.szerokosc == 1280 && o.klatki == 100 && o.czas_skladania_ns == 5000 && o.kopie_bajty == 4096);
    CHECK(!(o.flagi & BWS_GRAF_LICZNIKI_VIRTIO) && !(o.flagi & BWS_GRAF_PCI) && o.polecenia_gpu == 0 && o.transfery == 0);
    z.backend = 4; z.pci_device = 0x1050; z.blad_trwaly = true; z.kursor_sprzetowy = true;
    UrzadzeniaMigawka::grafika(o, z);
    CHECK((o.flagi & BWS_GRAF_LICZNIKI_VIRTIO) && (o.flagi & BWS_GRAF_PCI) && o.pci_vendor == 0x1AF4 && o.pci_device == 0x1050);
    CHECK((o.flagi & BWS_GRAF_BLAD_TRWALY) && (o.flagi & BWS_GRAF_KURSOR_SPRZETOWY) && o.polecenia_gpu == 7);
    z.zegar = false;   // bez HPET czasy nie maja znaczenia
    UrzadzeniaMigawka::grafika(o, z);
    CHECK(!(o.flagi & BWS_GRAF_ZEGAR) && o.czas_skladania_ns == 0 && o.czas_oczekiwania_gpu_ns == 0);
}

/* ---------------------------------------------------------------- brak wskaznikow jadra w odpowiedzi */
static void testy_bez_wskaznikow() {
    MockDysk m; BlockDevice hw = nowy_dysk(&m, BlockDeviceType::AHCI);
    static NetworkDevice n; static MockKarta k;
    przygotuj_karte(n, k, "eth0", 9, NetDeviceType::E1000);
    BwsUrzadzeniaOdpowiedz odp{};
    UrzadzeniaMigawka::dysk(odp.dyski[0], 0, hw, &hw, false);
    UrzadzeniaMigawka::siec(odp.sieci[0], n, true, NetLinkState::Up);
    const uintptr_t zakazane[] = {reinterpret_cast<uintptr_t>(&hw), reinterpret_cast<uintptr_t>(&m),
                                  reinterpret_cast<uintptr_t>(&g_mock_ops), reinterpret_cast<uintptr_t>(&n),
                                  reinterpret_cast<uintptr_t>(&k), reinterpret_cast<uintptr_t>(&g_net_ops)};
    const auto* bajty = reinterpret_cast<const uint8_t*>(&odp);
    bool znaleziono = false;
    for (size_t i = 0; i + sizeof(uintptr_t) <= sizeof(odp); ++i) {
        uintptr_t v; std::memcpy(&v, bajty + i, sizeof(v));
        for (uintptr_t z : zakazane) if (v == z) znaleziono = true;
    }
    CHECK(!znaleziono);
    CHECK(UrzadzeniaTelemetria::bufor_poprawny(0x1000, sizeof(odp), sizeof(odp)));
    CHECK(!UrzadzeniaTelemetria::bufor_poprawny(0, sizeof(odp), sizeof(odp)));
    CHECK(!UrzadzeniaTelemetria::bufor_poprawny(0x1000, sizeof(odp) - 1, sizeof(odp)));
    char nazwa[4];
    UrzadzeniaTelemetria::kopiuj_nazwe(nazwa, sizeof(nazwa), "abcdef", 6);
    CHECK(std::strcmp(nazwa, "abc") == 0);
    UrzadzeniaTelemetria::kopiuj_nazwe(nazwa, sizeof(nazwa), nullptr, 0);
    CHECK(nazwa[0] == 0);
}

/* ---------------------------------------------------------------- model Menedzera zadan */
using namespace MenedzerZadan;

static BwsUrzadzeniaOdpowiedz odpowiedz(uint64_t czas_ns, uint32_t dyski, uint32_t sieci) {
    BwsUrzadzeniaOdpowiedz o{};
    auto& n = o.naglowek;
    n.wersja = BWS_URZADZENIA_V1_ABI; n.rozmiar_naglowka = sizeof(n); n.rozmiar_dysku = sizeof(BwsDyskInfoV1);
    n.rozmiar_sieci = sizeof(BwsSiecInfoV1); n.rozmiar_grafiki = sizeof(BwsGrafikaInfoV1);
    n.liczba_dyskow = dyski; n.liczba_sieci = sieci; n.liczba_grafiki = 1; n.psf_dysk = BWS_URZ_BRAK;
    n.flagi = BWS_URZ_ZEGAR | BWS_URZ_PSF_W_RAM; n.czas_ns = czas_ns;
    for (uint32_t i = 0; i < dyski; ++i) { o.dyski[i].id = i; o.dyski[i].typ = BWS_DYSK_VIRTIO; o.dyski[i].flagi = BWS_DYSK_ONLINE; }
    for (uint32_t i = 0; i < sieci; ++i) { o.sieci[i].id = 10 + i; o.sieci[i].typ = BWS_SIEC_E1000; }
    o.grafika[0].backend = 4; o.grafika[0].flagi = BWS_GRAF_ZEGAR | BWS_GRAF_LICZNIKI_VIRTIO;
    return o;
}

static void testy_modelu() {
    uint64_t v = 0;
    CHECK(na_sekunde(2048, 1024, 1000000000ULL, &v) && v == 1024);
    CHECK(na_sekunde(3000, 1000, 500000000ULL, &v) && v == 4000);
    CHECK(!na_sekunde(5, 10, 1000000000ULL, &v));               // spadek licznika = reset
    CHECK(!na_sekunde(10, 5, 0, &v));                           // zerowy przedzial
    CHECK(na_sekunde(UINT64_MAX, 0, 1000000000ULL, &v) && v == UINT64_MAX);   // nasycenie bez UB
    CHECK(na_sekunde(UINT64_MAX / 2, 0, 3ULL * 60 * 1000000000ULL, &v) && v > 0);
    char t[32];
    format_tempa(0, t, sizeof(t)); CHECK(std::strcmp(t, "0 B/s") == 0);
    format_tempa(1536, t, sizeof(t)); CHECK(std::strcmp(t, "1,5 KiB/s") == 0);
    format_tempa(5 * 1024 * 1024, t, sizeof(t)); CHECK(std::strcmp(t, "5,0 MiB/s") == 0);

    {   // zero urzadzen (Live ISO bez dysku i sieci)
        ModelUrzadzen m;
        auto o = odpowiedz(1000000000ULL, 0, 0);
        CHECK(m.zastosuj(o));
        CHECK(m.liczba_dyskow() == 0 && m.liczba_sieci() == 0 && m.dysk(0) == nullptr && m.psf_w_ram());
        CHECK(m.grafika() != nullptr && !m.grafika()->tempo);
    }
    {   // jeden i wiele urzadzen, tempa z roznic, nowe urzadzenie bez tempa, reset licznika
        ModelUrzadzen m;
        auto o = odpowiedz(1000000000ULL, 2, 2);
        CHECK(m.zastosuj(o));
        CHECK(m.liczba_dyskow() == 2 && !m.dysk(0)->tempo && m.dysk(0)->czas_io_promile == NIEDOSTEPNE);   // pierwsza probka
        o.naglowek.czas_ns = 2000000000ULL;
        o.dyski[0].bajty_odczytane = 4096; o.dyski[0].operacje_odczytu = 2;
        o.dyski[0].czas_odczytu_ns = 10000000; o.dyski[0].operacje_z_czasem = 2;
        o.dyski[1].bajty_zapisane = 1024 * 1024; o.dyski[1].operacje_zapisu = 4; o.dyski[1].operacje_z_czasem = 3;   // 1 bez czasu
        o.sieci[0].rx_bajty = 1500; o.sieci[0].rx_pakiety = 1; o.sieci[0].tx_bajty = 60; o.sieci[0].tx_pakiety = 1;
        o.grafika[0].klatki = 60; o.grafika[0].czas_skladania_ns = 250000000; o.grafika[0].piksele_transferu = 1000;
        CHECK(m.zastosuj(o));
        const Dysk* d0 = m.dysk(0); const Dysk* d1 = m.dysk(1);
        CHECK(d0->tempo && d0->odczyt_bps == 4096 && d0->zapis_bps == 0 && d0->odczyty_ps == 2);
        CHECK(d0->czas && d0->czas_io_promile == 10 && d0->sredni_czas_us == 5000);
        CHECK(d1->tempo && d1->zapis_bps == 1024 * 1024 && !d1->czas && d1->czas_io_promile == NIEDOSTEPNE);
        CHECK(m.siec(0)->tempo && m.siec(0)->rx_bps == 1500 && m.siec(0)->tx_bps == 60 && m.siec(1)->rx_bps == 0);
        CHECK(m.grafika()->tempo && m.grafika()->klatki_10ps == 600 && m.grafika()->skladanie_promile == 250);
        CHECK(m.grafika()->transfer_bps == 4000);
        // trzecia probka: dysk 1 zniknal, pojawil sie dysk 5; licznik sieci 0 spadl (reinicjalizacja)
        o.naglowek.czas_ns = 3000000000ULL;
        o.dyski[1].id = 5;
        o.sieci[0].rx_bajty = 100;
        CHECK(m.zastosuj(o));
        CHECK(m.znajdz_dysk(1) == nullptr && m.znajdz_dysk(5) != nullptr && !m.znajdz_dysk(5)->tempo);
        CHECK(!m.siec(0)->tempo && m.siec(0)->rx_bps == 0 && m.siec(1)->tempo);
        CHECK(m.dysk(0)->h_odczyt.n == 2);       // historia zachowana dla dopasowanego id
    }
    {   // bez zegara: brak temp; ten sam czas: brak temp
        ModelUrzadzen m;
        auto o = odpowiedz(0, 1, 0); o.naglowek.flagi &= ~BWS_URZ_ZEGAR;
        CHECK(m.zastosuj(o) && m.zastosuj(o) && !m.dysk(0)->tempo);
        auto p = odpowiedz(5, 1, 0);
        CHECK(m.zastosuj(p) && m.zastosuj(p) && !m.dysk(0)->tempo);
    }
    {   // odrzucane odpowiedzi
        ModelUrzadzen m;
        auto o = odpowiedz(1, 2, 0); o.dyski[1].id = 0;
        CHECK(!m.zastosuj(o));
        o = odpowiedz(1, 0, 0); o.naglowek.wersja = 2; CHECK(!m.zastosuj(o));
        o = odpowiedz(1, 0, 0); o.naglowek.liczba_dyskow = 9; CHECK(!m.zastosuj(o));
        o = odpowiedz(1, 0, 0); o.naglowek.rozmiar_sieci = 4; CHECK(!m.zastosuj(o));
        o = odpowiedz(1, 0, 2); o.sieci[1].id = o.sieci[0].id; CHECK(!m.zastosuj(o));
        CHECK(m.bledy() == 5 && m.probki() == 0);
    }
    {   // historia ograniczona do 60 probek
        ModelUrzadzen m;
        auto o = odpowiedz(1000000000ULL, 1, 1);
        for (int i = 0; i < 200; ++i) {
            o.naglowek.czas_ns += 1000000000ULL; o.dyski[0].bajty_odczytane += 1024; o.sieci[0].rx_bajty += 2048;
            CHECK(m.zastosuj(o));
        }
        CHECK(m.dysk(0)->h_odczyt.n == HISTORIA && m.siec(0)->h_rx.n == HISTORIA);
        int h[HISTORIA + 5];
        CHECK(m.dysk(0)->h_odczyt.kopiuj(h, HISTORIA + 5) == HISTORIA && h[0] == 1 && h[HISTORIA - 1] == 1);
        CHECK(m.grafika()->h_klatki.n <= HISTORIA);
    }
}


/* ---------------------------------------------------------------- etap 2: rozliczanie I/O procesow */
static void testy_rozliczania_io() {
    using namespace RozliczanieIo;
    Tabela t{};
    uint64_t gen[BWS_IO_MAKS_PROCESOW] = {};
    gen[3] = 100; gen[4] = 101;
    // zwykly proces: pliki i siec (wlasciciel sesji = proces 4)
    dodaj(t, 3, 100, gen[3], PlikOdczyt, 4096);
    dodaj(t, 3, 100, gen[3], PlikZapis, 10);
    dodaj(t, 4, 101, gen[4], SiecWyslane, 300);
    dodaj(t, 4, 101, gen[4], SiecOdebrane, 1460);
    dodaj(t, 3, 100, gen[3], PlikOdczyt, 0);          // zero bajtow nie tworzy niczego
    CHECK(t.procesy[3].generacja == 100 && t.procesy[3].v[PlikOdczyt] == 4096 && t.procesy[3].v[PlikZapis] == 10);
    CHECK(t.procesy[4].v[SiecWyslane] == 300 && t.procesy[4].v[SiecOdebrane] == 1460 && t.procesy[4].v[PlikOdczyt] == 0);
    // pakiet odebrany po zakonczeniu wlasciciela (slot pusty): nie trafia do nikogo zywego
    gen[4] = 0;
    dodaj(t, 4, 101, gen[4], SiecOdebrane, 500);
    CHECK(t.zakonczone[SiecOdebrane] == 500 && t.procesy[4].v[SiecOdebrane] == 1460);
    // PID 4 uzyty ponownie (nowa generacja): spozniony pakiet starej sesji -> "zakonczone", nowy proces od zera
    gen[4] = 102;
    dodaj(t, 4, 101, gen[4], SiecOdebrane, 700);
    CHECK(t.zakonczone[SiecOdebrane] == 1200);
    dodaj(t, 4, 102, gen[4], PlikZapis, 1);
    CHECK(t.procesy[4].generacja == 102 && t.procesy[4].v[PlikZapis] == 1 && t.procesy[4].v[SiecOdebrane] == 0);
    // ruch jadra (sesja otwarta w PID 0) i sesja bez wlasciciela
    dodaj(t, 0, 0, 0, SiecWyslane, 40);
    dodaj(t, -1, 0, 0, SiecWyslane, 50);
    dodaj(t, 99, 5, 5, PlikOdczyt, 7);                 // PID poza zakresem
    CHECK(t.jadro[SiecWyslane] == 40 && t.zakonczone[SiecWyslane] == 50 && t.zakonczone[PlikOdczyt] == 7);
    // nasycenie
    t.procesy[3].v[PlikOdczyt] = UINT64_MAX - 1;
    dodaj(t, 3, 100, gen[3], PlikOdczyt, 10);
    CHECK(t.procesy[3].v[PlikOdczyt] == UINT64_MAX);
    t.procesy[3].v[PlikOdczyt] = 4096;

    // migawka: wpis dla PID 0 i kazdego zajetego slotu; stara generacja w tabeli = zera dla nowej
    gen[5] = 200;                                       // nowy proces bez I/O
    BwsProcesyIoOdpowiedz o{};
    migawka(t, gen, 123456789, true, o);
    CHECK(o.naglowek.wersja == BWS_PROCESY_IO_V1_ABI && o.naglowek.rozmiar_wpisu == sizeof(BwsProcesIoV1));
    CHECK(o.naglowek.liczba_wpisow == 4 && o.naglowek.czas_ns == 123456789 && (o.naglowek.flagi & BWS_IO_ZEGAR));
    CHECK(o.wpisy[0].pid == 0 && o.wpisy[0].flagi == BWS_IO_SIEC && o.wpisy[0].siec_wyslane_bajtow == 40);
    CHECK(o.wpisy[1].pid == 3 && o.wpisy[1].generacja == 100 && o.wpisy[1].pliki_odczyt_bajtow == 4096);
    CHECK(o.wpisy[2].pid == 4 && o.wpisy[2].generacja == 102 && o.wpisy[2].pliki_zapis_bajtow == 1 && o.wpisy[2].siec_odebrane_bajtow == 0);
    CHECK(o.wpisy[3].pid == 5 && o.wpisy[3].generacja == 200 && o.wpisy[3].pliki_odczyt_bajtow == 0 &&
          o.wpisy[3].flagi == (BWS_IO_PLIKI | BWS_IO_SIEC));
    CHECK(o.naglowek.siec_zakonczone_odebrane == 1200 && o.naglowek.siec_jadro_wyslane == 40 && o.naglowek.siec_zakonczone_wyslane == 50);
    // spojnosc: suma przypisana + kubelki = wszystko, co dodano (siec odebrana: 1460 + 500 + 700)
    uint64_t rx = o.naglowek.siec_jadro_odebrane + o.naglowek.siec_zakonczone_odebrane;
    for (uint32_t i = 0; i < o.naglowek.liczba_wpisow; ++i) rx += o.wpisy[i].siec_odebrane_bajtow;
    CHECK(rx == 1200);                                  // 1460 z generacji 101 nie jest juz widoczne (proces zniknal)
    migawka(t, gen, 5, false, o);
    CHECK(o.naglowek.czas_ns == 0 && !(o.naglowek.flagi & BWS_IO_ZEGAR));
    migawka(t, nullptr, 5, true, o);
    CHECK(o.naglowek.liczba_wpisow == 1);

    // logiczne vs fizyczne: zapis pliku w RAM (PSF) to licznik procesu, nie polecenie dysku;
    // fizyczny zapis zmian z pamieci podrecznej to polecenie dysku bez wlasciciela-procesu.
    MockDysk m; BlockDevice d = nowy_dysk(&m);
    Tabela t2{};
    dodaj(t2, 3, 100, 100, PlikZapis, 10000);
    CHECK(d.io.write_ops == 0 && d.io.write_bytes == 0);
    static uint8_t buf[8 * 512];
    CHECK(d.write(0, 8, buf) == BlockError::Ok);
    CHECK(d.io.write_bytes == 4096 && t2.procesy[3].v[PlikZapis] == 10000);
}

int main() {
    testy_dysku();
    testy_migawki_dysku();
    testy_sieci();
    testy_grafiki();
    testy_bez_wskaznikow();
    testy_modelu();
    testy_rozliczania_io();
    std::printf("test_telemetria_urzadzen_host: %d OK, %d bledow\n", g_ok, g_bledy);
    return g_bledy ? 1 : 0;
}
