# Process/thread/signal: bezpieczny podzakres

## Zakres tej zmiany

Obecny scheduler przechowuje jeden kontekst wykonania na proces. Wejście
przez `int 0x80` wraca z syscallu przez tę samą ramę ISR; nie istnieje jeszcze
bezpieczna kolejka waiterów ani osobny `watek_t`. Dlatego ta zmiana nie udaje
pełnego modelu Linux/POSIX.

### Futex

- `FUTEX_WAIT` i `FUTEX_WAKE` rozpoznają również `FUTEX_PRIVATE_FLAG`.
- Adres musi być niezerowy, wyrównany do 4 bajtów i przejść walidację oraz
  `copy_from_user`.
- `WAIT` zwraca `-EAGAIN`, gdy wartość różni się od oczekiwanej. Następnie
  wykonuje najwyżej 256 instrukcji `pause`; jeśli wartość się zmieni, zwraca
  sukces, a jeśli nie, `-ENOSYS`.
- `WAKE` zwraca 0, ponieważ w tym modelu nie ma zarejestrowanych waiterów.
- Timed wait, `CLOCK_REALTIME` i inne operacje pozostają `-ENOSYS`.

Ten kontrakt nie blokuje jedynego kontekstu i pozwala libc użyć bezpiecznego
fallbacku zamiast otrzymać fałszywy sukces.

### Jawnie niezaimplementowane bramki

`pipe/pipe2`, `dup/dup2/dup3`, `fcntl`, `clone`, `fork/vfork`, `execve`,
`wait4`, `kill`, `rt_sigaction` i `rt_sigprocmask` mają nazwy w ABI oraz
testowany, jawny wynik `-ENOSYS`, gdy usługa nie jest podpięta.

Nie dodano:

- pipe bez per-procesowej własności końców i bez limitu bufora,
- `dup`, bo wymaga współdzielonego obiektu open-file-description i poprawnego
  współdzielenia offsetu/flag,
- sygnałów, bo sama rejestracja handlera bez ścieżki delivery byłaby obietnicą
  bez pokrycia; `KeyboardInterrupt` nie jest przez to gwarantowany,
- `clone`, fork/exec/wait, bo wymagają osobnych kontekstów, procesu potomnego,
  COW oraz bezpiecznej synchronizacji stanu rodzic-dziecko.

## Weryfikacja

`make test-process-thread-signal-host` uruchamia testy hostowe ABI, VFS oraz
POSIX/VFS pod ASan/UBSan. Nie uruchamia QEMU. Pełny kernel build pozostaje
wymagany po zmianie ABI i implementacji futexa.
