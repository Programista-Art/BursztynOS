// Interakcja punktu kontrolnego audio z produkcyjna petla oczekiwania AHCI (uruchom_i_czekaj_na_komende).
// Sterownik jest kompilowany w calosci (ahci.cpp), port to pamiec z emulacja rejestrow W1C (PxIS/PxSERR), a
// "urzadzenie" konczy komende z wnetrza punktu kontrolnego (g_host_punkt) - dokladnie miedzy iteracjami petli.
// Sprawdzane: ukonczenie / blad TFES / blad TFD / timeout maja te sama semantyke co bez audio, punkt jest wolany
// w kazdej iteracji, praca audio nie zuzywa prob urzadzenia (limit TIMEOUT_COMMAND), zagniezdzenie jest odrzucane.
#define BURSZTYN_PUNKT_AUDIO_HOST_TEST 1
#define BURSZTYN_AHCI_HOST_TEST 1
#include "../sterowniki/magazyn/ahci.cpp"
#include <cstdio>
#include <cstdlib>

static unsigned g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

// Zaleznosci linkera sterownika (nieuzywane przez badana funkcje).
void wypisz_log(const char*) {}
extern "C" uint32_t pci_odczytaj_dword(uint8_t, uint8_t, uint8_t, uint8_t) { return 0xFFFFFFFFU; }
extern "C" void pci_zapisz_dword(uint8_t, uint8_t, uint8_t, uint8_t, uint32_t) {}
bool blokowy_zarejestruj_dysk(Storage::Device*) { return false; }
bool CzyZakresWMapowaniuFizycznym(uint64_t, size_t) { return false; }
void* FizycznyZakresDoWirtualnego(uint64_t, size_t) { return nullptr; }
void* ZaalokujRamke() { return nullptr; }
void ZwolnijRamke(void*) {}
void ZmapujStrone(void*, void*, uint32_t) {}

namespace {
using PunktKontrolnyAudio::Wynik;

PortAHCI g_port{};
uint64_t g_punkty = 0;          // wywolania punktu (= iteracje bez ukonczenia)
uint64_t g_ukoncz_po = 0;       // "urzadzenie" konczy komende przy tym wywolaniu punktu
uint32_t g_tfd_po = 0, g_is_po = 0;
uint64_t g_praca = 0;

struct SrodowiskoTestu {
    bool praca_oczekuje() const { return true; }   // zegar publikuje prace w kazdej iteracji (najgorszy przypadek)
    bool przerwania_wlaczone() const { return true; }
    bool blokada_audio_zajeta() const { return false; }
    void wykonaj() const {
        ++g_praca;
        // Przyszla regresja: praca audio wolajaca petle z punktem - musi zostac odrzucona, bez rekursji.
        CHECK(PunktKontrolnyAudio::obsluz(*const_cast<SrodowiskoTestu*>(this), PunktKontrolnyAudio::g_statystyki,
                                          PunktKontrolnyAudio::g_w_toku) == Wynik::OdrzuconoZagniezdzenie);
    }
};

Wynik punkt_testu() {
    ++g_punkty;
    if (g_ukoncz_po && g_punkty == g_ukoncz_po) {
        g_port.tfd = g_tfd_po;
        if (g_is_po) g_port.is.ustaw_sprzetowo(g_is_po);
        if (!(g_is_po & AHCI_PXIS_TFES)) g_port.ci = 0;       // komenda zakonczona (slot zwolniony)
    }
    SrodowiskoTestu s;
    return PunktKontrolnyAudio::obsluz(s, PunktKontrolnyAudio::g_statystyki, PunktKontrolnyAudio::g_w_toku);
}

void przygotuj(uint64_t ukoncz_po, uint32_t tfd = 0, uint32_t is = 0) {
    g_port = PortAHCI{};
    g_port.is.ustaw_sprzetowo(AHCI_PXIS_TFES);   // stary bit bledu: funkcja musi go skasowac (W1C) przed startem
    g_port.serr.ustaw_sprzetowo(0x4);
    g_punkty = 0; g_praca = 0; g_ukoncz_po = ukoncz_po; g_tfd_po = tfd; g_is_po = is;
    PunktKontrolnyAudio::g_host_punkt = punkt_testu;
}
} // namespace

int main() {
    // Ukonczenie po 37 iteracjach z praca audio w kazdej: Ok, punkt w kazdej iteracji, PxIS skasowany.
    przygotuj(37);
    CHECK(uruchom_i_czekaj_na_komende(&g_port) == BlockError::Ok);
    CHECK(g_punkty == 37 && g_praca == 37 && g_port.ci == 0 && g_port.is == 0);

    // Ukonczenie juz w pierwszym punkcie (krotka komenda): jeden punkt, druga iteracja widzi wolny slot.
    przygotuj(1);
    CHECK(uruchom_i_czekaj_na_komende(&g_port) == BlockError::Ok && g_punkty == 1);

    // Task File Error zgloszony w trakcie oczekiwania: IOError (semantyka bez zmian).
    przygotuj(5, 0, AHCI_PXIS_TFES);
    CHECK(uruchom_i_czekaj_na_komende(&g_port) == BlockError::IOError && g_punkty == 5);

    // Komenda zakonczona z bledem TFD: IOError.
    przygotuj(9, AHCI_PXTFD_ERR);
    CHECK(uruchom_i_czekaj_na_komende(&g_port) == BlockError::IOError && g_punkty == 9);

    // Urzadzenie nie odpowiada: Timeout po dokladnie TIMEOUT_COMMAND probach - praca audio nie zuzywa prob
    // i nie skraca limitu, punkt wolany w kazdej probie.
    przygotuj(0);
    CHECK(uruchom_i_czekaj_na_komende(&g_port) == BlockError::Timeout);
    CHECK(g_punkty == TIMEOUT_COMMAND && g_praca == TIMEOUT_COMMAND && g_port.ci == AHCI_SLOT_BIT);

    // Port zajety (CI ustawione przez poprzednia komende): DeviceUnavailable, bez punktu.
    przygotuj(0);
    g_port.ci = AHCI_SLOT_BIT;
    CHECK(uruchom_i_czekaj_na_komende(&g_port) == BlockError::DeviceUnavailable && g_punkty == 0);

    const auto st = PunktKontrolnyAudio::statystyki();
    CHECK(st.wykonane == 37 + 1 + 5 + 9 + TIMEOUT_COMMAND && st.odrzucone_zagniezdzone == st.wykonane);
    CHECK(!PunktKontrolnyAudio::g_w_toku);
    PunktKontrolnyAudio::g_host_punkt = nullptr;
    std::printf("AHCI + punkt kontrolny audio: %u sprawdzen OK (timeout po %u probach)\n", g_checks, TIMEOUT_COMMAND);
    return 0;
}
