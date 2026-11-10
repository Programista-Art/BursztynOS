#include "../bur_v2.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc,char**argv) {
    assert(argc==2);
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    assert(bur_v2_waliduj_plik(bytes.data(),bytes.size(),ASLR_PIE_WINDOW_BASE)==BUR_V2_OK);
    for(size_t size=0;size<160;++size)
        assert(bur_v2_waliduj_plik(bytes.data(),size,ASLR_PIE_WINDOW_BASE)!=BUR_V2_OK);
    uint64_t rng=0x572019ac;
    unsigned valid=0;
    for(unsigned trial=0;trial<30000;++trial) {
        auto mutated=bytes;
        rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;
        size_t off=rng%mutated.size();
        mutated[off]^=uint8_t(1+(rng>>32)%255);
        // Also reach deep validation behind the checksum.
        if(trial%2 && off<152) {
            NaglowekBurV2 n;
            std::memcpy(&n,mutated.data(),sizeof(n));
            n.suma_kontrolna_naglowka=bur_v2_oblicz_sume_naglowka(n);
            std::memcpy(mutated.data(),&n,sizeof(n));
        }
        const auto result=bur_v2_waliduj_plik(mutated.data(),mutated.size(),ASLR_PIE_WINDOW_BASE);
        if(result==BUR_V2_OK) ++valid; // Payload mutations may remain structurally valid.
    }
    std::printf("Production BUR v2 validation: 30000 reproducible mutations, %u structurally valid; truncated headers rejected\n",valid);
}
