# ELF musl w gościu — 2026-11-15

Loader ELF64 ET_EXEC przygotowuje dla programu statycznego musl minimalny
stos POSIX: `argc`, `argv[]`, puste `envp[]` oraz `AT_PAGESZ`, `AT_ENTRY`,
`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_EXECFN`, `AT_RANDOM` i `AT_NULL`.
Zawartość stosu jest zapisywana do nieaktywnego PML4 przez tłumaczenie
VA→PA i alias HHDM; loader nie dereferencjonuje user VA i nie przełącza CR3.

Gate:

```text
make test-musl-guest-qemu
```

Ten target nie uruchamia automatycznie `make musl-sysroot` ani nie pobiera
źródeł. Przygotowanie zależności jest jawne:

```text
make musl-sysroot
make test-musl-guest-qemu
```

Gate buduje statyczny `-no-pie` hello z sysrootu musl, pakuje go przez
istniejący launcher BWS do ISO i oczekuje markera `BURSZTYN-MUSL-HELLO`.
Brak staged sysrootu, natywnego `qemu-system-x86_64.exe` albo firmware OVMF
zgłasza `SKIP`; błąd budowy/ISO albo brak markera zgłasza `FAIL`. Na Windows
QEMU jest uruchamiane wyłącznie przez natywne `qemu-system-x86_64.exe`.
