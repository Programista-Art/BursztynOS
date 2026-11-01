#pragma once
#include "rysowanie.h"
#include "../obrazy/skalowanie.h"

namespace BursztynUI {
// One owning cache entry per asset, prepared during layout, never during paint.
// Failed loads are negative-cached for the lifetime of the session.
class IkonaPNG {
 struct Original {uint8_t* piksele=nullptr;uint32_t szerokosc=0,wysokosc=0;size_t bajty=0;} original_;
 uint32_t* scaled_=nullptr;
 int side_=0;
 bool attempted_=false;
public:
 IkonaPNG()=default;
 IkonaPNG(const IkonaPNG&)=delete;
 IkonaPNG& operator=(const IkonaPNG&)=delete;
 ~IkonaPNG();
 void przygotuj(const char* path,int side);
 const uint32_t* piksele()const{return scaled_;}
 int bok()const{return side_;}
};

// Straight alpha over the current target, including disabled opacity; originals
// and cached variants remain immutable. At most 128 pixels of stack scratch.
inline void rysuj_png(KontekstRysowania& ctx,Prostokat r,const uint32_t* pixels,int side,bool enabled){
 if(!pixels||side<1||side>128||r.szerokosc!=side||r.wysokosc!=side)return;
 auto off=ctx.pobierz_przesuniecie();
 auto clip=Prostokat(off.x+r.x,off.y+r.y,side,side).przeciecie(ctx.pobierz_przyciecie());
 uint32_t row[128];
 for(int y=clip.y;y<clip.dolny();++y){
  for(int x=clip.x;x<clip.prawy();++x){
   uint32_t c=pixels[size_t(y-off.y-r.y)*side+x-off.x-r.x];
   unsigned a=c>>24;if(!enabled)a=(a*96+127)/255;
   row[x-clip.x]=(c&0xffffffU)|(a<<24);
  }
  ctx.rysuj_wiersz_alfa_ekranu(clip.x,y,clip.szerokosc,row);
 }
}
}
