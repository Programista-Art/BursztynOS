#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define BURSZTYN_HOST_TEST 1
#include "../motyw.cpp"
static int checks=0,failures=0;
#define TEST_ASSERT(condition,description) do { ++checks; if(!(condition)){ ++failures; fprintf(stderr,"FAIL: %s (line %d)\n",description,__LINE__); } } while(0)
static void test_pulpit_selection() {
    for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY,PLIK_GRAFIT,PLIK_BALTYCKA_NOC,PLIK_BURSZTYN_KLASYCZNY}) {
        BursztynMotyw theme{};TEST_ASSERT(motyw_parsuj(source,strlen(source),&theme),"Desktop: system theme parsed");
        uint32_t fill=motyw_pulpit_zaznaczenie(theme),border=motyw_pulpit_ramka(theme);
        TEST_ASSERT(fill>>24==192,"Desktop: selection uses bounded dark contrast alpha 192/255");
        TEST_ASSERT(border>>24==208,"Desktop: single border alpha 208/255");
        TEST_ASSERT((border&0xffffff)!=0 && (border&0xffffff)!=theme.tekst,"Desktop: border derived from surface and frame, not text fallback");
        for(uint32_t background:{0x000000U,0x101010U,0x404040U,0x808080U,0xc0c0c0U,0xf0f0f0U,0xffffffU}) {
            uint32_t out=motyw_pulpit_skladaj(background,fill,true,&theme);
            uint32_t edge=motyw_pulpit_skladaj(background,border,true,&theme);
            int delta=int(motyw_jasnosc_rgb(out))-int(motyw_jasnosc_rgb(background));
            int edgeDelta=int(motyw_jasnosc_rgb(edge))-int(motyw_jasnosc_rgb(background));
            TEST_ASSERT(motyw_jasnosc_rgb(out)<150,"Desktop: selected label backdrop retains contrast for bright text");
            (void)delta;
            TEST_ASSERT(edge!=out,"Desktop: border distinct from fill");
            (void)edgeDelta;
            TEST_ASSERT(motyw_pulpit_skladaj(background,fill,false,&theme)==(fill&0xffffff),"Desktop: other layers keep RGB semantics");
            TEST_ASSERT(motyw_pulpit_skladaj(background,0x01000000,true,&theme)==0,"Desktop: visible-black marker retained");
        }
    }
    BursztynMotyw light{};motyw_parsuj(PLIK_BURSZTYN_JASNY,strlen(PLIK_BURSZTYN_JASNY),&light);
    auto original=motyw_pulpit_ramka(light);light.ramka^=0xffffff;
    TEST_ASSERT(motyw_pulpit_ramka(light)!=original,"Desktop: custom frame role changes selection border");
}

int main() {
    TEST_ASSERT(kolor_nad(0,0x80402010)==0x80402010,"Alpha over an empty desktop retains coverage and RGB");
    TEST_ASSERT(kolor_nad(0x800000ff,0x80ff0000)==0xc0aa0055,"Two translucent sources retain straight-alpha color");
    TEST_ASSERT(kolor_nad(0xff204060,0x80ffffff)==0xff90a0b0,"Coverage blends once over an opaque destination");
    TEST_ASSERT(kolor_nad(0,0x00ffffff)==0,"Zero glyph coverage does not create an opaque black pixel");
    test_pulpit_selection();
    printf("Desktop selection theme: %d/%d PASS\n",checks-failures,checks);
    return failures?1:0;
}
