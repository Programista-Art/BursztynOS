// Read-only QEMU observers use the actual host/kernel x86-64 process ABI.
#include "../pzb.h"
#include <cstdio>
#include <cstddef>
int main(){std::printf("{\"process_size\":%zu,\"state_offset\":%zu}\n",sizeof(proces_t),offsetof(proces_t,stan));}
