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

using ::motyw_kolor_jasnosc;
using ::motyw_kolor_tekstu_na_tle;
using ::motyw_kolor_tekstu_zaznaczenia;

namespace Kolory {
    struct DynamicznyKolor {
        MotywKolorRola rola;
        constexpr DynamicznyKolor(MotywKolorRola r) : rola(r) {}
        inline operator uint32_t() const { return motyw_kolor(rola); }
    };

    struct DynamicznyTekstZaznaczenia {
        inline operator uint32_t() const { return motyw_kolor_tekstu_zaznaczenia(); }
    };

    inline constexpr DynamicznyKolor Pulpit{MOTYW_KOLOR_PULPIT};
    inline constexpr DynamicznyKolor OknoTlo{MOTYW_KOLOR_OKNOTLO};
    inline constexpr DynamicznyKolor Panel{MOTYW_KOLOR_PANEL};
    inline constexpr DynamicznyKolor PanelPodniesiony{MOTYW_KOLOR_PODNIESIONY};
    inline constexpr DynamicznyKolor Ramka{MOTYW_KOLOR_RAMKA};
    inline constexpr DynamicznyKolor Tekst{MOTYW_KOLOR_TEKST};
    inline constexpr DynamicznyKolor TekstDrugorzedny{MOTYW_KOLOR_TEKST_DRUGI};
    inline constexpr DynamicznyKolor TekstWylaczony{MOTYW_KOLOR_WYLACZONY};
    inline constexpr DynamicznyKolor Akcent{MOTYW_KOLOR_AKCENT};
    inline constexpr DynamicznyKolor AkcentNajechany{MOTYW_KOLOR_AKCENT_NAJECHANY};
    inline constexpr DynamicznyKolor AkcentAktywny{MOTYW_KOLOR_AKCENT_AKTYWNY};
    inline constexpr DynamicznyKolor Blad{MOTYW_KOLOR_BLAD};
    inline constexpr DynamicznyKolor Sukces{MOTYW_KOLOR_SUKCES};
    inline constexpr DynamicznyKolor Ostrzezenie{MOTYW_KOLOR_OSTRZEZENIE};
    inline constexpr DynamicznyKolor Informacja{MOTYW_KOLOR_INFORMACJA};
    inline constexpr DynamicznyKolor PoleTekstowe{MOTYW_KOLOR_POLETEKSTOWE};
    inline constexpr DynamicznyKolor Zaznaczenie{MOTYW_KOLOR_ZAZNACZENIE};
    inline constexpr DynamicznyKolor Przycisk{MOTYW_KOLOR_PRZYCISK};
    inline constexpr DynamicznyKolor PrzyciskNajechany{MOTYW_KOLOR_PRZYCISK_NAJECHANY};
    inline constexpr DynamicznyKolor TekstAkcentu{MOTYW_KOLOR_TEKST_AKCENTU};

    // Aliases wstecznej zgodności
    inline constexpr DynamicznyKolor BursztynowyCiemnyTlo  = Pulpit;
    inline constexpr DynamicznyKolor PowierzchniaTlo       = OknoTlo;
    inline constexpr DynamicznyKolor PowierzchniaAlternatywna = Panel;
    inline constexpr DynamicznyKolor PowierzchniaNajechana = PanelPodniesiony;
    inline constexpr DynamicznyKolor PowierzchniaAktywna   = PanelPodniesiony;
    inline constexpr DynamicznyKolor WejscieTlo            = PoleTekstowe;
    inline constexpr DynamicznyKolor AkcentZloty           = Akcent;
    inline constexpr DynamicznyKolor AkcentWcisniety       = AkcentAktywny;
    inline constexpr DynamicznyKolor AkcentRamka           = Ramka;
    inline constexpr DynamicznyKolor AkcentFokus           = AkcentNajechany;
    inline constexpr DynamicznyKolor TekstBialy            = Tekst;
    inline constexpr DynamicznyKolor TekstWyciszony        = TekstDrugorzedny;
    inline constexpr DynamicznyKolor TekstCiemny           = TekstAkcentu;
    inline constexpr DynamicznyKolor TekstZastepczy        = TekstWylaczony;
    inline constexpr DynamicznyKolor RamkaDomyslna         = Ramka;
    inline constexpr DynamicznyKolor RamkaNajechana        = Akcent;
    inline constexpr DynamicznyKolor RamkaFokus            = AkcentNajechany;
    inline constexpr DynamicznyKolor PoleWyboruZaznaczenie = Zaznaczenie;
    inline constexpr DynamicznyKolor CzerwonyZamknij       = Blad;
    inline constexpr DynamicznyKolor CzerwonyZamknijNajechany = Blad;
    inline constexpr uint32_t WylaczonyTlo          = 0x0020242AU;
    inline constexpr uint32_t WylaczonyTekst        = 0x00F2F3F5U;
    inline constexpr DynamicznyKolor WylaczonyRamka        = Ramka;
    inline constexpr DynamicznyKolor ZaznaczenieTlo        = Zaznaczenie;
    inline constexpr DynamicznyTekstZaznaczenia ZaznaczenieTekst{};
    inline constexpr DynamicznyKolor NieokreslonyKolor     = Akcent;
}

struct StylStanu {
    uint32_t tlo{0};
    uint32_t tekst{0};
    uint32_t ramka{0};
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
        uint32_t przycisk_tlo = motyw_kolor(MOTYW_KOLOR_PRZYCISK);
        uint32_t przycisk_najechany = motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY);
        uint32_t tekst_akcentu = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);

        // Styl Okna
        styl_okna.normalny.tlo = tlo_okna;
        styl_okna.normalny.tekst = tekst;
        styl_okna.normalny.ramka = ramka;
        styl_okna.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_okna.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_OKNO);
        styl_okna.najechany = styl_okna.normalny;
        styl_okna.wcisniety = styl_okna.normalny;
        styl_okna.fokus = styl_okna.normalny;
        styl_okna.wylaczony = styl_okna.normalny;
        styl_okna.tylko_do_odczytu = styl_okna.normalny;

        // Styl Panelu
        styl_panelu.normalny.tlo = tlo_panelu;
        styl_panelu.normalny.tekst = tekst;
        styl_panelu.normalny.ramka = ramka;
        styl_panelu.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_panelu.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        styl_panelu.wypelnienie = Marginesy(8, 8, 8, 8);
        styl_panelu.najechany = styl_panelu.normalny;
        styl_panelu.wcisniety = styl_panelu.normalny;
        styl_panelu.fokus = styl_panelu.normalny;
        styl_panelu.wylaczony = styl_panelu.normalny;
        styl_panelu.tylko_do_odczytu = styl_panelu.normalny;

        // Styl Etykiety
        styl_etykiety.normalny.tlo = 0;
        styl_etykiety.normalny.tekst = tekst;
        styl_etykiety.normalny.ramka = 0;
        styl_etykiety.normalny.grubosc_ramki = 0;
        styl_etykiety.wypelnienie = Marginesy(2, 2, 2, 2);
        styl_etykiety.najechany = styl_etykiety.normalny;
        styl_etykiety.wcisniety = styl_etykiety.normalny;
        styl_etykiety.fokus = styl_etykiety.normalny;
        styl_etykiety.wylaczony = styl_etykiety.normalny;
        styl_etykiety.wylaczony.tekst = wylaczony;
        styl_etykiety.tylko_do_odczytu = styl_etykiety.normalny;
        styl_etykiety.tylko_do_odczytu.tekst = tekst_drugi;

        // Styl Przycisku
        styl_przycisku.normalny.tlo = przycisk_tlo;
        styl_przycisku.normalny.tekst = tekst;
        styl_przycisku.normalny.ramka = ramka;
        styl_przycisku.normalny.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_przycisku.normalny.promien_zaokraglenia = m ? m->rogprzycisku : 7;

        styl_przycisku.najechany.tlo = przycisk_najechany;
        styl_przycisku.najechany.tekst = tekst;
        styl_przycisku.najechany.ramka = akcent_najechany;
        styl_przycisku.najechany.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_przycisku.najechany.promien_zaokraglenia = m ? m->rogprzycisku : 7;

        styl_przycisku.wcisniety.tlo = akcent_aktywny;
        styl_przycisku.wcisniety.tekst = tekst_akcentu;
        styl_przycisku.wcisniety.ramka = akcent_aktywny;
        styl_przycisku.wcisniety.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_przycisku.wcisniety.promien_zaokraglenia = m ? m->rogprzycisku : 7;

        styl_przycisku.fokus.tlo = przycisk_tlo;
        styl_przycisku.fokus.tekst = tekst;
        styl_przycisku.fokus.ramka = akcent;
        styl_przycisku.fokus.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_przycisku.fokus.promien_zaokraglenia = m ? m->rogprzycisku : 7;

        styl_przycisku.wylaczony.tlo = przycisk_tlo;
        styl_przycisku.wylaczony.tekst = wylaczony;
        styl_przycisku.wylaczony.ramka = ramka;
        styl_przycisku.wylaczony.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_przycisku.wylaczony.promien_zaokraglenia = m ? m->rogprzycisku : 7;
        styl_przycisku.tylko_do_odczytu = styl_przycisku.wylaczony;
        styl_przycisku.wypelnienie = Marginesy(10, 6, 10, 6);

        // Styl Pola Wyboru
        styl_pola_wyboru.normalny.tlo = poletekstowe;
        styl_pola_wyboru.normalny.tekst = tekst;
        styl_pola_wyboru.normalny.ramka = ramka;
        styl_pola_wyboru.normalny.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_wyboru.normalny.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_wyboru.najechany.tlo = tlo_podniesiony;
        styl_pola_wyboru.najechany.tekst = tekst;
        styl_pola_wyboru.najechany.ramka = akcent_najechany;
        styl_pola_wyboru.najechany.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_wyboru.najechany.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_wyboru.wcisniety = styl_pola_wyboru.najechany;

        styl_pola_wyboru.fokus.tlo = poletekstowe;
        styl_pola_wyboru.fokus.tekst = tekst;
        styl_pola_wyboru.fokus.ramka = akcent;
        styl_pola_wyboru.fokus.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_wyboru.fokus.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_wyboru.wylaczony.tlo = tlo_panelu;
        styl_pola_wyboru.wylaczony.tekst = wylaczony;
        styl_pola_wyboru.wylaczony.ramka = wylaczony;
        styl_pola_wyboru.wylaczony.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_wyboru.wylaczony.promien_zaokraglenia = m ? m->rogpola : 6;
        styl_pola_wyboru.tylko_do_odczytu = styl_pola_wyboru.wylaczony;
        styl_pola_wyboru.wypelnienie = Marginesy(4, 4, 4, 4);

        // Styl Pola Tekstowego
        styl_pola_tekstowego.normalny.tlo = poletekstowe;
        styl_pola_tekstowego.normalny.tekst = tekst;
        styl_pola_tekstowego.normalny.ramka = ramka;
        styl_pola_tekstowego.normalny.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_tekstowego.normalny.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_tekstowego.najechany.tlo = poletekstowe;
        styl_pola_tekstowego.najechany.tekst = tekst;
        styl_pola_tekstowego.najechany.ramka = akcent_najechany;
        styl_pola_tekstowego.najechany.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_tekstowego.najechany.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_tekstowego.wcisniety = styl_pola_tekstowego.najechany;

        styl_pola_tekstowego.fokus.tlo = poletekstowe;
        styl_pola_tekstowego.fokus.tekst = tekst;
        styl_pola_tekstowego.fokus.ramka = akcent;
        styl_pola_tekstowego.fokus.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_tekstowego.fokus.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_tekstowego.wylaczony.tlo = tlo_panelu;
        styl_pola_tekstowego.wylaczony.tekst = wylaczony;
        styl_pola_tekstowego.wylaczony.ramka = wylaczony;
        styl_pola_tekstowego.wylaczony.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_tekstowego.wylaczony.promien_zaokraglenia = m ? m->rogpola : 6;

        styl_pola_tekstowego.tylko_do_odczytu.tlo = tlo_panelu;
        styl_pola_tekstowego.tylko_do_odczytu.tekst = tekst_drugi;
        styl_pola_tekstowego.tylko_do_odczytu.ramka = ramka;
        styl_pola_tekstowego.tylko_do_odczytu.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_pola_tekstowego.tylko_do_odczytu.promien_zaokraglenia = m ? m->rogpola : 6;
        styl_pola_tekstowego.wypelnienie = Marginesy(6, 4, 6, 4);

        // Styl Suwaka
        styl_suwaka.normalny.tlo = 0;
        styl_suwaka.normalny.tekst = tekst;
        styl_suwaka.normalny.ramka = ramka;
        styl_suwaka.normalny.grubosc_ramki = m ? m->gruboscramki : 1;
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
        styl_menu.normalny.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_menu.normalny.promien_zaokraglenia = m ? m->rogmenu : 8;
        styl_menu.najechany = styl_menu.normalny;
        styl_menu.najechany.tlo = motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE);
        styl_menu.najechany.tekst = motyw_kolor_tekstu_zaznaczenia();
        styl_menu.najechany.ramka = akcent_najechany;
        styl_menu.wcisniety = styl_menu.najechany;
        styl_menu.wcisniety.tlo = akcent_aktywny;
        styl_menu.wcisniety.tekst = tekst_akcentu;
        styl_menu.fokus = styl_menu.najechany;
        styl_menu.wylaczony = styl_menu.normalny;
        styl_menu.wylaczony.tekst = wylaczony;
        styl_menu.tylko_do_odczytu = styl_menu.wylaczony;

        // Styl Podpowiedzi (Tooltip)
        styl_podpowiedzi.normalny.tlo = tlo_podniesiony;
        styl_podpowiedzi.normalny.tekst = tekst;
        styl_podpowiedzi.normalny.ramka = ramka;
        styl_podpowiedzi.normalny.grubosc_ramki = m ? m->gruboscramki : 1;
        styl_podpowiedzi.normalny.promien_zaokraglenia = m ? m->rogpodpowiedzi : 6;
        styl_podpowiedzi.wypelnienie = Marginesy(6, 4, 6, 4);
        styl_podpowiedzi.najechany = styl_podpowiedzi.normalny;
        styl_podpowiedzi.wcisniety = styl_podpowiedzi.normalny;
        styl_podpowiedzi.fokus = styl_podpowiedzi.normalny;
        styl_podpowiedzi.wylaczony = styl_podpowiedzi.normalny;
        styl_podpowiedzi.wylaczony.tekst = wylaczony;
        styl_podpowiedzi.tylko_do_odczytu = styl_podpowiedzi.wylaczony;
    }
};

} // namespace BursztynUI
