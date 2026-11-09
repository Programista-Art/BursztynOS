// Reuse the PCI/MMIO/DMA apparatus, not its event injector or accumulators.
// This executable never starts an emulator. The production driver, queue and
// InputDevice translation units are linked normally.
#define main existing_virtio_suite_main
#include "test_virtio_pci.cpp"
#undef main
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include "../sterowniki/virtio/virtio_input_forensic.h"

extern "C" void virtio_input_set_desktop_ready(bool) __attribute__((weak));
static uint64_t simulated_ns=1, display_generation=1;
static bool simulated_splash=false;
static std::vector<std::string> forensic_serial;
void host_virtio_input_log(const char* s) { forensic_serial.emplace_back(s); }
uint64_t host_virtio_input_time_ns() { return simulated_ns; }
uint64_t host_virtio_input_geometry_generation() { return display_generation; }
bool host_virtio_input_splash_active() { return simulated_splash; }

namespace forensic {
unsigned checks=0, failures=0, cases=0;
#define CHECK_F(c) do { ++checks; if(!(c)) { ++failures; \
    if(failures<16) std::printf("FORENSIC FAIL line=%d %s\n",__LINE__,#c); } } while(0)
struct Report {
    int64_t x,y;
    bool operator==(const Report& other) const { return x==other.x && y==other.y; }
};
std::vector<Report> actual;
uint64_t digest=1469598103934665603ULL;
void capture(int64_t dx,int64_t dy,uint8_t,const char*) {
    actual.push_back({dx,-int64_t(dy)});
    // Independent signed coordinate model; do not use pointer_accumulate.
    int64_t x=int64_t(g_mock_input_capture.abs_x)+dx;
    int64_t y=int64_t(g_mock_input_capture.abs_y)-int64_t(dy);
    g_mock_input_capture.abs_x=int(x<0?0:x>=g_mock_screen_w?g_mock_screen_w-1:x);
    g_mock_input_capture.abs_y=int(y<0?0:y>=g_mock_screen_h?g_mock_screen_h-1:y);
}
using Bytes=std::array<uint8_t,8>;
Bytes wire(uint16_t type,uint16_t code,int32_t value) {
    const uint32_t bits=uint32_t(value);
    return {uint8_t(type),uint8_t(type>>8),uint8_t(code),uint8_t(code>>8),
        uint8_t(bits),uint8_t(bits>>8),uint8_t(bits>>16),uint8_t(bits>>24)};
}

// Deliberately independent: byte arithmetic, private report state and one
// screen-down report per SYN_REPORT. No production parsing/accumulation helper.
struct Reference {
    int64_t x=0,y=0;
    bool resync=false,relative=true;
    uint32_t syns=0;
    std::vector<Report> complete_reports;
    std::vector<Report> reports;
    void event(const Bytes& p,bool admit) {
        const unsigned type=unsigned(p[0])+256U*p[1];
        const unsigned code=unsigned(p[2])+256U*p[3];
        if(type==0 && code==0)++syns;
        uint64_t bits=uint64_t(p[4])+256ULL*p[5]+65536ULL*p[6]+16777216ULL*p[7];
        const int64_t value=bits>=2147483648ULL?int64_t(bits)-4294967296LL:int64_t(bits);
        if(type==0 && code==3) { x=y=0;resync=true;return; }
        if(!admit || !relative) {
            x=y=0;
            if(type==0 && code==0)resync=false;
            else if(type==2)resync=true;
            return;
        }
        if(resync) { if(type==0 && code==0)resync=false;return; }
        if(type==2 && code==0)x+=value;
        if(type==2 && code==1)y+=value;
        if(type==0 && code==0) {
            complete_reports.push_back({x,y}); // One logical report, even zero.
            if(x || y) reports.push_back({x,y});
            x=y=0;
        }
    }
};

struct Device {
    InitFunction function;
    uint16_t consumed=0;
    bool admit=true;
    std::deque<std::pair<Bytes,bool>> written;
    Reference reference;
    explicit Device(uint8_t slot,bool mouse=true,uint16_t size=64):function(slot,false,size) {
        function.relative=mouse;reference.relative=mouse;
        init_functions.push_back(&function);
    }
    ~Device() {
        if(function.drv.dev) CHECK_F(virtio_input_reset(&function.drv));
        auto i=std::find(init_functions.begin(),init_functions.end(),&function);
        if(i!=init_functions.end())init_functions.erase(i);
    }
    bool prepare() { return virtio_input_prepare(&function.drv,&function.dev); }
    bool activate() {
        VirtioInputDriver* drivers[]={&function.drv};
        return virtio_input_host_activate_prepared(drivers,1)==1;
    }
    bool start() { return prepare() && activate(); }
    void put(Bytes bytes,int bad_id=-1,int used_length=8) {
        auto* q=function.drv.eventq;
        assert(q && uint16_t(q->avail->idx-consumed)>0);
        const auto head=q->avail->ring[consumed&(q->queue_size-1)];
        assert(head<q->queue_size && q->desc[head].len>=8 && q->desc[head].flags==VRING_DESC_F_WRITE);
        auto* dma=static_cast<volatile uint8_t*>(host_phys_to_virt(q->desc[head].addr));
        assert(dma);
        for(unsigned j=0;j<8;++j)dma[j]=bytes[j];
        // Simulate device publication: data, used element, then used.idx.
        const auto at=q->used->idx&(q->queue_size-1);
        q->used->ring[at].id=bad_id<0?head:uint32_t(bad_id);
        q->used->ring[at].len=used_length;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        q->used->idx=uint16_t(q->used->idx+1);
        ++consumed;
        written.push_back({bytes,admit});
        simulated_ns+=1000000; // Deterministic host motion, no real sleeps.
        if(function.drv.use_msix && !(function.dev.msix_table[0].vector_ctrl&1))
            mock_trigger_irq(uint8_t(function.drv.irq_vector));
    }
    void put(uint16_t t,uint16_t c,int32_t v) { put(wire(t,c,v)); }
    void report(int32_t x,int32_t y) {
        put(2,0,x);put(2,1,y);put(0,0,0);
    }
    size_t poll(size_t budget=64,bool compare=true) {
        const auto n=virtio_input_poll_events(&function.drv,budget);
        CHECK_F(n<=written.size());
        for(size_t i=0;i<n && !written.empty();++i) {
            reference.event(written.front().first,written.front().second);written.pop_front();
        }
        CHECK_F(function.drv.syn_sequence==reference.syns);
        if(compare && reference.relative)CHECK_F(actual==reference.reports);
        return n;
    }
    void fresh_capture() { actual.clear();reference.reports.clear(); }
};
void begin() {
    init_fixture_clear();simulated_splash=false;display_generation=1;
    for(auto& s:VirtioInputForensic::sources)s={};
    VirtioInputForensic::source_count=0;
    forensic_serial.clear();
    if(virtio_input_set_desktop_ready)virtio_input_set_desktop_ready(true);
    g_mock_screen_w=1280;g_mock_screen_h=720;input_set_pointer_ready(true);
    g_mock_input_capture.reset();g_mock_input_capture.abs_x=640;g_mock_input_capture.abs_y=360;
    g_input_relative_observer=capture;actual.clear();
}
void finish() {
    for(auto r:actual) { digest^=uint64_t(r.x);digest*=1099511628211ULL;digest^=uint64_t(r.y);digest*=1099511628211ULL; }
    CHECK_F(g_host_dma_allocs.empty());CHECK_F(g_mock_idt_allocated_count==0);
    g_input_relative_observer=nullptr;init_fixture_clear();++cases;
}
void valid_stream() {
    begin();{
        Device m(8);CHECK_F(m.start());if(!m.function.drv.initialized)return;
        CHECK_F(m.function.drv.has_rel_mouse && !m.function.drv.has_keyboard && !m.function.drv.has_abs_tablet);
        CHECK_F(m.function.statuses==std::vector<uint8_t>({0,1,3,11,15}));
        CHECK_F(m.function.drv.eventq->queue_index==0 && m.function.drv.eventq->queue_size==64);
        m.poll();
        for(unsigned i=0;i<5000;++i) {
            const int x=int(i%7)-3,y=int(i%5)-2;
            m.report(x,y);
            CHECK_F(m.poll(i%3+1)==i%3+1);
            while(!m.written.empty())m.poll(i%5+1);
            CHECK_F(m.function.drv.input_dev.rel_x==0 && m.function.drv.input_dev.rel_y==0);
        }
        CHECK_F(g_mock_input_capture.abs_x>=0 && g_mock_input_capture.abs_x<1280);
        CHECK_F(g_mock_input_capture.abs_y>=0 && g_mock_input_capture.abs_y<720);
        CHECK_F(input_pointer_is_active(&m.function.drv.input_dev));
        std::printf("FORENSIC valid-small-stream reports=%zu first=(%lld,%lld) last=(%lld,%lld) cursor=%d,%d\n",
            actual.size(),(long long)actual.front().x,(long long)actual.front().y,
            (long long)actual.back().x,(long long)actual.back().y,g_mock_input_capture.abs_x,g_mock_input_capture.abs_y);
    }finish();
}
void boot_stream(bool missing_syn,bool dropped) {
    begin();{
        Device m(8);if(virtio_input_set_desktop_ready)virtio_input_set_desktop_ready(false);
        m.admit=false;CHECK_F(m.start());if(!m.function.drv.initialized)return;
        for(unsigned i=0;i<3000;++i) {
            m.put(2,0,-3);m.put(2,1,2);
            if(!missing_syn)m.put(0,0,0);
            if(dropped && i==1500)m.put(0,3,0);
            if(m.written.size()>48)m.poll(64,false);
        }
        while(!m.written.empty())m.poll(64,false);
        std::printf("FORENSIC boot reports actual=%zu reference=%zu pending=%d,%d cursor=%d,%d missingSYN=%u dropped=%u\n",
            actual.size(),m.reference.reports.size(),m.function.drv.input_dev.rel_x,m.function.drv.input_dev.rel_y,
            g_mock_input_capture.abs_x,g_mock_input_capture.abs_y,missing_syn,dropped);
        CHECK_F(actual==m.reference.reports);CHECK_F(actual.empty());
        CHECK_F(g_mock_input_capture.abs_x==640 && g_mock_input_capture.abs_y==360);
        m.put(0,0,0);m.poll(64,false); // End the last pre-interactive report.
        if(virtio_input_set_desktop_ready)virtio_input_set_desktop_ready(true);
        m.admit=true;m.poll(64,false);m.fresh_capture();
        m.report(3,-2);m.poll();
        CHECK_F(actual==std::vector<Report>({{3,-2}}));
        CHECK_F(g_mock_input_capture.abs_x==643 && g_mock_input_capture.abs_y==358);
    }finish();
}
void dropped_sync() {
    begin();{
        Device m(8);CHECK_F(m.start());if(!m.function.drv.initialized)return;
        m.poll();m.put(2,0,4);m.put(0,3,0);
        for(unsigned i=0;i<12;++i)m.put(2,0,3);
        m.put(0,2,0);m.put(0,1,0);m.put(0,0,0);m.poll(64,false);
        std::printf("FORENSIC SYN_DROPPED actual=%zu reference=%zu first_dx=%lld\n",actual.size(),m.reference.reports.size(),actual.empty()?0:(long long)actual[0].x);
        CHECK_F(actual==m.reference.reports);
        m.report(2,1);m.poll();CHECK_F(actual==std::vector<Report>({{2,1}}));
    }finish();
}
void previous_keyboard_claim() {
    begin();{
        Device k(7,false),m(8);CHECK_F(k.prepare() && m.prepare());
        // Restore the old pre-Mimo mode to test the claimed mechanism itself.
        // Capability/kinds remain keyboard-only; buffers/accumulators are distinct.
        k.function.drv.input_dev.pointer_mode=INPUT_POINTER_RELATIVE;
        CHECK_F(k.activate() && m.activate());if(!m.function.drv.initialized)return;
        CHECK_F(k.function.drv.eventq!=m.function.drv.eventq);
        CHECK_F(k.function.drv.dma_phys!=m.function.drv.dma_phys);
        CHECK_F(k.function.dev.notify_base!=m.function.dev.notify_base);
        CHECK_F(k.function.drv.irq_vector!=m.function.drv.irq_vector);
        for(unsigned n=0;n<1000;++n) {
            k.report(-27,44);k.poll(64,false);
            CHECK_F(m.function.drv.input_dev.rel_x==0 && m.function.drv.input_dev.rel_y==0);
        }
        m.poll();m.report(3,-2);m.poll();
        CHECK_F(actual==std::vector<Report>({{3,-2}}));
        std::printf("FORENSIC keyboard-isolation keyboard_pending=%d,%d mouse_report=%lld,%lld\n",
            k.function.drv.input_dev.rel_x,k.function.drv.input_dev.rel_y,(long long)actual.back().x,(long long)actual.back().y);
    }finish();
}
void raw_large_values() {
    begin();{
        Device m(8);CHECK_F(m.start());if(!m.function.drv.initialized)return;
        m.poll();m.report(-27496,44561);m.poll();
        CHECK_F(actual==std::vector<Report>({{-27496,44561}}));
        std::printf("FORENSIC exact-observed-wire decoded/report=-27496,44561 legacy-adapter=-27496,-44561 cursor=%d,%d\n",
            g_mock_input_capture.abs_x,g_mock_input_capture.abs_y);
    }finish();
}

void queue_wrap_and_batch() {
    begin();{
        Device m(8);CHECK_F(m.start());if(!m.function.drv.initialized)return;
        m.poll();auto* q=m.function.drv.eventq;
        std::vector<uint16_t> heads;for(unsigned i=0;i<64;++i)heads.push_back(uint16_t(q->avail->ring[i]));
        constexpr uint16_t initial=65520;
        for(unsigned i=0;i<64;++i)q->avail->ring[uint16_t(initial+i)&63]=heads[i];
        q->avail->idx=uint16_t(initial+64);q->used->idx=q->last_used_idx=m.consumed=initial;
        for(unsigned batch=0;batch<100;++batch) {
            for(unsigned n=0;n<21;++n)m.report(n%2?1:-1,n%2?-2:2);
            while(!m.written.empty())m.poll(1+batch%64);
            CHECK_F(q->last_used_idx==m.consumed && q->avail->idx==uint16_t(m.consumed+64));
            CHECK_F(q->num_free==0 && q->outstanding_heads==64);
        }
        CHECK_F(m.consumed<initial); // Both 16-bit rings crossed zero.
    }finish();
}
static Device* immediate_target=nullptr;
static uint16_t immediate_avail=0;
void immediate_reuse() {
    begin();{
        Device m(8);CHECK_F(m.start());if(!m.function.drv.initialized)return;
        m.poll();
        for(unsigned n=0;n<21;++n)m.report(2,-2);
        m.put(2,0,1); // Exactly 64 available descriptors consumed.
        immediate_target=&m;immediate_avail=uint16_t(m.function.drv.eventq->avail->idx+64);
        g_input_mb_hook=+[](){
            auto* q=immediate_target->function.drv.eventq;
            if(q->avail->idx!=immediate_avail)return;
            g_input_mb_hook=nullptr;
            immediate_target->report(1,-1); // Overwrite returned DMA immediately.
        };
        CHECK_F(m.poll()==64);CHECK_F(g_input_mb_hook==nullptr);
        CHECK_F(actual.size()==21 && actual.front()==Report({2,-2}));
        CHECK_F(m.written.size()==3);CHECK_F(m.poll()==3);
        CHECK_F(actual.back()==Report({2,-1}));
        CHECK_F(!m.function.drv.has_error);
        immediate_target=nullptr;g_input_mb_hook=nullptr;
    }finish();
}
void malformed_and_capacity() {
    begin();{
        Device m(8);CHECK_F(m.start());m.poll();auto* q=m.function.drv.eventq;
        const uint16_t first=q->avail->ring[0],last=q->avail->ring[63];
        q->avail->ring[0]=last;q->avail->ring[63]=first;
        std::memset(m.function.drv.dma_virt,0xEF,512);
        m.report(3,-2);CHECK_F(m.poll()==3);
        CHECK_F(actual==std::vector<Report>({{3,-2}}));
        CHECK_F(VirtioInputForensic::source(0x800)->events[0].descriptor==63);
    }finish();
    for(int which=0;which<6;++which) {
        begin();{
            Device m(8,true,128);CHECK_F(m.start());if(!m.function.drv.initialized)return;
            m.poll();auto* q=m.function.drv.eventq;
            std::memset(m.function.drv.dma_virt,0xEF,512);
            if(which==0)m.put(wire(2,0,3),128,8);
            if(which==1)m.put(wire(2,0,3),127,8); // In table, never allocated.
            if(which==2)m.put(wire(2,0,3),-1,7); // Old byte 7 must not be decoded.
            if(which==3)m.put(wire(2,0,3),-1,9); // Above offered eight-byte capacity.
            if(which==4) {
                m.put(2,0,3);const auto id=q->used->ring[0].id;m.put(wire(0,0,0),int(id),8);
            }
            if(which==5) {
                m.put(2,0,3);q->desc[q->used->ring[0].id].addr+=8;
            }
            CHECK_F(m.poll(64,false)==0);CHECK_F(m.function.drv.has_error);
            CHECK_F(!m.function.drv.input_dev.registered && actual.empty());
            CHECK_F(q->avail->idx==64);
        }finish();
    }
    begin();{
        Device m(8);CHECK_F(m.start());if(!m.function.drv.initialized)return;
        m.poll();auto* q=m.function.drv.eventq;const auto id=q->avail->ring[0];
        q->desc[id].len=16;q->desc_state[id].buffer_len=q->desc_state[id].total_buf_len=16;
        m.put(wire(2,0,4),-1,16);m.put(0,0,0);CHECK_F(m.poll()==2);
        CHECK_F(actual==std::vector<Report>({{4,0}}) && !m.function.drv.has_error);
        CHECK_F(q->desc[m.function.drv.buffer_head[0]].len==8);
    }finish();
}
void keyboard_and_reset() {
    begin();{
        Device k(7,false),m(8);CHECK_F(k.start() && m.start());
        CHECK_F(k.function.drv.input_dev.registered && k.function.drv.input_dev.pointer_mode==INPUT_POINTER_UNSPECIFIED);
        CHECK_F(k.function.drv.eventq->free_desc_next!=m.function.drv.eventq->free_desc_next);
        CHECK_F(k.function.drv.eventq->desc_state!=m.function.drv.eventq->desc_state);
        CHECK_F(k.function.dev.device_cfg!=m.function.dev.device_cfg);
        for(unsigned n=0;n<500;++n) {
            k.put(1,30,n%2);k.put(0,0,0);k.poll(64,false);
            m.report(1,-1);m.poll();
        }
        CHECK_F(g_mock_input_capture.key_calls==500);
        const auto keyboard_idx=k.function.drv.eventq->last_used_idx;
        m.put(2,0,12);m.poll();CHECK_F(m.function.drv.input_dev.rel_x==12);
        CHECK_F(virtio_input_reset(&m.function.drv));CHECK_F(m.function.drv.input_dev.rel_x==0);
        m.consumed=0;m.reference.x=m.reference.y=0;m.reference.resync=false;m.reference.syns=0;
        CHECK_F(m.start());m.poll();m.report(2,3);m.poll();
        CHECK_F(actual.back()==Report({2,3}));
        CHECK_F(k.function.drv.eventq->last_used_idx==keyboard_idx && k.function.drv.initialized);
        CHECK_F(m.function.drv.irq_count>0 && k.function.drv.irq_count>0);
    }finish();
}
void phase_matrix() {
    // Stationary; pre-registration; queue-ready; splash; continuous motion;
    // just-before/after interactive admission; multiple complete buffered SYNs;
    // a partial report straddling the boundary; SYN_DROPPED in the backlog.
    for(unsigned scenario=0;scenario<10;++scenario) {
        begin();{
            Device m(8);if(virtio_input_set_desktop_ready)virtio_input_set_desktop_ready(false);
            m.admit=false;CHECK_F(m.prepare());
            if(scenario==1 || scenario==2) {
                m.report(2,-1);m.report(3,-2);
                CHECK_F(virtio_input_poll_events(&m.function.drv,64)==0);
                virtio_input_host_early_irq(&m.function.drv);
                CHECK_F(m.function.drv.eventq->last_used_idx==0);
            }
            CHECK_F(m.activate());
            const auto activation_events=m.function.drv.total_events_processed;
            for(size_t i=0;i<activation_events;++i){m.reference.event(m.written.front().first,false);m.written.pop_front();}
            if(scenario==3 || scenario==4) {
                // Splash alone no longer gates admission; keep desktop_ready
                // false so the pre-handshake discard path is what we measure.
                simulated_splash=true;
                for(unsigned n=0;n<1000;++n){m.report(1,-1);m.poll(64,false);}
                simulated_splash=false;
            }
            if(scenario>=5) {
                m.report(2,-2);m.report(3,-3);
                if(scenario==8)m.put(2,0,99);
                if(scenario==9){m.put(2,0,99);m.put(0,3,0);m.put(2,0,100);}
            }
            if(virtio_input_set_desktop_ready)virtio_input_set_desktop_ready(true);
            // Consumer establishes a used-index cutoff before normal motion.
            while(!m.written.empty())m.poll(1,false);
            m.poll(64,false);CHECK_F(actual.empty());
            m.admit=true;
            if(scenario==8 || scenario==9){m.put(0,0,0);m.poll();}
            m.report(3,-2);m.poll();CHECK_F(actual==std::vector<Report>({{3,-2}}));
            CHECK_F(g_mock_input_capture.abs_x==643 && g_mock_input_capture.abs_y==358);
            auto* trace=VirtioInputForensic::source(0x800);
            CHECK_F(trace && trace->summary);
            if(scenario==1 || scenario==2)CHECK_F(trace->counts.events[0]==6);
            if(scenario==3 || scenario==4)CHECK_F(trace->counts.reports[3][0]==1000);
        }finish();
    }
}
void geometry_and_extremes() {
    for(unsigned scenario=0;scenario<4;++scenario) {
        begin();{
            Device m(8);CHECK_F(m.start());m.poll();
            m.put(2,0,99);m.poll();m.reference.x=0;m.reference.resync=true;
            if(scenario==0)++display_generation;
            if(scenario==1){g_mock_screen_w=0;g_mock_screen_h=0;}
            if(scenario==2)input_set_pointer_ready(false);
            if(scenario==3){g_mock_screen_w=640;g_mock_screen_h=480;}
            m.poll();m.put(0,0,0);m.poll(64,false);
            CHECK_F(actual.empty() && m.function.drv.input_dev.rel_x==0);
            g_mock_screen_w=1280;g_mock_screen_h=720;input_set_pointer_ready(true);m.poll();
            m.report(3,-2);m.poll();CHECK_F(actual==std::vector<Report>({{3,-2}}));
        }finish();
    }
    begin();{
        Device m(8);CHECK_F(m.start());m.poll();
        for(auto value:{INT32_MIN,INT32_MAX,-1,1}) {
            m.report(value,value);m.poll();
            CHECK_F(actual.back()==Report({value,value}));
        }
    }finish();
}
void committed_geometry_first_report() {
    begin();{
        Device m(8);CHECK_F(m.start());m.poll();
        // Nothing is pending at commit. The very first later report is new.
        ++display_generation;g_mock_screen_w=1920;g_mock_screen_h=1080;
        CHECK_F(virtio_input_commit_geometry(&m.function.drv,1920,1080,display_generation));
        m.report(17,23);m.poll();CHECK_F(actual==std::vector<Report>({{17,23}}));
        m.fresh_capture();
        // A complete old report still in DMA is discarded, a new one survives.
        m.report(99,99);++display_generation;
        CHECK_F(virtio_input_commit_geometry(&m.function.drv,1920,1080,display_generation));
        m.poll(64,false);CHECK_F(actual.empty());
        m.report(5,-7);m.poll(64,false);CHECK_F(actual==std::vector<Report>({{5,-7}}));
    }finish();
}
void random_reference_stream() {
    for(unsigned schedule=1;schedule<=4;++schedule) {
        begin();{
            Device m(8);CHECK_F(m.start());m.poll();uint32_t random=0x721be50;
            for(unsigned i=0;i<12000;++i) {
                random=random*1664525U+1013904223U;
                switch(random%16) {
                    case 0:m.put(0,3,0);break;
                    case 1:m.put(0,2,0);break;
                    case 2:m.put(0,1,0);break;
                    case 3:case 4:case 5:m.put(0,0,0);break;
                    case 6:m.put(4,4,0x01ff);break;
                    default:m.put(2,random%2,int32_t((random>>8)%15)-7);break;
                }
                if(m.written.size()>48 || i%schedule==0)m.poll(1+i%64);
            }
            m.put(0,0,0);while(!m.written.empty())m.poll(7);
            CHECK_F(!m.function.drv.has_error);
        }finish();
    }
}
void diagnostic_records() {
    begin();{
        Device m(8);CHECK_F(m.start());m.poll();
        for(unsigned n=0;n<80;++n){m.report(3,-2);m.poll();}
        auto* s=VirtioInputForensic::source(0x800);
        CHECK_F(s && s->count==128 && s->counts.total_events==240);
        CHECK_F(s->events[0].raw[0]==2 && s->events[0].raw[4]==3);
        CHECK_F(s->events[1].raw[4]==254 && s->events[1].raw[7]==255);
        CHECK_F(s->events[1].value==-2 && s->events[1].before_x==3 && s->events[1].after_y==-2);
        CHECK_F(s->events[2].outcome==VirtioInputForensic::Published && s->events[2].dx==3 && s->events[2].dy==-2);
        CHECK_F(s->events[2].cursor_x==640 && s->events[2].cursor_y==360 && s->events[2].new_x==643 && s->events[2].new_y==358);
        CHECK_F(s->events[0].queue==0 && s->events[0].used_len==8 && s->events[0].mode==INPUT_POINTER_RELATIVE);
        CHECK_F(s->events[0].kinds==INPUT_DEVICE_KIND_MOUSE && s->events[0].readiness==7 && s->events[0].phases==2);
        for(unsigned n=0;n<40;++n){m.report(2000,-1000);m.poll();}
        CHECK_F(s->count==160 && s->anomalies==32 && s->counts.total_events==360);
        CHECK_F(s->counts.total_reports==120 && s->summary);
        CHECK_F(s->interactive_counts.total_events==0);
        for(unsigned i=0;i<s->count;++i) {
            CHECK_F(i==0 || s->events[i].ns>=s->events[i-1].ns);
            CHECK_F(i<128 || s->events[i].anomaly);
        }
        for(unsigned i=0;i<140;++i)VirtioInputForensic::dump();
        unsigned summaries=0,events=0;bool all_fields_present=true;
        for(const auto& line:forensic_serial) {
            summaries+=line.find("[VINPUT FORENSIC INTERACTIVE]")!=std::string::npos;
            if(line.find("[VINPUT FORENSIC EVENT]")==std::string::npos)continue;
            ++events;
            for(const auto* field:{"ns=","bdf=00:08.0","role=","lifecycle=","queue=","used_idx=","used_element=","descriptor=","used_len=","bytes=","type=","code=","signed_value=","mode=","kinds=","ready=","proven=","relx_before=","rely_before=","relx_after=","rely_after=","syn_action=","outcome=","published_dx=","published_dy=","cursor_x=","cursor_y=","new_x=","new_y="})
                all_fields_present &= line.find(field)!=std::string::npos;
        }
#if BURSZTYNDIAGVIRTIOINPUTFORENSIC
        // Emission-specific checks do not change the functional comparison count.
        if(summaries!=1 || events!=160 || !all_fields_present){++failures;std::printf("FORENSIC FAIL diagnostic output summary=%u events=%u fields=%u\n",summaries,events,all_fields_present);}
#else
        if(summaries || events){++failures;std::printf("FORENSIC FAIL unexpected diagnostic output\n");}
#endif
        std::printf("FORENSIC DIAGNOSTIC emitted=%u summaries=%u stored=%u anomalies=%u\n",events,summaries,s->count,s->anomalies);
    }finish();
}
} // namespace forensic
int main(int argc,char** argv) {
    using namespace forensic;
    const std::string mode=argc>1?argv[1]:"all";
    if(mode=="valid" || mode=="all") { valid_stream();previous_keyboard_claim();raw_large_values(); }
    if(mode=="lifecycle" || mode=="all") { boot_stream(false,false);boot_stream(true,false);boot_stream(true,true);dropped_sync(); }
    if(mode=="extended" || mode=="all") {
        queue_wrap_and_batch();immediate_reuse();malformed_and_capacity();keyboard_and_reset();phase_matrix();geometry_and_extremes();committed_geometry_first_report();random_reference_stream();diagnostic_records();
    }
    std::printf("FORENSIC RESULT cases=%u checks=%u failures=%u digest=%016llx\n",cases,checks,failures,(unsigned long long)digest);
    return failures?1:0;
}
