#pragma once

#include <stdint.h>
#include "typy.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

enum class StanWizualny {
    Normalny,
    Najechany,
    Wcisniety,
    Fokus,
    Wylaczony,
    TylkoDoOdczytu
};

namespace Kolory {
    // Paleta autorytatywna Bursztyn Nocny (Bursztyn OS v2)
    inline constexpr uint32_t Pulpit                = 0x00111315U;
    inline constexpr uint32_t OknoTlo               = 0x00191C20U;
    inline constexpr uint32_t Panel                 = 0x0020242AU;
    inline constexpr uint32_t PanelPodniesiony      = 0x00262B31U;
    inline constexpr uint32_t Ramka                 = 0x00343A42U;
    inline constexpr uint32_t Tekst                 = 0x00F2F3F5U;
    inline constexpr uint32_t TekstDrugorzedny      = 0x00A7ADB5U;
    inline constexpr uint32_t TekstWylaczony        = 0x006F7680U;
    inline constexpr uint32_t Akcent                = 0x00F2A900U;
    inline constexpr uint32_t AkcentNajechany       = 0x00FFBC32U;
    inline constexpr uint32_t AkcentAktywny         = 0x00D99100U;
    inline constexpr uint32_t Blad                  = 0x00E85D5DU;
    inline constexpr uint32_t Sukces                = 0x0051B87AU;
    inline constexpr uint32_t Ostrzezenie           = 0x00F2A900U;
    inline constexpr uint32_t Informacja            = 0x0058A6E7U;
    inline constexpr uint32_t PoleTekstowe          = 0x0015181CU;
    inline constexpr uint32_t Zaznaczenie           = 0x00F2A900U;
    inline constexpr uint32_t Przycisk              = 0x00262B31U;
    inline constexpr uint32_t PrzyciskNajechany     = 0x00343A42U;
    inline constexpr uint32_t TekstAkcentu          = 0x00111315U;

    // Aliases wstecznej zgodności
    inline constexpr uint32_t BursztynowyCiemnyTlo  = Pulpit;
    inline constexpr uint32_t PowierzchniaTlo       = OknoTlo;
    inline constexpr uint32_t PowierzchniaAlternatywna = Panel;
    inline constexpr uint32_t PowierzchniaNajechana = PanelPodniesiony;
    inline constexpr uint32_t PowierzchniaAktywna   = PanelPodniesiony;
    inline constexpr uint32_t WejscieTlo            = PoleTekstowe;
    inline constexpr uint32_t AkcentZloty           = Akcent;
    inline constexpr uint32_t AkcentWcisniety       = AkcentAktywny;
    inline constexpr uint32_t AkcentRamka           = Ramka;
    inline constexpr uint32_t AkcentFokus           = AkcentNajechany;
    inline constexpr uint32_t TekstBialy            = Tekst;
    inline constexpr uint32_t TekstWyciszony        = TekstDrugorzedny;
    inline constexpr uint32_t TekstCiemny           = Pulpit;
    inline constexpr uint32_t TekstZastepczy        = TekstWylaczony;
    inline constexpr uint32_t RamkaDomyslna         = Ramka;
    inline constexpr uint32_t RamkaNajechana        = Akcent;
    inline constexpr uint32_t RamkaFokus            = AkcentNajechany;
    inline constexpr uint32_t PoleWyboruZaznaczenie = Zaznaczenie;
    inline constexpr uint32_t CzerwonyZamknij       = Blad;
    inline constexpr uint32_t CzerwonyZamknijNajechany = 0x00F07070U;
    inline constexpr uint32_t WylaczonyTlo          = 0x0020242AU;
    inline constexpr uint32_t WylaczonyTekst        = 0x009E9E9EU;
    inline constexpr uint32_t WylaczonyRamka        = 0x00343A42U;
    inline constexpr uint32_t ZaznaczenieTlo        = Zaznaczenie;
    inline constexpr uint32_t ZaznaczenieTekst      = TekstAkcentu;
    inline constexpr uint32_t NieokreslonyKolor     = Akcent;
}

struct StylStanu {
    uint32_t tlo{Kolory::PowierzchniaTlo};
    uint32_t tekst{Kolory::TekstBialy};
    uint32_t ramka{Kolory::RamkaDomyslna};
    int grubosc_ramki{1};
    int promien_zaokraglenia{0};
    uint8_t przezroczystosc{255};
    bool wlasne_tlo{false};
    bool wlasny_tekst{false};
    bool wlasna_ramka{false};
};

struct StylKontrolki {
    StylStanu normalny;
    StylStanu najechany;
    StylStanu wcisniety;
    StylStanu fokus;
    StylStanu wylaczony;
    StylStanu tylko_do_odczytu;
    Marginesy wypelnienie{4, 4, 4, 4};
    Marginesy margines{0, 0, 0, 0};
};

class Motyw {
public:
    StylKontrolki styl_okna;
    StylKontrolki styl_panelu;
    StylKontrolki styl_etykiety;
    StylKontrolki styl_przycisku;
    StylKontrolki styl_pola_wyboru;
    StylKontrolki styl_pola_tekstowego;
    StylKontrolki styl_suwaka;
    StylKontrolki styl_menu;
    StylKontrolki styl_podpowiedzi;

    static Motyw& pobierz_domyslny() {
        static Motyw instancja;
        return instancja;
    }

    Motyw() {
        zaktualizuj_z_systemu();
    }

    void zaktualizuj_z_systemu() {
        motyw_odswiez_lokalny();
        const BursztynMotyw* m = gui_pobierz_motyw();

        uint32_t tlo_okna = motyw_kolor(MOTYW_KOLOR_OKNOTLO);
        uint32_t tlo_panelu = motyw_kolor(MOTYW_KOLOR_PANEL);
        uint32_t tlo_podniesiony = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        uint32_t ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        uint32_t tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
        uint32_t tekst_drugi = motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI);
        uint32_t wylaczony = motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
        uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
        uint32_t akcent_najechany = motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY);
        uint32_t akcent_aktywny = motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY);
        uint32_t poletekstowe = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        uint32_t zaznaczenie = motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE);
        uint32_t przycisk_tlo = motyw_kolor(MOTYW_KOLOR_PRZYCISK);
        uint32_t przycisk_najechany = motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY);
        uint32_t tekst_akcentu = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);

        // Styl Okna
        styl_okna.normalny.tlo = tlo_okna;
        styl_okna.normalny.tekst = tekst;
        styl_okna.normalny.ramka = ramka;
        styl_okna.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_okna.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_OKNO);

        // Styl Panelu
        styl_panelu.normalny.tlo = tlo_panelu;
        styl_panelu.normalny.tekst = tekst;
        styl_panelu.normalny.ramka = ramka;
        styl_panelu.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_panelu.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        styl_panelu.wypelnienie = Marginesy(8, 8, 8, 8);

        // Styl Etykiety
        styl_etykiety.normalny.tlo = 0;
        styl_etykiety.normalny.tekst = tekst;
        styl_etykiety.normalny.ramka = 0;
        styl_etykiety.normalny.grubosc_ramki = 0;
        styl_etykiety.wypelnienie = Marginesy(2, 2, 2, 2);

        // Styl Przycisku
        styl_przycisku.normalny.tlo = przycisk_tlo;
        styl_przycisku.normalny.tekst = tekst;
        styl_przycisku.normalny.ramka = ramka;
        styl_przycisku.normalny.grubosc_ramki = m->gruboscramki;
        styl_przycisku.normalny.promien_zaokraglenia = m->rogprzycisku;

        styl_przycisku.najechany.tlo = przycisk_najechany;
        styl_przycisku.najechany.tekst = tekst;
        styl_przycisku.najechany.ramka = akcent_najechany;
        styl_przycisku.najechany.grubosc_ramki = m->gruboscramki;
        styl_przycisku.najechany.promien_zaokraglenia = m->rogprzycisku;

        styl_przycisku.wcisniety.tlo = akcent_aktywny;
        styl_przycisku.wcisniety.tekst = tekst_akcentu;
        styl_przycisku.wcisniety.ramka = akcent_aktywny;
        styl_przycisku.wcisniety.grubosc_ramki = m->gruboscramki;
        styl_przycisku.wcisniety.promien_zaokraglenia = m->rogprzycisku;

        styl_przycisku.fokus.tlo = przycisk_tlo;
        styl_przycisku.fokus.tekst = tekst;
        styl_przycisku.fokus.ramka = akcent;
        styl_przycisku.fokus.grubosc_ramki = m->gruboscramki;
        styl_przycisku.fokus.promien_zaokraglenia = m->rogprzycisku;

        styl_przycisku.wylaczony.tlo = tlo_panelu;
        styl_przycisku.wylaczony.tekst = wylaczony;
        styl_przycisku.wylaczony.ramka = wylaczony;
        styl_przycisku.wypelnienie = Marginesy(10, 6, 10, 6);

        // Styl Pola Wyboru
        styl_pola_wyboru.normalny.tlo = poletekstowe;
        styl_pola_wyboru.normalny.tekst = tekst;
        styl_pola_wyboru.normalny.ramka = ramka;
        styl_pola_wyboru.normalny.grubosc_ramki = m->gruboscramki;
        styl_pola_wyboru.normalny.promien_zaokraglenia = m->rogpola;

        styl_pola_wyboru.najechany.tlo = tlo_podniesiony;
        styl_pola_wyboru.najechany.tekst = tekst;
        styl_pola_wyboru.najechany.ramka = akcent_najechany;
        styl_pola_wyboru.najechany.grubosc_ramki = m->gruboscramki;
        styl_pola_wyboru.najechany.promien_zaokraglenia = m->rogpola;

        styl_pola_wyboru.fokus.tlo = poletekstowe;
        styl_pola_wyboru.fokus.tekst = tekst;
        styl_pola_wyboru.fokus.ramka = akcent;
        styl_pola_wyboru.fokus.grubosc_ramki = m->gruboscramki;
        styl_pola_wyboru.fokus.promien_zaokraglenia = m->rogpola;
        styl_pola_wyboru.wypelnienie = Marginesy(4, 4, 4, 4);

        // Styl Pola Tekstowego
        styl_pola_tekstowego.normalny.tlo = poletekstowe;
        styl_pola_tekstowego.normalny.tekst = tekst;
        styl_pola_tekstowego.normalny.ramka = ramka;
        styl_pola_tekstowego.normalny.grubosc_ramki = m->gruboscramki;
        styl_pola_tekstowego.normalny.promien_zaokraglenia = m->rogpola;

        styl_pola_tekstowego.najechany.tlo = poletekstowe;
        styl_pola_tekstowego.najechany.tekst = tekst;
        styl_pola_tekstowego.najechany.ramka = akcent_najechany;
        styl_pola_tekstowego.najechany.grubosc_ramki = m->gruboscramki;
        styl_pola_tekstowego.najechany.promien_zaokraglenia = m->rogpola;

        styl_pola_tekstowego.fokus.tlo = poletekstowe;
        styl_pola_tekstowego.fokus.tekst = tekst;
        styl_pola_tekstowego.fokus.ramka = akcent;
        styl_pola_tekstowego.fokus.grubosc_ramki = m->gruboscramki;
        styl_pola_tekstowego.fokus.promien_zaokraglenia = m->rogpola;

        styl_pola_tekstowego.wylaczony.tlo = tlo_panelu;
        styl_pola_tekstowego.wylaczony.tekst = wylaczony;
        styl_pola_tekstowego.wylaczony.ramka = wylaczony;
        styl_pola_tekstowego.wylaczony.grubosc_ramki = m->gruboscramki;

        styl_pola_tekstowego.tylko_do_odczytu.tlo = tlo_panelu;
        styl_pola_tekstowego.tylko_do_odczytu.tekst = tekst_drugi;
        styl_pola_tekstowego.tylko_do_odczytu.ramka = ramka;
        styl_pola_tekstowego.tylko_do_odczytu.grubosc_ramki = m->gruboscramki;
        styl_pola_tekstowego.wypelnienie = Marginesy(6, 4, 6, 4);

        // Styl Suwaka
        styl_suwaka.normalny.tlo = 0;
        styl_suwaka.normalny.tekst = tekst;
        styl_suwaka.normalny.ramka = ramka;
        styl_suwaka.normalny.grubosc_ramki = m->gruboscramki;
        styl_suwaka.najechany = styl_suwaka.normalny;
        styl_suwaka.najechany.ramka = akcent_najechany;
        styl_suwaka.wcisniety = styl_suwaka.normalny;
        styl_suwaka.wcisniety.ramka = akcent_aktywny;
        styl_suwaka.fokus = styl_suwaka.normalny;
        styl_suwaka.fokus.ramka = akcent;
        styl_suwaka.wylaczony = styl_suwaka.normalny;
        styl_suwaka.wylaczony.tekst = wylaczony;
        styl_suwaka.wylaczony.ramka = wylaczony;
        styl_suwaka.tylko_do_odczytu = styl_suwaka.wylaczony;
        styl_suwaka.wypelnienie = Marginesy(4, 4, 4, 4);

        // Styl Menu
        styl_menu.normalny.tlo = tlo_panelu;
        styl_menu.normalny.tekst = tekst;
        styl_menu.normalny.ramka = ramka;
        styl_menu.normalny.grubosc_ramki = m->gruboscramki;
        styl_menu.normalny.promien_zaokraglenia = m->rogmenu;
        styl_menu.najechany = styl_menu.normalny;
        styl_menu.najechany.tlo = tlo_podniesiony;
        styl_menu.najechany.tekst = akcent;

        // Styl Podpowiedzi (Tooltip)
        styl_podpowiedzi.normalny.tlo = tlo_podniesiony;
        styl_podpowiedzi.normalny.tekst = tekst;
        styl_podpowiedzi.normalny.ramka = ramka;
        styl_podpowiedzi.normalny.grubosc_ramki = m->gruboscramki;
        styl_podpowiedzi.normalny.promien_zaokraglenia = m->rogpodpowiedzi;
        styl_podpowiedzi.wypelnienie = Marginesy(6, 4, 6, 4);
    }
};

} // namespace BursztynUI
