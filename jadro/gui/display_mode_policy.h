#pragma once
#include <stdint.h>

// Host UI size is a hint, never the guest's selected physical mode or DPI.
// Default: bootloader/config/authorized guest selection owns the mode.
// Opt-in host following requires a stable hint and stops on a feedback cycle.
struct DisplayModePolicy {
    bool follow_host=false,observe_host=false,transaction_pending=false;
    uint32_t initial_width=1280,initial_height=720;
    uint32_t candidate_w=0,candidate_h=0,previous_w=0,previous_h=0;
    uint64_t candidate_since=0,last_commit=0;
    void guest_selected(){follow_host=false;candidate_since=0;}
    bool observe(uint32_t w,uint32_t h,uint32_t active_w,uint32_t active_h,uint64_t now){
        if(!follow_host||transaction_pending||!now)return false;
        if(w<640||h<480||w>4096||h>4096||uint64_t(w)*h*4>64ULL*1024*1024){candidate_since=0;return false;}
        if(w==active_w&&h==active_h){candidate_since=0;return false;}
        if(w!=candidate_w||h!=candidate_h||!candidate_since){candidate_w=w;candidate_h=h;candidate_since=now;return false;}
        if(now<candidate_since||now-candidate_since<1000000000ULL)return false;
        // A -> B -> A within ten seconds is frontend feedback, not a new mode.
        if(last_commit&&now>=last_commit&&now-last_commit<10000000000ULL&&w==previous_w&&h==previous_h){guest_selected();return false;}
        return true;
    }
    void committed(uint32_t old_w,uint32_t old_h,uint64_t now){
        previous_w=old_w;previous_h=old_h;last_commit=now;candidate_since=0;
    }
};
inline DisplayModePolicy display_mode_policy{};
