#define BURSZTYNDIAGGUIRUNTIME 1
#include "gui_runtime_diag.h"
#include <cassert>
#include <string>
#include <cstring>
static uint64_t mock_time=1000000;
static std::string log;
proces_t tablica_procesow[MAKS_PROCESOW]{};
bool hpet_dostepny(){return true;}
uint64_t czas_monotoniczny_ns(){return mock_time;}
void klog_serial(const char* text){log+=text;}
int main(){
    tablica_procesow[2].stan=PROCES_GOTOWY;
    strcpy(tablica_procesow[2].sciezka_pliku,"/programy/shell.bur");
    gui_perf_switch(2);mock_time+=2000000;gui_perf_block(2);
    mock_time+=100000000;gui_perf_switch(0);
    assert(gui_perf_counts[2][GP_CPU_NS]==2000000); // HLT wait is excluded
    gui_perf_add(2,GP_WAKE);gui_perf_add(2,GP_MOVE,100);
    gui_perf_max(2,GP_LOCK_MAX_NS,100);gui_perf_max(2,GP_LOCK_MAX_NS,50);
    assert(gui_perf_counts[2][GP_LOCK_MAX_NS]==100);
    mock_time+=1000;gui_perf_idle_begin();
    mock_time+=7000;gui_perf_idle_end();
    assert(gui_perf_counts[0][GP_IDLE_NS]==7000);
    mock_time+=1000;gui_perf_mark_input();
    mock_time+=4000;gui_perf_frame_presented();
    mock_time+=10000;gui_perf_frame_presented();
    assert(gui_perf_counts[0][GP_FRAME_PRESENT]==2);
    assert(gui_perf_counts[0][GP_FRAME_INTERVAL_NS]==10000);
    assert(gui_perf_counts[0][GP_INPUT_PRESENT_NS]==4000);
    assert(gui_perf_counts[0][GP_INPUT_PRESENT_MAX_NS]==4000);
    gui_perf_report();assert(log.empty());mock_time+=1000000000;gui_perf_report();
    assert(log.find("pid=2 path=/programy/shell.bur")!=std::string::npos);
    assert(log.find("cpu_ns=2000000 idle_ns=0 wake=1")!=std::string::npos);
    assert(log.find("move_requested=100")!=std::string::npos);
    assert(gui_perf_counts[2][GP_CPU_NS]==0);
}
