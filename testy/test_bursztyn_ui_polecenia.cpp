#include <stdio.h>
#include <string.h>

#include "../bursztyn_ui/polecenie_ui.h"
#include "../bursztyn_ui/menu.h"

using namespace BursztynUI;

static int razem = 0;
static int udane = 0;
#define SPRAWDZ(w) do { ++razem; if (w) { ++udane; } else { \
    printf("[FAIL] linia %d: %s\n", __LINE__, #w); } } while (0)

static void wykonanie(PolecenieUI&, void* dane) {
    ++*static_cast<int*>(dane);
}

static void wykonanie_usuwajace(PolecenieUI& polecenie, void* dane) {
    *static_cast<bool*>(dane) = true;
    delete &polecenie;
}

static void test_wiele_wiazan_i_stany() {
    int wywolania = 0;
    PolecenieUI p(7, "Zapisz");
    p.ustaw_ikone(3);
    p.ustaw_skrot(SkrotPolecenia::usb(22, BWS_MOD_CTRL), "Ctrl+S");
    p.przy_wykonaniu(wykonanie, &wywolania);
    Menu a, b;
    ElementMenu& ea = a.dodaj_polecenie(p);
    ElementMenu& eb = b.dodaj_polecenie(p);
    SPRAWDZ(p.liczba_odbiorcow() == 2);
    SPRAWDZ(strcmp(ea.tekst(), "Zapisz") == 0 && eb.ikona() == 3);
    p.ustaw_tekst("Zapisz jako"); p.ustaw_wlaczona(false);
    p.ustaw_widoczna(false); p.ustaw_zaznaczona(true);
    SPRAWDZ(strcmp(ea.tekst(), "Zapisz jako") == 0 && strcmp(eb.tekst(), "Zapisz jako") == 0);
    SPRAWDZ(!ea.czy_wlaczony() && !eb.czy_widoczny() && ea.czy_zaznaczony());
    ea.wykonaj();
    SPRAWDZ(wywolania == 0);
    p.ustaw_widoczna(true); p.ustaw_wlaczona(true); eb.wykonaj();
    SPRAWDZ(wywolania == 1);
}

static void test_przenoszenie_i_czas_zycia() {
    PolecenieUI p(9, "Przenoszone");
    Menu* menu = new Menu();
    ElementMenu& pierwszy = menu->dodaj_polecenie(p);
    (void)pierwszy;
    for (int i = 0; i < 40; ++i) menu->dodaj_element("wypelniacz");
    SPRAWDZ(p.liczba_odbiorcow() == 1);
    SPRAWDZ(menu->element(0)->polecenie() == &p);
    delete menu;
    SPRAWDZ(p.liczba_odbiorcow() == 0);

    Menu pozostale;
    PolecenieUI* chwilowe = new PolecenieUI(10, "Chwilowe");
    ElementMenu& e = pozostale.dodaj_polecenie(*chwilowe);
    delete chwilowe;
    SPRAWDZ(e.polecenie() == nullptr);
}

static void test_lista_i_skroty() {
    int wywolania = 0;
    ListaPolecen lista;
    PolecenieUI* p = new PolecenieUI(11, "Kopiuj");
    p->ustaw_skrot(SkrotPolecenia::usb(6, BWS_MOD_CTRL), "Ctrl+C");
    p->przy_wykonaniu(wykonanie, &wywolania);
    SPRAWDZ(lista.dodaj(*p));
    SPRAWDZ(lista.wykonaj_skrot(BWS_KEY_SOURCE_USB | 6U, BWS_MOD_CTRL));
    SPRAWDZ(wywolania == 1);
    p->ustaw_wlaczona(false);
    SPRAWDZ(!lista.wykonaj_skrot(BWS_KEY_SOURCE_USB | 6U, BWS_MOD_CTRL));
    SPRAWDZ(wywolania == 1);
    p->ustaw_wlaczona(true); p->ustaw_widoczna(false);
    SPRAWDZ(!lista.wykonaj_skrot(BWS_KEY_SOURCE_USB | 6U, BWS_MOD_CTRL));
    delete p;
    SPRAWDZ(lista.liczba_polecen() == 0);
}

static void test_ponowne_uzycie_slotu_menu() {
    Menu menu;
    menu.dodaj_element("Ukryty", 1).ustaw_widoczny(false).ustaw_wlaczony(false)
        .ustaw_zaznaczony(true).ustaw_ikone(9);
    SPRAWDZ(menu.usun_element(0));
    ElementMenu& nowy = menu.dodaj_element("Nowy", 2);
    SPRAWDZ(nowy.czy_widoczny() && nowy.czy_wlaczony() && !nowy.czy_zaznaczony());
    SPRAWDZ(nowy.ikona() == 0 && nowy.polecenie() == nullptr);

    menu.wstaw_element(0, "Pierwszy", 3);
    ElementMenu* pierwszy = menu.element(0);
    SPRAWDZ(pierwszy && pierwszy->czy_widoczny() && pierwszy->czy_wlaczony() &&
            !pierwszy->czy_zaznaczony() && pierwszy->ikona() == 0);
}

static void test_samousuniecie_w_callbacku() {
    bool wykonano = false;
    Menu menu;
    PolecenieUI* polecenie = new PolecenieUI(20, "Jednorazowe");
    polecenie->przy_wykonaniu(wykonanie_usuwajace, &wykonano);
    ElementMenu& element = menu.dodaj_polecenie(*polecenie);
    element.wykonaj();
    SPRAWDZ(wykonano);
    SPRAWDZ(element.polecenie() == nullptr);
}

int main() {
    test_wiele_wiazan_i_stany();
    test_przenoszenie_i_czas_zycia();
    test_lista_i_skroty();
    test_ponowne_uzycie_slotu_menu();
    test_samousuniecie_w_callbacku();
    printf("BursztynUI polecenia: %d/%d testow udanych\n", udane, razem);
    return udane == razem ? 0 : 1;
}
