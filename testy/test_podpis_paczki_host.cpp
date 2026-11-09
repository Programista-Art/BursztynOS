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
    std::puts("Package signature: valid RSA, altered payload/signature, missing/short signature and 520 path boundaries PASS");
}
