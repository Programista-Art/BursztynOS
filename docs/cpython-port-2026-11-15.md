# Cross-build CPython dla BursztynOS — 2026-11-15

## Zakres

Repozytorium ma kontrakt cross-builda CPython 3.12.8 przeciwko sysrootowi
musl BursztynOS. Źródło jest pobierane dopiero przez jawny target
`make cpython-build` do ignorowanego `.cpython-build/`; skrypt sprawdza
SHA-256 archiwum przed rozpakowaniem.

Manifest:

```text
narzedzia/python/cpython-runtime.manifest.json
```

Opis layoutu stagingu:

```text
/usr/bin/python3
/usr/bin/python3.12
/usr/lib/python3.12/
/usr/lib/python3.12/encodings/
/usr/lib/python3.12/sysconfig.py
/usr/lib/python3.12/site-packages/
```

Skrypt używa istniejącego `.musl-build/sysroot/`, statycznego linkowania
`-static -fno-pie -no-pie` i nie włącza dynamicznego `libpython`. Nie buduje
sysrootu automatycznie. W WSL pełna próba wygląda tak:

```text
make musl-sysroot
make cpython-build
```

`make test-cpython-manifest` jest celowo niezależny od obu targetów budujących:
czyta tylko wersjonowany JSON i tekst skryptu. Nie pobiera źródeł, nie uruchamia
kompilatora cross i nie wymaga QEMU.

## Uczciwe ograniczenia

- To jest przygotowany, walidowalny cross-build i staging runtime, a nie
  deklaracja, że CPython już działa w obrazie gościa.
- `pure-Python` może być wspierany dopiero po przejściu osobnego gate’a
  statycznego interpretera w gościu. Obecnie standard library nie jest jeszcze
  dołączona do obrazu.
- `pip` i `ensurepip` są wyłączone. Nie jest dostarczany menedżer pakietów ani
  internetowy bootstrap.
- Zewnętrzne rozszerzenia C (`.so`) nie są pakowane: wymagają ELF `ld.so` oraz
  `dlopen`, których BursztynOS jeszcze nie obsługuje. Skrypt usuwa takie pliki
  ze stagingu, zamiast sugerować ich działanie.
- Moduły C wbudowane statycznie w interpreter mogą istnieć, ale nie oznacza to
  obsługi dowolnych rozszerzeń C ani wątków/procesów. Funkcje zależne od
  niegotowych syscalli są wyłączane przez `config.site`.
- `make test-python-contract` jest offline i nie uruchamia QEMU. Wiersze
  `test-python-hello-qemu` oraz `test-python-posix/security/regression` są
  osobnymi gate'ami macierzy; bez staged runtime, natywnego
  `qemu-system-x86_64.exe`, OVMF albo `CPYTHON_GUEST_ISO` zgłaszają jawny
  `SKIP`, a nie PASS. QEMU może być uruchomione wyłącznie natywnie na Windows.

