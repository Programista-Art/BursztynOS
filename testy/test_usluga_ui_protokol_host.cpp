#include <cassert>
#include <cstdio>
#include <cstring>
#include "bursztyn_ui/usluga_ui_protokol.h"

int main() {
    using namespace BursztynUI::UslugaUi;
    PowiadomienieZadanie z{};
    wypelnij_naglowek(&z.nag, Rodzaj::PowiadomieniePokaz, 7);
    assert(naglowek_poprawny(&z.nag));
    assert(z.nag.rodzaj == static_cast<uint32_t>(Rodzaj::PowiadomieniePokaz));
    assert(z.nag.request_id == 7);
    z.typ = 0;
    z.timeout_ms = 4000;
    std::snprintf(z.tytul, sizeof(z.tytul), "Test");
    std::snprintf(z.tresc, sizeof(z.tresc), "Zalazek protokolu UI");
    assert(sizeof(z) <= MAX_IPC_DANYCH);

    DialogZadanie d{};
    wypelnij_naglowek(&d.nag, Rodzaj::DialogOtworz, 1);
    assert(naglowek_poprawny(&d.nag));
    assert(sizeof(d) <= MAX_IPC_DANYCH);

    OdpowiedzWiadomosc o{};
    wypelnij_naglowek(&o.nag, Rodzaj::Odpowiedz, 1);
    o.kod = static_cast<uint32_t>(KodOdpowiedzi::Ok);
    assert(naglowek_poprawny(&o.nag));
    assert(sizeof(o) <= MAX_IPC_DANYCH);

    Naglowek bad{};
    assert(!naglowek_poprawny(&bad));
    std::puts("test_usluga_ui_protokol_host: PASS");
    return 0;
}
