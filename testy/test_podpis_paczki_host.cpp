// Execute the production signature verifier against real RSA/SHA-256.
#include "../jadro/fs/podpis_paczki.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<char> signature;
static bool missing;
bool czytaj_fragment_pliku(const char*, uint64_t offset, char* out,
                          uint32_t size, uint32_t* got) {
    assert(offset == 0);
    if (missing) return false;
    *got = signature.size() < size ? signature.size() : size;
    std::memcpy(out, signature.data(), *got);
    return true;
}
int main(int argc, char** argv) {
    assert(argc == 3);
    std::ifstream payload_file(argv[1], std::ios::binary);
    std::vector<uint8_t> payload(std::istreambuf_iterator<char>(payload_file), {});
    std::ifstream signature_file(argv[2], std::ios::binary);
    signature.assign(std::istreambuf_iterator<char>(signature_file), {});
    assert(!payload.empty() && signature.size() == 256);
    {   // podpis_sha256: FIPS 180-4 vector "abc" and rejection of empty/oversized input
        const uint8_t abc[] = {'a','b','c'};
        const uint8_t expected[32] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
                                      0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
        uint8_t got[32]{};
        assert(podpis_sha256(abc, 3, got) && std::memcmp(got, expected, 32) == 0);
        assert(!podpis_sha256(abc, 0, got) && !podpis_sha256(nullptr, 3, got) && !podpis_sha256(abc, 3, nullptr));
        assert(!podpis_sha256(abc, 129ULL * 1024 * 1024, got));
    }
    auto verify = [&] { return podpis_paczki_weryfikuj("/test.bur", payload.data(), payload.size()); };
    assert(verify() == PodpisPaczkiWynik::Ok);
    signature.push_back(0);
    assert(verify() == PodpisPaczkiWynik::ZlyRozmiar);
    signature.pop_back();
    payload[0] ^= 1;
    assert(verify() == PodpisPaczkiWynik::Niezgodnosc);
    payload[0] ^= 1;
    signature[0] ^= 1;
    assert(verify() == PodpisPaczkiWynik::Niezgodnosc);
    signature.pop_back();
    assert(verify() == PodpisPaczkiWynik::ZlyRozmiar);
    missing = true;
    assert(verify() == PodpisPaczkiWynik::BrakPliku);
    char out[PODPIS_MAKS_SCIEZKI];
    for (size_t length = 0; length < 520; ++length) {
        std::string path(length, 'x');
        assert(sciezka_podpisu(path.c_str(), out, sizeof(out)) ==
               (length + sizeof(".podpis") <= sizeof(out)));
    }
    {   // System identity: built-in SHA-256 table OR release signature, over the same bytes.
        using TozsamoscSystemowa::Wpis;
        signature.assign(256, 0);  // restore size (contents invalid below unless re-read)
        Wpis entry{"/test.bur", {}, static_cast<uint32_t>(payload.size())};
        assert(podpis_sha256(payload.data(), payload.size(), entry.skrot));
        auto ident = [&](const Wpis* t, size_t n, const char* path = "/test.bur") {
            return tozsamosc_systemowa_zweryfikuj_tablica(t, n, path, payload.data(), payload.size());
        };
        missing = true;  // no .podpis file at all: only the table can authenticate
        assert(ident(&entry, 1));
        assert(!ident(nullptr, 0) && !ident(&entry, 0));
        assert(!ident(&entry, 1, "/test.bur/") && !ident(&entry, 1, "/other.bur") && !ident(&entry, 1, nullptr));
        Wpis shorter = entry; shorter.rozmiar += 1; assert(!ident(&shorter, 1));
        for (int i = 0; i < 32; ++i) { Wpis bad = entry; bad.skrot[i] ^= 1; assert(!ident(&bad, 1)); }
        payload[payload.size() / 2] ^= 1;               // tampered bytes: table no longer matches
        assert(!ident(&entry, 1));
        payload[payload.size() / 2] ^= 1;
        // Valid release signature authenticates an updated binary absent from the table.
        std::ifstream sf(argv[2], std::ios::binary);
        signature.assign(std::istreambuf_iterator<char>(sf), {});
        missing = false;
        assert(ident(nullptr, 0));                       // signature only
        Wpis other = entry; other.skrot[0] ^= 1;
        assert(ident(&other, 1));                        // stale table entry, valid signature (update)
        payload[0] ^= 1;
        assert(!ident(&other, 1) && !ident(nullptr, 0)); // tampered + stale signature: refused
        payload[0] ^= 1;
    }
    std::puts("Package signature: valid RSA, altered payload/signature, missing/short signature and 520 path boundaries PASS");
}
