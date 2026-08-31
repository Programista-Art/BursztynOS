#pragma once

#include <stdint.h>
#include "typy.h"

namespace BursztynUI {

namespace Kolory {
    // Paleta Bursztynowa (Amber Gold Theme)
    inline constexpr uint32_t BursztynowyCiemnyTlo    = 0x001A0B00U;
    inline constexpr uint32_t PowierzchniaTlo         = 0x00280F00U;
    inline constexpr uint32_t PowierzchniaAlternatywna= 0x00391B02U;
    inline constexpr uint32_t PowierzchniaNajechana   = 0x004A2605U;
    inline constexpr uint32_t PowierzchniaAktywna     = 0x00603800U;
    inline constexpr uint32_t WejscieTlo              = 0x00120700U;

    inline constexpr uint32_t AkcentZloty             = 0x00FFBF00U;
    inline constexpr uint32_t AkcentNajechany         = 0x00FFCF40U;
    inline constexpr uint32_t AkcentWcisniety         = 0x00E58A00U;
    inline constexpr uint32_t AkcentRamka             = 0x00794400U;
    inline constexpr uint32_t AkcentFokus             = 0x00FFBF00U;

    inline constexpr uint32_t TekstBialy              = 0x00FFFFFFU;
    inline constexpr uint32_t TekstWyciszony          = 0x00D1D5DBU;
    inline constexpr uint32_t TekstCiemny             = 0x001A0B00U;
    inline constexpr uint32_t TekstZastepczy          = 0x008A7060U;

    inline constexpr uint32_t RamkaDomyslna           = 0x00794400U;
    inline constexpr uint32_t RamkaNajechana          = 0x00A05A00U;
    inline constexpr uint32_t RamkaFokus              = 0x00FFBF00U;

    inline constexpr uint32_t PoleWyboruZaznaczenie   = 0x00FFBF00U;
    inline constexpr uint32_t CzerwonyZamknij         = 0x00AA0000U;
    inline constexpr uint32_t CzerwonyZamknijNajechany= 0x00CC2222U;

    inline constexpr uint32_t WylaczonyTlo            = 0x00222222U;
    inline constexpr uint32_t WylaczonyTekst          = 0x00666666U;
    inline constexpr uint32_t WylaczonyRamka          = 0x00444444U;
}

struct StylStanu {
    uint32_t tlo{Kolory::PowierzchniaTlo};
    uint32_t tekst{Kolory::TekstBialy};
    uint32_t ramka{Kolory::RamkaDomyslna};
    int grubosc_ramki{1};
    int promien_zaokraglenia{0};
};

struct StylKontrolki {
    StylStanu normalny;
    StylStanu najechany;
    StylStanu wcisniety;
    StylStanu fokus;
    StylStanu wylaczony;
    Marginesy wypelnienie{4, 4, 4, 4};
};

class Motyw {
public:
    StylKontrolki styl_okna;
    StylKontrolki styl_panelu;
    StylKontrolki styl_etykiety;
    StylKontrolki styl_przycisku;
    StylKontrolki styl_pola_wyboru;
    StylKontrolki styl_pola_tekstowego;

    static Motyw& pobierz_domyslny() {
        static Motyw instancja;
        return instancja;
    }

    Motyw() {
        zainicjalizuj_bursztynowy();
    }

private:
    void zainicjalizuj_bursztynowy() {
        // Styl Okna
        styl_okna.normalny.tlo = Kolory::BursztynowyCiemnyTlo;
        styl_okna.normalny.tekst = Kolory::TekstBialy;
        styl_okna.normalny.ramka = Kolory::RamkaDomyslna;
        styl_okna.normalny.grubosc_ramki = 1;

        // Styl Panelu
        styl_panelu.normalny.tlo = Kolory::PowierzchniaTlo;
        styl_panelu.normalny.tekst = Kolory::TekstBialy;
        styl_panelu.normalny.ramka = Kolory::RamkaDomyslna;
        styl_panelu.normalny.grubosc_ramki = 1;
        styl_panelu.wypelnienie = Marginesy(8, 8, 8, 8);

        // Styl Etykiety
        styl_etykiety.normalny.tlo = 0; // Przezroczysty
        styl_etykiety.normalny.tekst = Kolory::TekstBialy;
        styl_etykiety.normalny.ramka = 0;
        styl_etykiety.normalny.grubosc_ramki = 0;
        styl_etykiety.wypelnienie = Marginesy(2, 2, 2, 2);

        // Styl Przycisku
        styl_przycisku.normalny.tlo = Kolory::AkcentWcisniety; // 0x00E58A00
        styl_przycisku.normalny.tekst = Kolory::TekstCiemny;   // 0x001A0B00
        styl_przycisku.normalny.ramka = Kolory::RamkaDomyslna;
        styl_przycisku.normalny.grubosc_ramki = 1;
        styl_przycisku.normalny.promien_zaokraglenia = 2;

        styl_przycisku.najechany.tlo = Kolory::AkcentNajechany;
        styl_przycisku.najechany.tekst = Kolory::TekstCiemny;
        styl_przycisku.najechany.ramka = Kolory::RamkaNajechana;
        styl_przycisku.najechany.grubosc_ramki = 1;
        styl_przycisku.najechany.promien_zaokraglenia = 2;

        styl_przycisku.wcisniety.tlo = Kolory::AkcentZloty;
        styl_przycisku.wcisniety.tekst = Kolory::TekstCiemny;
        styl_przycisku.wcisniety.ramka = Kolory::RamkaFokus;
        styl_przycisku.wcisniety.grubosc_ramki = 1;
        styl_przycisku.wcisniety.promien_zaokraglenia = 2;

        styl_przycisku.fokus.tlo = Kolory::AkcentWcisniety;
        styl_przycisku.fokus.tekst = Kolory::TekstCiemny;
        styl_przycisku.fokus.ramka = Kolory::RamkaFokus;
        styl_przycisku.fokus.grubosc_ramki = 2;
        styl_przycisku.fokus.promien_zaokraglenia = 2;

        styl_przycisku.wylaczony.tlo = Kolory::WylaczonyTlo;
        styl_przycisku.wylaczony.tekst = Kolory::WylaczonyTekst;
        styl_przycisku.wylaczony.ramka = Kolory::WylaczonyRamka;
        styl_przycisku.wypelnienie = Marginesy(10, 6, 10, 6);

        // Styl Pola Wyboru
        styl_pola_wyboru.normalny.tlo = Kolory::WejscieTlo;
        styl_pola_wyboru.normalny.tekst = Kolory::TekstBialy;
        styl_pola_wyboru.normalny.ramka = Kolory::RamkaDomyslna;
        styl_pola_wyboru.normalny.grubosc_ramki = 1;

        styl_pola_wyboru.najechany.tlo = Kolory::PowierzchniaNajechana;
        styl_pola_wyboru.najechany.tekst = Kolory::TekstBialy;
        styl_pola_wyboru.najechany.ramka = Kolory::RamkaNajechana;
        styl_pola_wyboru.najechany.grubosc_ramki = 1;

        styl_pola_wyboru.fokus.tlo = Kolory::WejscieTlo;
        styl_pola_wyboru.fokus.tekst = Kolory::TekstBialy;
        styl_pola_wyboru.fokus.ramka = Kolory::RamkaFokus;
        styl_pola_wyboru.fokus.grubosc_ramki = 2;
        styl_pola_wyboru.wypelnienie = Marginesy(4, 4, 4, 4);

        // Styl Pola Tekstowego
        styl_pola_tekstowego.normalny.tlo = Kolory::WejscieTlo;
        styl_pola_tekstowego.normalny.tekst = Kolory::TekstBialy;
        styl_pola_tekstowego.normalny.ramka = Kolory::RamkaDomyslna;
        styl_pola_tekstowego.normalny.grubosc_ramki = 1;

        styl_pola_tekstowego.najechany.tlo = Kolory::WejscieTlo;
        styl_pola_tekstowego.najechany.tekst = Kolory::TekstBialy;
        styl_pola_tekstowego.najechany.ramka = Kolory::RamkaNajechana;
        styl_pola_tekstowego.najechany.grubosc_ramki = 1;

        styl_pola_tekstowego.fokus.tlo = Kolory::WejscieTlo;
        styl_pola_tekstowego.fokus.tekst = Kolory::TekstBialy;
        styl_pola_tekstowego.fokus.ramka = Kolory::RamkaFokus;
        styl_pola_tekstowego.fokus.grubosc_ramki = 2;

        styl_pola_tekstowego.wylaczony.tlo = Kolory::WylaczonyTlo;
        styl_pola_tekstowego.wylaczony.tekst = Kolory::WylaczonyTekst;
        styl_pola_tekstowego.wylaczony.ramka = Kolory::WylaczonyRamka;
        styl_pola_tekstowego.wypelnienie = Marginesy(6, 4, 6, 4);
    }
};

} // namespace BursztynUI
