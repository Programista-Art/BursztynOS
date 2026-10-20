/* Test-only Ring 3 probe. It is linked into a private test kernel, never the ISO. */
.section .naglowek,"a"
.global naglowek
naglowek:
.byte 'B','U','R',0
.quad _start
.quad 0x1000,0x40000,0x1001000,0x41000,0x80000,0x1041000
.section .text
.global _start
_start:
#ifdef HUSARZ_FAULT_PAGE
    xor %rax,%rax
    mov (%rax),%rax
#endif
    ud2
.section .note.GNU-stack,"",@progbits
