/*
 * Bursztyn OS - Abstrakcja Urzadzenia Sieciowego (NetworkDevice API)
 *
 * Wspolny interfejs sieciowy dla kart Ethernet w jadrze Bursztyn OS.
 * Umozliwia obsluge sterownikow Intel E1000, przyszlego VirtIO-NET oraz
 * innych kart sieciowych bez zaleznosci wyzszych warstw (Ethernet, ARP,
 * IPv4, ICMP, UDP, TCP, TLS) od konkretnego sprzetu.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Jawne kody bledow operacji wejscia/wyjscia warstwy sieciowej.
 */
enum class NetError : int32_t {
    Ok = 0,
    InvalidArgument = -1,
    NotReady = -2,
    LinkDown = -3,
    BufferTooSmall = -4,
    BufferTooLarge = -5,
    QueueFull = -6,
    IOError = -7,
    Timeout = -8,
    Unsupported = -9,
};

inline constexpr bool net_sukces(NetError err) noexcept {
    return err == NetError::Ok;
}

inline const char* net_error_str(NetError err) noexcept {
    switch (err) {
        case NetError::Ok: return "OK";
        case NetError::InvalidArgument: return "Nieprawidlowy argument";
        case NetError::NotReady: return "Urzadzenie niegotowe";
        case NetError::LinkDown: return "Brak lacza (link down)";
        case NetError::BufferTooSmall: return "Ramka zbyt mala (<14 B)";
        case NetError::BufferTooLarge: return "Ramka przekracza MTU";
        case NetError::QueueFull: return "Kolejka TX pelna";
        case NetError::IOError: return "Blad sprzetowy / DMA I/O";
        case NetError::Timeout: return "Przekroczenie limitu czasu transmisji";
        case NetError::Unsupported: return "Operacja nieobslugiwana";
        default: return "Nieznany blad sieciowy";
    }
}

/*
 * Typ urzadzenia sieciowego dla celow diagnostyki i zarzadzania.
 */
enum class NetDeviceType : uint32_t {
    Unknown = 0,
    E1000 = 1,
    VirtIO = 2,
    Loopback = 3,
};

/*
 * Stan fizycznego lacza (carrier / PHY).
 */
enum class NetLinkState : uint32_t {
    Unknown = 0,
    Up = 1,
    Down = 2,
};

/*
 * Statystyki i liczniki diagnostyczne karty sieciowej.
 */
struct NetDeviceStats {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t rx_dropped;

    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
    uint64_t tx_dropped;
    uint64_t tx_timeouts;
    uint64_t tx_ring_full;
};

struct NetworkDevice;

/*
 * Niskopoziomowa tabela operacji sterownika karty sieciowej.
 */
struct NetDeviceOps {
    NetError (*transmit)(NetworkDevice* dev, const void* frame, size_t length);
    void (*poll_rx)(NetworkDevice* dev);
    NetLinkState (*get_link_state)(const NetworkDevice* dev);
    void (*get_stats)(const NetworkDevice* dev, NetDeviceStats* out_stats);
};

/*
 * Stale ramki Ethernet II.
 */
inline constexpr size_t NET_MIN_ETH_FRAME = 14;
inline constexpr size_t NET_MAX_ETH_FRAME = 1518;
inline constexpr uint16_t NET_DEFAULT_MTU = 1500;

/*
 * Sprawdza poprawnosc adresu MAC:
 * - nie nullptr
 * - nie 00:00:00:00:00:00
 * - nie FF:FF:FF:FF:FF:FF
 * - bit multicast wyzerowany (bajt 0, bit 0 == 0)
 */
inline constexpr bool net_mac_valid(const uint8_t mac[6]) noexcept {
    if (!mac) return false;
    uint8_t suma_or = 0;
    bool wszystkie_ff = true;
    for (unsigned int i = 0; i < 6U; ++i) {
        suma_or = static_cast<uint8_t>(suma_or | mac[i]);
        if (mac[i] != 0xFF) wszystkie_ff = false;
    }
    return (suma_or != 0) && !wszystkie_ff && ((mac[0] & 0x01) == 0);
}

/*
 * Glowna struktura urzadzenia sieciowego (NetworkDevice).
 */
struct NetworkDevice {
    union {
        void* driver_data;
        void* ctx;
    };
    const NetDeviceOps* ops;

    char name[32];
    NetDeviceType device_type;
    uint8_t mac[6];
    uint16_t mtu;
    bool active;

    NetDeviceStats stats;

    /* Numer rejestracji (1, 2, ...) nadawany przez net_register_device; staly w obrebie uruchomienia, w odroznieniu
     * od indeksu rejestru, ktory przesuwa sie po wyrejestrowaniu. Ponowna rejestracja = nowy numer (telemetria
     * traktuje to jak nowe urzadzenie i nie liczy tempa z poprzednich licznikow). 0 = niezarejestrowane. */
    uint32_t registration_id;

    constexpr NetworkDevice()
        : driver_data(nullptr), ops(nullptr),
          name{0}, device_type(NetDeviceType::Unknown),
          mac{0, 0, 0, 0, 0, 0}, mtu(NET_DEFAULT_MTU),
          active(false), stats{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, registration_id(0) {}

    NetError transmit(const void* frame, size_t length);
    void poll_rx();
    NetLinkState get_link_state() const;
    void get_stats(NetDeviceStats* out) const;
    bool is_up() const;
};

inline NetError NetworkDevice::transmit(const void* frame, size_t length) {
    if (!frame) return NetError::InvalidArgument;
    if (length < NET_MIN_ETH_FRAME) return NetError::BufferTooSmall;
    if (mtu > 0 && length > static_cast<size_t>(mtu) + NET_MIN_ETH_FRAME) {
        return NetError::BufferTooLarge;
    }
    if (!ops || !ops->transmit) {
        return NetError::Unsupported;
    }
    return ops->transmit(this, frame, length);
}

inline void NetworkDevice::poll_rx() {
    if (ops && ops->poll_rx) {
        ops->poll_rx(this);
    }
}

inline NetLinkState NetworkDevice::get_link_state() const {
    if (ops && ops->get_link_state) {
        return ops->get_link_state(this);
    }
    return NetLinkState::Unknown;
}

inline void NetworkDevice::get_stats(NetDeviceStats* out) const {
    if (!out) return;
    if (ops && ops->get_stats) {
        ops->get_stats(this, out);
    } else {
        *out = stats;
    }
}

inline bool NetworkDevice::is_up() const {
    return active && (get_link_state() != NetLinkState::Down);
}

/* =========================================================================
 * Rejestr urzadzen sieciowych i mostek ze stosem
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

bool net_register_device(NetworkDevice* dev);
void net_unregister_device(NetworkDevice* dev);

NetworkDevice* net_get_primary_device(void);
bool net_set_primary_device(NetworkDevice* dev);

uint32_t net_get_device_count(void);
NetworkDevice* net_get_device_by_index(uint32_t idx);
NetworkDevice* net_get_device_by_name(const char* name);

/*
 * Punkt odbioru ramek przez sprzet. Sterowniki (E1000, VirtIO-NET)
 * wywoluja te funkcje po odebraniu poprawnej ramki Ethernet.
 * Funkcja aktualizuje liczniki i przekazuje pakiet do obsluz_pakiet_sieciowy().
 */
void net_receive_frame(NetworkDevice* dev, const void* frame, size_t len);

/*
 * Pompka pollingowa dla warstwy transportowej/aplikacyjnej (TCP/TLS/DHCP).
 * Wywoluje poll_rx() na zarejestrowanych kartach.
 */
void net_poll_rx(void);

/*
 * Zgodnosc wsteczna ze starszym kodem: zwraca MAC biezacej karty glownej.
 */
uint8_t* pobierz_mac_adres(void);

#ifdef __cplusplus
}
#endif
