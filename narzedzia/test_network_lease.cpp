#include "../network_lease.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
int main(){
    NetworkLease lease;
    CHECK(!lease.busy());
    for(int invalid:{-1,0,int(PZB_MAKS_PROCESOW),2147483647}){
        CHECK(!lease.acquire(invalid));CHECK(!lease.release(invalid));CHECK(!lease.owned_by(invalid));
    }
    for(int cycle=0;cycle<8;++cycle)for(int owner=1;owner<int(PZB_MAKS_PROCESOW);++owner){
        CHECK(lease.acquire(owner));CHECK(lease.busy());CHECK(lease.owned_by(owner));
        for(int other=1;other<int(PZB_MAKS_PROCESOW);++other){
            CHECK(!lease.acquire(other));
            if(other!=owner){CHECK(!lease.release(other));CHECK(!lease.owned_by(other));}
            CHECK(lease.owned_by(owner));
        }
        // A completed call or cleanup releases only its own transport lease.
        CHECK(lease.release(owner));CHECK(!lease.busy());CHECK(!lease.release(owner));
    }
    std::printf("Native network transport ownership: %u checks PASS\n",checks);
}
