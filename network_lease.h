#pragma once
#include "pzb.h"

// One existing native transport, with ownership retained across explicit waits.
class NetworkLease {
    int owner_{};
public:
    bool acquire(int pid) {
        if(pid<=0 || pid>=int(PZB_MAKS_PROCESOW))return false;
        int empty=0;
        return __atomic_compare_exchange_n(&owner_,&empty,pid,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    }
    bool release(int pid) {
        if(pid<=0 || pid>=int(PZB_MAKS_PROCESOW))return false;
        int expected=pid;
        return __atomic_compare_exchange_n(&owner_,&expected,0,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    }
    bool owned_by(int pid)const{return pid>0 && pid<int(PZB_MAKS_PROCESOW) && __atomic_load_n(&owner_,__ATOMIC_ACQUIRE)==pid;}
    bool busy()const{return __atomic_load_n(&owner_,__ATOMIC_ACQUIRE)!=0;}
};
