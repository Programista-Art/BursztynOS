#include "../../../tapety_format.h"
#include "../tapetybtp.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <random>
#include <string>
static unsigned checks;
static void check(bool ok) { ++checks; if(!ok) { std::fprintf(stderr,"FAIL check %u\n",checks);std::exit(1); } }
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::mt19937 rng(0xb7f3);
    auto c=tapety_domyslna();char buf[512];const size_t n=tapety_serializuj(c,buf,sizeof(buf));
    TapetyKonfiguracja out{};check(n && tapety_parsuj(buf,n,&out));check(tapety_rowne(c.sciezka,out.sciezka));
    check(tapety_sciezka("/tapety/a.btp"));check(tapety_sciezka("/tapety/Bałtyk.btp"));
    check(!tapety_sciezka("/tapety/a/../b.btp"));check(!tapety_sciezka("/tapety/\xed\xa0\x80.btp"));
    for(unsigned i=0;i<100000;++i) {
        char fuzz[512];size_t len=rng()%513;
        for(size_t j=0;j<len;++j)fuzz[j]=rng()%256;
        if(i%2==0) {len=n;for(size_t j=0;j<n;++j)fuzz[j]=buf[j];fuzz[rng()%n]=rng()%256;}
        bool ok=tapety_parsuj(fuzz,len,&out);
        check(tapety_sciezka(out.sciezka));
        if(ok) {char canonical[512];size_t k=tapety_serializuj(out,canonical,sizeof(canonical));check(k>0);}
        else check(tapety_rowne(out.sciezka,c.sciezka));
    }
    for(unsigned i=0;i<100000;++i) {
        uint32_t sw=1+rng()%8192,sh=1+rng()%8192,dw=1+rng()%8192,dh=1+rng()%8192;
        auto mode=static_cast<TrybSkalowaniaObrazu>(rng()%3);GeometriaObrazu g{};
        check(geometria_obrazu(sw,sh,dw,dh,mode,&g));
        check(g.src_w && g.src_h && g.dst_w && g.dst_h && g.src_x+g.src_w<=sw && g.src_y+g.src_h<=sh && g.dst_x+g.dst_w<=dw && g.dst_y+g.dst_h<=dh);
        check(probka_obrazu(g.dst_w-1,g.src_w,g.dst_w)<g.src_w);
    }
    for(unsigned i=1;i<=10;++i) {
        char path[1024];std::snprintf(path,sizeof(path),"%s/bursztyn%02u.btp",argv[1],i);
        std::ifstream f(path,std::ios::binary);check(f.good());std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
        BtpInfo h{};check(btpsprawdzplik(b.data(),b.size(),&h)==BTPOK);check(h.szerokosc==1280 && h.wysokosc==720 && h.kompresja==1);
        size_t count=size_t(h.szerokosc)*h.wysokosc;
        std::vector<uint32_t> pixels(count+2,0xabcdef01);
        check(btpwczytajirysuj(b.data(),b.size(),pixels.data()+1,h.szerokosc,h.wysokosc,h.szerokosc,count*4)==BTPOK);
        check(pixels.front()==0xabcdef01 && pixels.back()==0xabcdef01);
        for(size_t p=1;p<=count;++p)check(pixels[p]>>24==255);
        std::vector<uint32_t> thumb(160*90+2,0xabcdef01);
        for(unsigned m=0;m<3;++m) {
            check(skaluj_obraz(pixels.data()+1,h.szerokosc,h.wysokosc,count*4,thumb.data()+1,160,90,160,160*90*4,static_cast<TrybSkalowaniaObrazu>(m),TAPETY_TLO));
            check(thumb.front()==0xabcdef01 && thumb.back()==0xabcdef01);
        }
        std::printf("asset %02u: %ux%u RLE %zu B %.4f%% raw\n",i,h.szerokosc,h.wysokosc,b.size(),100.*b.size()/(count*4));
    }
    std::printf("Stage3 PASS: %u checks; 100000 config fuzz; 100000 geometry fuzz; 10 assets decoded/scaled\n",checks);
}
