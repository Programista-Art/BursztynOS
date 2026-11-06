#include "ikona_png.h"
#include "../obrazy/dekoder_obrazu.h"
namespace BursztynUI {
IkonaPNG::~IkonaPNG(){gui_free(original_.piksele);delete[] scaled_;}
void IkonaPNG::przygotuj(const char* path,int side){
  if(!attempted_){
   attempted_=true;uint32_t size=0,got=0;
   BladObrazu error=BladObrazu::Format;
   const char* reason="missing icon";
   if(pobierz_rozmiar_pliku(path,&size)&&size&&size<=256U*1024U){
    reason="read/decode failure";
    auto* data=new uint8_t[size];
    if(data){
     if(czytaj_fragment_pliku(path,0,reinterpret_cast<char*>(data),size,&got)&&got==size)
      {
      DekodowanyObraz decoded{};
      error=dekoduj_ikone_png(data,size,&decoded,gui_malloc,gui_free);
      if(error==BladObrazu::ZaDuzy)reason="rejected dimensions or allocation limit";
      else if(error==BladObrazu::Format||error==BladObrazu::Uszkodzony)reason="invalid PNG";
      original_.piksele=decoded.piksele;original_.szerokosc=decoded.szerokosc;original_.wysokosc=decoded.wysokosc;original_.bajty=decoded.bajty;
     }
     delete[] data;
    }else error=BladObrazu::Pamiec;
   }else if(size>256U*1024U){error=BladObrazu::ZaDuzy;reason="rejected file size";}
#if defined(BURSZTYN_DIAG_PNG) && BURSZTYN_DIAG_PNG
   if(error!=BladObrazu::OK){
    bws_wywolaj(1,reinterpret_cast<uint64_t>(path),0,0,0);
    bws_wywolaj(1,reinterpret_cast<uint64_t>(reason),0,0,0);
   }
#else
   (void)error;(void)reason;
#endif
  }
  if(!original_.piksele||side==side_||side<1||side>128)return;
  delete[] scaled_;scaled_=nullptr;side_=0;
  auto* p=new uint32_t[size_t(side)*size_t(side)];if(!p)return;
  if(!skaluj_obraz_bilinear(reinterpret_cast<const uint32_t*>(original_.piksele),original_.szerokosc,original_.wysokosc,original_.bajty,
   p,side,side,side,size_t(side)*side*4,TrybSkalowaniaObrazu::Dopasuj,0)){delete[] p;return;}
  scaled_=p;side_=side;
 }
}
