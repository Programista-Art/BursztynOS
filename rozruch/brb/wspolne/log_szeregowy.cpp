#include "log_szeregowy.h"

namespace brb {

namespace {
// Przesuniecia rejestrow UART 16550 wzgledem portu bazowego.
constexpr uint16_t THR = 0, DLL = 0, IER = 1, DLM = 1, FCR = 2, LCR = 3, MCR = 4, LSR = 5, SCR = 7;
constexpr uint8_t LCR_DLAB = 0x80, LCR_8N1 = 0x03, LSR_THRE = 0x20;
}  // namespace

void LogSzeregowy::inicjalizuj(const PortyWeWy& porty, uint16_t baza) {
    porty_ = porty;
    baza_ = baza;
    zainicjowal_uart_ = false;
    wyslane_ = 0;
    if (!porty_.czytaj || !porty_.zapisz) { stan_ = StanPortu::Nieobecny; return; }
    auto we = [&](uint16_t r) { return porty_.czytaj(porty_.kontekst, uint16_t(baza_ + r)); };
    auto wy = [&](uint16_t r, uint8_t v) { porty_.zapisz(porty_.kontekst, uint16_t(baza_ + r), v); };

    // 1. Pusta szyna: wszystkie bity LSR ustawione.
    if (we(LSR) == 0xFF) { stan_ = StanPortu::Nieobecny; return; }
    // 2. Test rejestru SCR (16450/16550): zapis wzorca i odczyt; przywrocenie poprzedniej wartosci.
    const uint8_t stary_scr = we(SCR);
    wy(SCR, 0x5A);
    const bool scr_ok1 = we(SCR) == 0x5A;
    wy(SCR, 0xA5);
    const bool scr_ok2 = we(SCR) == 0xA5;
    wy(SCR, stary_scr);
    if (!scr_ok1 || !scr_ok2) { stan_ = StanPortu::Nieobecny; return; }

    // 3. Odczyt dzielnika (DLAB=1 tylko na czas odczytu) i formatu slowa.
    const uint8_t lcr = we(LCR);
    wy(LCR, uint8_t(lcr | LCR_DLAB));
    const uint16_t dzielnik = uint16_t(we(DLL) | (uint16_t(we(DLM)) << 8));
    wy(LCR, uint8_t(lcr & ~LCR_DLAB));
    if (dzielnik == 0 || (lcr & 0x03) != 0x03) {
        // Firmware nie skonfigurowalo portu - konfiguracja jak serial_inicjalizuj() w jadrze (38400 8N1).
        wy(IER, 0x00);
        wy(LCR, LCR_DLAB);
        wy(DLL, 0x03);
        wy(DLM, 0x00);
        wy(LCR, LCR_8N1);
        wy(FCR, 0xC7);
        wy(MCR, 0x0B);
        zainicjowal_uart_ = true;
    }
    stan_ = StanPortu::Gotowy;
}

void LogSzeregowy::bajt(uint8_t b) {
    if (stan_ != StanPortu::Gotowy) return;
    for (uint32_t proba = 0; proba < LIMIT_PROB_NADAJNIKA; ++proba) {
        const uint8_t lsr = porty_.czytaj(porty_.kontekst, uint16_t(baza_ + LSR));
        if (lsr == 0xFF) break;               // urzadzenie zniknelo
        if (lsr & LSR_THRE) {
            porty_.zapisz(porty_.kontekst, uint16_t(baza_ + THR), b);
            ++wyslane_;
            return;
        }
    }
    stan_ = StanPortu::Wylaczony;             // nadajnik nie odpowiada: koniec logowania, bez blokady
}

void LogSzeregowy::znak(char c) {
    const uint8_t b = static_cast<uint8_t>(c);
    if (b == '\n') { bajt('\r'); bajt('\n'); return; }
    bajt((b >= 0x20 && b <= 0x7E) ? b : uint8_t('?'));
}

void LogSzeregowy::tekst(const char* t) {
    if (!t) return;
    for (size_t i = 0; t[i] != '\0' && i < 4096; ++i) znak(t[i]);
}

void LogSzeregowy::linia(const char* t) {
    tekst(t);
    znak('\n');
}

void LogSzeregowy::hex(uint64_t wartosc) {
    static constexpr char cyfry[] = "0123456789ABCDEF";
    znak('0');
    znak('x');
    for (int i = 15; i >= 0; --i) znak(cyfry[(wartosc >> (i * 4)) & 0xF]);
}

void LogSzeregowy::dziesietnie(uint64_t wartosc) {
    char bufor[20];
    int n = 0;
    do {
        bufor[n++] = char('0' + wartosc % 10);
        wartosc /= 10;
    } while (wartosc != 0 && n < 20);
    while (n > 0) znak(bufor[--n]);
}

}  // namespace brb
