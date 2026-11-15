#pragma once

/*
 * Minimal, self-contained syscall ABI for native BursztynOS programs.
 *
 * This header intentionally does not include kernel or GUI headers. It is
 * suitable for a sysroot and for libc/runtime ports. The fourth argument is
 * explicitly bound to R10; using RCX here would silently corrupt mmap,
 * prlimit64 and every other four-or-more argument syscall.
 */

#include <stdint.h>

static inline long bursztyn_syscall0(long nr) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr)
                     : "memory");
    return result;
}

static inline long bursztyn_syscall1(long nr, long a1) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr), "D"(a1)
                     : "memory");
    return result;
}

static inline long bursztyn_syscall2(long nr, long a1, long a2) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr), "D"(a1), "S"(a2)
                     : "memory");
    return result;
}

static inline long bursztyn_syscall3(long nr, long a1, long a2, long a3) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "memory");
    return result;
}

static inline long bursztyn_syscall4(long nr, long a1, long a2, long a3,
                                     long a4) {
    long result;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                     : "memory");
    return result;
}

static inline long bursztyn_syscall5(long nr, long a1, long a2, long a3,
                                     long a4, long a5) {
    long result;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                     : "memory");
    return result;
}

static inline long bursztyn_syscall6(long nr, long a1, long a2, long a3,
                                     long a4, long a5, long a6) {
    long result;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8),
                       "r"(r9)
                     : "memory");
    return result;
}
