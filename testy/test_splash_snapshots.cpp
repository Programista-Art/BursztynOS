// Actual production module and Unicode renderer. No emulator or duplicate state machine.
#define main existing_welcome_suite_main
#include "test_powitanie.cpp"
#undef main
#include "diagnostyka_klatek_startu.h"
using namespace Powitanie;
static constexpr unsigned samples[]={0,1,1999,2000,2200,2399,2400,4199,4200,4201,4400,4599,4600,4601,6399,6400,6700,6999,7000,7001};
// Full-frame snapshots captured from the intended existing design before edits.
// They preserve typography, position, colours, timings and all crossfade pixels.
static constexpr uint64_t golden[2][16]={
 {15339559154842318014ULL,15339559154842318014ULL,15339559154842318014ULL,15339559154842318014ULL,
  11888514091736741290ULL,14367078734538182350ULL,4159717306052895862ULL,4159717306052895862ULL,
  4159717306052895862ULL,4159717306052895862ULL,3036876580659170367ULL,14266831582863797075ULL,
  16268960460685872866ULL,16268960460685872866ULL,16268960460685872866ULL,16268960460685872866ULL},
 {4427222597618984909ULL,4427222597618984909ULL,4427222597618984909ULL,4427222597618984909ULL,
  12838159530492151537ULL,3308495186249998258ULL,14841564629360609523ULL,14841564629360609523ULL,
  14841564629360609523ULL,14841564629360609523ULL,3494095708422153455ULL,53107964425025786ULL,
  4519467412195004335ULL,4519467412195004335ULL,4519467412195004335ULL,4519467412195004335ULL}
};
static uint64_t frame_hash(const uint32_t* p,int pitch=1280) {
    return KlatkiStartuDiag::hash(p,1280,720,pitch);
}
struct Bounds {int x0=1280,y0=720,x1=-1,y1=-1;};
static Bounds glyph_mask(const char* text,unsigned dpi,std::vector<uint32_t>& mask) {
    mask.assign(1280*720,TLO);
    const auto g=geometria(1280,720,dpi,0);
    napis(mask.data(),1280,720,1280,text,g.komunikat.y,0xE1E6ED,
          skala_tekstu(WYSOKOSC_KOMUNIKATU,dpi),{0,0,1280,720});
    Bounds b;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)if(mask[y*1280+x]!=TLO) {
        if(x<b.x0)b.x0=x;if(x>b.x1)b.x1=x;if(y<b.y0)b.y0=y;if(y>b.y1)b.y1=y;
    }
    assert(b.x1>=b.x0&&b.y1>=b.y0);return b;
}
int main() {
    width=1280;height=720;
    const int pitch=1288;const uint32_t pad=0xDEADBEEF;
    std::vector<uint32_t> scene(size_t(pitch)*height,0x253749);
    auto start=[&](unsigned dpi){now=0;powitanie_inicjalizuj(true);
        powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),width,height,pitch*4,0,0,width,height,dpi);
        powitanie_publikacja();};
    for(unsigned dpi:{100U,125U}) {
        std::vector<uint32_t> second,third;
        const auto b2=glyph_mask(tekst(1),dpi,second),b3=glyph_mask(tekst(2),dpi,third);
        auto g=geometria(width,height,dpi,0);
        for(unsigned m=0;m<3;++m) {
            const auto gm=geometria(width,height,dpi,m);
            assert(gm.baza_komunikatu==g.baza_komunikatu&&gm.komunikat.y==g.komunikat.y);
        }
        assert(b2.y0>=g.komunikat.y&&b3.y0>=g.komunikat.y);
        assert(b2.y1<g.komunikat.y+g.komunikat.h&&b3.y1<g.komunikat.y+g.komunikat.h);
        printf("Message bounds %u%%: second=(%d,%d)-(%d,%d), third=(%d,%d)-(%d,%d), shared baseline=%d\n",
            dpi,b2.x0,b2.y0,b2.x1,b2.y1,b3.x0,b3.y0,b3.x1,b3.y1,g.baza_komunikatu);
        for(unsigned index=0;index<sizeof(samples)/sizeof(samples[0]);++index) {
            const auto ms=samples[index];start(dpi);powitanie_pulpit_gotowy();
            now=uint64_t(ms)*1000;
            auto* out=reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),
                width,height,pitch*4,0,0,width,height,dpi));
            if(ms>=7000) {assert(out==scene.data()&&!powitanie_aktywne());powitanie_wycisz();continue;}
            assert(out!=scene.data()&&powitanie_pierwsza_klatka_gotowa());
            if(index<16)assert(frame_hash(out,pitch)==golden[dpi==125][index]);
            Sekwencja state;state.rozpocznij(true,0);state.gotowy=true;state.aktualizuj(now);
            assert(sekwencja.komunikat==state.komunikat&&sekwencja.nastepny_komunikat==state.nastepny_komunikat);
            assert(sekwencja.przenikanie==state.przenikanie&&sekwencja.odsloniecie==state.odsloniecie);
            if(ms>=2400&&ms<4200) {
                assert(state.faza==Faza::Drugi&&state.alfa_komunikatu(2)==0&&state.komunikat==1);
                // Unique third-string glyph pixels must be background in the actual frame.
                bool absent=true;unsigned exclusive=0;
                for(size_t i=0;i<third.size();++i)if(third[i]!=TLO&&second[i]==TLO) {
                    ++exclusive;absent&=out[(i/1280)*pitch+i%1280]==TLO;
                }
                assert(exclusive>0&&absent);
            }
            if(ms>=4200&&ms<4600) {
                assert(state.faza==Faza::DrugiDoTrzeciego);
                assert(state.alfa_starego()+state.alfa_nowego()==255);
                if(ms==4200)assert(state.alfa_starego()==255&&state.alfa_nowego()==0);
                // No message ink outside the union of the two real glyph masks.
                bool only_one_row=true;
                for(int y=g.komunikat.y;y<height;++y)for(int x=0;x<width;++x)
                    if(second[y*width+x]==TLO&&third[y*width+x]==TLO)
                        only_one_row&=out[y*pitch+x]==TLO;
                assert(only_one_row);
            }
            if(ms>=4600)assert(state.komunikat==2&&state.nastepny_komunikat==2&&state.alfa_komunikatu(1)==0);
            if(ms>=6400) {
                std::vector<uint32_t> expected(scene.size(),pad);
                rysuj(expected.data(),width,height,pitch,dpi,state,true);
                bool exact=true;
                for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                    exact&=out[y*pitch+x]==mieszaj(expected[y*pitch+x],scene[y*pitch+x],state.odsloniecie);
                assert(exact);
            }
            powitanie_wycisz();assert(live==0);
        }
        // Consecutive production frames share the actual allocation. The harness
        // never clears that memory, including around inclusive/exclusive boundaries.
        start(dpi);uint32_t* backing=nullptr;
        for(unsigned ms:{2400U,4199U,4200U,4201U,4400U,4599U,4600U,4601U}) {
            now=uint64_t(ms)*1000;
            auto* out=reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),
                width,height,pitch*4,0,0,width,height,dpi));
            if(!backing)backing=out;assert(out==backing);
            std::vector<uint32_t> fresh(scene.size(),0);
            rysuj(fresh.data(),width,height,pitch,dpi,sekwencja,true);
            assert(frame_hash(out,pitch)==frame_hash(fresh.data(),pitch));
        }
        powitanie_wycisz();assert(live==0);
        // Dirty memory from any previous geometry/line is reconstructed by the
        // real renderer even when a legacy caller passes its old incremental hint.
        std::vector<uint32_t> reuse(scene.size(),0xBADA55),fresh(scene.size(),pad);
        Sekwencja state;state.rozpocznij(true,0);state.aktualizuj(4600000);
        rysuj(reuse.data(),width,height,pitch,dpi,state,false);
        rysuj(fresh.data(),width,height,pitch,dpi,state,true);
        assert(frame_hash(reuse.data(),pitch)==frame_hash(fresh.data(),pitch));
        bool padding=true;for(int y=0;y<height;++y)for(int x=width;x<pitch;++x)padding&=fresh[y*pitch+x]==pad;
        assert(padding);
        std::vector<uint32_t> tall,short_text;
        auto bt=glyph_mask("ŻÓŁĆgy",dpi,tall),bs=glyph_mask("xxxx",dpi,short_text);
        assert(bt.y0!=bs.y0||bt.y1!=bs.y1);
        assert(bt.y0>=g.komunikat.y&&bs.y0>=g.komunikat.y);
        assert(bt.y1<g.komunikat.y+g.komunikat.h&&bs.y1<g.komunikat.y+g.komunikat.h);
        // A scheduler gap renders only the current absolute-time state.
        start(dpi);now=4400000;
        auto* out=reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),
            width,height,pitch*4,0,0,width,height,dpi));
        assert(sekwencja.faza==Faza::DrugiDoTrzeciego&&sekwencja.przenikanie==127);
        assert(frame_hash(out,pitch)==golden[dpi==125][10]);powitanie_wycisz();
    }
    printf("Production splash snapshots/baseline/reusable backing/absolute boundaries: %u/%u PASS\n",checks,checks);
}
