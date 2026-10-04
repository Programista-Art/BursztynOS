#pragma once
#include "../bursztyn_ui/menu_tekstowe.h"
static void text_context_cases() {
    for(unsigned scale:{100U,125U})for(auto resolution:{std::pair<int,int>{1280,720},{1920,1080}}) {
        ui_skala_procent=scale;
        auto* field=new BursztynUI::PoleTekstowe();auto* menu=new BursztynUI::MenuTekstowe();
        BursztynUI::Prostokat work{0,0,resolution.first,resolution.second};
        field->ustaw_skale_dpi(ui_skala());field->rozmiesc({20,20,500,40});field->ustaw_tekst("/test/zażółć.txt");
        field->ustaw_zaznaczenie(6,16);std::string selected(field->tekst()+field->poczatek_zaznaczenia(),field->dlugosc_zaznaczenia());
        auto start=field->poczatek_zaznaczenia(),end=field->koniec_zaznaczenia();
        CHECK(menu->otworz(*field,40,30,work,work));CHECK(field->poczatek_zaznaczenia()==start);CHECK(field->koniec_zaznaczenia()==end);
        CHECK(menu->menu.menu().element(0)->czy_wlaczony());menu->menu.menu().element(0)->wykonaj();
        CHECK(std::string(g_mock_clipboard)==selected);menu->zamknij();
        CHECK(menu->otworz(*field,40,30,work,work));CHECK(menu->menu.menu().element(1)->czy_wlaczony());menu->menu.menu().element(1)->wykonaj();
        CHECK(!field->czy_ma_zaznaczenie());CHECK(std::string(field->tekst())=="/test/.txt");menu->zamknij();
        CHECK(menu->otworz(*field,40,30,work,work));CHECK(menu->menu.menu().element(2)->czy_wlaczony());menu->menu.menu().element(2)->wykonaj();
        CHECK(std::string(field->tekst())=="/test/zażółć.txt");menu->zamknij();
        field->ustaw_zaznaczenie(6,16);CHECK(menu->otworz(*field,40,30,work,work));menu->menu.menu().element(3)->wykonaj();
        CHECK(std::string(field->tekst())=="/test/.txt");menu->zamknij();
        CHECK(menu->otworz(*field,40,30,work,work));menu->menu.menu().element(4)->wykonaj();CHECK(field->dlugosc_zaznaczenia()==strlen(field->tekst()));menu->zamknij();
        clipboard_set("text/plain","/żółć",strlen("/żółć")+1);CHECK(menu->otworz(*field,40,30,work,work));menu->menu.menu().element(2)->wykonaj();
        CHECK(std::string(field->tekst())=="/żółć");menu->zamknij();
        CHECK(menu->otworz(*field,40,30,work,work));
        for(int i:{0,1,3})CHECK(!menu->menu.menu().element(i)->czy_wlaczony());CHECK(menu->menu.menu().element(4)->czy_wlaczony());
        bws_zdarzenie escape{};escape.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;escape.x=1;escape.kod=1;
        CHECK(menu->zdarzenie(escape));CHECK(!menu->menu.czy_otwarte());CHECK(!field->czy_ma_zaznaczenie());
        CHECK(menu->otworz(*field,40,30,work,work));
        bws_zdarzenie outside{};outside.typ=BWS_ZDARZENIE_MYSZ_DOWN;outside.x=resolution.first-1;outside.y=resolution.second-1;
        CHECK(menu->zdarzenie(outside));CHECK(!menu->menu.czy_otwarte());outside.typ=BWS_ZDARZENIE_MYSZ_UP;CHECK(menu->zdarzenie(outside));
        // Action activation through native popup routing, followed by consumed UP.
        CHECK(menu->otworz(*field,40,30,work,work));
        CHECK(menu->events.popups.liczba_powierzchni()==1);
        BursztynUI::ZdarzenieKlawiatury key{};key.kod_klawisza=0x50; // selects first enabled (Paste)
        menu->events.popups.obsluz_klawisz(key);key.kod_klawisza=13;menu->events.popups.obsluz_klawisz(key);
        CHECK(!menu->menu.czy_otwarte());
        field->zaznacz_wszystko();CHECK(menu->otworz(*field,40,30,work,work));
        auto popupRect=menu->events.popups.prostokat_zarejestrowany_ekranu();
        bws_zdarzenie mouse{};mouse.typ=BWS_ZDARZENIE_MYSZ_DOWN;mouse.x=popupRect.x+ui_skaluj(10,scale);mouse.y=popupRect.y+ui_skaluj(12,scale);
        CHECK(menu->zdarzenie(mouse));mouse.typ=BWS_ZDARZENIE_MYSZ_UP;CHECK(menu->zdarzenie(mouse));CHECK(!menu->menu.czy_otwarte());
        CHECK(std::string(g_mock_clipboard)==field->tekst());
        field->ustaw_tekst("");clipboard_set("text/plain",nullptr,0);CHECK(menu->otworz(*field,40,30,work,work));
        for(int i=0;i<5;++i)CHECK(!menu->menu.menu().element(i)->czy_wlaczony());menu->zamknij();
        clipboard_set("application/octet-stream","binary",7);CHECK(menu->otworz(*field,40,30,work,work));CHECK(!menu->menu.menu().element(2)->czy_wlaczony());menu->zamknij();
        field->ustaw_tekst("zażółć");field->ustaw_pozycje_kursora(2);key.kod_klawisza=0x53;field->obsluz_wcisniecie_klawisza(key);CHECK(!strcmp(field->tekst(),"zaółć"));
        key.kod_klawisza=8;field->obsluz_wcisniecie_klawisza(key);CHECK(!strcmp(field->tekst(),"zółć"));
        for(unsigned code:{0x47U,0x4dU,0x4bU,0x4fU}) {key.kod_klawisza=code;field->obsluz_wcisniecie_klawisza(key);CHECK((static_cast<unsigned char>(field->tekst()[field->pozycja_kursora()])&192)!=128);}
        // Capacity truncation keeps a complete codepoint (both setter and paste).
        std::string near(BursztynUI::PoleTekstowe::MAKS_BUFOR-2,'a');near+="ż";field->ustaw_tekst(near.c_str());CHECK(strlen(field->tekst())==BursztynUI::PoleTekstowe::MAKS_BUFOR-2);
        field->zastap_zaznaczenie("ż");CHECK(strlen(field->tekst())==BursztynUI::PoleTekstowe::MAKS_BUFOR-2);
        delete menu;delete field;
    }
}
