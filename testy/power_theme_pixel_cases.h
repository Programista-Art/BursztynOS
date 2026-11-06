#pragma once
// Compare real PNG button pixels with an ordinary control and current roles.
static void power_theme_pixel_cases(){
 auto previous=gui_pobierz_motyw();auto old_scale=ui_skala_procent;ui_skala_procent=100;
 std::vector<BursztynMotyw> themes;
 for(auto id:{"bursztynnocny","baltyckanoc","grafit","bursztynklasyczny","bursztynjasny"}){
  BursztynMotyw m{};CHECK(motyw_zaladuj_z_pliku(id,&m));themes.push_back(m);
 }
 auto extra=themes.back();strcpy(extra.id,"fixture_extra");
 extra.przycisk=0x395127;extra.przycisknajechany=0x516b38;
 extra.akcent=0xc7d26b;extra.akcentnajechany=0xd8e28a;extra.akcentaktywny=0x738137;extra.ramka=0x68745e;
 themes.push_back(extra); // Same enumeration, no renderer switch on ID/name.
 StartMenu::Surface retained;IkonaPNG png;png.przygotuj("/ikonki/przyciskzasilania.png",22);CHECK(png.piksele());
 std::vector<uint32_t> sheet(216*216,0xff808080);
 std::ofstream results(".bsf-test/power-theme-states.json");results<<"[";bool comma=false;
 for(size_t ti=0;ti<themes.size();++ti){
  test_ustaw_aktywny_motyw(&themes[ti]);retained.refresh();retained.open({0,0,1280,680},10);
  auto* button=retained.dziecko(2);CHECK(!button->czy_ma_fokus());
  Przycisk ordinary("");ordinary.rozmiesc({0,0,36,36});
  for(int state=0;state<6;++state){
   button->obsluz_opuszczenie_myszy();button->przy_utracie_fokusu();
   ordinary.obsluz_opuszczenie_myszy();ordinary.przy_utracie_fokusu();
   uint32_t bg=motyw_kolor(MOTYW_KOLOR_PRZYCISK),border=motyw_kolor(MOTYW_KOLOR_RAMKA);
   if(state==1){button->obsluz_wejscie_myszy();ordinary.obsluz_wejscie_myszy();bg=motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY);border=motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY);}
   if(state==2){ZdarzenieMyszki e{};e.przycisk=1;button->obsluz_wcisniecie_myszy(e);ordinary.obsluz_wcisniecie_myszy(e);bg=border=motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY);}
   if(state==3){button->przy_fokusie();ordinary.przy_fokusie();border=motyw_kolor(MOTYW_KOLOR_AKCENT);}
   if(state==4){retained.open_power();CHECK(!button->czy_ma_fokus());bg=border=motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY);}
   if(state==5){retained.events.popups.zamknij_od_poziomu(1);button->ustaw_wlaczona(false);ordinary.ustaw_wlaczona(false);}
   std::vector<uint32_t> actual(36*36,0xff808080),reference=actual;
   KontekstRysowania a({0,0,36,36}),b({0,0,36,36});a.ustaw_cel(actual.data(),{0,0,36,36});b.ustaw_cel(reference.data(),{0,0,36,36});
   button->rysuj_siebie(a);
   if(state==4){auto active=ordinary.styl().wcisniety;ordinary.rysuj_tlo_i_ramke(b,active);}
   else ordinary.rysuj_siebie(b);
   CHECK((actual[3*36+18]&0xffffff)==bg);CHECK((actual[18]&0xffffff)==border);
   // All shape/background/border pixels must agree, including transparent PNG
   // pixels. Composite the source independently over the ordinary button.
   for(int y=0;y<36;++y)for(int x=0;x<36;++x){
    auto expected=reference[y*36+x];
    if(x>=7&&x<29&&y>=7&&y<29){auto c=png.piksele()[(y-7)*22+x-7];if(state==5)c=(c&0xffffff)|((((c>>24)*96+127)/255)<<24);expected=kolor_nad(expected,c);}
    CHECK(actual[y*36+x]==expected);sheet[(ti*36+y)*216+state*36+x]=actual[y*36+x];
   }
   if(comma)results<<",";comma=true;results<<"{\"theme\":\""<<themes[ti].id<<"\",\"state\":"<<state<<",\"background_role_value\":"<<bg<<",\"border_role_value\":"<<border<<",\"pixels\":1296}";
  }
  button->ustaw_wlaczona(true);retained.close();
  // Change theme while both surfaces are open; compare the complete palette
  // before repaint, including focus/hover roles and popup selection retention.
  retained.open({0,0,1280,680},10);retained.open_power();
  test_ustaw_aktywny_motyw(&themes[(ti+1)%themes.size()]);retained.refresh();CHECK(retained.power_open());
  auto expected=Przycisk("").styl();auto got=button->styl();
  CHECK(got.normalny.tlo==expected.normalny.tlo);CHECK(got.najechany.tlo==expected.najechany.tlo);
  CHECK(got.wcisniety.tlo==expected.wcisniety.tlo);CHECK(got.fokus.ramka==expected.fokus.ramka);
  retained.close();
 }
 results<<"]\n";
 std::ofstream ppm(".bsf-test/power-theme-states.ppm",std::ios::binary);ppm<<"P6\n216 216\n255\n";
 for(auto c:sheet){char rgb[]={char(c>>16),char(c>>8),char(c)};ppm.write(rgb,3);}
 test_ustaw_aktywny_motyw(previous);ui_skala_procent=old_scale;
}
