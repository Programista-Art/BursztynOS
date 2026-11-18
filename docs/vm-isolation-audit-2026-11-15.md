# Audyt izolacji pamięci wirtualnej i ASLR (Ring 3 ↔ jądro)

Zakres: inwentarz VA jądra, niezmienniki izolacji, testy. Bez migracji do jądra „higher-half”.

## 1. Inwentarz VA jądra (stan faktyczny, nie z komentarza w `pamiec.h`)
Każdy proces dostaje **klon dolnej połowy bazowego PML4** (`sklonuj_hierarchie`: wpisy PML4[0..255] kopiowane głęboko, liście 4 KiB
i huge-page jako kopie wpisów; PML4[256..511] współdzielone). Wszystko poniżej z dolnej połowy jest więc widoczne w KAŻDEJ
przestrzeni procesu i może kolidować z mapowaniami użytkownika.

| Region (VA) | Rozmiar | Właściciel / kod | Dolna połowa? | Okno ASLR może go przeciąć? | Na liście `zarezerwowane_va.h`? |
|---|---|---|---|---|---|
| 0 – 4 GiB identity-mapa (huge 2 MiB, supervisor) | 4 GiB | `vmm.cpp` (`przygotuj_mapowania_jadra`) | tak | nie (PIE ≥ 4 GiB); legacy v1 @16 MiB tak, patrz §5 | n/d (granica `LIMIT_IDENTITY_MAPY`) |
| obraz jądra 0x100000 – 0xB17370 (~11,1 MiB) | | linker | tak (w identity-mapie) | nie | n/d |
| 0x120000000 scratch Tapet | 32 MiB | `grafika.cpp` (`VADDR_RAW_BMP`) | tak | **tak** (PIE 4–8 GiB) | tak |
| 0x130000000 alias LFB (UC-) | ≤ 64 MiB | `grafika.cpp` (`VADDR_FRAMEBUFFER`) | tak | **tak** | tak |
| 0x140000000 okno ACPI | 1 MiB | `acpi.cpp` | tak | **tak** | **dodane** (wcześniej brak) |
| 0x141000000 HPET (BAR ≥ 4 GiB) | 4 KiB | `hpet.cpp` | tak | **tak** | **dodane** (wcześniej brak) |
| 0x144000000 OHCI MMIO | 4 KiB | `ohci.cpp` | tak | **tak** | **dodane** (wcześniej brak) |
| 0x200000000 RAM-dysk PSF | 128 MiB | `kernel.cpp` | tak | brzeg okna PIE | tak |
| 0x500000000 sterta jądra | 16 MiB | `kernel.cpp` | tak | nie | tak |
| 0x2000000000 MMIO xHCI | 8×8×16 MiB | `xhci.cpp` | tak | nie | **dodane** |
| BAR-y mapowane VA==PA (virtio_pci, AHCI, e1000, LAPIC) | zależne od PA | `ZmapujStrone(p,p)` | tak | **tak, gdy PA ≥ 4 GiB (64-bit BAR)** — adres zależy od maszyny | **nie da się wymienić** → sprawdzane dynamicznie |
| HHDM (bezpośrednia mapa fizyczna, tablice stron, PMM) | RAM | `vmm.cpp` | **nie** (PML4 ≥ 256) | nie | n/d |
| bufory grafiki `0xffff900000000000` (sloty) | | `bufory_grafiki.*` | nie | nie | n/d |
| MMIO/bufory HDA `0xffff920000000000` | | `hda_transport.cpp` | nie | nie | n/d |
| stosy jądra `0xFFFF940000000000` | | `stos_jadra.*` | nie | nie | n/d |
| okna użytkownika ASLR: PIE [4,8) GiB, sterta [32,36) GiB, SHM [64,68) GiB, stos 0x7FFFE0000000.. | | `bur_v2.h` | tak (prywatne) | — | — |

Komentarz w `pamiec.h` (układ z „0x800000000 sterta, 0x900000000 SHM”) i w `kernel.cpp` („0x100000000 backbuffer, 0x110000000 tapeta”) jest
przestarzały: brak kodu używającego 0x100000000/0x110000000 (bufor tapety i backbuffer są w slotach grafiki, górna połowa).
Prawdziwą granicą jest `LIMIT_IDENTITY_MAPY` (4 GiB) i wpisy PML4.

**Lista statyczna była niekompletna:** ACPI, HPET, OHCI i xHCI leżały w dolnej połowie (ACPI/HPET/OHCI wewnątrz okna PIE) i nie były
zarezerwowane — ten sam typ kolizji co #PF Tapet (przy HPET/ACPI pod adresem ≥ 4 GiB proces czytałby własną stronę zamiast rejestrów; OHCI podobnie).
Dynamiczne BAR-y (VA==PA ≥ 4 GiB) w ogóle nie dają się wpisać na listę.

## 2. Niezmienniki i punkty egzekwowania
| # | Niezmiennik | Egzekwowanie |
|---|---|---|
| I1 | Plan ASLR (obraz, sterta, SHM, stos) nie przecina znanych okien jądra | `bur_v2_zaplanuj_aslr` + `ZarezerwowaneVA::koliduje_z_jadrem` (szybki test statyczny; `static_assert` w sterownikach wiążą stałe z listą) |
| I2 | **Plan ASLR nie przecina ŻADNEGO obecnego mapowania bazowej przestrzeni jądra** (niezależnie od listy) | `loader.cpp` → `VmmZakresWolnyWBazie` (`vmm_zakres.h::zakres_wolny` po prawdziwym drzewie tablic, pomija puste poddrzewa, fail-closed) dla obrazu, sterty, SHM, stosu+guardów; losowanie ponowne ≤ 64×, potem odmowa |
| I3 | Mapper strony użytkownika nigdy nie zastępuje strony jądra ≥ 4 GiB ani żadnej strony użytkownika | `vmm_mapuj_nowa_strone_procesu_wewnetrznie` → `wolno_mapowac_strone_uzytkownika` (czysty predykat); błąd mapowania → loader cofa mapowania i niszczy klon (istniejąca ścieżka) |
| I4 | SHM nie nadpisuje żadnej obecnej strony | `VmmMapujStroneWspoldzielona`: `sprawdz_mapowanie(...).obecna` → odmowa (dowolna obecna strona) |
| I5 | Sterta rośnie wyłącznie w `[sterta_baza, sterta_limit)` oknie planu, bez przepełnień | `syscalls.cpp` (bws_alokuj_sterte): `poczatek ≥ heap_base`, `koniec ≤ heap_limit`, dodawanie bez przepełnień; mapper (I3) |
| I6 | Użytkownik nie wybiera adresu mapowania | żaden syscall nie przyjmuje VA do mapowania (sterta/SHM: adresy z planu; loader: plan) |
| I7 | Niszczenie procesu zwalnia tylko liście USER niewspółdzielone | `zniszcz_hierarchie_procesu`: liście supervisor pomijane, `PTE_SHARED` odpinane, huge nie zwalniane; PML4[256+] nigdy rekurencyjnie |
| I8 | Klon nie dzieli tablic dolnej połowy między procesami | `sklonuj_hierarchie` kopiuje poziomy 4→2; liście 4 KiB kopiowane jako wpisy (PTE prywatne) |
| I9 | NX, W^X, SMEP, SMAP, SSP aktywne | `vmm.cpp` (CR4/EFER) + natywny `test_bur_v2_qemu.py` (CR4.SMEP/SMAP, EFER.NXE, zapis do code/rodata odrzucony, wykonanie sterty/stosu odrzucone) |
| I10 | Wskaźniki Ring 3 do jądra/NULL/guard/ro nie są dereferencjonowane przez jądro | `bws_vmm_sprawdz_zakres_uzytkownika` + `OknoUzytkownika` (STAC/CLAC); natywny `test_bws_abuse_qemu.py` |

Zmienione/dodane w tym etapie: I2, I3 (wydzielony predykat), rozszerzenie I1 o ACPI/HPET/OHCI/xHCI i stos, testy.

## 3. Proponowana granica architektoniczna (nie wdrożona)
1. **Dolna połowa wyłącznie dla użytkownika.** Przenieść stałe mapowania jądra z dolnej połowy (scratch Tapet, alias LFB, RAM-dysk PSF,
   sterta jądra, okna ACPI/HPET/OHCI/xHCI, BAR-y) do współdzielonej górnej połowy, za jednym alokatorem VA jądra
   (`KernelVa::alokuj(rozmiar)` w obszarze np. `0xFFFF9800…`). Wtedy klon dolnej połowy zawiera tylko identity-mapę < 4 GiB i
   kolizja ASLR z mapowaniem jądra jest niemożliwa z konstrukcji (lista statyczna zbędna).
2. Identity-mapa < 4 GiB zostaje do migracji jądra do higher-half (poza zakresem); okno PIE ≥ 4 GiB jej nie dotyka.
3. Do tego czasu: I2 jako obrona niezależna od listy + I3 jako wymuszenie w jedynym wspólnym mapperze + `static_assert`y wiążące
   stałe sterowników z jedną nagłówkową tabelą.

## 4. Testy
Host (ASan/UBSan, wszystkie PASS):
* `test-vmm-zakres-host` (100): predykat mapowania, `zakres_wolny` po 4-poziomowym drzewie (liście 4K/2M/1G, granice ±1 bajt,
  dokładna baza z awarii `0x1201F0000`, mapowania dynamiczne spoza listy, nieczytelna tablica = fail-closed, pusty/odwrócony zakres,
  poza dolną połową), spójność listy statycznej.
* `test-bur-v2-aslr-host`: odrzucenie dokładnych granic każdego okna (w tym ACPI/HPET/OHCI), wyczerpująco 2^20 slotów PIE
  (24 960 odrzuconych = geometria okien), sterta/SHM nigdy w oknach jądra, 10 000 losowań.
* `test-startup-visibility` (11 610 + `test_boot_graphics` 127 + 20): geometria warstw oraz transakcje BWS 19.
Natywnie:
* Rozruchy produkcyjne: 21/21 (UEFI tablet 12, UEFI mysz USB 6, BIOS tablet 3) bez paniki; 4 dodatkowe po ostatniej zmianie (stos w I2).
* `testy/test_bws_abuse_qemu.py` PASS (6 kategorii: NULL, wskaźniki jądra/HHDM/niekanoniczne, guard stosu, zapis do `.text`, przepełnienia,
  operacje uprzywilejowane, ścieżki).
* `narzedzia/test_bur_v2_qemu.py` PASS (SMEP/SMAP/NXE włączone, W^X, brak wykonania sterty/stosu, SSP, zmienny układ ASLR w 5 uruchomieniach).
  Oba harnessy QEMU były nieaktualne (brak skompresowanych blobów `_bin_bz_*`; odczyt rejestrów monitora) — poprawione bez osłabiania asercji.

## 5. Pozostałe ryzyka i ograniczenia
* Do czasu przeniesienia mapowań jądra do górnej połowy ochrona opiera się na I2/I3, nie na konstrukcji. Mapowania jądra tworzone
  po sklonowaniu procesu (np. BAR-y mapowane `ZmapujStrone` w kontekście procesu) trafiają do jego bieżącego CR3, nie do bazy — pre-existing,
  nie zmieniane (I2 sprawdza bazę, czyli źródło klonów).
* Binarki legacy BUR v1 (nie-PIE, 16 MiB) mapują się w identity-mapie < 4 GiB i zastępują w prywatnym klonie wpisy identity; jądro korzysta
  z HHDM, więc nie traci dostępu do pamięci fizycznej, ale to nadal nakładanie na identity-mapę (wszystkie produkcyjne aplikacje są v2/PIE).
* Brak testu hosta na prawdziwym `vmm.cpp` (zależy od CR3/PMM); logika reguł jest wydzielona i testowana, a mapper/loader — natywnie.
* Zakres statyczny xHCI (1 GiB) i LFB (64 MiB) to górne ograniczenia.
