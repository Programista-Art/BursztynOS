/*
 * BRB - lekki dziennik na porcie szeregowym COM1 (UART 16550, port bazowy 0x3F8).
 *
 * Zgodny z konwencja jadra (jadro/rdzen/kernel.cpp: serial_inicjalizuj - COM1, 8N1, linie ASCII
 * z prefiksem w nawiasach), ale bezpieczny na komputerach BEZ dzialajacego COM1:
 *   - wykrycie obecnosci: rejestr LSR = 0xFF (pusta szyna ISA) albo nieudany test rejestru SCR -> port
 *     uznany za nieobecny, zadnych dalszych zapisow,
 *   - BRB nie zmienia predkosci ustawionej przez firmware (firmware moze uzywac COM1 jako konsoli);
 *     inicjalizuje UART (38400 8N1 jak jadro) tylko wtedy, gdy dzielnik = 0 albo format slowa != 8 bitow,
 *   - kazdy bajt czeka na THRE (LSR bit 5) najwyzej LIMIT_PROB_NADAJNIKA odczytow; przekroczenie ->
 *     port wylaczony do konca dzialania (lagodny powrot, bez blokowania).
 * Dostep do portow idzie przez interfejs PortyWeWy, wiec testy hosta podstawiaja model UART i nie
 * wykonuja instrukcji in/out.
 */
#ifndef BRB_LOG_SZEREGOWY_H
#define BRB_LOG_SZEREGOWY_H

#include <stddef.h>
#include <stdint.h>

namespace brb {

struct PortyWeWy {
    uint8_t (*czytaj)(void* kontekst, uint16_t port);
    void (*zapisz)(void* kontekst, uint16_t port, uint8_t wartosc);
    void* kontekst;
};

enum class StanPortu : uint8_t { Niezainicjowany, Gotowy, Nieobecny, Wylaczony };

class LogSzeregowy {
public:
    static constexpr uint16_t COM1 = 0x3F8;
    static constexpr uint32_t LIMIT_PROB_NADAJNIKA = 100000;  // ~ kilkadziesiat ms na realnym sprzecie

    void inicjalizuj(const PortyWeWy& porty, uint16_t baza = COM1);
    void znak(char c);
    void tekst(const char* t);            // ASCII; bajty spoza 0x20..0x7E (poza \n) -> '?'
    void linia(const char* t);            // tekst + CR LF
    void hex(uint64_t wartosc);           // "0x" + 16 cyfr
    void dziesietnie(uint64_t wartosc);

    StanPortu stan() const { return stan_; }
    bool zainicjowal_uart() const { return zainicjowal_uart_; }
    uint64_t wyslane() const { return wyslane_; }

private:
    PortyWeWy porty_{};
    uint16_t baza_ = COM1;
    StanPortu stan_ = StanPortu::Niezainicjowany;
    bool zainicjowal_uart_ = false;
    uint64_t wyslane_ = 0;
    void bajt(uint8_t b);
};

}  // namespace brb

#endif
