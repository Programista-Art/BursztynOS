#pragma once
#include <stdint.h>

// Exact identity only. This registry intentionally has no active entries until
// a real hardware report and a documented required vendor operation exist.
struct HdaTozsamoscKodeka {
    uint16_t vendor_id, device_id;
    uint32_t subsystem_id;
};
struct HdaQuirk {
    HdaTozsamoscKodeka tozsamosc;
    bool wymagany_subsystem;
    // A future implementation must be a bounded kernel-owned operation.
    bool (*zastosuj)(uint32_t kontroler, uint8_t cad);
};
inline const HdaQuirk* hda_znajdz_quirk(HdaTozsamoscKodeka) {
    return nullptr;
}

// Controller-level quirks based on PCI Vendor ID / Device ID.
struct HdaControllerQuirks {
    bool corbrp_self_clear; // NVIDIA CORBRP.RST self-clears immediately
};

inline HdaControllerQuirks hda_pobierz_quirki_kontrolera(uint16_t vendor_id, uint16_t device_id) {
    HdaControllerQuirks q{};
    if (vendor_id == 0x10DE) { // NVIDIA Corporation
        q.corbrp_self_clear = true;
    }
    (void)device_id;
    return q;
}

// Standard amp capabilities use offset as 0 dB, steps as max encoded gain,
// and (StepSize+1)*0.25 dB. Prepare a conservative <= -18 dB setting without
// writing any codec register. A missing attenuator cannot guarantee safe gain.
inline bool hda_konserwatywne_wzmocnienie(uint32_t caps, uint8_t* gain) {
    if(!gain)return false;
    const uint32_t offset=caps&127, max=(caps>>8)&127, quarter_db=((caps>>16)&127)+1;
    if(offset>max || offset*quarter_db<72)return false;
    const uint32_t attenuation=(72+quarter_db-1)/quarter_db;
    *gain=static_cast<uint8_t>(offset-attenuation);
    return true;
}
