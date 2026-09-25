#pragma once

// Publiczny ABI dekodera BTP1/BTP2: jadro, aplikacje i testy hostowe.
#include <stdint.h>
#include <stddef.h>

struct BtpNaglowek {
    uint8_t magic[4];
    uint32_t szerokosc;
    uint32_t wysokosc;
    uint8_t bpp;
    uint8_t kompresja;
} __attribute__((packed));

static_assert(sizeof(BtpNaglowek) == 14, "BTP1: naglowek ma 14 bajtow");
static_assert(alignof(BtpNaglowek) == 1, "BTP1: naglowek bez wyrownania");
static_assert(offsetof(BtpNaglowek, magic) == 0, "BTP1: offset magic");
static_assert(offsetof(BtpNaglowek, szerokosc) == 4, "BTP1: offset szerokosci");
static_assert(offsetof(BtpNaglowek, wysokosc) == 8, "BTP1: offset wysokosci");
static_assert(offsetof(BtpNaglowek, bpp) == 12, "BTP1: offset bpp");
static_assert(offsetof(BtpNaglowek, kompresja) == 13, "BTP1: offset kompresji");

enum BtpWynik : int32_t {
    BTPOK = 0,
    BTPBLADARGUMENTU = -1,       // Null, zle wyrownanie lub nakladanie buforow.
    BTPBLADZAMALYPLIK = -2,     // Niepelny naglowek.
    BTPBLADMAGIC = -3,
    BTPBLADWYMIAROW = -4,       // Zero, limit lub obraz wiekszy od celu.
    BTPBLADBPP = -5,
    BTPBLADKOMPRESJI = -6,      // Obslugiwane tylko RAW (0) i RLE (1).
    BTPBLADRLE = -7,            // Zly pakiet, niepelny obraz lub nadmiar danych.
    BTPBLADPRZEPELNIENIA = -8,  // Rozmiar lub zakres adresow niereprezentowalny.
    BTPBLADBUFORA = -9,         // Za maly stride lub pojemnosc celu.
    BTPBLADRAW = -10,           // Niepelny lub nadmiarowy payload RAW.
    BTPBLADV2 = -11,            // Niepoprawny naglowek, chunk, token lub CRC BTP2.
};


struct BtpInfo {
    uint32_t szerokosc;
    uint32_t wysokosc;
    uint8_t kompresja;
};

// Tylko naglowek: do ustalenia bounded rozmiaru celu; payload pozostaje niezaufany.
BtpWynik btpnaglowek(const uint8_t*, size_t, BtpInfo*) noexcept;

// Walidacja struktury i payload CRC BTP2; decoded CRC jest weryfikowany przy dekodowaniu.
BtpWynik btpsprawdzplik(const uint8_t*, size_t, BtpInfo*) noexcept;
BtpWynik btpwczytajirysuj(const uint8_t*, size_t, uint32_t*, uint32_t,
                         uint32_t, uint32_t, size_t) noexcept;
