#pragma once
#include "bursztyn_ui/czcionka.h"
namespace StartMenu {
struct App {const char* name;const char* alias;int action;const char* icon_png;};
inline constexpr App apps[]={
 {"Powłoka Bursztyna","terminal shell",0,nullptr},
 {"Eksplorator Plików","pliki foldery",1,"/ikonki/app_eksplorator.png"},
 {"Notatnik","tekst",2,"/ikonki/app_notatnik.png"},
 {"Kalkulator","liczenie",3,"/ikonki/app_kalkulator.png"},
 {"Husarz","przeglądarka internet",4,"/ikonki/app_husarz.png"},
 {"Przeglądarka Obrazów","zdjęcia",5,"/ikonki/app_obrazy.png"},
 {"Demo BursztynUI","",6,"/ikonki/app_demo_ui.png"},
 {"Kontrolki 2","",7,"/ikonki/app_kontrolki2.png"},
 {"Kontrolki 3","",8,"/ikonki/app_kontrolki3.png"},
 {"Kontrolki 4","",9,"/ikonki/app_kontrolki4.png"},
 {"Testy systemu","",10,"/ikonki/app_testy.png"},
 {"Tapety","",11,nullptr},
 {"Ustawienia ekranu","monitor rozdzielczość",12,nullptr},
 {"Aktualizacje Systemu","",13,"/ikonki/app_aktualizacje.png"},
 {"Test dźwięku","audio",15,"/ikonki/app_test_dzwieku.png"},
 {"Diagnostyka systemu","",16,"/ikonki/app_diagnostyka.png"},
 {"Snake","gry",21,"/ikonki/app_snake.png"},
 {"Ustawienia","czcionki font",20,"/ikonki/app_ustawienia.png"}};
constexpr int Count=sizeof(apps)/sizeof(apps[0]), QueryMax=64;
inline uint32_t lower(uint32_t c) {
 if(c>='A'&&c<='Z')return c+32;
 switch(c){case 0x104:return 0x105;case 0x106:return 0x107;case 0x118:return 0x119;
 case 0x141:return 0x142;case 0x143:return 0x144;case 0xd3:return 0xf3;
 case 0x15a:return 0x15b;case 0x179:return 0x17a;case 0x17b:return 0x17c;default:return c;}
}
inline int normalize(const char* s,uint32_t* out,int cap) {
 int n=0;for(size_t at=0;s&&s[at]&&n<cap;){uint32_t c;size_t bytes=BursztynUI::CzcionkaUI::dekoduj_utf8(s,at,&c);if(!bytes)break;at+=bytes;out[n++]=lower(c);}return n;
}
inline int compare(const char* a,const char* b) {
 size_t x=0,y=0;while(a[x]&&b[y]){uint32_t ca,cb;x+=BursztynUI::CzcionkaUI::dekoduj_utf8(a,x,&ca);y+=BursztynUI::CzcionkaUI::dekoduj_utf8(b,y,&cb);ca=lower(ca);cb=lower(cb);if(ca!=cb)return ca<cb?-1:1;}return a[x]?1:b[y]?-1:0;
}
inline int score(const char* name,const uint32_t* q,int qn) {
 uint32_t n[128]{};int nn=normalize(name,n,128),best=99;
 for(int at=0;at+qn<=nn;++at){int k=0;while(k<qn&&n[at+k]==q[k])++k;
  if(k==qn){int rank=at==0?(nn==qn?0:1):(n[at-1]==' '||n[at-1]=='-'?2:3);if(rank<best)best=rank;}
 }return best;
}
struct Model {
 int matches[Count]{},scores[Count]{},count=0,selected=0,first=0;
 void filter(const char* text) {
  uint32_t q[QueryMax]{};int n=normalize(text,q,QueryMax);count=0;first=selected=0;
  for(int i=0;i<Count;++i){int rank=n?score(apps[i].name,q,n):0;if(rank==99&&score(apps[i].alias,q,n)!=99)rank=4;
   if(rank==99)continue;
   int j=count++;while(j>0&&(rank<scores[j-1]||(rank==scores[j-1]&&compare(apps[i].name,apps[matches[j-1]].name)<0))){matches[j]=matches[j-1];scores[j]=scores[j-1];--j;}matches[j]=i;scores[j]=rank;
  }
 }
 void visible(int rows){if(rows<1)rows=1;if(selected<first)first=selected;if(selected>=first+rows)first=selected-rows+1;clamp(rows);}
 void clamp(int rows){int max=count-rows;if(max<0)max=0;if(first>max)first=max;if(first<0)first=0;}
 void move(int delta,int rows){if(!count)return;selected+=delta;if(selected<0)selected=0;if(selected>=count)selected=count-1;visible(rows);}
 int action()const{return count?apps[matches[selected]].action:-1;}
};
}
