#pragma once
#include <stdint.h>

// Generic display publication contract. Initialization and mode replacement
// may prepare memory, but only a completed frame can authorize its publication.
// The existing screen lock owns this state; drivers never inspect UI contents.
namespace PrezentacjaStartu {
struct Stan {
    uint64_t generacja=0;
    bool wybrano=true, gotowa=true, pelna=false, inicjalizacja=false;
    bool backend=true;
};
inline Stan& stan() { static Stan s; return s; }
inline void reset() { auto& s=stan();++s.generacja;s.wybrano=false;s.gotowa=false;s.pelna=true;s.inicjalizacja=true;s.backend=false; }
inline void wybierz() { stan().wybrano=true; }
inline void uniewaznij() { stan().gotowa=false;stan().pelna=true;++stan().generacja; }
inline void przygotowana() { if(stan().wybrano)stan().gotowa=true; }
inline void wybierz_backend() { stan().backend=true; }
inline bool dozwolona() { return stan().backend&&stan().wybrano&&stan().gotowa; }
inline bool wymaga_pelnej() { return stan().pelna; }
inline void opublikowana() { if(dozwolona())stan().pelna=false; }
inline void zakoncz_inicjalizacje() { stan().inicjalizacja=false; }
}
