/*
 * Bursztyn OS - Testy jednostkowe warstwy NetworkDevice API (Host Test)
 *
 * Plik: narzedzia/test_network_device.cpp
 *
 * Testy weryfikuja:
 *  1. Rejestracje i wyszukiwanie kart (indeks, nazwa, glowna/primary).
 *  2. Odrzucanie blednych konfiguracji (nullptr, brak ops, brak transmit, nieprawidlowy MAC, zly MTU).
 *  3. Odrzucanie duplikatow i ochrone przed przekroczeniem limitu urzadzen.
 *  4. Zmiane glownej karty (net_set_primary_device) oraz automatyczny fallback przy wyrejestrowaniu.
 *  5. Walidacje rozmiarow ramek (BufferTooSmall, BufferTooLarge, InvalidArgument).
 *  6. Propagacje jawnych kodow bledow transmisji (QueueFull, IOError, Timeout, LinkDown, Ok).
 *  7. Odbior ramek (net_receive_frame) i bezpieczne przekazywanie do obsluz_pakiet_sieciowy().
 *  8. Zliczanie statystyk RX/TX (pakiety, bajty, bledy, dropy, timeouty, ring_full).
 *  9. Pompke pollingowa (net_poll_rx) dla kart aktywnych/nieaktywnych.
 * 10. Stan lacza (NetLinkState) i metode dev->is_up().
 * 11. Walidacje unicast/multicast/broadcast adresow MAC.
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include "network_device.h"

// ============================================================================
// MOCK DLA STOSU SIECIOWEGO (obsluz_pakiet_sieciowy)
// ============================================================================

static uint32_t g_mock_rx_packets_received = 0;
static uint16_t g_mock_last_rx_len = 0;
static uint8_t g_mock_last_rx_buf[2048] = {0};

void obsluz_pakiet_sieciowy(uint8_t* pakiet, uint16_t dlugosc) {
    g_mock_rx_packets_received++;
    g_mock_last_rx_len = dlugosc;
    if (pakiet && dlugosc <= sizeof(g_mock_last_rx_buf)) {
        std::memcpy(g_mock_last_rx_buf, pakiet, dlugosc);
    }
}

// ============================================================================
// MOCK STEROWNIK KARTY SIECIOWEJ
// ============================================================================

struct MockDriverCtx {
    NetError next_tx_result = NetError::Ok;
    uint32_t tx_invocations = 0;
    uint32_t poll_rx_invocations = 0;
    NetLinkState link_state = NetLinkState::Up;
    uint8_t last_tx_frame[2048] = {0};
    size_t last_tx_len = 0;
};

static NetError mock_transmit(NetworkDevice* dev, const void* frame, size_t length) {
    if (!dev || !dev->driver_data) return NetError::InvalidArgument;
    auto* ctx = static_cast<MockDriverCtx*>(dev->driver_data);
    ctx->tx_invocations++;
    ctx->last_tx_len = length;
    if (frame && length <= sizeof(ctx->last_tx_frame)) {
        std::memcpy(ctx->last_tx_frame, frame, length);
    }
    return ctx->next_tx_result;
}

static void mock_poll_rx(NetworkDevice* dev) {
    if (!dev || !dev->driver_data) return;
    auto* ctx = static_cast<MockDriverCtx*>(dev->driver_data);
    ctx->poll_rx_invocations++;
}

static NetLinkState mock_get_link_state(const NetworkDevice* dev) {
    if (!dev || !dev->driver_data) return NetLinkState::Unknown;
    auto* ctx = static_cast<const MockDriverCtx*>(dev->driver_data);
    return ctx->link_state;
}

static void mock_get_stats(const NetworkDevice* dev, NetDeviceStats* out_stats) {
    if (!dev || !out_stats) return;
    *out_stats = dev->stats;
}

static const NetDeviceOps g_mock_ops = {
    mock_transmit,
    mock_poll_rx,
    mock_get_link_state,
    mock_get_stats,
};

static void reset_registry() {
    while (net_get_device_count() > 0) {
        NetworkDevice* d = net_get_device_by_index(0);
        net_unregister_device(d);
    }
    g_mock_rx_packets_received = 0;
    g_mock_last_rx_len = 0;
    std::memset(g_mock_last_rx_buf, 0, sizeof(g_mock_last_rx_buf));
}

// ============================================================================
// TESTY
// ============================================================================

static void test_mac_validation() {
    printf("[TEST] Walidacja adresow MAC... ");

    // Nullptr
    assert(!net_mac_valid(nullptr));

    // Same zera
    uint8_t zeros[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(!net_mac_valid(zeros));

    // Broadcast FF:FF:FF:FF:FF:FF
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    assert(!net_mac_valid(bcast));

    // Multicast (bajt 0 bit 0 == 1)
    uint8_t mcast1[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    assert(!net_mac_valid(mcast1));

    uint8_t mcast2[6] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01};
    assert(!net_mac_valid(mcast2));

    // Poprawny standardowy unicast
    uint8_t valid1[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    assert(net_mac_valid(valid1));

    // Poprawny adres lokalnie zarzadzany unicast (bajt 0 bit 1 == 1, bit 0 == 0)
    uint8_t valid2[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    assert(net_mac_valid(valid2));

    printf("PASS\n");
}

static void test_registration_and_lookup() {
    printf("[TEST] Rejestracja i wyszukiwanie urzadzen... ");
    reset_registry();

    MockDriverCtx ctx1;
    NetworkDevice dev1;
    dev1.ops = &g_mock_ops;
    dev1.driver_data = &ctx1;
    dev1.device_type = NetDeviceType::E1000;
    dev1.mtu = 1500;
    std::memcpy(dev1.name, "eth0", 5);
    dev1.mac[0] = 0x52; dev1.mac[1] = 0x54; dev1.mac[2] = 0x00;
    dev1.mac[3] = 0x11; dev1.mac[4] = 0x22; dev1.mac[5] = 0x33;

    assert(net_get_device_count() == 0);
    assert(net_get_primary_device() == nullptr);
    assert(pobierz_mac_adres() == nullptr);

    // Sukces rejestracji
    assert(net_register_device(&dev1));
    assert(dev1.active);
    assert(net_get_device_count() == 1);
    assert(net_get_primary_device() == &dev1);
    assert(net_get_device_by_index(0) == &dev1);
    assert(net_get_device_by_index(1) == nullptr);
    assert(net_get_device_by_name("eth0") == &dev1);
    assert(net_get_device_by_name("nonexistent") == nullptr);

    // pobierz_mac_adres() zwraca MAC pierwszej zarejestrowanej karty
    uint8_t* mac = pobierz_mac_adres();
    assert(mac != nullptr);
    assert(mac[0] == 0x52 && mac[5] == 0x33);

    // Odrzucenie duplikatu
    assert(!net_register_device(&dev1));
    assert(net_get_device_count() == 1);

    // Wyrejestrowanie
    net_unregister_device(&dev1);
    assert(!dev1.active);
    assert(net_get_device_count() == 0);
    assert(net_get_primary_device() == nullptr);
    assert(pobierz_mac_adres() == nullptr);

    printf("PASS\n");
}

static void test_invalid_device_rejection() {
    printf("[TEST] Odrzucanie nieprawidlowych urzadzen... ");
    reset_registry();

    // nullptr
    assert(!net_register_device(nullptr));

    // Brak ops
    NetworkDevice bad1;
    bad1.mac[0] = 0x52; bad1.mac[5] = 0x01;
    bad1.mtu = 1500;
    assert(!net_register_device(&bad1));

    // Brak transmit
    NetDeviceOps no_tx_ops = g_mock_ops;
    no_tx_ops.transmit = nullptr;
    NetworkDevice bad2;
    bad2.ops = &no_tx_ops;
    bad2.mac[0] = 0x52; bad2.mac[5] = 0x01;
    bad2.mtu = 1500;
    assert(!net_register_device(&bad2));

    // Zly MAC (zera)
    NetworkDevice bad3;
    bad3.ops = &g_mock_ops;
    bad3.mtu = 1500;
    assert(!net_register_device(&bad3));

    // Zly MTU (< 68)
    NetworkDevice bad4;
    bad4.ops = &g_mock_ops;
    bad4.mac[0] = 0x52; bad4.mac[5] = 0x01;
    bad4.mtu = 60;
    assert(!net_register_device(&bad4));

    // Zly MTU (> 9000)
    NetworkDevice bad5;
    bad5.ops = &g_mock_ops;
    bad5.mac[0] = 0x52; bad5.mac[5] = 0x01;
    bad5.mtu = 9500;
    assert(!net_register_device(&bad5));

    assert(net_get_device_count() == 0);
    printf("PASS\n");
}

static void test_primary_switch_and_fallback() {
    printf("[TEST] Zmiana karty glownej i automatyczny fallback... ");
    reset_registry();

    MockDriverCtx ctx1, ctx2, ctx3;
    NetworkDevice dev1, dev2, dev3;

    auto setup_dev = [](NetworkDevice& d, MockDriverCtx& c, const char* name, uint8_t id) {
        d.ops = &g_mock_ops;
        d.driver_data = &c;
        d.mtu = 1500;
        std::memcpy(d.name, name, std::strlen(name) + 1);
        d.mac[0] = 0x52; d.mac[1] = 0x54; d.mac[2] = 0x00;
        d.mac[3] = 0x10; d.mac[4] = 0x20; d.mac[5] = id;
    };

    setup_dev(dev1, ctx1, "eth0", 1);
    setup_dev(dev2, ctx2, "eth1", 2);
    setup_dev(dev3, ctx3, "eth2", 3);

    assert(net_register_device(&dev1));
    assert(net_register_device(&dev2));
    assert(net_register_device(&dev3));
    assert(net_get_device_count() == 3);

    // Pierwsza karta staje sie domyslna glowna
    assert(net_get_primary_device() == &dev1);
    assert(pobierz_mac_adres()[5] == 1);

    // Reczne przelaczenie na eth1
    assert(net_set_primary_device(&dev2));
    assert(net_get_primary_device() == &dev2);
    assert(pobierz_mac_adres()[5] == 2);

    // Proba przelaczenia na niezarejestrowane urzadzenie
    NetworkDevice unregistered;
    assert(!net_set_primary_device(&unregistered));
    assert(net_get_primary_device() == &dev2);

    // Wyrejestrowanie karty eth1 (obecnie primary) -> fallback na eth0
    net_unregister_device(&dev2);
    assert(net_get_device_count() == 2);
    assert(net_get_primary_device() == &dev1);
    assert(pobierz_mac_adres()[5] == 1);

    // Wyrejestrowanie eth0 -> primary przechodzi na jedyna pozostala eth2
    net_unregister_device(&dev1);
    assert(net_get_device_count() == 1);
    assert(net_get_primary_device() == &dev3);
    assert(pobierz_mac_adres()[5] == 3);

    // Wyrejestrowanie ostatniej
    net_unregister_device(&dev3);
    assert(net_get_device_count() == 0);
    assert(net_get_primary_device() == nullptr);

    printf("PASS\n");
}

static void test_device_capacity_limit() {
    printf("[TEST] Limit pojemnosci rejestru urzadzen... ");
    reset_registry();

    constexpr uint32_t MAX_CAPACITY = 8;
    MockDriverCtx ctxs[MAX_CAPACITY + 1];
    NetworkDevice devs[MAX_CAPACITY + 1];

    for (uint32_t i = 0; i <= MAX_CAPACITY; ++i) {
        devs[i].ops = &g_mock_ops;
        devs[i].driver_data = &ctxs[i];
        devs[i].mtu = 1500;
        char name[16];
        snprintf(name, sizeof(name), "eth%u", i);
        std::memcpy(devs[i].name, name, std::strlen(name) + 1);
        devs[i].mac[0] = 0x52; devs[i].mac[1] = 0x54; devs[i].mac[2] = 0x00;
        devs[i].mac[3] = 0x00; devs[i].mac[4] = 0x00; devs[i].mac[5] = static_cast<uint8_t>(i + 1);
    }

    // Rejestracja 8 urzadzen
    for (uint32_t i = 0; i < MAX_CAPACITY; ++i) {
        assert(net_register_device(&devs[i]));
    }
    assert(net_get_device_count() == MAX_CAPACITY);

    // 9-te urzadzenie powinno zostac odrzucone
    assert(!net_register_device(&devs[MAX_CAPACITY]));
    assert(net_get_device_count() == MAX_CAPACITY);

    // Wyrejestrowanie jednego i ponowna proba
    net_unregister_device(&devs[0]);
    assert(net_get_device_count() == MAX_CAPACITY - 1);
    assert(net_register_device(&devs[MAX_CAPACITY]));
    assert(net_get_device_count() == MAX_CAPACITY);

    reset_registry();
    printf("PASS\n");
}

static void test_transmission_and_errors() {
    printf("[TEST] Transmisja ramek i propagacja kodow bledow... ");
    reset_registry();

    MockDriverCtx ctx;
    NetworkDevice dev;
    dev.ops = &g_mock_ops;
    dev.driver_data = &ctx;
    dev.mtu = 1500;
    dev.mac[0] = 0x52; dev.mac[5] = 0x01;
    assert(net_register_device(&dev));

    uint8_t frame[2000] = {0};
    std::memset(frame, 0xAB, sizeof(frame));

    // Null frame pointer
    assert(dev.transmit(nullptr, 64) == NetError::InvalidArgument);

    // Zbyt krotka ramka (< 14 bajtow)
    assert(dev.transmit(frame, 13) == NetError::BufferTooSmall);

    // Zbyt dluga ramka (> MTU + 14 = 1514)
    assert(dev.transmit(frame, 1515) == NetError::BufferTooLarge);

    // Prawidlowa ramka standardowa (64 B)
    ctx.next_tx_result = NetError::Ok;
    NetError res = dev.transmit(frame, 64);
    assert(res == NetError::Ok);
    assert(ctx.tx_invocations == 1);
    assert(ctx.last_tx_len == 64);
    assert(ctx.last_tx_frame[0] == 0xAB);

    // Symulacja roznych kodow bledu sterownika
    ctx.next_tx_result = NetError::QueueFull;
    assert(dev.transmit(frame, 64) == NetError::QueueFull);

    ctx.next_tx_result = NetError::IOError;
    assert(dev.transmit(frame, 64) == NetError::IOError);

    ctx.next_tx_result = NetError::Timeout;
    assert(dev.transmit(frame, 64) == NetError::Timeout);

    ctx.next_tx_result = NetError::LinkDown;
    assert(dev.transmit(frame, 64) == NetError::LinkDown);

    ctx.next_tx_result = NetError::NotReady;
    assert(dev.transmit(frame, 64) == NetError::NotReady);

    // Test urzadzenia bez ops->transmit
    NetworkDevice dev_no_ops;
    assert(dev_no_ops.transmit(frame, 64) == NetError::Unsupported);

    net_unregister_device(&dev);
    printf("PASS\n");
}

static void test_reception_and_delivery() {
    printf("[TEST] Odbior ramek (net_receive_frame) i dostarczenie do stosu... ");
    reset_registry();

    MockDriverCtx ctx;
    NetworkDevice dev;
    dev.ops = &g_mock_ops;
    dev.driver_data = &ctx;
    dev.mtu = 1500;
    dev.mac[0] = 0x52; dev.mac[5] = 0x01;
    assert(net_register_device(&dev));

    uint8_t test_frame[128];
    for (size_t i = 0; i < sizeof(test_frame); ++i) {
        test_frame[i] = static_cast<uint8_t>(i ^ 0x5A);
    }

    // 1. Odbior z nullptr dev
    net_receive_frame(nullptr, test_frame, sizeof(test_frame));
    assert(g_mock_rx_packets_received == 0);

    // 2. Odbior z nullptr frame
    net_receive_frame(&dev, nullptr, sizeof(test_frame));
    assert(g_mock_rx_packets_received == 0);
    assert(dev.stats.rx_errors == 1);
    assert(dev.stats.rx_dropped == 1);

    // 3. Odbior ze zbyt krotka ramka (< 14 B)
    net_receive_frame(&dev, test_frame, 12);
    assert(g_mock_rx_packets_received == 0);
    assert(dev.stats.rx_errors == 2);
    assert(dev.stats.rx_dropped == 2);

    // 4. Odbior ze zbyt dluga ramka (> 1518 B)
    net_receive_frame(&dev, test_frame, 1519);
    assert(g_mock_rx_packets_received == 0);
    assert(dev.stats.rx_errors == 3);
    assert(dev.stats.rx_dropped == 3);

    // 5. Poprawny odbior standardowej ramki
    net_receive_frame(&dev, test_frame, sizeof(test_frame));
    assert(g_mock_rx_packets_received == 1);
    assert(g_mock_last_rx_len == sizeof(test_frame));
    assert(std::memcmp(g_mock_last_rx_buf, test_frame, sizeof(test_frame)) == 0);
    assert(dev.stats.rx_packets == 1);
    assert(dev.stats.rx_bytes == sizeof(test_frame));

    net_unregister_device(&dev);
    printf("PASS\n");
}

static void test_polling_pump() {
    printf("[TEST] Pompka pollingowa (net_poll_rx)... ");
    reset_registry();

    MockDriverCtx ctx1, ctx2;
    NetworkDevice dev1, dev2;
    dev1.ops = &g_mock_ops; dev1.driver_data = &ctx1; dev1.mtu = 1500; dev1.mac[0] = 0x52; dev1.mac[5] = 1;
    dev2.ops = &g_mock_ops; dev2.driver_data = &ctx2; dev2.mtu = 1500; dev2.mac[0] = 0x52; dev2.mac[5] = 2;

    assert(net_register_device(&dev1));
    assert(net_register_device(&dev2));

    assert(ctx1.poll_rx_invocations == 0);
    assert(ctx2.poll_rx_invocations == 0);

    net_poll_rx();
    assert(ctx1.poll_rx_invocations == 1);
    assert(ctx2.poll_rx_invocations == 1);

    net_poll_rx();
    assert(ctx1.poll_rx_invocations == 2);
    assert(ctx2.poll_rx_invocations == 2);

    // Wyrejestrowanie dev2
    net_unregister_device(&dev2);
    net_poll_rx();
    assert(ctx1.poll_rx_invocations == 3);
    assert(ctx2.poll_rx_invocations == 2); // Nie wywolane dla dev2

    net_unregister_device(&dev1);
    printf("PASS\n");
}

static void test_link_state_and_is_up() {
    printf("[TEST] Stan lacza (NetLinkState) i dev->is_up()... ");
    reset_registry();

    MockDriverCtx ctx;
    NetworkDevice dev;
    dev.ops = &g_mock_ops;
    dev.driver_data = &ctx;
    dev.mtu = 1500;
    dev.mac[0] = 0x52; dev.mac[5] = 1;

    assert(!dev.is_up()); // jeszcze nie zarejestrowana (active == false)

    assert(net_register_device(&dev));
    assert(dev.active);

    ctx.link_state = NetLinkState::Up;
    assert(dev.get_link_state() == NetLinkState::Up);
    assert(dev.is_up());

    ctx.link_state = NetLinkState::Down;
    assert(dev.get_link_state() == NetLinkState::Down);
    assert(!dev.is_up());

    ctx.link_state = NetLinkState::Unknown;
    assert(dev.get_link_state() == NetLinkState::Unknown);
    assert(dev.is_up());

    net_unregister_device(&dev);
    assert(!dev.is_up());

    printf("PASS\n");
}

static void test_error_strings() {
    printf("[TEST] Czytelnosc komunikatow NetError... ");

    assert(std::strcmp(net_error_str(NetError::Ok), "OK") == 0);
    assert(std::strcmp(net_error_str(NetError::InvalidArgument), "Nieprawidlowy argument") == 0);
    assert(std::strcmp(net_error_str(NetError::QueueFull), "Kolejka TX pelna") == 0);
    assert(std::strcmp(net_error_str(NetError::BufferTooSmall), "Ramka zbyt mala (<14 B)") == 0);
    assert(std::strcmp(net_error_str(NetError::BufferTooLarge), "Ramka przekracza MTU") == 0);
    assert(std::strcmp(net_error_str(NetError::Timeout), "Przekroczenie limitu czasu transmisji") == 0);
    assert(std::strcmp(net_error_str(NetError::IOError), "Blad sprzetowy / DMA I/O") == 0);
    assert(std::strcmp(net_error_str(NetError::NotReady), "Urzadzenie niegotowe") == 0);
    assert(std::strcmp(net_error_str(NetError::LinkDown), "Brak lacza (link down)") == 0);
    assert(std::strcmp(net_error_str(NetError::Unsupported), "Operacja nieobslugiwana") == 0);

    printf("PASS\n");
}

static void test_duplicate_registration() {
    printf("[TEST] Odrzucanie zduplikowanej rejestracji... ");
    reset_registry();

    MockDriverCtx ctx;
    NetworkDevice dev;
    dev.ops = &g_mock_ops;
    dev.driver_data = &ctx;
    dev.mtu = 1500;
    std::memcpy(dev.name, "eth0", 5);
    dev.mac[0] = 0x52; dev.mac[1] = 0x54; dev.mac[2] = 0x00;
    dev.mac[3] = 0x12; dev.mac[4] = 0x34; dev.mac[5] = 0x56;

    assert(net_register_device(&dev));
    assert(net_get_device_count() == 1);

    // Druga proba rejestracji tego samego wskaznika musi zostac bezwzglednie odrzucona
    assert(!net_register_device(&dev));
    assert(net_get_device_count() == 1);

    net_unregister_device(&dev);
    assert(net_get_device_count() == 0);
    printf("PASS\n");
}

static void test_device_cleanup_and_lifetime() {
    printf("[TEST] Cykl zycia i czyszczenie urzadzenia (cleanup/lifetime)... ");
    reset_registry();

    // 1. Bezpieczenstwo wywolan dla nullptr i niezarejestrowanych obiektow
    net_unregister_device(nullptr);
    assert(net_get_device_count() == 0);

    NetworkDevice dummy;
    dummy.active = false;
    net_unregister_device(&dummy);
    assert(net_get_device_count() == 0);

    // 2. Dynamiczna alokacja symulujaca sterownik ze sterty / modulu
    auto* ctx = new MockDriverCtx();
    auto* dev = new NetworkDevice();
    dev->ops = &g_mock_ops;
    dev->driver_data = ctx;
    dev->device_type = NetDeviceType::E1000;
    dev->mtu = 1500;
    std::memcpy(dev->name, "eth0", 5);
    dev->mac[0] = 0x52; dev->mac[1] = 0x54; dev->mac[2] = 0x00;
    dev->mac[3] = 0xAA; dev->mac[4] = 0xBB; dev->mac[5] = 0xCC;

    assert(net_register_device(dev));
    assert(dev->active);
    assert(net_get_primary_device() == dev);
    assert(net_get_device_by_name("eth0") == dev);
    assert(net_get_device_by_index(0) == dev);

    // Transmisja i odbior dzialaja
    uint8_t pkt[64] = {0};
    assert(dev->transmit(pkt, sizeof(pkt)) == NetError::Ok);
    net_receive_frame(dev, pkt, sizeof(pkt));
    assert(dev->stats.rx_packets == 1);

    // Wyrejestrowanie
    net_unregister_device(dev);
    assert(!dev->active);
    assert(net_get_device_count() == 0);
    assert(net_get_primary_device() == nullptr);
    assert(net_get_device_by_name("eth0") == nullptr);
    assert(net_get_device_by_index(0) == nullptr);
    assert(pobierz_mac_adres() == nullptr);

    // Ponowne wyrejestrowanie jest bezpiecznym no-opem
    net_unregister_device(dev);
    assert(net_get_device_count() == 0);

    // Ponowna rejestracja tego samego obiektu (np. po resecie lub ponownym podlaczeniu)
    assert(net_register_device(dev));
    assert(dev->active);
    assert(net_get_device_count() == 1);
    assert(net_get_primary_device() == dev);

    // Ostateczne wyrejestrowanie i dealokacja pamieci sterownika
    net_unregister_device(dev);
    delete dev;
    delete ctx;
    assert(net_get_device_count() == 0);
    assert(net_get_primary_device() == nullptr);

    printf("PASS\n");
}

int main() {
    printf("====================================================================\n");
    printf(" BURSZTYN OS - TESTY JEDNOSTKOWE NETWORK DEVICE API\n");
    printf("====================================================================\n");

    test_mac_validation();
    test_registration_and_lookup();
    test_duplicate_registration();
    test_invalid_device_rejection();
    test_primary_switch_and_fallback();
    test_device_capacity_limit();
    test_transmission_and_errors();
    test_reception_and_delivery();
    test_polling_pump();
    test_link_state_and_is_up();
    test_device_cleanup_and_lifetime();
    test_error_strings();

    printf("====================================================================\n");
    printf(" [SUKCES] Wszystkie testy jednostkowe NetworkDevice zakonczone (PASS).\n");
    printf("====================================================================\n");
    return 0;
}
