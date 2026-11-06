#include "wyjatek_uzytkownika.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
int main() {
    unsigned checks = 0;
    const unsigned recoverable[] = {0,  1,  3,  4,  5,  6,  7,  9,  10, 11, 12, 13, 14, 15, 16,
                                    17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    const unsigned user_selectors[] = {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 63};
    for (unsigned vector = 0; vector < 256; ++vector)
        for (unsigned cs = 0; cs < 64; ++cs)
            for (bool active : {false, true}) {
                bool listed_vector = false, user = false;
                for (auto v : recoverable)
                    if (vector == v)
                        listed_vector = true;
                for (auto selector : user_selectors)
                    if (cs == selector)
                        user = true;
                bool expected = active && listed_vector && user;
                ++checks;
                if (wyjatek_izoluj_proces(vector, cs, active) != expected)
                    std::abort();
            }
    std::printf(
        "User fault isolation policy: %u checks PASS (Ring 0, inactive PID, NMI/DF/MC remain fatal)\n",
        checks);
}
