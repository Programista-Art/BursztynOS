// Read-only QEMU observers use the actual host/kernel x86-64 process ABI.
#include "../uklad_procesu.h"
#include <cstdio>
#include <cstddef>
int main(){std::printf("{\"process_size\":%zu,\"state_offset\":%zu,\"cr3_offset\":%zu,\"layout_size\":%zu,\"version_offset\":%zu}\n",sizeof(proces_t),offsetof(proces_t,stan),offsetof(proces_t,cr3),sizeof(UkladProcesu),offsetof(UkladProcesu,wersja));}
