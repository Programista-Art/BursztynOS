

# Dokumentacja Systemu Operacyjnego Bursztyn OS

![alt](image/logo-bursztyn-os.png)

Witaj w oficjalnej dokumentacji **Bursztyn OS** – niezależnego, 64-bitowego systemu operacyjnego z polską duszą inżynieryjną, tworzonego całkowicie od zera (czysty bare-metal).

System jest rozwijany z myślą o architekturze x86-64, implementując własne Jądro, autorski system plików, niezależny logiczny model bezpieczeństwa oraz natywne środowisko uruchomieniowe dla programów skompilowanych z języka Avocado.

## 🗂️ Struktura Dokumentacji

Poniższe pliki zawierają pełną specyfikację techniczną, opisy mechanizmów oraz analizę kodu źródłowego systemu:

1. [01_wstep_i_filozofia.md](docs/01_wstep_i_filozofia.md) – Wizja projektu, założenia ideologiczne, polskie nazewnictwo i roadmapa rozwoju.
2. [02_architektura_systemu.md](docs/02_architektura_systemu.md) – Podział na przestrzenie Ring 0/Ring 3, szczegółowa specyfikacja modelu BZL (Bursztynowy Poziom Zaufania) oraz wprowadzenie do BWS.
3. [03_proces_rozruchu.md](docs/03_proces_rozruchu.md) – Analiza `boot.S`, wielopoziomowe tablice stron, przejście do trybu Long Mode i przekazanie parametrów z GRUB.
4. [04_zarzadzanie_sprzetem.md](docs/04_zarzadzanie_sprzetem.md) – Inicjalizacja GDT, IDT, kontroler APIC, Zegar Systemowy (LAPIC Timer) oraz sterowniki wejścia/wyjścia (klawiatura PS/2, ekran tekstowy).
5. [05_bursztynowy_system_plikow.md](docs/05_bursztynowy_system_plikow.md) – Specyfikacja BSP, struktura węzłów indeksowych, parser ścieżek i implementacja RAM-dysku.
6. [06_wywolania_systemowe.md](docs/06_wywolania_systemowe.md) – Architektura BWS, Standard BWS dla rejestrów (R8-R13) i wykaz dostępnych wywołań systemowych.
7. [07_ekosystem_i_formaty.md](docs/07_ekosystem_i_formaty.md) – Specyfikacja binarna `.bur`, struktura paczek `.cebula` oraz manifesty `opis.aplikacji`.
8. [08_bursztynowy_slownik_i_architektura.md](docs/08_bursztynowy_slownik_i_architektura.md) – Oficjalny słownik pojęć rdzennych (Teczka, Włókna, Planista) oraz strategia pełnego wdrożenia UTF-8.

## 🛠️ Architektura w pigułce

* **Tryb procesora:** 64-bit Long Mode (wymagany start z GRUB za pomocą Multiboot2).
* **Zarządzanie pamięcią:** Zarządca Pamięci Fizycznej (mapa bitowa) + Zarządca Pamięci Wirtualnej (4-poziomowe stronicowanie PML4/PDP/PD/PT).
* **Wielozadaniowość/Asynchroniczność:** Nowoczesny APIC + Zegar LAPIC (Wektor 32) po całkowitym uśpieniu archaicznego PIC.
* **System plików:** BSP (Bursztynowy System Plików) – bloki 512 B, drzewo teczek oparte na węzłach indeksowych.

## Uruchomiony system Bursztyn OS na Linux Mint w QEMU
![alt](image/BursztynOS.png)

# Uruchomienie systemu na Linux Mint w QEMU
### Przygotowanie Środowiska w Linux Mint
1. Otwórz natywny terminal i wykonaj te trzy polecenia:
1. Aktualizacja repozytoriów:
```
sudo apt update && sudo apt upgrade -y
```

2. Instalacja narzędzi budujących i kompilatora skrośnego: Narzędzie build-essential dostarczy nam program make, a pakiety g++ pozwolą kompilować kod.
sudo apt install build-essential gcc-x86-64-linux-gnu g++-x86-64-linux-gnu -y

3. Instalacja narzędzi do tworzenia obrazu ISO (Multiboot)
Aby komenda grub-mkrescue działała bezbłędnie podczas budowania obrazu .iso  systemu Bursztyn OS, musisz doinstalować te pakiety:
```
sudo apt install xorriso mtools grub-pc-bin grub-common -y
```

4. Emulator do szybkich testów (Opcjonalnie)
Zamiast za każdym razem uruchamiać cięższe środowiska i przeklikiwać się przez interfejs VirtualBoxa, w systemie Linux Mint możesz zainstalować lekki emulator QEMU. Pozwala on na błyskawiczne uruchomiać system bezpośrednio z terminala za pomocą jednej komendy (np. qemu-system-x86_64 -cdrom BursztynOS.iso).
Aby zainstalować QEMU, wpisz w terminalu:
```
sudo apt install qemu-system-x86 -y
```


5. Wybór Edytora Kodu
    1. Możesz użyć wbudowanego w Minta lekkiego programu Xed.
    1. Możesz pobrać Visual Studio Code (wersję natywną dla Linuxa, instalowaną z pakietu .deb).
    1. Możesz użyć dowolnego innego narzędzia (np. Vim, Nano, CLion), w którym pisze Ci się wygodnie.
Skrypt Makefile i tak zajmie się całą "magią" kompilacji i budowania pliku ISO w tle, niezależnie od tego, w jakim programie edytujesz pliki źródłowe.


Aby w pełni skompilować kod Bursztyn OS i uruchomić go na maszynie wirtualnej, musisz połączyć wygenerowany plik binarny z programem rozruchowym GRUB (zgodnie ze standardem Multiboot2) i stworzyć obraz ISO.
Oto instrukcja krok po kroku, jak to zrobić na Twoim systemie Linux Mint:
## Krok 1: Dostosowanie Makefile do kompilatorów w Linux Mint
Makefile narzędzia są zdefiniowane jako standardowy cross-compiler x86_64-elf:

```
CC = x86_64-elf-g++
AS = x86_64-elf-as
LD = x86_64-elf-ld
```

## Krok 2: Kompilacja Jądra Bursztyna
Otwórz terminal w folderze, w którym znajduje się Twój plik Makefile i kod źródłowy, a następnie wpisz:
make

Skrypt automatycznie skompiluje pliki boot.o, gdt.o, pmm.o, vmm.o, kernel.o itd. i połączy je za pomocą Twojego skryptu linker.ld. Jeśli nie ma błędów w kodzie, w folderze pojawi się plik system_operacyjny.bin.
## Krok 3: Przygotowanie struktury pliku ISO (GRUB Multiboot2)
Bursztyn OS operuje w 64-bitowym Long Mode, więc wymaga programu rozruchowego (GRUB), który przekaże parametry Multiboot2. Musisz zbudować prostą strukturę teczek dla obrazu płyty:
W terminalu utwórz katalogi: 
```
mkdir -p isodir/boot/grub 
```
Skopiuj swoje skompilowane jądro do katalogu /boot: 
```
cp system_operacyjny.bin isodir/boot/ 
```

Stwórz plik konfiguracyjny GRUBa o nazwie grub.cfg (np. za pomocą xed isodir/boot/grub/grub.cfg lub nano isodir/boot/grub/grub.cfg) i wklej do niego ten kod: 

set timeout=0
set default=0

menuentry "Bursztyn OS" {
    multiboot2 /boot/system_operacyjny.bin
    boot
}

## Krok 4: Generowanie bootowalnego obrazu ISO
Mając gotową strukturę (isodir), użyj zainstalowanego wcześniej narzędzia grub-mkrescue, aby "zamknąć" to w plik .iso:

grub-mkrescue -o BursztynOS.iso isodir 

W głównym folderze pojawi się nowy plik BursztynOS.iso.

## Krok 5: Uruchomienie w QEMU
Teraz czas ożywić polski system operacyjny! Użyj QEMU, ładując wygenerowany obraz płyty:
```
qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G -serial stdio
```

(Flaga -m 2G przydziela 2 Gigabajty pamięci RAM dla maszyny, co przyda się przy testowaniu Zarządcy Pamięci (PMM/VMM) w Etapie 3, a -serial stdio pozwala kierować logi z jądra bezpośrednio do Twojego terminala Linux).
Po wykonaniu tej komendy powinno wyskoczyć okno emulatora wyświetlające GRUB, a zaraz po nim kod z pliku kernel.cpp Bursztyn OS! 


Komenda make clear - usuwa pliki .o
```
make clear
```

Skompiluj wszystko od nowa wpisując: 

make 

Teraz kompilator przejdzie przez wszystkie moduły (włącznie z systemem plików psf.o i urządzeniami), a na końcu wyświetli Ci poprawnie sklejony plik system_operacyjny.bin, który będziesz mógł podpiąć do GRUBa za pomocą komendy 


Ostatnie kroki w terminalu:
Wpisz po kolei te polecenia 
1. Skompiluj kod:
```
make
```
2. Skopiuj wygenerowany obraz do struktury wirtualnej płyty:
```
cp system_operacyjny.bin isodir/boot/
```
3. Stwórz plik ISO:
```
grub-mkrescue -o BursztynOS.iso isodir
```
lub
```
grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso
```
4. Uruchom system!:
```
qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G -serial stdio
```

ewentualnie po prostu wpisz ```make run``` i wszystko się zrobi automatycznie
```
make run 
```
