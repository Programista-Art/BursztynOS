#include "diagnostyka_systemu.h"
/*
 * Bursztyn OS - Rejestr i Mostek Urzadzen Sieciowych
 *
 * Implementacja:
 *   network_device.cpp
 */

#include "network_device.h"
#include "siec.h"

namespace {

constexpr uint32_t MAX_NET_DEVICES = 8;
NetworkDevice* s_devices[MAX_NET_DEVICES] = {nullptr};
uint32_t s_device_count = 0;
NetworkDevice* s_primary_device = nullptr;

bool str_equals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

} // namespace

static void net_diag(NetworkDevice* dev,bool active) {
    const bool virtio=dev->device_type==NetDeviceType::VirtIO;
    auto d=diag_device(virtio?DIAG_VIRTIO_NET:DIAG_NETWORK,dev->name,virtio?"virtio-net":"e1000",
        active?DIAG_OK:DIAG_INACTIVE,true,active?"Sterownik sieciowy zarejestrowany":"Sterownik sieciowy nieaktywny");
    d.transport=virtio?DIAG_TRANSPORT_VIRTIO:DIAG_TRANSPORT_PCI;
    // Key is kernel-private; it is never part of the exported record.
    diag_publish((1ULL<<63)|reinterpret_cast<uint64_t>(dev),d);
}
extern "C" bool net_register_device(NetworkDevice* dev) {
    if (!dev) return false;
    if (!dev->ops || !dev->ops->transmit) return false;
    if (!net_mac_valid(dev->mac)) return false;
    if (dev->mtu < 68 || dev->mtu > 9000) return false;

    // Reject duplicate registration
    for (uint32_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] == dev) return false;
    }

    if (s_device_count >= MAX_NET_DEVICES) return false;

    dev->active = true;
    net_diag(dev,true);
    s_devices[s_device_count++] = dev;

    if (!s_primary_device) {
        s_primary_device = dev;
    }

    return true;
}

extern "C" void net_unregister_device(NetworkDevice* dev) {
    if (!dev) return;

    for (uint32_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] == dev) {
            dev->active = false;
            net_diag(dev,false);
            for (uint32_t j = i; j + 1 < s_device_count; ++j) {
                s_devices[j] = s_devices[j + 1];
            }
            s_devices[--s_device_count] = nullptr;

            if (s_primary_device == dev) {
                s_primary_device = (s_device_count > 0) ? s_devices[0] : nullptr;
            }
            return;
        }
    }
}

extern "C" NetworkDevice* net_get_primary_device(void) {
    return s_primary_device;
}

extern "C" bool net_set_primary_device(NetworkDevice* dev) {
    if (!dev) return false;
    for (uint32_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] == dev) {
            s_primary_device = dev;
            return true;
        }
    }
    return false;
}

extern "C" uint32_t net_get_device_count(void) {
    return s_device_count;
}

extern "C" NetworkDevice* net_get_device_by_index(uint32_t idx) {
    if (idx >= s_device_count) return nullptr;
    return s_devices[idx];
}

extern "C" NetworkDevice* net_get_device_by_name(const char* name) {
    if (!name) return nullptr;
    for (uint32_t i = 0; i < s_device_count; ++i) {
        if (str_equals(s_devices[i]->name, name)) {
            return s_devices[i];
        }
    }
    return nullptr;
}

extern "C" void net_receive_frame(NetworkDevice* dev, const void* frame, size_t len) {
    if (!dev || !frame || len < NET_MIN_ETH_FRAME || len > NET_MAX_ETH_FRAME) {
        if (dev) {
            __atomic_add_fetch(&dev->stats.rx_errors, 1ULL, __ATOMIC_RELAXED);
            __atomic_add_fetch(&dev->stats.rx_dropped, 1ULL, __ATOMIC_RELAXED);
        }
        return;
    }

    __atomic_add_fetch(&dev->stats.rx_packets, 1ULL, __ATOMIC_RELAXED);
    __atomic_add_fetch(&dev->stats.rx_bytes, static_cast<uint64_t>(len), __ATOMIC_RELAXED);

    obsluz_pakiet_sieciowy(
        static_cast<uint8_t*>(const_cast<void*>(frame)),
        static_cast<uint16_t>(len)
    );
}

extern "C" void net_poll_rx(void) {
    for (uint32_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] && s_devices[i]->active) {
            s_devices[i]->poll_rx();
        }
    }
}

extern "C" uint8_t* pobierz_mac_adres(void) {
    NetworkDevice* dev = net_get_primary_device();
    if (!dev) return nullptr;
    return dev->mac;
}
