# POSIX network — bezpieczny podzakres

## Zaimplementowane

- ABI Linux x86-64 dla `socket`, `connect`, `sendto`, `recvfrom`,
  `shutdown`, `getsockopt` i `setsockopt` oraz jawne dispatcherowe wejścia
  dla `bind`, `listen` i `accept`.
- Obsługiwane są wyłącznie `AF_INET` + `SOCK_STREAM` + TCP. Inne domeny,
  typy i protokoły są odrzucane (`-EAFNOSUPPORT` albo
  `-EPROTONOSUPPORT`).
- `sockaddr_in` jest kopiowany przez `copy_from_user`; bufory I/O,
  `socklen_t`, opcje i adresy wyjściowe przechodzą walidację zakresu Ring 3
  przed użyciem. `sendto` dla połączonego TCP wymaga pustego adresu, a
  `recvfrom` może zwrócić adres peer'a.
- Socket dostaje fd z tej samej per-PID tablicy VFS co pliki. Rekord VFS ma
  typ `VFS_GNIAZDO`, a stan TCP pozostaje w ograniczonej tablicy
  `1 socket/PID`; fd nie koliduje z fd pliku i jest czyszczony przy recyklingu
  PID.
- `connect` przejmuje istniejącą własność operacji sieciowej i utrzymuje ją
  przez życie połączenia. Dzięki temu globalny transport BWS/TCP nie jest
  mapowany bezpośrednio na przypadkowy POSIX fd ani współdzielony przez dwa
  procesy. Jednocześnie istnieje tylko jedno rzeczywiste połączenie TCP
  naraz, zgodnie z ograniczeniem `siec.cpp`.
- `SO_TYPE` i `SO_ERROR` są dostępne przez `getsockopt`; nieobsługiwane opcje
  zwracają `-ENOPROTOOPT` lub `-ENOSYS`, nigdy pozorny sukces.
- `read`/`write` na socket fd są kierowane do `recvfrom`/`sendto`.
  Pojedynczy transfer ma limit 64 KiB; chwilowy brak danych zwraca
  `-EAGAIN`.

## Jawne braki

- `bind`, `listen` i `accept` walidują fd, argumenty oraz wskaźniki, ale
  zwracają `-ENOSYS`, ponieważ obecny stos TCP nie ma tablicy nasłuchu,
  kolejki SYN ani wielu obiektów połączeń.
- `getsockname`, `getpeername`, `socketpair` oraz pozostałe warianty
  wiadomości nie mają usługi i zwracają `-ENOSYS`.
- `setsockopt` nie zmienia jeszcze parametrów transportu. Nie deklarujemy
  timeoutów, keepalive ani non-blocking przez opcje, których stos nie
  respektuje.
- `poll` nadal zwraca `-ENOSYS`. Kernel nie ma jeszcze wait queue procesu;
  implementacja busy-loop byłaby niebezpieczna dla jednordzeniowego
  schedulera. Nie dodano `epoll` jako atrapowego odpowiednika.
- TCP zachowuje istniejące ograniczenia: jedna sesja globalna, brak pełnej
  retransmisji/RTO i congestion control. Błąd niższej warstwy jest mapowany
  na `-ENETUNREACH` dla `connect` albo `-EIO` dla transferu.

## Weryfikacja

- `make test-posix-abi-host`: PASS, 244 asercje.
- `make test-vfs-host`: PASS, 130 asercji, w tym izolacja socket fd.
- `make test-posix-vfs-host`: PASS, 30 asercji.
- QEMU nie był uruchamiany zgodnie z zakresem zadania.
- `make kernel`: wszystkie nowe obiekty POSIX/VFS skompilowały się, ale link
  zatrzymała istniejąca twarda asercja rozmiaru obrazu:
  `__kernel_end przekracza limit 15 MiB`. Nie uruchamiano QEMU.
- `posix_jadro.cpp` i dispatcher przeszły lokalną kontrolę składni C++.
