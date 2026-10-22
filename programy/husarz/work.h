#pragma once
#include <stddef.h>
#include <stdint.h>
#include "perf.h"
void *operator new(size_t);
void operator delete(void*) noexcept;
// Compiler coroutine ABI only. No standard-library implementation, runtime,
// threads, exceptions or stack switching. Frames use the process allocator.
namespace std {
template<class R, class... Args> struct coroutine_traits { using promise_type=typename R::promise_type; };
template<class P=void> struct coroutine_handle;
template<> struct coroutine_handle<void> {
    void *p{};
    static coroutine_handle from_address(void *p) { return {p}; }
    void *address() const {return p;}
    void resume() const {__builtin_coro_resume(p);}
    void destroy() const {__builtin_coro_destroy(p);}
    bool done() const {return !p || __builtin_coro_done(p);}
    explicit operator bool() const {return p;}
};
template<class P> struct coroutine_handle : coroutine_handle<void> {
    static coroutine_handle from_promise(P &v) {return {{__builtin_coro_promise(&v,alignof(P),true)}};}
    P &promise() const {return *static_cast<P*>(__builtin_coro_promise(this->p,alignof(P),false));}
};
}
namespace Husarz {
struct WorkDriver;
inline WorkDriver *running_work{};
// At most six retained jobs, helper nesting bounded by fixed pipeline structure.
constexpr size_t MaxWorkMemory=512*1024;
inline size_t work_memory{}, peak_work_memory{};
inline void *default_work_allocate(size_t n){return ::operator new(n);}
inline void default_work_release(void *p){::operator delete(p);}
inline void *(*work_allocate)(size_t){default_work_allocate};
inline void (*work_release)(void*){default_work_release};
struct WorkDriver {
    std::coroutine_handle<> current{};
    uint32_t units{}, maximum{}, slices{},next_clock{};
    uint64_t (*clock)(){}; uint64_t started{};
    bool exhausted{}, failed{},active{},allow_clock{true};uint64_t cpu{},maximum_ns{};
#if HUSARZ_PERF
    Stage stage{Stage::Idle};uint32_t cursor{},remaining{},navigation{},document{};uint64_t perf_started{};
#endif
};
struct Suspend {
    bool await_ready()const noexcept{return false;}
    void await_suspend(std::coroutine_handle<> h)const noexcept {running_work->current=h;}
    void await_resume()const noexcept{}
};
// Charge before doing work; a deterministic bound applies even without a clock.
#if HUSARZ_PERF
#define HUSARZ_PROGRESS(s,c,n) do{if(::Husarz::running_work){auto &w=*::Husarz::running_work;w.stage=::Husarz::Stage::s;w.cursor=(c);w.remaining=(n)>(c)?(n)-(c):0;}}while(0)
#else
#define HUSARZ_PROGRESS(s,c,n) ((void)0)
#endif
constexpr unsigned MaxSliceWork=2048;
constexpr unsigned ClockPollWork=128;
struct WorkPoint {
    uint32_t units;
    static bool poll_clock(WorkDriver &w)noexcept {
        // The calibrated budget clock is user-mode RDTSC, so poll in small
        // work increments without putting BWS60 syscalls in hot loops.
        w.next_clock=w.units+ClockPollWork;return w.clock()-w.started>=2000000;
    }
    bool await_ready()const noexcept {
        auto &w=*running_work;
        if(units>MaxSliceWork){w.failed=true;w.exhausted=true;return false;}
        if(w.units+units>MaxSliceWork || (w.clock && w.units>=w.next_clock && poll_clock(w))) {
            w.exhausted=true; return false;
        }
        w.units+=units;return true;
    }
    void await_suspend(std::coroutine_handle<> h)const noexcept {running_work->current=h;}
    void await_resume()const noexcept {
        // A suspended checkpoint is charged in the iteration that resumes it.
        if(!running_work->units)running_work->units=units;
    }
};
// A single bounded slab recycles compiler frames by size class. Selector
// continuations therefore do not call the process heap for every candidate.
struct alignas(16) WorkBlock { WorkBlock *next; size_t size; };
inline unsigned char *work_slab{};
inline size_t work_bump{};
inline WorkBlock *work_free[14]{};
inline void *allocate_work_frame(size_t n) noexcept {
    size_t size=64;unsigned bucket=0;
    while(size<n+sizeof(WorkBlock) && bucket<13){size*=2;++bucket;}
    if(size<n+sizeof(WorkBlock))return nullptr;
    if(!work_slab){work_slab=static_cast<unsigned char*>(work_allocate(MaxWorkMemory));work_bump=0;}
    if(!work_slab)return nullptr;
    WorkBlock *block=work_free[bucket];
    if(block)work_free[bucket]=block->next;
    else {if(size>MaxWorkMemory-work_bump)return nullptr;block=reinterpret_cast<WorkBlock*>(work_slab+work_bump);work_bump+=size;}
    block->size=size;work_memory+=size;if(work_memory>peak_work_memory)peak_work_memory=work_memory;
    return block+1;
}
inline void release_work_frame(void *v) noexcept {
    auto *block=static_cast<WorkBlock*>(v)-1;size_t size=block->size;unsigned bucket=0;
    for(size_t s=64;s<size;s*=2)++bucket;
    work_memory-=size;block->next=work_free[bucket];work_free[bucket]=block;
    if(!work_memory){work_release(work_slab);work_slab=nullptr;work_bump=0;for(auto &p:work_free)p=nullptr;}
}
struct Work {
    struct promise_type {
        std::coroutine_handle<> parent{};
        static void *operator new(size_t n) noexcept {return allocate_work_frame(n);}
        static void operator delete(void *v,size_t) noexcept {release_work_frame(v);}
        static Work get_return_object_on_allocation_failure()noexcept{return {};}
        Work get_return_object()noexcept{return Work{std::coroutine_handle<promise_type>::from_promise(*this)};}
        struct Initial {
            bool await_ready()const noexcept{return false;}
            void await_suspend(std::coroutine_handle<>)const noexcept{}
            void await_resume()const noexcept{}
        };
        Initial initial_suspend()noexcept{return {};}
        struct Final {
            bool await_ready()const noexcept{return false;}
            void await_suspend(std::coroutine_handle<promise_type> h)const noexcept {running_work->current=h.promise().parent;}
            void await_resume()const noexcept{}
        };
        Final final_suspend()noexcept{return {};}
        void return_void()noexcept{}
        void unhandled_exception()noexcept{__builtin_trap();}
    };
    std::coroutine_handle<promise_type> handle{};
    Work()=default;
    explicit Work(std::coroutine_handle<promise_type> h):handle(h){}
    Work(const Work&)=delete;
    Work(Work &&o)noexcept:handle(o.handle){o.handle={};}
    Work &operator=(Work &&o)noexcept {if(this!=&o){cancel();handle=o.handle;o.handle={};}return *this;}
    ~Work(){cancel();}
    void cancel(){if(handle)handle.destroy();handle={};}
    bool await_ready()const noexcept{return !handle;}
    void await_suspend(std::coroutine_handle<> parent)noexcept{handle.promise().parent=parent;running_work->current=handle;}
    void await_resume()const noexcept{if(!handle)running_work->failed=true;}
    bool step(WorkDriver &w) {
        if(!handle)return true;
        if(!w.current)w.current=handle;
#if HUSARZ_PERF
        if(!w.clock && w.allow_clock)w.clock=perf_clock;
        void *old_context=perf_context;perf_context=&w;
        perf_context_time=[](void *p)->uint64_t{auto &v=*static_cast<WorkDriver*>(p);return v.cpu+(v.active&&v.perf_started&&perf_clock?perf_clock()-v.perf_started:0);};
#endif
        auto *previous=running_work;running_work=&w;w.units=0;w.next_clock=ClockPollWork;w.exhausted=false;w.started=w.clock?w.clock():0;w.active=true;
#if HUSARZ_PERF
        w.perf_started=perf_clock?perf_clock():0;
#endif
        while(w.current && !w.exhausted && !w.failed)w.current.resume();
        if(w.units>w.maximum)w.maximum=w.units;
#if HUSARZ_PERF
        if(perf_clock && w.perf_started){uint64_t elapsed=perf_clock()-w.perf_started;w.cpu+=elapsed;if(elapsed>w.maximum_ns)w.maximum_ns=elapsed;
#if HUSARZ_PERF
            if(elapsed>=50000000 && perf_log){char line[256];unsigned n=0;perf_word(line,n,"[HUSARZ UI STALL] stage=");perf_word(line,n,stage_name(w.stage));perf_word(line,n," nav=");perf_number(line,n,w.navigation);perf_word(line,n," doc=");perf_number(line,n,w.document);perf_word(line,n," elapsedms=");perf_number(line,n,elapsed/1000000);perf_word(line,n," cursor=");perf_number(line,n,w.cursor);perf_word(line,n," remaining=");perf_number(line,n,w.remaining);perf_word(line,n," units=");perf_number(line,n,w.units);line[n++]='\n';line[n]=0;perf_log(line);}
#endif
        }
#endif
        w.active=false;
#if HUSARZ_PERF
        perf_context=old_context;
#endif
        ++w.slices;running_work=previous;return !w.current || w.failed;
    }
};
inline void finish_work(Work work) { WorkDriver driver;while(!work.step(driver)){} }
}
