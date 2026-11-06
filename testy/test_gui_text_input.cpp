#include "file_operations_host_fixture.h"
#include "plik_popup_ui.h"
#include "../bursztyn_ui/okno.h"
#include "../bursztyn_ui/pole_wielowierszowe.h"
using namespace BursztynUI;
static std::vector<bws_zdarzenie> stream;
extern "C" bool zaktualizuj_klawisz_fizyczny_gui(uint32_t code,bool down,uint32_t mods) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.kod=code;e.x=down;e.przyciski=mods;stream.push_back(e);return true;
}
extern "C" bool zaktualizuj_tekst_gui(uint32_t cp,uint32_t mods) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_TEXT_INPUT;e.kod=cp;e.przyciski=mods;stream.push_back(e);return true;
}
extern "C" bool zaktualizuj_klawiature_gui(char c) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ;e.kod=uint8_t(c);stream.push_back(e);return true;
}
extern "C" void scheduler_wybudz_klawiature() {}
extern "C" void klawiatura_obsluz_klawisz(uint16_t,uint32_t);
static bws_zdarzenie key(uint32_t code,uint32_t mods=0) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.x=1;e.kod=code;e.przyciski=mods;return e;
}
static bws_zdarzenie text(uint32_t cp) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_TEXT_INPUT;e.kod=cp;return e;
}
template<class Send> void type(const char* name,Send send) {
    for(size_t i=0;name[i];) {
        uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(name,i,&cp);CHECK(n);
        if(cp>='0' && cp<='9') {
            stream.clear();uint16_t scan=cp=='0'?11:uint16_t(cp-'0'+1);
            klawiatura_obsluz_klawisz(scan,1);klawiatura_obsluz_klawisz(scan,0);
            CHECK(stream.size()==4);CHECK(stream[0].typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY);
            CHECK(stream[1].typ==BWS_ZDARZENIE_TEXT_INPUT && stream[1].kod==cp);
            for(auto e:stream)send(e);
        } else {send(key(BWS_KEY_SOURCE_USB|4));send(text(cp));}
        i+=n;
    }
}
static const char* names[]={"0","1","2","3","4","5","6","7","8","9","10","11","12",
    "17","18","19","20","21","22","27","28","29","70","71","72","78","79",
    "80","81","82","88","89","90","91","98","99","1234567890",
    "folder 8","folder 9","folder 17","folder 21","folder 29","Folder 21 Żółty"};
int main() {
    PlikiUI::DialogPlikowy d;
    for(const char* title:{"Utwórz folder","Utwórz plik","Zmień nazwę"})for(const char* name:names) {
        CHECK(d.otworz(title,"",nullptr,{0,0,800,600},{0,0,800,600}));
        auto* field=d.events.popups.fokus_popupu();
        type(name,[&](bws_zdarzenie e){CHECK(d.zdarzenie(e));CHECK(d.wynik()==0);CHECK(d.events.popups.fokus_popupu()==field);});
        CHECK(!strcmp(d.tekst(),name));CHECK(d.wynik()==0);
        d.zdarzenie(key(28));CHECK(d.wynik()==1);d.zamknij();
    }
    CHECK(d.otworz("Edycja","abc",nullptr,{0,0,800,600},{0,0,800,600}));
    d.zdarzenie(key(BWS_KEY_SOURCE_E0|0x4b));d.zdarzenie(text('8'));CHECK(!strcmp(d.tekst(),"ab8c"));
    d.zdarzenie(key(BWS_KEY_SOURCE_E0|0x47));d.zdarzenie(text('9'));CHECK(!strcmp(d.tekst(),"9ab8c"));
    d.zdarzenie(key(BWS_KEY_SOURCE_E0|0x4f));d.zdarzenie(key(14));CHECK(!strcmp(d.tekst(),"9ab8"));
    d.zdarzenie(key(BWS_KEY_SOURCE_E0|0x47));d.zdarzenie(key(BWS_KEY_SOURCE_E0|0x53));CHECK(!strcmp(d.tekst(),"ab8"));
    d.zdarzenie(key(BWS_KEY_SOURCE_E0|0x4d,BWS_MOD_SHIFT));d.zdarzenie(text('1'));CHECK(!strcmp(d.tekst(),"1b8"));
    auto* edit=d.events.popups.fokus_popupu();d.zdarzenie(key(15));CHECK(d.events.popups.fokus_popupu()!=edit);
    d.zdarzenie(key(15,BWS_MOD_SHIFT));CHECK(d.events.popups.fokus_popupu()==edit);
    d.zdarzenie(key(1));CHECK(d.wynik()==2);d.zamknij();
    CHECK(d.otworz("Powtarzanie","",nullptr,{0,0,800,600},{0,0,800,600}));
    for(int i=0;i<20;++i)type("8",[&](bws_zdarzenie e){d.zdarzenie(e);});
    CHECK(strlen(d.tekst())==20);CHECK(d.wynik()==0);
    d.zdarzenie(key(15));d.zdarzenie(key(28));CHECK(d.wynik()==1);d.zamknij();
    // NumLock keypad is text; its physical usages never move the caret.
    CHECK(d.otworz("Klawiatura numeryczna","",nullptr,{0,0,800,600},{0,0,800,600}));
    for(uint32_t digit=0;digit<10;++digit) {
        d.zdarzenie(key(BWS_KEY_SOURCE_USB|(digit?88+digit:98),BWS_MOD_NUM));d.zdarzenie(text('0'+digit));
    }
    CHECK(!strcmp(d.tekst(),"0123456789"));CHECK(d.wynik()==0);d.zamknij();
    // The ordinary window route is shared by Save/Open and Installer controls.
    Okno window;auto* field=new PoleTekstowe();auto* button=new Przycisk("OK");
    window.kontrolka_glowna()->dodaj_kontrolke(field);window.kontrolka_glowna()->dodaj_kontrolke(button);
    window.ustaw_fokus(field);
    for(const char* name:names) {
        field->ustaw_tekst("");type(name,[&](bws_zdarzenie e){window.obsluz_zdarzenie(e);CHECK(window.fokus()==field);});
        CHECK(!strcmp(field->tekst(),name));
    }
    int confirms=0;field->przy_zatwierdzeniu([](PoleTekstowe*,const char*,void* p){++*static_cast<int*>(p);},&confirms);
    field->ustaw_tekst("17");window.obsluz_zdarzenie(key(10));window.obsluz_zdarzenie(text('9'));CHECK(confirms==0);
    window.obsluz_zdarzenie(key(28));bws_zdarzenie echo{};echo.typ=BWS_ZDARZENIE_KLAWISZ;echo.kod=10;
    window.obsluz_zdarzenie(echo);CHECK(confirms==1);
    window.obsluz_zdarzenie(key(15));CHECK(window.fokus()==button);window.obsluz_zdarzenie(key(15,BWS_MOD_SHIFT));CHECK(window.fokus()==field);
    window.obsluz_zdarzenie(key(0x1e,BWS_MOD_CTRL));window.obsluz_zdarzenie(text('2'));CHECK(!strcmp(field->tekst(),"2"));
    field->ustaw_tekst("Żółty");window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_E0|0x47));
    window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_E0|0x4d));CHECK(field->pozycja_kursora()==2);
    window.obsluz_zdarzenie(text('8'));CHECK(!strcmp(field->tekst(),"Ż8ółty"));
    window.obsluz_zdarzenie(key(14));CHECK(!strcmp(field->tekst(),"Żółty"));
    window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_E0|0x53));CHECK(!strcmp(field->tekst(),"Żłty"));
    window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_E0|0x4d,BWS_MOD_SHIFT));window.obsluz_zdarzenie(text(0x00f3));
    CHECK(!strcmp(field->tekst(),"Żóty"));
    for(uint32_t invalid:{0U,8U,9U,13U,127U,159U,0xd800U,0xdfffU,0x110000U,0xffffffffU})window.obsluz_zdarzenie(text(invalid));
    CHECK(!strcmp(field->tekst(),"Żóty"));
    field->ustaw_tekst("");for(unsigned i=0;i<300;++i)window.obsluz_zdarzenie(text('8'));
    CHECK(strlen(field->tekst())==PoleTekstowe::MAKS_BUFOR-1);
    field->ustaw_pozycje_kursora(999999);window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_E0|0x53));
    CHECK(strlen(field->tekst())==255);window.obsluz_zdarzenie(key(14));CHECK(strlen(field->tekst())==254);
    window.obsluz_zdarzenie(text(0x017b));CHECK(strlen(field->tekst())==254); // no partial UTF-8 at capacity
    window.obsluz_zdarzenie(key(14));window.obsluz_zdarzenie(text(0x017b));CHECK(strlen(field->tekst())==255);
    window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_E0|0x47));window.obsluz_zdarzenie(key(14));CHECK(strlen(field->tekst())==255);
    // Legacy control-only devices still work. Printable legacy bytes insert nothing.
    PlikiUI::DialogPlikowy legacy;CHECK(legacy.otworz("Legacy","",nullptr,{0,0,800,600},{0,0,800,600}));
    klawiatura_fizyczna_gui=false; // separate legacy-only process fixture
    bws_zdarzenie l{};l.typ=BWS_ZDARZENIE_KLAWISZ;l.kod='8';legacy.zdarzenie(l);CHECK(!strcmp(legacy.tekst(),""));
    legacy.zdarzenie(text('8'));CHECK(!strcmp(legacy.tekst(),"8"));l.kod=9;legacy.zdarzenie(l);
    l.kod=13;legacy.zdarzenie(l);CHECK(legacy.wynik()==1);legacy.zamknij();
    PlikiUI::MenuPlikowe context;context.events.geometria({0,0,800,600},{0,0,800,600});context.otworz(10,10,{});
    context.events.zdarzenie(key(BWS_KEY_SOURCE_E0|0x47));context.events.zdarzenie(key(28));
    CHECK(context.pobierz_akcje()==PlikiUI::Akcja::NEW_FOLDER);
    PlikiUI::DialogPlikowy from_context;CHECK(from_context.otworz("Folder z menu","",nullptr,{0,0,800,600},{0,0,800,600}));
    l.kod=10;CHECK(from_context.zdarzenie(l));CHECK(from_context.wynik()==0);CHECK(!strcmp(from_context.tekst(),""));
    type("29",[&](bws_zdarzenie e){CHECK(from_context.zdarzenie(e));});CHECK(!strcmp(from_context.tekst(),"29"));
    CHECK(from_context.wynik()==0);from_context.zdarzenie(key(28));CHECK(from_context.wynik()==1);from_context.zamknij();
    auto* multiline=new PoleWielowierszowe();window.kontrolka_glowna()->dodaj_kontrolke(multiline);window.ustaw_fokus(multiline);
    for(const char* name:names) {
        multiline->ustaw_tekst("");type(name,[&](bws_zdarzenie e){window.obsluz_zdarzenie(e);CHECK(window.fokus()==multiline);});
        CHECK(!strcmp(multiline->tekst(),name));
    }
    multiline->ustaw_tekst("");window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_USB|28));window.obsluz_zdarzenie(text('y'));
    CHECK(!strcmp(multiline->tekst(),"y"));window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_USB|88));CHECK(!strcmp(multiline->tekst(),"y\n"));
    window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_USB|80));window.obsluz_zdarzenie(text('8'));CHECK(!strcmp(multiline->tekst(),"y8\n"));
    window.obsluz_zdarzenie(key(BWS_KEY_SOURCE_USB|76));CHECK(!strcmp(multiline->tekst(),"y8"));
    printf("Production keyboard / TEXTINPUT / dialogs: %d/%d PASS\n",checks,checks);
}
