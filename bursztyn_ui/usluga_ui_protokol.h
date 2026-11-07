#pragma once

// Zalążek protokołu Ring 3 dla usług UI z docs/11_audyt_bws_nowoczesne.md
// (dialogi systemowe / powiadomienia). Transportem pozostaje istniejące BWS IPC
// (68–71, max 512 B). Brak renderera w jądrze — broker i UI są w userspace.
// Nie jest wciągane przez bursztyn_ui.h, żeby hostowe testy kontrolek nie
// zależały od nagłówków BWS.

#include <stdint.h>

namespace BursztynUI {
namespace UslugaUi {

static constexpr uint32_t PROTOKOL_WERSJA = 1;
static constexpr uint32_t MAGIA = 0x42555349u; // 'BUSI'
static constexpr uint32_t MAX_IPC_DANYCH = 512U; // == BWS_IPC_MAX_DANYCH

enum class Rodzaj : uint32_t {
    PowiadomieniePokaz = 1,
    PowiadomienieZamknij = 2,
    DialogOtworz = 3,
    DialogZapisz = 4,
    DialogFolder = 5,
    DialogWiadomosc = 6,
    Odpowiedz = 100,
};

enum class KodOdpowiedzi : uint32_t {
    Ok = 0,
    Anulowano = 1,
    Blad = 2,
    Nieobslugiwane = 3,
};

#pragma pack(push, 1)
struct Naglowek {
    uint32_t magia;
    uint32_t wersja;
    uint32_t rodzaj;
    uint32_t request_id;
};

struct PowiadomienieZadanie {
    Naglowek nag;
    uint32_t typ;
    uint32_t timeout_ms;
    char tytul[64];
    char tresc[256];
};

struct DialogZadanie {
    Naglowek nag;
    uint32_t flagi;
    char tytul[64];
    char sciezka_start[256];
};

struct OdpowiedzWiadomosc {
    Naglowek nag;
    uint32_t kod;
    char sciezka[256];
};
#pragma pack(pop)

static_assert(sizeof(PowiadomienieZadanie) <= MAX_IPC_DANYCH,
              "notification IPC payload must fit BWS IPC message");
static_assert(sizeof(DialogZadanie) <= MAX_IPC_DANYCH,
              "dialog IPC payload must fit BWS IPC message");
static_assert(sizeof(OdpowiedzWiadomosc) <= MAX_IPC_DANYCH,
              "reply IPC payload must fit BWS IPC message");

inline void wypelnij_naglowek(Naglowek* n, Rodzaj rodzaj, uint32_t id) {
    if (!n) return;
    n->magia = MAGIA;
    n->wersja = PROTOKOL_WERSJA;
    n->rodzaj = static_cast<uint32_t>(rodzaj);
    n->request_id = id;
}

inline bool naglowek_poprawny(const Naglowek* n) {
    return n && n->magia == MAGIA && n->wersja == PROTOKOL_WERSJA;
}

} // namespace UslugaUi
} // namespace BursztynUI
