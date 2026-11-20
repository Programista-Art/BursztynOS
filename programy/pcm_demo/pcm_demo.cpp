#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#include "bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../jadro/bws/pcm_api.h"
#include "refill_policy.h"
#ifndef PCM_HZ
#define PCM_HZ 440
#endif
class PcmWindow : public BursztynUI::Okno {
    uint64_t handle = 0, phase = 0, epoch = 0;
    uint32_t volume = 100;
    bool muted = false, playing = true, validated = false;
    int automatic_stage = -1;
    BursztynUI::Etykieta* status = nullptr;
    int64_t call(uint64_t op, uint64_t a=0, uint64_t b=0, uint64_t c=0) {
        return static_cast<int64_t>(bws_wywolaj(BWS_PCM, op, a, b, c));
    }
    void stop() {
        if (handle && call(PCM_CLOSE,handle) == PCM_OK) handle=0;
    }
    bool gain() { return !handle || call(PCM_GAIN,handle,volume,muted)==PCM_OK; }
public:
    PcmWindow() : BursztynUI::Okno(PCM_HZ == 440 ? "PCM 440 Hz" : "PCM 660 Hz", 360, 280) {}
    void init() {
        using namespace BursztynUI;
        auto& root = dodaj<PanelDokowany>();
        root.ustaw_wypelnienie(Marginesy(12));
        status=&root.dodaj<Etykieta>(Dokowanie::Gora, PCM_HZ == 440 ? "PCM 440 Hz" : "PCM 660 Hz");
        auto& play=root.dodaj<Przycisk>(Dokowanie::Gora,"Play / Stop");
        play.przy_kliknieciu([](Przycisk*,void* p) {
            auto* w=static_cast<PcmWindow*>(p); w->playing=!w->playing;
            if(!w->playing) w->stop();
        },this);
        auto& level=root.dodaj<Przycisk>(Dokowanie::Gora,"100% / 25%");
        level.przy_kliknieciu([](Przycisk*,void* p) {
            auto* w=static_cast<PcmWindow*>(p); w->volume=w->volume==100?25:100; w->gain();
        },this);
        auto& mute=root.dodaj<Przycisk>(Dokowanie::Gora,"Mute / Unmute");
        mute.przy_kliknieciu([](Przycisk*,void* p) {
            auto* w=static_cast<PcmWindow*>(p); w->muted=!w->muted; w->gain();
        },this);
        char argument[32]{};
        if(pobierz_argument_startowy(argument,sizeof(argument)))
            for(unsigned i=0; argument[i]>='0'&&argument[i]<='9';++i)
                epoch=epoch*10+static_cast<unsigned>(argument[i]-'0');
    }
    void feed() {
        if(epoch) {
            const auto elapsed=(gui_czas_monotoniczny_ns()-epoch)/1000000;
            const int stage=elapsed<4000?0:elapsed<6000?1:elapsed<8000?2:3;
            if(PCM_HZ==440 && stage!=automatic_stage) {
                volume=stage==0?100:25; muted=stage==2;
                if (gain()) automatic_stage=stage;
            }
            if(PCM_HZ==660 && elapsed>=17000) { playing=false; stop(); }
        }
        if(!playing) return;
        if(!handle) {
            BwsPcmFormat fmt{1,sizeof(fmt),48000,2,1,0};
            const auto result=call(PCM_OPEN,reinterpret_cast<uint64_t>(&fmt),sizeof(fmt),1);
            if(result<=0) { status->ustaw_tekst("PCM unavailable / busy"); return; }
            handle=static_cast<uint64_t>(result);
            validated=false;
            gain();
        }
        if(!validated) {
            BwsPcmState state{};
            const auto invalid=call(PCM_WRITE,handle,0x70000ffe,256);
            const auto oversized=call(PCM_WRITE,handle,0,UINT64_MAX);
            const auto queried=call(PCM_QUERY,handle,reinterpret_cast<uint64_t>(&state),sizeof(state));
            if(invalid==PCM_BUSY || queried==PCM_BUSY) return;
            if(invalid!=PCM_MEMORY || oversized!=PCM_INVALID || queried!=PCM_OK || state.queued) {
                status->ustaw_tekst("PCM validation failed"); playing=false; stop(); return;
            }
            validated=true;
        }
        (void)PcmDemo::refill_stream(
            phase, PCM_HZ,
            [this](BwsPcmState& queue) {
                return call(PCM_QUERY, handle, reinterpret_cast<uint64_t>(&queue), sizeof(queue));
            },
            [this](const int16_t* samples, uint32_t requested) {
                return call(PCM_WRITE, handle, reinterpret_cast<uint64_t>(samples), requested);
            });
    }
    ~PcmWindow() { stop(); }
};
extern "C" __attribute__((noreturn)) void _start() {
    BursztynUI::AplikacjaGUI app;
    auto* window=new PcmWindow;
    window->init();
    app.ustaw_odstep_animacji(1);
    app.ustaw_zadanie_okresowe(1,[](void* p){static_cast<PcmWindow*>(p)->feed();},window);
    (void)app.uruchom(*window);
    delete window;
    bws_zakoncz_proces();
}
