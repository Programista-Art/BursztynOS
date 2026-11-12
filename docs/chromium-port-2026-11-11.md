# Port Chromium na Bursztyn OS — etapy 0–2 (2026-11-11/12)

Dokument domyka cztery zadania z planu: akcelerację 2D (etap 0), fundament ABI
SysV (etap 1, części 2.12–2.18 z `docs/audyty/PROMPT_faza2_posix.md`) oraz
konfigurację backendu Ozone dla Chromium (etap 2). Nie zawiera gotowej binarki
Chromium — powód w sekcji 6.

## 1. Punkt wyjścia

Sprawdzone w kodzie, nie w dokumentacji:

| Obszar | Stan przed |
|---|---|
| Bramka SysV | brak. Tylko BWS: numer w R8, argumenty R9/R10/R12/R13 |
| ELF64 | brak `Elf64_Ehdr`, brak `PT_LOAD`; jedyny format to `.bur` |
| TLS per proces | `IA32_FS_BASE` nigdzie nie ustawiany; brak `arch_prctl` |
| `mmap` | brak. Tylko bump-allocator BWS35 w oknie 256 MiB |
| Skalowanie | brak w jądrze; tylko nearest w Ring 3 (`probka_obrazu`) |
| Blit do warstwy | pętla piksel-po-pikselu, bez skalowania i bez batchy |
| Backend Ozone | brak |

## 2. Etap 0 — akceleracja 2D i skalowanie

Szczegóły pomiarów w [graphics-2d-skaling-2026-11-11.md](graphics-2d-skaling-2026-11-11.md).
Podsumowanie:

- `jadro/gui/skalowanie.h` — jedno źródło matematyki skalowania: nearest,
  bilinear (premultiplikowany), box, mapowanie 16.16 na `int64_t`, zero
  float/double (jądro ma `-mno-sse`), zero alokacji.
- `BWS_GUI_BLIT_SKALOWANY = 98` — jeden skalowany blit z bufora Ring 3 do
  warstwy, z filtrem, trybem dopasowania i flagami (przezroczystość,
  source-over, zachowanie proporcji). Walidacja: `PRAWO_GUI`, wersja ABI,
  zakresy 8192 px, ≤ 4 M pikseli, `OknoUzytkownika` na cały bufor (SMAP).
- `BWS_GUI_SKALA_WARSTWY = 99` — deklaracja „ten bufor zawiera obraz
  W×H". Kompozytor sam próbkuje go w prostokąt okna, więc Ring 3 nie płaci
  za skalowanie. ABI `warstwa_obrazu` pozostało zamrożone (stan w prywatnej
  tablicy `skala_warstwy[]`).
- BWS57 tryb 5 — batch do 64 skalowanych regionów w jednej operacji pod
  jedną blokadą ekranu; walidacja wszystkich regionów **przed** pierwszym
  zapisem.
- `bursztyn_ui/skalowanie.h` — te same prymitywy jako inline dla aplikacji;
  `bursztyn_ui/obraz.h` używa bilinearu w środku obrazu i nearest na
  krawędziach, zachowując kontrakt „zero = przezroczyste".

## 3. Etap 1 — fundament ABI SysV

### 3.1 Druga bramka `int 0x80` (`jadro/posix/`)

ABI dokładnie linuxowy: numer w **RAX**, argumenty
**RDI/RSI/RDX/R10/R8/R9**, wynik w RAX, błąd jako `-errno` z zakresu
−1..−4095. Numer w RAX (a nie RDI) jest konieczny, bo `mmap` ma 6 argumentów.

| Plik | Rola |
|---|---|
| `posix_abi.h` | errno, numery wywołań, `PosixUtsname`, `PosixRama` — wspólne dla jądra i testu |
| `posix_dyspozytor.cpp` | mapowanie rejestrów → usługi, konwencja errno; **zero zależności od jądra**, testowalne na hoście |
| `posix_jadro.cpp` | jedyne miejsce, gdzie bramka dotyka podsystemów (VFS, PSF, czas, RNG, TLS, mmap) |

Zabezpieczenia:

- bramka DPL=3 **tylko** dla wektora 0x80; wszystkie pozostałe wpisy IDT
  pozostają DPL=0, więc aplikacja nie może wywołać wektora wyjątku ani IRQ,
- wektor 0x80 jest **wyłączony z puli MSI** (`wektor_rezerwowany()` w
  `idt.cpp`): bez tego sterownik mógłby zainstalować tam handler sprzętowy
  i zniszczyć bramkę użytkownika,
- wywołanie z Ring 0 jest traktowane jako błąd jądra (panic), nie jako
  żądanie użytkownika,
- każdy wskaźnik Ring 3 przechodzi przez `copy_from_user`/`copy_to_user`
  i `bws_vmm_sprawdz_zakres_uzytkownika`,
- `getrandom` ma limit 4 KiB na wywołanie (nie blokuje jądra),
- `write` obsługuje wyłącznie fd 1 i 2; inny deskryptor to `-EBADF`,
  a nie zgadywanie.

### 3.2 Loader ELF64 (`jadro/fs/elf_loader.{h,cpp}`)

Walidacja i opis — bez mapowania. Mapper używa tego opisu w 2.18+.

Odrzucane: zła magia/klasa/endianness/wersja, `e_type` spoza EXEC/DYN,
architektura ≠ x86-64, zły `e_phentsize`, `e_phoff` poza plikiem, brak
`PT_LOAD`, `p_memsz < p_filesz`, segment poza plikiem, adres poza dolną
połową kanoniczną lub w stronie 0, **segment jednocześnie PF_X i PF_W**
(W^X), nachodzące segmenty (także przy nieposortowanej tablicy), wejście
poza segment RX, `PT_INTERP`/`PT_DYNAMIC` (brak linkowania dynamicznego).

`lodader_status_formatu()` w `loader.cpp` to jedno miejsce rozpoznania
formatu: `"bur"`, `"elf-ok:Nseg"`, `"elf:<powód>"`, `"nieznany"`. Funkcja
jest czysta — nie alokuje i nie mapuje.

### 3.3 TLS per proces (`jadro/mm/tls.{h,cpp}`)

- tablica baz `%fs` per PID, `WRMSR IA32_FS_BASE` (0xC0000100) przy każdym
  przełączeniu kontekstu (po zmianie CR3, przed powrotem do Ring 3),
- `arch_prctl(ARCH_SET_FS/GET_FS)` działa i stosuje się natychmiast,
- `ARCH_SET_GS` jest **odmówione** (`-EPERM`): `%gs` w Ring 0 wskazuje dane
  per-CPU i służy bramce BWS jako źródło szczytu stosu jądra — dopuszczenie
  zapisu z Ring 3 pozwoliłoby aplikacji podmienić sobie stos jądra,
- recykling PID czyści bazę (nowy proces nie dziedziczy cudzego TLS),
- walidacja adresu: dolna połowa kanoniczna, ≥ 64 KiB, wyrównanie do 8.

### 3.4 `mmap` / `munmap` / `mprotect`

Nowe okno adresowe `RING3_MMAP_BASE = 40 GiB .. +256 MiB`, pomiędzy SHM
(36 GiB) i stosem — bez kolizji ze wszystkimi istniejącymi zakresami
(`static_assert` w `mmap.h`).

- mapowanie **eager**: każda strona dostaje ramkę od razu; brak demand
  pagingu i COW (świadomie — patrz dług techniczny),
- first-fit w oknie z zachowaniem odstępu (off-by-one w kodzie użytkownika
  daje `#PF`, a nie ciche nadpisanie sąsiada),
- `MAP_FIXED` **nie przykrywa** istniejącego mapowania (odstępstwo od
  Linuksa celowe: cicha podmiana to klasyczne źródło UAF),
- W^X także tutaj: `PROT_WRITE|PROT_EXEC` → `-EACCES`, również przez
  `mprotect`,
- `munmap` obsługuje cały region i przycięcie od początku,
- `mprotect` zwraca `-ENOSYS` i **nie** zmienia księgowania: VMM nie ma
  jeszcze API zmiany flag istniejącego mapowania, a cicha rozbieżność
  między deklaracją a sprzętem jest gorsza niż jawny brak wsparcia.

### 3.5 Poprawione błędy znalezione przy implementacji

1. **`mmap` zwracał dodatnie `13` zamiast `-EACCES`.** Podwójna negacja
   errno w dwóch warstwach sprawiała, że odrzucenie wyglądało jak poprawny
   adres. Wykryte przez test QEMU (`8/8 mmap… W^X`), nie przez host.
   Naprawa: jedna konwencja (wszystkie błędy w `mmap_jadro.cpp` są ujemne),
   usunięcie drugiej negacji.
2. **`tls.o` nadpisał obiekt mbedTLS.** `jadro/siec/tls.cpp` i
   `jadro/mm/tls.cpp` produkowały ten sam `tls.o`; link się zerwał, a na
   dysku został obiekt z TLS per proces. Naprawa: obiekt nazywa się teraz
   `watek_tls.o`.
3. **Bramka SysV mogła zostać nadpisana przez MSI.** Wektor 0x80 leży
   w zakresie 48..254, który alokuje `idt_alokuj_wektor_irq`. Naprawa:
   jawne pominięcie + `static_assert` + odmowa w `idt_zarejestruj_irq`.
4. **Test nadużyć używał numeru 99 jako „nieznanego".** Numer 99 jest teraz
   `BWS_GUI_SKALA_WARSTWY`; regresja sprawdza `BWS_PIERWSZY_WOLNY + 1000`.
5. **Test QEMU czytał log serial podczas zapisu.** Na dysku 9p (Windows)
   `read_text` rzuca `OSError: No data available`; dodano ponawianie.

## 4. Etap 2 — backend Ozone

Wszystko w `narzedzia/chromium/`, kopiowane do drzewa Chromium przez
`przygotuj.py`. Chromium nie jest wendorowane.

| Plik | Rola |
|---|---|
| `bursztyn_toolchain.gn` | toolchain GN: `current_os = "bursztyn"`, clang, libc++, bez sysrootu |
| `args.gn` | pełny zestaw flag z uzasadnieniem każdej |
| `ozone_BUILD.gn` | target `source_set("bursztyn")` z listą źródeł i zależności |
| `ozone_platform_bursztyn.{h,cc}` | `OzonePlatform`: brak kompozycji (robi ją jądro), brak GL/Vulkan |
| `platform_window_bursztyn.{h,cc}` | okno ↔ warstwa BWS; z-order 1, rozmiar zaokrąglony do 4 px |
| `surface_factory_bursztyn.{h,cc}` | pusta lista GL implementacji, `nullptr` dla pixmap |
| `surface_ozone_bursztyn.{h,cc}` | Skia → `SkBitmap` → **BWS98**; tu jest „akceleracja 2D" |
| `platform_event_source_bursztyn.{h,cc}` | BWS 37/38 → `ui::MouseEvent`/`ui::KeyEvent` |
| `message_pump_bursztyn.{h,cc}` | pętla: `DoWork` → `DoDelayedWork` → BWS38 z timeoutem ≤ 8 ms |
| `client_native_pixmap_factory_bursztyn.{h,cc}` | `nullptr` (brak ścieżki GPU) |
| `posix_bws.{h,cc}` | cienkie wrappery BWS dla Chromium — żadnej logiki |
| `opis.aplikacji` | manifest `.cebula`: PZB-4, uprawnienia okna/pliki/sieć |
| `przygotuj.py` | pinning tagu `141.0.7390.65`, kopiowanie backendu, wypisanie komend |
| `weryfikuj_backend.py` | **gate**: numery BWS, args.gn, brak zakazanych API, manifest |

Krytyczne w `weryfikuj_backend.py`: każdy numer BWS w backendzie jest
porównywany z `bws_api.h`. Rozjazd numerów to ciche przekłamanie ABI, które
w QEMU wygląda jak losowy bug — test wyłapuje to bez kompilowania Chromium.

## 5. Weryfikacja

### 5.1 Nowe gate'y (wszystkie PASS)

| Cel | Wynik |
|---|---|
| `test-blit-skalowanie-host` | 105 168 asercji |
| `test-posix-abi-host` | 131 asercji |
| `test-elf-loader-host` | 1 133 asercje |
| `fuzz-elf-loader` (20 000 iteracji) | 9 581 przyjętych, 10 419 odrzuconych, 0 błędów |
| `test-tls-host` | 36 asercji |
| `test-mmap-host` | 50 asercji |
| `test-posix-gate-qemu` | **8/8 kategorii w prawdziwym Ring 3** |
| `test-chromium-backend` | PASS, 0 ostrzeżeń |

`test-posix-gate-qemu` sprawdza w Ring 3: dostępność bramki (DPL=3),
nieznany numer → `-ENOSYS`, errno w zakresie −1..−4095,
`write(1)` na terminal, zły fd → `-EBADF`, brak regresji BWS,
TLS (`arch_prctl` + zapis/odczyt przez `%fs` + odmowa `ARCH_SET_GS`),
`mmap` (adres, wyzerowanie, zapis/odczyt 3 stron, W^X, `munmap`,
podwójny `munmap` odrzucony).

### 5.2 Regresje hosta

PASS: `stack-protector`, `bur-v2-aslr`, `virtio-pci`, `motywy-unit`,
`wm-click-routing`, `wm-dekoracje`, `compositor-cursor`, `shell-host`,
`desktop-files`, `bursztyn-ui-polecenia`, `cursor-host`, `cursor-hover`,
`desktop-capacity`, `husarz-engine` (**127 432 asercje**).

FAIL: `test-explorer-host` (`:60: opaque`) — **regresja zastana**,
identyczna na czystym `HEAD` w osobnym worktree. Nie jest skutkiem tej pracy.

### 5.3 QEMU: porównanie z baseline `HEAD`

| Cel | HEAD | Ta zmiana |
|---|---|---|
| `test-guard-page` | PASS | PASS |
| `test-bws-abuse-qemu` | PASS | PASS (+ nowa kategoria 4b dla BWS98/99) |
| `test-virtio-gpu-qemu` | FAIL (31 linii) | FAIL (31 linii, ten sam zbiór) |
| `test-normal-boot` | FAIL (`Kursor widoczny w (500,300)`) | FAIL (ten sam) |

Boot natywny na Windows (`qemu-system-x86_64.exe`, nigdy przez WSL):
`ready=True wallpaper=True desktop=True`, `System operacyjny gotowy!`,
brak paniki — po każdej z pięciu zmian jądra (2D, SysV, ELF, TLS, mmap).

## 6. Czego świadomie NIE ma

| Brak | Dlaczego |
|---|---|
| Binarka Chromium | build wymaga hosta ≥32 GB RAM, kilkuset GB dysku i `depot_tools`; w tym workspace nie da się go wykonać. Etap 2 dostarcza backend + konfigurację + gate, nie artefakt |
| Mapowanie ELF do procesu | wymaga demand pagingu i BSS zero-fill w handlerze `#PF`; walidacja i opis są gotowe, mapper jest następnym krokiem |
| `clone`/`futex`/pthreads | `futex` zwraca `-ENOSYS` (pthread fallbackuje na spinlock); bez wątków nie ma wieloprocesowej obsługi, ale też nie ma ryzyka zakleszczenia jądra |
| COW i demand paging | `mmap` jest eager; `MAP_PRIVATE` ≡ `MAP_SHARED` dopóki nie ma `fork()` |
| `mprotect` | zwraca `-ENOSYS`; VMM nie ma API zmiany flag istniejącego mapowania |
| SMP | planista jednordzeniowy; Chromium z jednym rdzeniem działa, płynność wymaga 2.21 |
| Virgl/3D/GPU raster | świadomie 2D only (zgodnie z decyzją o zakresie) |
| Linkowanie dynamiczne | `PT_INTERP`/`PT_DYNAMIC` odrzucane; binaria muszą być statyczne |
| Sandbox Chromium | `use_seccomp_bpf = false` — brak namespace'ów i `clone` |

## 7. Następne kroki (kolejność wymuszona zależnościami)

1. **Mapper ELF + BSS** — użyć `elf_opisz()` w `loader.cpp`, mapować przez
   `VmmMapujNowaStroneWPrzestrzeniProcesu` z W^X z opisu. Gate: `hello world`
   z musl w QEMU.
2. **`watek_t` + `clone` + `futex`** — rozdzielić proces (przestrzeń) od
   wątku (kontekst); podnieść limit do 64. Uwaga: `SKLADACZ_MAKS_WARSTW`
   jest powiązany z `PZB_MAKS_PROCESOW`.
3. **Demand paging + COW + `mprotect`** — handler `#PF`, RSS per proces,
   API zmiany flag w VMM.
4. **SMP** (2.21) — MADT, AP bringup, per-CPU TSS/GS, spinlocki.
5. **Build Chromium** na hoście: `przygotuj.py` → `gn gen` → `autoninja`
   → `headless_shell` → uruchomienie w QEMU przez `int 0x80`.

## 8. Gate'y do uruchomienia przed commitem

```
make test-blit-skalowanie-host
make test-posix-abi-host
make test-elf-loader-host
make fuzz-elf-loader
make test-tls-host
make test-mmap-host
make test-chromium-backend
make test-posix-gate-qemu
make test-bws-abuse-qemu
make test-guard-page
make test-smoke
```

Znane, zastane porażki (nie z tej pracy): `test-explorer-host`,
`test-virtio-gpu-qemu`, `test-normal-boot` — szczegóły w 5.2 i 5.3.
