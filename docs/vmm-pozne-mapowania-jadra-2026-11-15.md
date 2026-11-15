# Pozne mapowania jadra a klony PML4 procesow

## Ryzyko wyjsciowe
`UtworzPrzestrzenAdresowaProcesu()` klonuje **bazowy** PML4 w chwili tworzenia procesu. Mapowanie jadra wykonane PO tym
momencie mogło byc niewidoczne w istniejacych procesach albo — gdy wykonano je przy CR3 procesu — widoczne tylko w nim.

## Faktyczna architektura wspoldzielenia tablic (vmm.cpp, `sklonuj_hierarchie`)
| Czesc | Co jest wspolne po sklonowaniu |
|---|---|
| PML4[256..511] (gorna polowa) | wpisy PML4 kopiowane **po wartosci** → tablice PDP/PD/PT pod nimi sa **wspolne** (nowe strony w istniejacym slocie widoczne wszedzie); **sam wpis PML4 utworzony po sklonowaniu NIE trafia do istniejacych klonow** |
| PML4[0..255] (dolna polowa) | poziomy 4→2 klonowane gleboko (prywatne PDP/PD), liscie 4 KiB kopiowane jako wpisy → **nic nie jest wspolne**; pozniejsza zmiana PTE w bazie/innym procesie nie jest widoczna |
| identity-mapa < 4 GiB | huge-page 2 MiB kopiowane jako liscie; zmiany po sklonowaniu niewidoczne |

Mapowania jadra sa wykonywane przez `pml4_z_cr3()` (biezace CR3), a nie przez baze.

## Audyt sciezek mapowania po starcie procesow
Pomiar natywny (`BURSZTYN_DIAG_LATE_KMAP=1`, UEFI, uruchomienie Notatnika, Eksploratora, Snake'a, Przegladarki Obrazow, Kalkulatora,
Ustawien): **> 8 000 mapowan jadra po utworzeniu pierwszego procesu, 0 w dolnej polowie, ~99,6% wykonanych przy CR3 procesu**
(bufory grafiki w gornej polowie). Dziala wylacznie dlatego, ze slot PML4 `0xffff9000…` istnial przed pierwszym procesem
(komentarz w `bufory_grafiki.h`: „Inicjalizacja przed pierwszym Ring3”). Zrodla pozniejszych mapowan:
bufory grafiki i stosy jadra (gorna polowa, runtime), FB alias/ACPI/HPET/xHCI/OHCI/BAR (dolna polowa, glownie boot; FB alias takze przy zmianie trybu).

## Zidentyfikowane wady
* **D1 (gorna polowa):** mapowanie wymagajace NOWEGO slotu PML4 przy CR3 procesu zakladalo prywatny PDP tego procesu: niewidoczne w bazie
  i innych procesach, a PML4[256+] nie sa zwalniane przy niszczeniu → wyciek i „znikajace” mapowanie. (Dowod natywny w kontroli negatywnej: `T2` FAIL.)
* **D2 (dolna polowa):** pozne mapowanie jadra (np. FB alias po zmianie trybu, okno ACPI) trafialo tylko do jednej przestrzeni (`T1` FAIL).
* **D3:** mapowanie jadra pod adresem zajetym przez mapowanie uzytkownika w innej przestrzeni nie bylo wykrywane (kontrola dotyczyla tylko CR3).
* **D4:** `VmmOdmapujStroneJadra` odmapowuje wylacznie w biezacym CR3 (brak wywolan dla dolnej polowy; dodano wariant globalny).

## Poprawka (najmniejsza, bez migracji higher-half)
Nowe `jadro/mm/vmm_synchronizacja.h` (czysta logika) + integracja w `vmm.cpp`:
1. **Rejestr zywych klonow PML4** (`Rejestr<256>`, pod `BlokadaVMM`): klonowanie i rejestracja sa atomowe (obie pod jedna blokada), wyrejestrowanie
   PRZED zwalnianiem tablic.
2. **Gorna polowa:** `zapewnij_slot_jadra` — slot PML4 tworzony RAZ w bazie i **propagowany do wszystkich zywych klonow** (naprawia tez niezgodny
   wpis); wywolywany z `mapuj_strone_wewnetrznie` dla kazdego mapowania jadra z indeksem PML4 ≥ 256.
3. **Dolna polowa:** `mapuj_jadro_z_routingiem` (wejscie `ZmapujStroneSprawdzona`, `ZmapujStrone64Sprawdzona`, `VmmMapujNowaStroneJadra`): po
   utworzeniu pierwszego procesu mapowanie jadra (bez `PTE_USER`) idzie do **wszystkich** przestrzeni (`mapuj_globalnie`): (a) kolizja z obecna
   stroną uzytkownika w DOWOLNEJ przestrzeni → odmowa bez zmian, (b) tablice posrednie we wszystkich celach (jedyne miejsce, gdzie moze zabraknac
   pamieci; zmiany semantycznie neutralne → brak cofania), (c) zapis lisci z cofnieciem przy (teoretycznej) porazce. Mapowanie uzytkownika (heap
   przez `ZmapujStrone64Sprawdzona` z `PTE_USER`) nie jest rozglaszane.
4. **Centralna rezerwacja:** pozne mapowanie jadra w dolnej polowie ≥ 4 GiB wymaga adresu w `ZarezerwowaneVA` (inaczej odmowa + log); wszystkie
   regiony gornej polowy maja stale w `zarezerwowane_va.h` z `static_assert` unikalnosci slotow PML4 i zgodnosci ze stalymi wlascicieli
   (bufory grafiki, stosy jadra, HDA). Nowe okno testowe zarezerwowane (`VMM_SAMOTEST_*`).
5. `VmmOdmapujStroneJadraGlobalnie` (odmapowanie liscia 4K we wszystkich przestrzeniach; odmowa dla huge-page i stron uzytkownika).

## Wlasnosc pamieci, uprawnienia, czas zycia
* Liscie jadra maja `PTE_USER = 0` i nie sa zwalniane przy niszczeniu procesu (`zniszcz_hierarchie_procesu` pomija supervisor-owe liscie); ramke zwalnia
  wlasciciel mapowania po `VmmOdmapujStroneJadraGlobalnie`/`VmmOdmapujStroneJadra`.
* Tablice PDP/PD/PT klonu naleza do klonu (zwalniane z procesem); wspolne PDP gornej polowy naleza do jadra na stale.
* Rejestr trzyma surowe wskazniki tylko dla zywych klonow; nie ma dostepu do tablic po wyrejestrowaniu.
* Brak zmian w SMEP/SMAP/NX/W^X/ASLR (nie dotykano CR4/EFER, mapperow uzytkownika ani loadera).

## TLB i SMP
* Wszystko pod `BlokadaVMM` (IRQ wylaczone + spinlock) — modyfikacja aktywnych tablic jest atomowa wzgledem przerwan.
* Kazdy zapis/usuniecie liscia robi `invlpg` na lokalnym CPU; dodanie wpisu PML4 nie wymaga flush (wpis nieobecny nie jest cache'owany); przelaczenie CR3 przeladowuje
  nie-globalne wpisy TLB (brak PCID, strony jadra tu bez `PTE_GLOBAL`), wiec inne przestrzenie zobacza zmiane przy nastepnym przelaczeniu.
* **Ograniczenie SMP:** w jadrze nie ma rozruchu AP (brak SIPI/trampoliny; dziala tylko BSP), wiec shootdown IPI nie jest potrzebny i nie
  jest zaimplementowany. Przy dodaniu SMP trzeba dodac shootdown we wszystkich miejscach `uniewaznij_tlb` w `OpsVmm::{zapisz_lisc,ustaw_lisc_surowy}` (komentarz w `vmm_synchronizacja.h`).

## Testy
Host (ASan/UBSan): `make test-vmm-synchronizacja-host` (10 752 sprawdzenia: brak celow, mapowanie w bazie+3 klonach, pozny klon, **konflikt z mapowaniem uzytkownika
bez zadnej zmiany**, **porazka alokacji w k-tej przestrzeni dla k=0..3**, porazka zapisu z cofnieciem zastapienia, odmapowanie/konflikt/huge, slot PML4 + naprawa
niezgodnego + brak pamieci, rejestr (duplikaty, przepelnienie, kolejnosc, pojemnosc), 4 000 operacji map/unmap przy tworzeniu/niszczeniu procesow) oraz
`test-vmm-zakres-host`, `test-bur-v2-aslr-host` (okno testowe zarezerwowane).
Natywnie (`BURSZTYN_DIAG_VMM_SELFTEST=1`, procesy dzialaja, prawdziwe klony PML4 i prawdziwe przelaczenia CR3 z IRQ wylaczonymi): 46/46 PASS w 2 przebiegach:
T1 pozne mapowanie dolnej polowy widoczne w bazie i 3 klonach; T2 NOWY slot PML4 utworzony z CR3 klonu → propagowany do bazy i wszystkich klonow, bez `PTE_USER`;
T3 konflikt z mapowaniem uzytkownika → odmowa, inne przestrzenie niezmienione, mapowanie uzytkownika nienaruszone, odmowa poza zarezerwowanym oknem;
T4 wstrzyknieta porazka alokacji tablic → odrzucone, brak lisci wszedzie, ponowienie dziala; T5 300× map/unmap globalne bez wycieku ramek;
T6 niszczenie procesow zwraca ich tablice, mapowania jadra zyja w pozostalych, pozne mapowanie po zniszczeniu; T7 bilans ramek: przebieg 2 = **0** (brak wyciekow).
**Kontrola negatywna** (`BURSZTYN_DIAG_VMM_NO_SYNC=1`, zachowanie sprzed poprawki): T1–T6 FAIL (10 z 24 sprawdzen) — bledy D1/D2 odtworzone natywnie.
