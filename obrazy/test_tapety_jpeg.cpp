#include "../tapety_format.h"
#include <cassert>

int main() {
    assert(tapety_sciezka_jpeg("/uzytkownicy/Pulpit/tapeta.jpg"));
    assert(tapety_sciezka_jpeg("/uzytkownicy/Zdjecia/photo.JPEG"));
    assert(!tapety_sciezka_jpeg("/uzytkownicy/../system/x.jpg"));
    assert(!tapety_sciezka_jpeg("/uzytkownicy//x.jpg"));
    assert(!tapety_sciezka_jpeg("/uzytkownicy/x.png"));
    assert(tapety_sciezka(TAPETY_SCIEZKI[0]));
    assert(tapety_sciezka_powierzchni("/uzytkownicy/Pulpit/obraz.btp"));
    assert(!tapety_sciezka_powierzchni("/uzytkownicy/../system/obraz.btp"));
    assert(!tapety_sciezka_powierzchni(TAPETY_SCIEZKI[0]));
    TapetyKonfiguracja original{};
    assert(tapety_kopiuj(original.sciezka, "/uzytkownicy/Pulpit/tapeta.jpg", sizeof(original.sciezka)));
    original.tryb = TrybSkalowaniaObrazu::Rozciagnij;
    char buffer[TAPETY_CFG_MAX]{};
    const size_t bytes = tapety_serializuj(original, buffer, sizeof(buffer));
    TapetyKonfiguracja decoded{};
    assert(bytes && tapety_parsuj(buffer, bytes, &decoded));
    assert(tapety_rowne(original.sciezka, decoded.sciezka));
    assert(decoded.tryb == TrybSkalowaniaObrazu::Rozciagnij);
    const uint32_t source[4] = {0xff000001, 0xff000002, 0xff000003, 0xff000004};
    uint32_t dest[24]{};
    assert(skaluj_obraz(source, 2, 2, sizeof(source), dest, 6, 4, 6,
                        sizeof(dest), TrybSkalowaniaObrazu::Rozciagnij, 0));
    assert(dest[0] == source[0] && dest[5] == source[1] &&
           dest[18] == source[2] && dest[23] == source[3]);
}
