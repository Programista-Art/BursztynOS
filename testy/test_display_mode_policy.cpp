#include <initializer_list>
#include <cassert>
#include <cstdio>
#include "jadro/gui/display_mode_policy.h"
int main(){
 DisplayModePolicy p{};
 // The actual reported sizes must never override an explicit guest mode.
 for(uint64_t t=1;t<30;++t)assert(!p.observe(3440,t%2?1322:1440,1280,720,t*1000000000ULL));
 p.follow_host=true;
 assert(!p.observe(1920,1080,1280,720,1000000000ULL));
 assert(!p.observe(1920,1080,1280,720,1500000000ULL));
 assert(p.observe(1920,1080,1280,720,2000000000ULL));
 p.committed(1280,720,2000000000ULL);
 // Alternating hints never settle.
 for(uint64_t t=3;t<20;++t)assert(!p.observe(3440,t%2?1322:1440,1920,1080,t*500000000ULL));
 // Full monitor <-> client-area feedback is stopped, even beyond 32px.
 p={};p.follow_host=true;
 assert(!p.observe(3440,1322,3440,1440,20000000000ULL));
 assert(p.observe(3440,1322,3440,1440,21000000000ULL));
 p.committed(3440,1440,21000000000ULL);
 assert(!p.observe(3440,1440,3440,1322,22000000000ULL));
 assert(!p.observe(3440,1440,3440,1322,23000000000ULL));
 assert(!p.follow_host);
 p.follow_host=true;
 assert(!p.observe(2560,1440,1920,1080,30000000000ULL));
 assert(p.observe(2560,1440,1920,1080,32000000000ULL));
 p.guest_selected();assert(!p.observe(2560,1440,1920,1080,34000000000ULL));
 p.follow_host=true;
 for(auto wh:{0U,4097U,0xffffffffU})assert(!p.observe(wh,1440,1280,720,35000000000ULL));
 assert(!p.observe(1920,1080,1280,720,0));
 puts("Display mode policy: explicit selection, debounce, feedback suppression, invalid hints PASS");
}
