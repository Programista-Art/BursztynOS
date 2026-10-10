#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include "input_device.h"
#include "pointer_coordinates.h"
struct HostPointerReady { HostPointerReady(){input_set_pointer_ready(true);} };
static HostPointerReady host_pointer_ready;
static unsigned checks{},motions{},clicks{},logs{};
static int width=1280,height=720,px=640,py=360;
static uint8_t last_buttons{};
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();} } while(0)
void klog_serial(const char*){++logs;}
void wypisz_log(const char*){++logs;}
int grafika_pobierz_szerokosc(){return width;}
int grafika_pobierz_wysokosc(){return height;}
extern "C" int grafika_pobierz_mysz_x(){return px;}
extern "C" int grafika_pobierz_mysz_y(){return py;}
extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x,int y,uint8_t b,const char*){px=x;py=y;++motions;if(b!=last_buttons)++clicks;last_buttons=b;}
extern "C" void zaktualizuj_mysze_zrodlo(int dx,int dy,uint8_t b,const char*){
    if(width<=0||height<=0)return;
    px=pointer_accumulate(px,int64_t(dx),width);
    py=pointer_accumulate(py,-int64_t(dy),height);
    ++motions;if(b!=last_buttons)++clicks;last_buttons=b;
}
extern "C" void zaktualizuj_kolo_myszy(int,int,uint8_t){}
#ifndef REAL_KEYBOARD
extern "C" void klawiatura_obsluz_klawisz(uint16_t,uint32_t){}
#else
extern "C" bool zaktualizuj_klawiature_gui(char){return true;}
extern "C" bool zaktualizuj_klawisz_fizyczny_gui(uint32_t,bool,uint32_t){return true;}
extern "C" bool zaktualizuj_tekst_gui(uint32_t,uint32_t){return true;}
extern "C" void scheduler_wybudz_klawiature(){}
#endif
