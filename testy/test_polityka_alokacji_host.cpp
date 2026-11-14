#include "../jadro/bws/polityka_alokacji.h"
#include <initializer_list>
#include <cstdio>
using namespace PolitykaAlokacji;
static int ok = 0, bad = 0;
#define CHECK(c) do { if (c) ++ok; else { ++bad; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
int main() {
    for (bool t : {false, true}) for (bool p : {false, true}) CHECK(!dozwolona(0, t, p));
    // zwykly proces: do 16 MiB wlacznie
    CHECK(dozwolona(1, false, false) && dozwolona(16 * MIB, false, false) && !dozwolona(16 * MIB + 1, false, false));
    CHECK(!dozwolona(40 * MIB, false, false) && !dozwolona(72 * MIB, false, false) && !dozwolona(UINT64_MAX, false, false));
    // Tapety: do 40 MiB
    CHECK(dozwolona(40 * MIB, true, false) && !dozwolona(40 * MIB + 1, true, false) && !dozwolona(72 * MIB, true, false));
    // Przegladarka zweryfikowana: do 72 MiB, ani bajtu wiecej
    CHECK(dozwolona(72 * MIB, false, true) && !dozwolona(72 * MIB + 1, false, true) && !dozwolona(UINT64_MAX, false, true));
    CHECK(dozwolona(65 * MIB, false, true) && !dozwolona(65 * MIB, false, false));   // 17 Mpx BGRA (64,9 MiB) tylko po weryfikacji
    CHECK(maks_rozmiar(true, true) == 72 * MIB);
    // 17 Mpx BGRA miesci sie w limicie przegladarki, 18 Mpx nie musi
    CHECK(17000000ULL * 4 <= LIMIT_PRZEGLADARKA);
    std::printf("%s test_polityka_alokacji_host: %d checks, %d failed\n", bad ? "FAIL" : "PASS", ok + bad, bad);
    return bad ? 1 : 0;
}
