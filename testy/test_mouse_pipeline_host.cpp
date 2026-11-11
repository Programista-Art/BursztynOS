// Mouse pipeline host test (2026-11-11): host frontend model -> QEMU input
// event -> production InputDevice -> production graphics cursor/hit-test.
// Usage: mouse-pipeline WIDTH HEIGHT   (the Makefile runs every resolution)
// Reference: docs/virtio-gpu-mouse-reliability-2026-11-11.md
#define BURSZTYN_POINTER_GRAPHICS_FIXTURE 1
#include "test_splash_production_path.cpp"
#include <cstdlib>
#include "jadro/gui/xhci_pointing_diag.h"
extern "C" bool zaktualizuj_klawisz_fizyczny_gui(uint32_t,bool,uint32_t);
extern "C" bool zaktualizuj_klawiature_gui(char);
extern "C" bool zaktualizuj_tekst_gui(uint32_t,uint32_t);
void ZwolnijCiagleRamki(void*,size_t){assert(false);}
void kfree(void* p){free(p);}
extern "C" void WybudzProcesyOczekujaceNaMysz(){}
extern "C" void bws_gui_ustaw_przejecie_myszy(bool);

namespace {
unsigned mchecks=0;
#define MCHECK(c) do{++mchecks;if(!(c)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c);std::abort();}}while(0)
constexpr int32_t QEMU_ABS_MAX=0x7FFF;
int W=1280,H=720;

void init_wh(std::vector<uint32_t>& native) {
    alignas(8) uint8_t tags[72]{};
    uint32_t total=64;memcpy(tags,&total,4);
    TagFramebufferMB2 fb{8,38,reinterpret_cast<uint64_t>(native.data()),uint32_t(W*4),uint32_t(W),uint32_t(H),32,1,0};
    memcpy(tags+8,&fb,sizeof(fb));const uint8_t rgb[]={16,8,8,8,0,8};memcpy(tags+40,rgb,6);
    uint32_t tag=12,size=8;memcpy(tags+48,&tag,4);memcpy(tags+52,&size,4);
    uint32_t end_size=8;memcpy(tags+60,&end_size,4);
    InicjalizujGrafike(reinterpret_cast<uint64_t>(tags));
}
int cx(){return grafika_pobierz_mysz_x();}
int cy(){return grafika_pobierz_mysz_y();}

// ---- host frontend models (sources named in the reliability report) ----
// QEMU 8.2 ui/input.c qemu_input_scale_axis(value, 0, size, 0, 0x7fff).
int32_t qemu_abs(int px,int size){return int32_t(int64_t(px)*QEMU_ABS_MAX/size);}
// SDL 2.30.0 src/video/x11/SDL_x11xinput2.c:301, XI_RawMotion of a device whose
// valuator is in absolute mode (VirtualBox "mouse integration": 0..65535):
//     processed = prev_coords - coords   /* convert absolute to relative */
struct SdlXi2AbsoluteDevice {
    double prev=0; int screen;
    explicit SdlXi2AbsoluteDevice(int host_screen):screen(host_screen){}
    double raw(int host_px)const{return double(host_px)*65535.0/(screen-1);}
    int relative_from(int host_px){const double c=raw(host_px);const int d=int(prev-c);prev=c;return d;}
};

void rel(InputDevice& d,int32_t dx,int32_t dy){
    InputEvent e[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(dx)},{INPUT_EV_REL,INPUT_REL_Y,uint32_t(dy)},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&d,e,3);
}
void abs_raw(InputDevice& d,int32_t x,int32_t y){
    InputEvent e[]={{INPUT_EV_ABS,INPUT_ABS_X,uint32_t(x)},{INPUT_EV_ABS,INPUT_ABS_Y,uint32_t(y)},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&d,e,3);
}
void host_pointer_at(InputDevice& tablet,int px,int py){abs_raw(tablet,qemu_abs(px,W),qemu_abs(py,H));}
void button(InputDevice& d,bool down){
    InputEvent e[]={{INPUT_EV_KEY,INPUT_BTN_LEFT,down?1U:0U},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&d,e,2);
}
InputDevice virtio_tablet(){
    // QEMU >= 8.0 virtio-tablet: ABS 0..0x7fff, BTN_TOUCH bit but no
    // INPUT_PROP_DIRECT -> pointer tablet, hover moves (no contact gating).
    InputDevice d{};std::snprintf(d.name,sizeof(d.name),"QEMU Virtio Tablet");
    d.kinds=INPUT_DEVICE_KIND_TABLET;d.bus_type=INPUT_BUS_VIRTIO;d.pointer_mode=INPUT_POINTER_ABSOLUTE;
    d.abs_x={0,QEMU_ABS_MAX,0,0,0};d.abs_y={0,QEMU_ABS_MAX,0,0,0};d.abs_contact_required=false;return d;
}
InputDevice virtio_mouse(){
    InputDevice d{};std::snprintf(d.name,sizeof(d.name),"QEMU Virtio Mouse");
    d.kinds=INPUT_DEVICE_KIND_MOUSE;d.bus_type=INPUT_BUS_VIRTIO;d.pointer_mode=INPUT_POINTER_RELATIVE;return d;
}
InputDevice ps2_mouse(){
    InputDevice d{};std::snprintf(d.name,sizeof(d.name),"ps2-mouse");
    d.kinds=INPUT_DEVICE_KIND_MOUSE;d.bus_type=INPUT_BUS_PS2;d.pointer_mode=INPUT_POINTER_RELATIVE;return d;
}
} // namespace

int main(int argc,char** argv){
    if(argc==3){W=std::atoi(argv[1]);H=std::atoi(argv[2]);}
    MCHECK(W>=640&&H>=480&&W<=4096&&H<=4096);
    std::vector<uint32_t> native(size_t(W)*H,0);
    init_wh(native);input_reset_all();input_set_pointer_ready(true);
    MCHECK(grafika_pobierz_szerokosc()==W&&grafika_pobierz_wysokosc()==H);
    InputDevice tablet=virtio_tablet(),mouse=virtio_mouse(),ps2=ps2_mouse();
    MCHECK(input_register_device(&ps2));MCHECK(input_register_device(&mouse));MCHECK(input_register_device(&tablet));

    // 1. Root cause model: VirtualBox-style absolute host pointer + SDL relative
    //    grab -> QEMU relative event -> guest. Host moves RIGHT, guest receives a
    //    NEGATIVE, device-unit sized delta and (faithfully) moves LEFT.
    {
        zaktualizuj_mysze_pozycja_zrodlo(W/2,H/2,0,"setup");
        SdlXi2AbsoluteDevice sdl(1920);
        (void)sdl.relative_from(900);           // first raw sample after grab
        const int dx=sdl.relative_from(910);    // host pointer 10 px to the right
        MCHECK(dx<0 && -dx>10*30);              // inverted and ~34x magnified
        const int before=cx();rel(mouse,dx,0);
        MCHECK(cx()<before);                    // guest applies exactly what it got
        MCHECK(cx()==std::max(0,before+dx));
    }

    // 2. Relative path (host pointer that really is relative): 1:1, signed.
    for(InputDevice* d:{&mouse,&ps2}){
        const std::pair<int,int> cases[]={{1,0},{-1,0},{0,1},{0,-1},{25,0},{-25,0},{0,25},{0,-25},
            {17,17},{-17,17},{17,-17},{-17,-17},{0,0},{127,-127},{-128,127}};
        for(auto [dx,dy]:cases){
            zaktualizuj_mysze_pozycja_zrodlo(W/2,H/2,0,"setup");
            rel(*d,dx,dy);
            MCHECK(cx()==W/2+dx&&cy()==H/2+dy); // +X right, +Y down (screen-down)
        }
        zaktualizuj_mysze_pozycja_zrodlo(W/2,H/2,0,"setup");
        for(int i=0;i<200;++i)rel(*d,i%2?-9:9,i%2?7:-7);   // repeated reversals
        MCHECK(cx()==W/2&&cy()==H/2);
        rel(*d,INT32_MIN,INT32_MIN);MCHECK(cx()==0&&cy()==0);         // clamp, no overflow
        rel(*d,INT32_MAX,INT32_MAX);MCHECK(cx()==W-1&&cy()==H-1);
        rel(*d,-W*4,0);MCHECK(cx()==0&&cy()==H-1);
        rel(*d,1,0);MCHECK(cx()==1);                                  // leaves the edge at once
    }

    // 3. Absolute path (the fix): host pixel -> QEMU abs -> guest pixel.
    int max_err=0;
    for(int px=0;px<W;++px){host_pointer_at(tablet,px,H/2);max_err=std::max(max_err,std::abs(cx()-px));}
    for(int py=0;py<H;++py){host_pointer_at(tablet,W/2,py);max_err=std::max(max_err,std::abs(cy()-py));}
    MCHECK(max_err<=1);                                   // resolution-independent scaling
    int prev=-1;for(int px=0;px<W;++px){host_pointer_at(tablet,px,0);MCHECK(cx()>=prev);prev=cx();}
    abs_raw(tablet,0,0);MCHECK(cx()==0&&cy()==0);                         // minimum
    abs_raw(tablet,QEMU_ABS_MAX,QEMU_ABS_MAX);MCHECK(cx()==W-1&&cy()==H-1);// maximum
    abs_raw(tablet,QEMU_ABS_MAX/2,QEMU_ABS_MAX/2);                        // midpoint
    MCHECK(std::abs(cx()-(W-1)/2)<=1&&std::abs(cy()-(H-1)/2)<=1);
    abs_raw(tablet,-5000,QEMU_ABS_MAX+5000);MCHECK(cx()==0&&cy()==H-1);   // out of range clamps
    // Directions through the absolute path.
    const int ox=W/2,oy=H/2;
    const std::pair<int,int> dirs[]={{40,0},{-40,0},{0,40},{0,-40},{30,30},{-30,30},{30,-30},{-30,-30}};
    for(auto [dx,dy]:dirs){
        host_pointer_at(tablet,ox,oy);const int bx=cx(),by=cy();
        host_pointer_at(tablet,ox+dx,oy+dy);
        MCHECK((dx>0)==(cx()>bx)&&(dx<0)==(cx()<bx)&&(dy>0)==(cy()>by)&&(dy<0)==(cy()<by));
        MCHECK(std::abs((cx()-bx)-dx)<=1&&std::abs((cy()-by)-dy)<=1);
    }

    // 4. Duplicate prevention: an identical absolute report is not a new motion.
    host_pointer_at(tablet,123,77);const unsigned before_dup=pointing_counters.cursor_commits;
    host_pointer_at(tablet,123,77);
    MCHECK(pointing_counters.cursor_commits==before_dup);

    // 5. Device switching: relative after absolute continues from the cursor,
    //    absolute after relative jumps exactly to the host position (no drift).
    host_pointer_at(tablet,200,100);const int sx=cx(),sy=cy();
    rel(mouse,5,-3);MCHECK(cx()==sx+5&&cy()==sy-3);
    rel(ps2,-2,2);MCHECK(cx()==sx+3&&cy()==sy-1);
    // An identical absolute state is not a new motion (HID/virtio reports are
    // state; a repeated tablet sample must not steal the cursor from another
    // device, e.g. PS/2 beside a USB tablet). The next real host move resyncs.
    host_pointer_at(tablet,200,100);MCHECK(cx()==sx+3&&cy()==sy-1);
    host_pointer_at(tablet,240,130);
    MCHECK(std::abs(cx()-240)<=1&&std::abs(cy()-130)<=1);

    // 6. Malformed: absolute device with an empty range never moves the cursor.
    InputDevice bad=virtio_tablet();bad.abs_x={10,10,0,0,0};bad.abs_y={5,1,0,0,0};
    MCHECK(input_register_device(&bad));
    const int mx=cx(),my=cy();abs_raw(bad,999,999);MCHECK(cx()==mx&&cy()==my);
    MCHECK(input_unregister_device(&bad));
    // SYN_DROPPED discards the partial report.
    InputEvent dropped[]={{INPUT_EV_REL,INPUT_REL_X,50},{INPUT_EV_SYN,INPUT_SYN_DROPPED,0},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&mouse,dropped,3);MCHECK(cx()==mx&&cy()==my);

    // 7. Click position == visible cursor == hit-test: two overlapping layers.
    MCHECK(utworz_warstwe(1,0,0,W,H-40,0)==1);
    MCHECK(utworz_warstwe(2,100,100,400,300,10)==2);
    MCHECK(utworz_warstwe(3,300,250,400,300,11)==3);
    MCHECK(skladacz_obrazu_ustaw_tytul(2,"A"));MCHECK(skladacz_obrazu_ustaw_tytul(3,"B"));
    // Opaque client pixels, as drawn by an application: hit-testing passes
    // through fully transparent pixels by design.
    for(int pid:{2,3}){warstwa_obrazu* l=pobierz_warstwe(pid);MCHECK(l&&l->bufor_pikseli);
        std::fill(l->bufor_pikseli,l->bufor_pikseli+size_t(l->szerokosc)*l->wysokosc,0xFF303030U);
        MCHECK(skladacz_obrazu_publikuj_klatke(pid));}   // first frame: window becomes hit-testable
    powitanie_inicjalizuj(false);
    aktualny_pid=2;bws_gui_ustaw_przejecie_myszy(true);aktualny_pid=0; // GUI owns the mouse (as after BWS start)
    host_pointer_at(tablet,150,150);button(tablet,true);button(tablet,false);   // only layer 2
    MCHECK(cx()==150&&cy()==150);MCHECK(bws_gui_aktywny_pid()==2);
    // The click raised layer 2: the overlap now belongs to 2.
    host_pointer_at(tablet,350,300);button(tablet,true);button(tablet,false);
    MCHECK(bws_gui_aktywny_pid()==2);
    host_pointer_at(tablet,650,500);button(tablet,true);button(tablet,false);   // only layer 3
    MCHECK(bws_gui_aktywny_pid()==3);
    host_pointer_at(tablet,350,300);button(tablet,true);button(tablet,false);   // 3 raised: overlap is 3
    MCHECK(bws_gui_aktywny_pid()==3);
    host_pointer_at(tablet,150,150);button(tablet,true);button(tablet,false);   // back to 2
    MCHECK(bws_gui_aktywny_pid()==2);
    usun_warstwe(3);usun_warstwe(2);usun_warstwe(1);

    // 8. Re-initialisation: unregister/register the tablet; first report maps exactly.
    MCHECK(input_unregister_device(&tablet));tablet=virtio_tablet();MCHECK(input_register_device(&tablet));
    host_pointer_at(tablet,W-10,H-10);MCHECK(std::abs(cx()-(W-10))<=1&&std::abs(cy()-(H-10))<=1);

    input_reset_all();StanBuforowEkranu last{};grafika_pobierz_stan(&last);grafika_wymien_stan({});grafika_zwolnij_stan(&last);
    MCHECK(graphical_allocs.empty());
    printf("Mouse pipeline %dx%d: %u checks PASS (max absolute error %d px)\n",W,H,mchecks,max_err);
}
