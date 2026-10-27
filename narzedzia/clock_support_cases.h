#pragma once
static void clock_support_cases(){
 using namespace SupportData;
 auto category=std::make_unique<Category>();
 auto parse_text=[&](const std::string& text){return parse(text.data(),text.size(),*category);};
 CHECK(parse_text("\xef\xbb\xbf Jan Kowalski\r\n\r\n Anna Żółć \nFirma Przykład"));CHECK(category->count==3);CHECK(!strcmp(category->names[1],"Anna Żółć"));
 CHECK(parse_text(""));CHECK(category->loaded);CHECK(category->count==0);CHECK(!strcmp(message(*category),"Brak wpisów"));
 CHECK(parse_text("\n\r\n\r"));CHECK(category->count==0);
 CHECK(parse_text(std::string("good\n")+char(0xc4)+"\nlast"));CHECK(category->count==2);CHECK(category->rejected==1);
 CHECK(parse_text(std::string("<script>&plain\\n\n")+std::string(161,'a')+"\nOK"));CHECK(category->count==2);CHECK(category->rejected==1);CHECK(!strcmp(category->names[0],"<script>&plain\\n"));
 CHECK(parse_text(std::string(160,'a')));CHECK(category->count==1);
 CHECK(parse_text(std::string(159,'a')+"ą"));CHECK(category->count==0);CHECK(category->rejected==1);
 for(std::string bad:{std::string("x\0y",3),std::string("\xc0\xaf"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80")}){CHECK(parse_text(bad));CHECK(category->count==0);CHECK(category->rejected==1);}
 std::string many;for(int i=0;i<256;++i)many+="Nazwa żółć\n";
 CHECK(parse_text(many));CHECK(category->count==256);CHECK(!parse_text(many+"extra"));CHECK(category->status==Status::TooMany);
 CHECK(!parse_text(std::string(MaxFile+1,'a')));CHECK(category->status==Status::TooLarge);
 std::map<std::string,std::string> sources{{Paths[0],"Mecenas testowy\n"},{Paths[1],"Sponsor testowy\n"},{Paths[2],"Twórca testowy\n"}};
 bool fail=false,short_read=false;
 auto size=[&](const char* path,uint32_t* bytes){auto it=sources.find(path);if(it==sources.end())return false;*bytes=it->second.size();return true;};
 auto read=[&](const char* path,char* out,uint32_t n,size_t* got){if(fail)return false;memcpy(out,sources[path].data(),n);*got=n-(short_read?1:0);return true;};
 auto categories=std::make_unique<Category[]>(3);
 for(unsigned i=0;i<3;++i){CHECK(reload(categories[i],i,size,read));CHECK(categories[i].count==1);CHECK(sources[Paths[i]].find(categories[i].names[0])==0);}
 fail=true;CHECK(!reload(categories[0],0,size,read));CHECK(!strcmp(categories[0].names[0],"Mecenas testowy"));fail=false;
 short_read=true;CHECK(!reload(categories[0],0,size,read));CHECK(categories[0].status==Status::ReadError);short_read=false;
 sources.erase(Paths[1]);CHECK(!reload(categories[1],1,size,read));CHECK(categories[1].count==1);CHECK(!strcmp(categories[1].names[0],"Sponsor testowy"));
 Category missing;CHECK(!reload(missing,1,size,read));CHECK(missing.count==0);CHECK(missing.status==Status::Missing);
 sources[Paths[0]]=std::string(MaxFile+1,'a');CHECK(!reload(categories[0],0,size,read));CHECK(categories[0].count==1);
 sources[Paths[0]]=many+"extra";CHECK(!reload(categories[0],0,size,read));CHECK(categories[0].count==1);
 sources[Paths[0]]="";CHECK(reload(categories[0],0,size,read));CHECK(categories[0].count==0);
 sources[Paths[0]]=many;CHECK(reload(categories[0],0,size,read));CHECK(categories[0].count==256);
 for(int px:{12,16,20,24})for(unsigned dpi:{100U,125U,150U,200U}){
  auto config=BSF::configuration();config.family=BSF::default_id();config.size=px;CHECK(BSF::apply(config));
  auto s=SkalaUI::procent(dpi);
  SettingsUI::Body body;body.ustaw_skale_dpi(s);body.support->data[0]=categories[0];body.support->refresh();
  for(int width:{800,950,1100}){
   body.rozmiesc({0,0,ui_skaluj(width,dpi),ui_skaluj(512,dpi)});auto* page=body.support;
   CHECK(body.nav[0]->szerokosc()==body.nav[3]->szerokosc());CHECK(body.nav[3]->obszar().dolny()<=body.wysokosc());
   CHECK(page->heading->szerokosc()>=CzcionkaUI::zmierz_tekst("Wsparcie projektu Bursztyn OS",s));
   for(int i=0;i<3;++i){
    auto tab=page->tabs->prostokat_naglowka(i);CHECK(tab.x>=0);CHECK(tab.prawy()<=page->tabs->szerokosc());CHECK(tab.szerokosc>=CzcionkaUI::zmierz_tekst(Titles[i],s)+16*s);
    for(int j=0;j<i;++j)CHECK(!Desktop::intersects(tab,page->tabs->prostokat_naglowka(j)));
    CHECK(page->tabs->ustaw_aktywna_karte(i));CHECK(page->tabs->aktywna_karta()==i);CHECK(page->panes[i]->rows.category==page->data+i);
    auto* pane=page->panes[i];CHECK(pane->table->obszar().dolny()<=pane->status->obszar().y);CHECK(pane->status->obszar().dolny()<=pane->wysokosc());
   }
   if(dpi==100 && (px==16 || px==24) && width==950){
    std::ofstream f(std::string(".bsf-test/support-layout-")+std::to_string(px)+".json");
    auto rect=[&](Prostokat r){f<<"["<<r.x<<","<<r.y<<","<<r.szerokosc<<","<<r.wysokosc<<"]";};
    f<<"{\"nav\":[";for(int i=0;i<4;++i){if(i)f<<",";rect(body.nav[i]->obszar_ekranu());}f<<"],\"tabs\":[";
    auto origin=page->tabs->obszar_ekranu();for(int i=0;i<3;++i){if(i)f<<",";auto r=page->tabs->prostokat_naglowka(i);r.x+=origin.x;r.y+=origin.y;rect(r);}f<<"],\"table\":";rect(page->panes[0]->table->obszar_ekranu());
    f<<",\"refresh\":";rect(page->reload->obszar_ekranu());f<<",\"size\":";rect(body.fonts->size->obszar_ekranu());f<<",\"apply\":";rect(body.fonts->apply->obszar_ekranu());f<<"}";
   }
   CHECK(page->tabs->obszar().dolny()<page->reload->obszar().y);CHECK(page->reload->obszar().dolny()<=page->wysokosc());
   page->tabs->ustaw_aktywna_karte(0);page->panes[0]->table->ustaw_wybrany_wiersz(255);
   CHECK(page->panes[0]->table->wybrany_wiersz()==255);CHECK(page->data[0].count==256);
  }
  if(px==16 && dpi==100){
   auto* table=body.support->panes[0]->table;auto r=table->obszar();
   std::vector<uint32_t> pixels(size_t(r.szerokosc)*r.wysokosc);KontekstRysowania ctx({0,0,r.szerokosc,r.wysokosc});ctx.ustaw_cel(pixels.data(),{0,0,r.szerokosc,r.wysokosc});
   auto queries=table->liczba_zapytan_dostawcy();table->rysuj_siebie(ctx);
   CHECK(table->pierwszy_widoczny_wiersz()>0);CHECK(table->liczba_zapytan_dostawcy()-queries<32);
   auto first=table->pierwszy_widoczny_wiersz();ZdarzenieRolki wheel;wheel.delta_y=3;table->obsluz_rolke(wheel);table->rysuj_siebie(ctx);CHECK(table->pierwszy_widoczny_wiersz()<first);
   bool clicked=false;body.support->on_reload=[](SettingsUI::SupportPage*,void* p){*static_cast<bool*>(p)=true;};body.support->reload_context=&clicked;
   ZdarzenieKlawiatury enter;enter.kod_klawisza=13;body.support->reload->obsluz_wcisniecie_klawisza(enter);CHECK(clicked);
  }
  // Data and active tab survive a real live generation/family change.
  body.support->ustaw_widoczna(true);body.fonts->ustaw_widoczna(false);body.support->tabs->ustaw_aktywna_karte(2);config.family=BSF::resolve_family("DejaVu Sans Mono");CHECK(BSF::apply(config));
  body.zaktualizuj_motyw();body.rozmiesc({0,0,950*s,512*s});CHECK(body.support->tabs->aktywna_karta()==2);CHECK(body.support->data[0].count==256);
  TaskbarClock::Surface surface;surface.configure(1280*s,720*s,40*s,dpi);auto bounds=surface.bounds;
  int publishes=0;std::vector<uint32_t> visible;uint64_t old_misses=BSF::statistics().misses;
  for(const char* time:{"09:59:59","10:00:00","19:59:59","20:00:00","23:59:59","00:00:00"}){
   int before=publishes;
   CHECK(surface.update(time,false,[&](Prostokat r,const uint32_t* pixels){
    CHECK(r==bounds);++publishes;int aa=0,text=0;
    uint32_t bg=0xff000000U|motyw_kolor(MOTYW_KOLOR_PODNIESIONY),border=0xff000000U|motyw_kolor(MOTYW_KOLOR_RAMKA),fg=0xff000000U|motyw_kolor(MOTYW_KOLOR_AKCENT);
    for(int y=1;y<r.wysokosc;++y)for(int x=1;x<r.szerokosc;++x){auto p=pixels[y*r.szerokosc+x];if(p!=bg){++text;if(p!=fg && p!=border)++aa;}}
    CHECK(text>0);CHECK(aa>0);CHECK(pixels[0]==border);visible.assign(pixels,pixels+size_t(r.szerokosc)*r.wysokosc);return true;
   }));CHECK(publishes==before+1);CHECK(surface.bounds==bounds);
   CHECK(!surface.update(time,false,[&](Prostokat,const uint32_t*){++publishes;return true;}));CHECK(publishes==before+1);
  }
  // Cached face/masks are reused; a matching displayed string does no work.
  auto misses=BSF::statistics().misses;CHECK(misses>=old_misses);
  CHECK(!surface.update("00:00:00",false,[](Prostokat,const uint32_t*){return false;}));CHECK(BSF::statistics().misses==misses);
 }
 // WM integration: one region replacement and no rectangle/text GUI operations.
 ui_skala_procent=100;screen_w=1280;screen_h=720;metryki_ekranu();strcpy(ostatni_czas,"19:59:59");
 test_paint_rects.clear();test_capture_paints=true;int before=test_region_replacements;
 CHECK(RysujZegar());CHECK(test_region_replacements==before+1);CHECK(test_paint_rects.size()==1);
 CHECK(test_paint_rects[0].x==taskbar_clock->bounds.x);CHECK(test_paint_rects[0].w==taskbar_clock->bounds.szerokosc);
 CHECK(!RysujZegar());CHECK(test_region_replacements==before+1);test_capture_paints=false;
}
