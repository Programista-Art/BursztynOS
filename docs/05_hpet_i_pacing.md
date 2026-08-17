# HPET, LAPIC i pacing GUI

## Zrodlo czasu

Multiboot2 przekazuje opcjonalny tag ACPI old/new. Parser waliduje RSDP,
checksum RSDT/XSDT oraz kazda czytana tabele SDT. Tabela `HPET` jest
akceptowana tylko dla GAS `System Memory`, z niezerowym adresem i zerowym
bit offset. Adres nie jest wpisany na stale.

HPET jest mapowany przez VMM jako supervisor, writable, PWT+PCD. Main counter
jest wlaczany bez comparator IRQ. `COUNTER_CLK_PERIOD` daje czestotliwosc;
API monotoniczne uzywa dzielenia quotient/remainder bez float. Licznik
32-bitowy ma programowe rozszerzanie epoki przy wrap.

## Clockevent

LAPIC zachowuje divider 16. Podczas bootu jest uruchamiany od `0xffffffff`
na 20 ms mierzonych HPET. Roznica current-count wyznacza czestotliwosc
timera po dividerze. Periodic initial count jest liczony dla 250 Hz.
Bez HPET pozostaje jawny fallback z logiem `TIMER-WARN`.

## GUI

Scheduler IRQ nie renderuje. Zegar pulpitu dostaje event raz na 1 s wedlug
HPET. Dirty request pozostaje ustawiony do deadline compositora, maksymalnie
jednej pelnej klatki na 16 666 667 ns. PID 0 jest normalnym uczestnikiem
round-robin, wiec wykonuje pending compositor work poza IRQ.

Kursor jest overlayem ostatniego etapu pelnej klatki. IRQ myszy nie wykonuje
hide/restore na potencjalnie nieaktualnym backbufferze. Klik podnosi trafiona
warstwe aplikacji, ale warstwa pulpitu (`z_order <= 0`) nie jest podnoszona.

`BURSZTYN_PERF_TIMER=0` wylacza okresowe logi `[PERF]` w czasie kompilacji.
