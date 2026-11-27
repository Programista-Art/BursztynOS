# BRB — licencje i zależności

Status: audyt **[FAKT]** + propozycje **[PROJEKT]**. Dokument nie jest poradą prawną. Opisuje stan
repozytorium i obowiązki wynikające z treści licencji obecnych w repozytorium i na nośnikach.

---

## 1. Komponenty GRUB obecne w projekcie

| Element | Rodzaj | Śledzony w git | Rozprowadzany z Bursztyn OS | Źródło |
|---|---|---|---|---|
| `grub-mkrescue`, `grub-mkstandalone`, `grub-file` (pakiet hosta 2.12-1ubuntu7.3) | narzędzia budowania | nie | nie (same narzędzia) | `Makefile:43–44`, `:1361`, `:1498` |
| GRUB i386-pc + x86_64-efi w `BursztynOS.iso` (core.img, efi.img, moduły) | **binaria GRUB w nośniku** | nie (artefakt) | **tak** (każde ISO) | wynik `grub-mkrescue` |
| `.installer-build/BOOTX64.EFI` (GRUB standalone) | binarium GRUB | nie (artefakt) | **tak**: w ISO (`/boot/installer/BOOTX64.EFI`) i **na dysku użytkownika** (ESP) | `Makefile:1359–1361` |
| `.bws-test/isodir_bws/boot/installer/BOOTX64.EFI`, `isodir_virtio_forensic/boot/installer/BOOTX64.EFI` | binarium GRUB | **tak** (fikstury testowe) | tylko jeśli repozytorium jest rozprowadzane | `git ls-files` |
| `narzedzia/instalator/grub-embedded.cfg` | skrypt konfiguracji GRUB (kod projektu) | tak | wbudowany w `BOOTX64.EFI` | — |
| wpisy `grub.cfg` generowane w `Makefile` i fikstury `docs/graphics-performance-2026-11-09/*/grub.cfg`, `.bws-test/…/grub.cfg` | konfiguracja | częściowo | w ISO | — |
| `licenses/GRUB-copyright.txt`, `licenses/GPL-3.txt` | informacje licencyjne | w fiksturach (`.bws-test/…`, `.bsf-test/…`) | w ISO (kopiowane z hosta: `/usr/share/doc/grub-common/copyright`, `/usr/share/common-licenses/GPL-3`) | `narzedzia/package_release_notices.py` |
| `licenses/README.txt` | informacja projektu | w fiksturach | w ISO | treść: „GRUB is separately distributed boot software. Distributors must provide corresponding GRUB source…” |
| `check-kernel` (`grub-file --is-x86-multiboot2`) | kontrola w buildzie | — | nie | `Makefile:1262` |

**[WNIOSEK]** GRUB jest dziś **rozprowadzany** w postaci binarnej w każdym ISO i trafia na ESP każdego
zainstalowanego systemu. Licencja GPL-3.0-or-later wymaga m.in. udostępnienia odpowiadającego kodu źródłowego
na zasadach licencji. Projekt opisuje to w `licenses/README.txt` w ISO.

**[DO POTWIERDZENIA]** (P-10) Na ESP zainstalowanego systemu nie ma pliku z informacją licencyjną GRUB:
instalator zapisuje tylko 5 plików (`storage_format.cpp:335`). Czy wymaga to uzupełnienia — decyzja
właściciela i ewentualnie opinia prawna. Najprościej: zachować informację w `/licenses` na BSP2 albo
`\EFI\BURSZTYN\LICENCJE\` na ESP.

## 2. Inne komponenty firm trzecich (nie znikają po usunięciu GRUB)

| Komponent | Licencja (wg plików w repozytorium) | Gdzie |
|---|---|---|
| Mbed TLS 2.28.8 | Apache-2.0 **albo** GPL-2.0-or-later (do wyboru) | `biblioteki/mbedtls/LICENSE`, jądro (TLS, weryfikacja aktualizacji) |
| stb_truetype / stb_image | MIT **albo** public domain (Unlicense) — do wyboru | `bsf/vendor/LICENSE`, `obrazy/vendor/stb_image.h` |
| dr_mp3 | public domain (Unlicense) **albo** MIT-0 — do wyboru | `programy/grajek/zewnetrzne/LICENSE` |
| Czcionki DejaVu | licencja DejaVu (Bitstream Vera + public domain) | `czcionki/ttf/LICENSE-DejaVu.txt` |
| Dane Unicode | Unicode License | `narzedzia/unicode/LICENSE.txt` |
| Brotli (dekoder) | MIT (`bsf/vendor/brotli/LICENSE`) | `bsf/vendor/brotli/`, aplikacje Ring 3 (Husarz) |
| Bursztyn OS | PolyForm Noncommercial 1.0.0 | `LICENSE.md`, `NOTICE` |

**Usunięcie GRUB nie usuwa wszystkich obowiązków licencyjnych.** Informacje o pozostałych komponentach muszą
nadal trafiać na nośniki (`package_release_notices.py`).

## 3. Co można usunąć po zakończeniu migracji (Faza 15)

Dopiero po decyzji P-03/P-11 i gdy GRUB nie jest już rozprowadzany:

- cele i zmienne `GRUB_MKRESCUE`, `GRUB_FILE`, receptura `.installer-build/BOOTX64.EFI`,
  generowanie `grub.cfg` w `Makefile`;
- `narzedzia/instalator/grub-embedded.cfg`;
- skrypty pakujące dopisujące `module2` (`package_bsf.py`, `package_desktop_images.py`, `package_galeria.py`,
  `package_muzyka.py`) zastąpione generatorem `BRB.CFG`/manifestu modułów;
- kopiowanie `GRUB-copyright.txt` i `GPL-3.txt` w `package_release_notices.py` (tylko jeśli żaden inny
  rozprowadzany komponent nie wymaga tekstu GPL-3);
- fikstury testowe z binariami GRUB (`.bws-test/…/BOOTX64.EFI`, `isodir_virtio_forensic/…`) — jeśli testy ich
  nie potrzebują.

**Nie wolno** usuwać informacji o prawach autorskich ani plików licencji, dopóki odpowiadający im komponent
jest obecny w repozytorium lub na nośniku.

## 4. Zasady dla kodu BRB

1. BRB jest **oryginalną implementacją** na licencji projektu (PolyForm Noncommercial 1.0.0, chyba że
   właściciel zdecyduje inaczej — P-12).
2. **Zakaz kopiowania** kodu implementacji z GRUB, Limine, Syslinux, systemd-boot, rEFInd, gnu-efi, EDK II i
   innych programów rozruchowych.
3. Dozwolone źródła wiedzy: publiczne specyfikacje (UEFI, ACPI, Multiboot2, ELF gABI/psABI, PE/COFF,
   El Torito, ECMA-119, EDD, VBE, Intel SDM, AMD APM) — implementacja pisana od zera na ich podstawie.
4. **Deklaracje UEFI** (typy, GUID-y, układy struktur) piszemy samodzielnie w minimalnym nagłówku
   `rozruch/brb/uefi/uefi_typy.h`, tylko w potrzebnym zakresie (ok. 15 protokołów/struktur). Nie importujemy EDK II
   (licencja BSD-2-Clause-Patent jest liberalna, ale import frameworka jest niepotrzebny i niezgodny
   z założeniem minimalnych zależności). Identyczne GUID-y i układy wynikają ze specyfikacji, nie z kopiowania kodu.
5. **Wsparcie kompilatora**: GCC i binutils są narzędziami budowania. `libgcc` ma wyjątek GCC Runtime Library
   Exception (GPL-3.0 z wyjątkiem), który pozwala łączyć ją z programami na dowolnej licencji przy użyciu
   kwalifikowanego procesu kompilacji. Rekomendacja: BRB nie linkuje `libgcc` (`architektura-uefi.md` §12).
   Jądro już to robi (`-lgcc`, `Makefile:1256`).
6. **Kryptografia** (Faza 13): ponowne użycie Mbed TLS (Apache-2.0) albo innej uznanej biblioteki na licencji
   zgodnej. Bez własnych algorytmów.

## 5. Zależności BRB — zestawienie

| Zależność | Typ | Dystrybuowana z BRB | Powód |
|---|---|---|---|
| GCC 13 (`x86_64-linux-gnu-gcc/g++`) | narzędzie budowania | nie | kompilacja C/C++ |
| GNU binutils 2.42 (`as`, `ld -m i386pep`, `objcopy`) | narzędzie budowania | nie | asembler, PE32+, ELF |
| xorriso | narzędzie budowania ISO | nie | ISO 9660 + El Torito + obraz hybrydowy (zastąpi `grub-mkrescue`) |
| mtools albo własny generator FAT (Python) | narzędzie budowania | nie | obraz `efi.img`. Rekomendacja: generator w `narzedzia/brb/` oparty na tej samej logice co `fat_install_*`, żeby uniknąć nowych zależności |
| Python 3 | narzędzie budowania | nie | skrypty pakujące (już używany) |
| QEMU + OVMF + SeaBIOS | środowisko testowe | nie | testy (tylko za zgodą) |
| Firmware UEFI / BIOS | zależność czasu wykonania | nie (jest w komputerze) | usługi UEFI/BIOS |
| Mbed TLS (opcjonalnie, Faza 13) | biblioteka | tak (w BRB) | weryfikacja podpisów |
| Kod BRB | kod projektu | tak | — |
