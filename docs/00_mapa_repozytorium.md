# 00. Mapa repozytorium źródłowego

Mapa katalogów **kodu źródłowego** Bursztyn OS (nie mylić z layoutem runtime BSP / ISO w gościu).
Ścieżki runtime (`/programy/*.cebula/...`, sloty OTA) są opisane w kanonie `07_*` oraz `docs/aktualizacja-systemu.md`.

## Korzeń (cienki)

Korzeń repozytorium to **drzewo buildu**: `Makefile`, entrypointy użytkownika oraz katalogi domenowe.
Źródła Ring 0 leżą w `jadro/` (wariant B) — nie w korzeniu.

Świadomie w korzeniu:

| Pliki | Powód |
|-------|--------|
| `Makefile` | orchestracja buildu; artefakty `*.o` powstają w korzeniu |
| `rununix.sh`, `rununix_virtio_latest.sh`, `runwindows.ps1`, `napraw_mbedtls.sh` | entrypointy użytkownika |
| `README.md`, `.gitignore` | dokumentacja / ignore |

Artefakty lokalne (`*.o`, `*.iso`, `isodir/`, katalogi `.*-test`) są w `.gitignore` — nie są częścią mapy źródłowej.

## Katalogi top-level

| Katalog | Zawartość |
|---------|-----------|
| `jadro/` | Źródła jądra Ring 0 (arch, mm, fs, bws, gui, sieć, aktualizacja, diag, rdzeń) |
| `programy/` | Źródła aplikacji Ring 3 (`.bur` / paczki `.cebula`) |
| `sterowniki/` | Sterowniki i HAL urządzeń (USB, HDA, VirtIO, magazyn, sieć, …) |
| `testy/` | Harnessy host/QEMU/fuzz (`test_*`, corpus fuzz) |
| `narzedzia/` | Tooling: packaging, runners, SDK, generatory (bez testów) |
| `docs/` | Kanon techniczny `00`–`12`, changelog, audyty |
| `bursztyn_ui/` | Widgety / API UI Ring 3 (nagłówki) |
| `bsf/` | System czcionek BSF |
| `biblioteki/` | Zależności zewnętrzne (mbedTLS) |
| `obrazy/` | Dekodery obrazów, tapety źródłowe |
| `czcionki/`, `dane/`, `ikonki/` | Zasoby pakowane do ISO |
| `gry/` | Gry (np. snake) |
| `image/` | Zrzuty / materiały README (marketing) |
| `isodir/` | Staging ISO (generowane / lokalne) |

## `jadro/` (wariant B)

| Podkatalog | Zawartość |
|------------|-----------|
| `arch/` | `boot.S`, `przerwania.S`, `ring3.S`, `syscall.S`, GDT/IDT/TSS/FPU, `linker.ld` |
| `mm/` | PMM, VMM, heap, stos jądra, SSP |
| `fs/` | PSF, `storage_format`, loader `.bur`, bootstrap |
| `bws/` | syscalls, BWS, bezpieczeństwo, scheduler |
| `gui/` | GUI jądra (`bursztyn_gui`, motyw, składacz, ekran/tapety/instalator po stronie Ring 0) |
| `siec/` | stos sieci jądra, TLS / port mbedTLS |
| `aktualizacja/` | OTA / aktualizacja systemu (strona jądra) |
| `diag/` | diagnostyka systemu (Ring 0) |
| `rdzen/` | `kernel.cpp` i wspólne nagłówki startu / tożsamości |

Aplikacje Ring 3 dla ekranu/tapet/instalatora nadal leżą w `programy/{ekran,tapety,instalator}/` — jednostki w `jadro/gui/` to strona jądra / formaty linkowane do blobów.

## Stan po reorganizacji

- **A:** audyty → `docs/audyty/`; Ring 3 → `programy/`; sterowniki → `sterowniki/`; testy → `testy/`; skrypty ad-hoc → `narzedzia/archiwum/`.
- **B:** jądro → `jadro/{arch,mm,fs,bws,gui,siec,aktualizacja,diag,rdzen}/`; korzeń bez źródeł Ring 0.

Weryfikacja: `make test-smoke`, przy zmianach jądra także `make test-security` / `make test-regression` — [12_lokalna_weryfikacja.md](12_lokalna_weryfikacja.md).
