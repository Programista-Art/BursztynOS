#pragma once
// Uses actual BSF faces, shared decoder, production Start and retained pixels.
static void power_png_cases(){
 unsigned before=checks;
 const char* paths[]={"/ikonki/przyciskzasilania.png","/ikonki/przyciskzablokuj.png","/ikonki/przyciskuspij.png","/ikonki/przyciskzamknij.png","/ikonki/przyciskuruchomponownie.png"};
 for(auto path:paths){
  IkonaPNG icon;auto reads=icon_reads;
  icon.przygotuj(path,22);CHECK(icon.piksele());CHECK(icon.bok()==22);CHECK(icon_reads==reads+1);
  auto* first=icon.piksele();icon.przygotuj(path,22);CHECK(first==icon.piksele());CHECK(icon_reads==reads+1);
  for(int side:{28,33,44,22}){icon.przygotuj(path,side);CHECK(icon.bok()==side);CHECK(icon_reads==reads+1);}
  // Scaling always returns to exactly the same pixels from the original.
  std::vector<uint32_t> original(icon.piksele(),icon.piksele()+22*22);
  icon.przygotuj(path,44);icon.przygotuj(path,22);
  CHECK(!memcmp(original.data(),icon.piksele(),original.size()*4));
 }
 // Independent file failures; successful assets remain available and action IDs
 // and disabled state survive. No file mutation touches the checkout.
 for(int failure=0;failure<4;++failure){
  std::string path=paths[failure==0?2:failure==1?3:1],old=files[path];
  if(failure==0)files.erase(path);
  if(failure==1)files[path]="broken PNG";
  if(failure==2){files[path][16]=1;}
  if(failure==3)files[path]=std::string(256*1024+1,'x');
  StartMenu::Surface s;s.open({0,0,1280,680},10);s.open_power();CHECK(s.czy_otwarta());CHECK(s.power_open());
  for(size_t i=0;i<4;++i){auto* e=s.power_menu().element(i);CHECK(e->ikona()!=0);CHECK(e->czy_wlaczony()==(i>=2));
   const char* item=paths[i+1];CHECK(bool(e->obraz_ikony()->piksele())==(path!=item));}
  auto reads=icon_reads;s.refresh();CHECK(icon_reads==reads);s.close();files[path]=old;
 }
 // Exact source-over and disabled opacity, with target and clipping paths.
 for(uint32_t background:{0xff102030U,0xffeeeeeeU,0U})for(bool enabled:{true,false}){
  uint32_t source[]={0x80ff0000U,0xff00ff00U,0x000000ffU,0xff000000U};
  uint32_t target[4]={background,background,background,background};
  KontekstRysowania ctx({0,0,2,2});ctx.ustaw_cel(target,{0,0,2,2});
  rysuj_png(ctx,{0,0,2,2},source,2,enabled);
  for(int i=0;i<4;++i){auto c=source[i];if(!enabled)c=(c&0xffffffU)|((((c>>24)*96+127)/255)<<24);CHECK(target[i]==kolor_nad(background,c));}
 }
 std::ofstream matrix(".bsf-test/power-png-metrics.tmp");matrix<<"[";bool comma=false;
 for(int px:{12,16,20,24})for(unsigned scale:{100U,125U,150U,200U})for(auto screen:{Rozmiar(1280,720),Rozmiar(1920,1080)}){
  auto config=BSF::configuration();config.size=px;CHECK(BSF::apply(config));ui_skala_procent=scale;auto dpi=ui_skala();
  StartMenu::Surface s;s.open({0,0,screen.szerokosc,screen.wysokosc-40*dpi},10);s.open_power();
  auto button=s.shutdown_bounds(),start=s.obszar(),popup=s.power_popup_bounds();
  CHECK(start.prawy()-button.prawy()==8*dpi);CHECK(start.dolny()-button.dolny()==8*dpi);
  CHECK(button.szerokosc==36*dpi);CHECK(button.szerokosc==button.wysokosc);
  CHECK(popup.dolny()+10*dpi==button.y);CHECK(popup.prawy()==button.prawy());CHECK(!popup.przecina(button));
  CHECK(popup.x>=0&&popup.y>=0&&popup.prawy()<=screen.szerokosc&&popup.dolny()<=screen.wysokosc-40*dpi);
  int longest=0;const char* label="";
  for(size_t i=0;i<4;++i){auto* item=s.power_menu().element(i);int w=CzcionkaUI::zmierz_tekst(item->tekst(),dpi);if(w>longest){longest=w;label=item->tekst();}CHECK(item->obraz_ikony()->bok()==18*dpi);CHECK(item->obraz_ikony()->piksele());}
  CHECK(!strcmp(label,"Uruchom ponownie"));
  int expected=2*dpi+16*dpi+24*dpi+longest;if(expected<150*dpi)expected=150*dpi;
  CHECK(popup.szerokosc==expected);
  auto reads=icon_reads;s.refresh();CHECK(icon_reads==reads);CHECK(s.power_popup_bounds()==popup);
  // Theme repaint must retain metrics and PNG source data.
  for(auto theme:{MOTYW_KOLOR_TLO,MOTYW_KOLOR_PANEL}){(void)theme;s.refresh();CHECK(s.power_popup_bounds()==popup);CHECK(icon_reads==reads);}
  if(comma)matrix<<",";comma=true;
  matrix<<"{\"font\":"<<px<<",\"scale\":"<<scale<<",\"screen\":["<<screen.szerokosc<<","<<screen.wysokosc<<"],\"text\":"<<longest<<",\"width\":"<<popup.szerokosc<<",\"button\":["<<button.x<<","<<button.y<<","<<button.szerokosc<<","<<button.wysokosc<<"],\"popup\":["<<popup.x<<","<<popup.y<<","<<popup.szerokosc<<","<<popup.wysokosc<<"]}";
  s.close();
 }
 // Live font changes preserve a selected enabled action and cached originals.
 {
  ui_skala_procent=100;auto config=BSF::configuration();config.size=12;CHECK(BSF::apply(config));
  StartMenu::Surface s;s.open({23,31,1280,680},33);s.open_power();
  auto* menu=static_cast<WidokMenuPopup*>(s.events.popups.fokus_popupu());menu->ustaw_aktywny_indeks(3);
  int old_width=s.power_popup_bounds().szerokosc;auto reads=icon_reads;
  config.size=24;CHECK(BSF::apply(config));s.refresh();CHECK(s.power_open());
  CHECK(menu->aktywny_indeks()==3);CHECK(s.power_popup_bounds().szerokosc>old_width);CHECK(icon_reads==reads);
  CHECK(s.power_popup_bounds().x>=23);CHECK(s.power_popup_bounds().y>=31);
  config.family=BSF::resolve_family("DejaVu Sans Mono");CHECK(config.family!=BSF::default_id());CHECK(BSF::apply(config));s.refresh();
  int longest=0;for(size_t i=0;i<4;++i){int w=CzcionkaUI::zmierz_tekst(s.power_menu().element(i)->tekst());if(w>longest)longest=w;}
  CHECK(s.power_popup_bounds().szerokosc==42+longest);CHECK(icon_reads==reads);CHECK(menu->aktywny_indeks()==3);
  config.family=BSF::default_id();CHECK(BSF::apply(config));s.refresh();

  auto* icon=s.power_menu().element(0)->obraz_ikony();CHECK(icon->bok()==18);
  s.close();
  ui_skala_procent=200;s.open({0,0,1920,1000},10);s.open_power();CHECK(icon->bok()==36);CHECK(icon_reads==reads);s.close();
  ui_skala_procent=100;s.open({0,0,1280,680},10);s.open_power();CHECK(icon->bok()==18);CHECK(icon_reads==reads);s.close();
 }
 matrix<<"]";matrix.close();CHECK(!::rename(".bsf-test/power-png-metrics.tmp",".bsf-test/power-png-metrics.json"));
 printf("Power PNG / BSF geometry: %u checks PASS\n",checks-before);
}
