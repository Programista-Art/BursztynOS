/*
 * Bursztyn OS - PCI Configuration Mechanism #1
 *
 * Obsluga klasycznej przestrzeni konfiguracyjnej PCI przez porty:
 *
 *   0xCF8 - CONFIG_ADDRESS
 *   0xCFC - CONFIG_DATA
 *
 * Publiczne ABI pozostaje zgodne z obecnym E1000/AHCI:
 *
 *   extern "C" uint32_t pci_odczytaj_dword(...);
 *   extern "C" void     pci_zapisz_dword(...);
 *   extern "C" void     skanuj_magistrale_pci();
 *
 * Najwazniejsze zasady bezpieczenstwa:
 *
 *   - para 0xCF8/0xCFC jest jedna wspolna transakcja i jest chroniona
 *     spinlockiem,
 *   - na lokalnym CPU IRQ sa wylaczane na czas transakcji, aby handler nie
 *     zmienil CONFIG_ADDRESS pomiedzy OUT 0xCF8 i IN/OUT 0xCFC,
 *   - slot > 31 i funkcja > 7 sa odrzucane,
 *   - offset dword jest zawsze wyrownany maska 0xFC,
 *   - skaner sprawdza bit Multifunction i skanuje funkcje 0..7,
 *   - raport tekstowy ma scisle bounds-checking,
 *   - raport jest w BSS, a nie na malym stosie kernela,
 *   - brak miejsca w raporcie daje jawny wpis "[raport uciety]" zamiast
 *     nadpisania pamieci.
 *
 * Ograniczenie:
 *
 * To jest PCI Configuration Mechanism #1 (legacy I/O). PCIe ECAM/MCFG
 * powinien zostac dodany pozniej na podstawie ACPI MCFG. Do tego czasu
 * mechanizm CF8/CFC jest odpowiedni dla obecnej konfiguracji QEMU.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pci.h"
#include "virtio_input_activation_diag.h"
#ifdef BURSZTYN_LOCK_HOST_TEST
extern uint64_t host_memory_irq_save();
extern void host_memory_irq_restore(uint64_t);
extern void host_pci_lock_io_write(uint16_t, uint32_t);
extern uint32_t host_pci_lock_io_read(uint16_t);
#endif

#ifdef BURSZTYN_VIRTIO_HOST_TEST
extern uint32_t host_pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
extern void host_pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

extern "C" uint32_t pci_odczytaj_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return host_pci_read(bus, slot, func, offset);
}

extern "C" void pci_zapisz_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    host_pci_write(bus, slot, func, offset, data);
}

extern "C" void skanuj_magistrale_pci() {
}
#else

/* =========================================================================
 * 1. API ZEWNĘTRZNE
 * ========================================================================= */

void wypisz_log(
    const char* tekst
);

extern "C" bool utworz_plik(
    const char* sciezka
);

extern "C" bool zapisz_do_pliku(
    const char* sciezka,
    const char* dane,
    uint32_t dlugosc
);
extern "C" bool psf_czy_gotowy();

/* =========================================================================
 * 2. STALE PCI
 * ========================================================================= */

namespace {

constexpr uint16_t PCI_PORT_CONFIG_ADDRESS =
    0x0CF8U;

constexpr uint16_t PCI_PORT_CONFIG_DATA =
    0x0CFCU;

constexpr uint32_t PCI_CONFIG_ENABLE =
    UINT32_C(0x80000000);

constexpr uint8_t PCI_HEADER_MULTIFUNCTION =
    0x80U;

/*
 * 32 KiB w BSS:
 *   - nie obciaza 16-KiB stosu procesu kernela,
 *   - wystarcza na typowy raport QEMU,
 *   - nadal ma scisly limit.
 */
constexpr size_t PCI_ROZMIAR_RAPORTU =
    32U * 1024U;

constexpr const char* PCI_SCIEZKA_RAPORTU =
    "/logi/pci.txt";

/* =========================================================================
 * 3. BLOKADA CONFIG_ADDRESS / CONFIG_DATA
 * ========================================================================= */

uint32_t blokada_pci =
    0;

struct StanPrzerwan {
    uint64_t rflags;
};

StanPrzerwan pci_zapisz_i_wylacz_irq() {
    StanPrzerwan stan{};
#ifdef BURSZTYN_LOCK_HOST_TEST
    stan.rflags = host_memory_irq_save();
#else
    asm volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(stan.rflags)
        :
        : "memory", "cc"
    );
#endif

    return stan;
}

void pci_przywroc_irq(
    StanPrzerwan stan
) {
#ifdef BURSZTYN_LOCK_HOST_TEST
    host_memory_irq_restore(stan.rflags);
#else
    if ((stan.rflags &
         (UINT64_C(1) << 9)) != 0) {

        asm volatile(
            "sti"
            :
            :
            : "memory"
        );
    }
#endif
}

void pci_zablokuj() {
    while (__atomic_exchange_n(
               &blokada_pci,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {

        while (__atomic_load_n(
                   &blokada_pci,
                   __ATOMIC_RELAXED) != 0U) {

            asm volatile(
                "pause"
                :
                :
                : "memory"
            );
        }
    }
}

void pci_odblokuj() {
    __atomic_store_n(
        &blokada_pci,
        0U,
        __ATOMIC_RELEASE
    );
}

class BlokadaPCI {
public:
    BlokadaPCI()
        : stan_irq_(
              pci_zapisz_i_wylacz_irq()
          ) {

        pci_zablokuj();
    }

    ~BlokadaPCI() {
        pci_odblokuj();

        pci_przywroc_irq(
            stan_irq_
        );
    }

    BlokadaPCI(
        const BlokadaPCI&
    ) = delete;

    BlokadaPCI& operator=(
        const BlokadaPCI&
    ) = delete;

private:
    StanPrzerwan stan_irq_;
};

/* =========================================================================
 * 4. I/O PORT
 * ========================================================================= */

void pci_outl(
    uint16_t port,
    uint32_t wartosc
) {
#ifdef BURSZTYN_LOCK_HOST_TEST
    host_pci_lock_io_write(port, wartosc);
#else
    asm volatile(
        "outl %0, %1"
        :
        : "a"(wartosc),
          "Nd"(port)
        : "memory"
    );
#endif
}

uint32_t pci_inl(
    uint16_t port
) {
#ifdef BURSZTYN_LOCK_HOST_TEST
    return host_pci_lock_io_read(port);
#else
    uint32_t wartosc =
        0;

    asm volatile(
        "inl %1, %0"
        : "=a"(wartosc)
        : "Nd"(port)
        : "memory"
    );

    return wartosc;
#endif
}

/* =========================================================================
 * 5. WALIDACJA / ADRES KONFIGURACYJNY
 * ========================================================================= */

bool pci_bdf_poprawne(
    uint8_t slot,
    uint8_t func
) {
    return
        slot <= PCI_MAKS_SLOT &&
        func <= PCI_MAKS_FUNC;
}

uint32_t pci_zbuduj_adres(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset
) {
    return
        PCI_CONFIG_ENABLE |
        (static_cast<uint32_t>(
             bus) << 16) |
        (static_cast<uint32_t>(
             slot) << 11) |
        (static_cast<uint32_t>(
             func) << 8) |
        static_cast<uint32_t>(
            offset &
            0xFCU
        );
}

/* =========================================================================
 * 6. WEWNĘTRZNY ODCZYT/ZAPIS
 * ========================================================================= */

uint32_t pci_odczytaj_dword_internal(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset
) {
    if (!pci_bdf_poprawne(
            slot,
            func)) {

        return
            UINT32_MAX;
    }

    const uint32_t adres =
        pci_zbuduj_adres(
            bus,
            slot,
            func,
            offset
        );

    BlokadaPCI blokada;

    pci_outl(
        PCI_PORT_CONFIG_ADDRESS,
        adres
    );

    return
        pci_inl(
            PCI_PORT_CONFIG_DATA
        );
}

void pci_zapisz_dword_internal(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset,
    uint32_t dane
) {
    if (!pci_bdf_poprawne(
            slot,
            func)) {

        return;
    }

    const uint32_t adres =
        pci_zbuduj_adres(
            bus,
            slot,
            func,
            offset
        );

    BlokadaPCI blokada;

    pci_outl(
        PCI_PORT_CONFIG_ADDRESS,
        adres
    );

    pci_outl(
        PCI_PORT_CONFIG_DATA,
        dane
    );
}

/* =========================================================================
 * 7. ODCZYTY POL / IDENTYFIKACJA
 * ========================================================================= */

struct PciId {
    uint16_t vendor;
    uint16_t device;
};

PciId pci_odczytaj_id(
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) {
    const uint32_t v =
        pci_odczytaj_dword_internal(
            bus,
            slot,
            func,
            PCI_OFFSET_VENDOR_DEVICE
        );

    PciId id{};

    id.vendor =
        static_cast<uint16_t>(
            v &
            0xFFFFU
        );

    id.device =
        static_cast<uint16_t>(
            v >> 16
        );

    return id;
}

bool pci_funkcja_istnieje(
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) {
    return
        pci_odczytaj_id(
            bus,
            slot,
            func
        ).vendor !=
        PCI_VENDOR_BRAK;
}

uint8_t pci_header_type(
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) {
    const uint32_t dword =
        pci_odczytaj_dword_internal(
            bus,
            slot,
            func,
            PCI_OFFSET_HEADER_INFO
        );

    /*
     * 0x0C:
     *   byte 0 = Cache Line Size
     *   byte 1 = Latency Timer
     *   byte 2 = Header Type
     *   byte 3 = BIST
     */
    return
        static_cast<uint8_t>(
            (dword >> 16) &
            0xFFU
        );
}

/* =========================================================================
 * 8. SAFE STRING BUILDER
 * ========================================================================= */

struct Raport {
    char* dane;
    size_t pojemnosc;
    size_t dlugosc;
    bool uciety;
};

char raport_pci[
    PCI_ROZMIAR_RAPORTU
] = {};

void raport_reset(
    Raport* raport
) {
    if (!raport ||
        !raport->dane ||
        raport->pojemnosc == 0) {

        return;
    }

    raport->dlugosc =
        0;

    raport->uciety =
        false;

    raport->dane[0] =
        '\0';
}

bool raport_dodaj_znak(
    Raport* raport,
    char znak
) {
    if (!raport ||
        !raport->dane ||
        raport->pojemnosc == 0) {

        return false;
    }

    if (raport->dlugosc + 1U >=
        raport->pojemnosc) {

        raport->uciety =
            true;

        return false;
    }

    raport->dane[
        raport->dlugosc++] =
        znak;

    raport->dane[
        raport->dlugosc] =
        '\0';

    return true;
}

bool raport_dodaj(
    Raport* raport,
    const char* tekst
) {
    if (!raport ||
        !tekst) {

        return false;
    }

    for (size_t i = 0;
         tekst[i] != '\0';
         ++i) {

        if (!raport_dodaj_znak(
                raport,
                tekst[i])) {

            return false;
        }
    }

    return true;
}

bool raport_dodaj_dec(
    Raport* raport,
    uint32_t wartosc
) {
    char temp[16] = {};
    size_t n = 0;

    if (wartosc == 0) {
        return
            raport_dodaj_znak(
                raport,
                '0'
            );
    }

    while (wartosc != 0 &&
           n < sizeof(temp)) {

        temp[n++] =
            static_cast<char>(
                '0' +
                wartosc % 10U
            );

        wartosc /=
            10U;
    }

    while (n > 0) {
        if (!raport_dodaj_znak(
                raport,
                temp[--n])) {

            return false;
        }
    }

    return true;
}

bool raport_dodaj_hex8(
    Raport* raport,
    uint8_t wartosc
) {
    static constexpr char HEX[] =
        "0123456789ABCDEF";

    return
        raport_dodaj_znak(
            raport,
            HEX[
                (wartosc >> 4) &
                0x0FU
            ]
        ) &&
        raport_dodaj_znak(
            raport,
            HEX[
                wartosc &
                0x0FU
            ]
        );
}

bool raport_dodaj_hex16(
    Raport* raport,
    uint16_t wartosc
) {
    return
        raport_dodaj_hex8(
            raport,
            static_cast<uint8_t>(
                wartosc >> 8
            )
        ) &&
        raport_dodaj_hex8(
            raport,
            static_cast<uint8_t>(
                wartosc &
                0xFFU
            )
        );
}

/* =========================================================================
 * 9. NAZWY KLAS PCI
 * ========================================================================= */

const char* pci_nazwa_klasy(
    uint8_t klasa,
    uint8_t podklasa,
    uint8_t prog_if
) {
    (void)prog_if;

    switch (klasa) {
        case 0x00:
            return
                "Urzadzenie niesklasyfikowane";

        case 0x01:
            switch (podklasa) {
                case 0x00:
                    return
                        "Kontroler SCSI";

                case 0x01:
                    return
                        "Kontroler IDE";

                case 0x02:
                    return
                        "Kontroler stacji dyskietek";

                case 0x04:
                    return
                        "Kontroler RAID";

                case 0x05:
                    return
                        "Kontroler ATA";

                case 0x06:
                    return
                        "Kontroler SATA/AHCI";

                case 0x07:
                    return
                        "Kontroler SAS";

                case 0x08:
                    return
                        "Kontroler NVMe";

                default:
                    return
                        "Inny kontroler pamieci masowej";
            }

        case 0x02:
            switch (podklasa) {
                case 0x00:
                    return
                        "Kontroler Ethernet";

                case 0x80:
                    return
                        "Inny kontroler sieciowy";

                default:
                    return
                        "Kontroler sieciowy";
            }

        case 0x03:
            switch (podklasa) {
                case 0x00:
                    return
                        "Kontroler VGA";

                case 0x02:
                    return
                        "Kontroler 3D";

                default:
                    return
                        "Kontroler grafiki";
            }

        case 0x04:
            switch (podklasa) {
                case 0x01:
                    return
                        "Kontroler audio";

                case 0x03:
                    return
                        "Intel High Definition Audio";

                default:
                    return
                        "Urzadzenie multimedialne";
            }

        case 0x05:
            return
                "Kontroler pamieci";

        case 0x06:
            switch (podklasa) {
                case 0x00:
                    return
                        "Mostek hosta";

                case 0x01:
                    return
                        "Mostek ISA";

                case 0x04:
                    return
                        "Mostek PCI-to-PCI";

                default:
                    return
                        "Mostek systemowy";
            }

        case 0x07:
            return
                "Kontroler komunikacyjny";

        case 0x08:
            return
                "Kontroler systemowy";

        case 0x09:
            return
                "Urzadzenie wejsciowe";

        case 0x0A:
            return
                "Stacja dokujaca";

        case 0x0B:
            return
                "Procesor";

        case 0x0C:
            if (podklasa == 0x03) {
                switch (prog_if) {
                    case 0x00:
                        return
                            "Kontroler USB UHCI";

                    case 0x10:
                        return
                            "Kontroler USB OHCI";

                    case 0x20:
                        return
                            "Kontroler USB EHCI";

                    case 0x30:
                        return
                            "Kontroler USB xHCI";

                    default:
                        return
                            "Kontroler USB";
                }
            }

            return
                "Kontroler magistrali szeregowej";

        case 0x0D:
            return
                "Kontroler bezprzewodowy";

        case 0x0E:
            return
                "Urzadzenie inteligentne";

        case 0x0F:
            return
                "Kontroler satelitarny";

        case 0x10:
            return
                "Kontroler szyfrowania";

        case 0x11:
            return
                "Kontroler DSP";

        case 0x12:
            return
                "Akcelerator";

        default:
            return
                "Nierozpoznana klasa PCI";
    }
}

/* =========================================================================
 * 10. RAPORT JEDNEJ FUNKCJI
 * ========================================================================= */

bool pci_dodaj_funkcje_do_raportu(
    Raport* raport,
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) {
    if (!raport) {
        return false;
    }

    const PciId id =
        pci_odczytaj_id(
            bus,
            slot,
            func
        );

    if (id.vendor ==
        PCI_VENDOR_BRAK) {

        return false;
    }

    const uint32_t class_info =
        pci_odczytaj_dword_internal(
            bus,
            slot,
            func,
            PCI_OFFSET_CLASS_INFO
        );

    const uint8_t revision =
        static_cast<uint8_t>(
            class_info &
            0xFFU
        );

    const uint8_t prog_if =
        static_cast<uint8_t>(
            (class_info >> 8) &
            0xFFU
        );

    const uint8_t podklasa =
        static_cast<uint8_t>(
            (class_info >> 16) &
            0xFFU
        );

    const uint8_t klasa =
        static_cast<uint8_t>(
            (class_info >> 24) &
            0xFFU
        );

    const uint32_t command_status =
        pci_odczytaj_dword_internal(
            bus,
            slot,
            func,
            PCI_OFFSET_COMMAND_STATUS
        );

    const uint16_t command =
        static_cast<uint16_t>(
            command_status &
            0xFFFFU
        );

    /*
     * Format:
     *
     * 00:03.0 vendor=8086 device=100E class=02:00 prog_if=00 rev=03
     *         cmd=0007 Kontroler Ethernet
     */
    if (!raport_dodaj_dec(
            raport,
            bus) ||
        !raport_dodaj_znak(
            raport,
            ':')) {

        return true;
    }

    if (slot < 10U) {
        if (!raport_dodaj_znak(
                raport,
                '0')) {

            return true;
        }
    }

    if (!raport_dodaj_dec(
            raport,
            slot) ||
        !raport_dodaj_znak(
            raport,
            '.') ||
        !raport_dodaj_dec(
            raport,
            func) ||
        !raport_dodaj(
            raport,
            " vendor=") ||
        !raport_dodaj_hex16(
            raport,
            id.vendor) ||
        !raport_dodaj(
            raport,
            " device=") ||
        !raport_dodaj_hex16(
            raport,
            id.device) ||
        !raport_dodaj(
            raport,
            " class=") ||
        !raport_dodaj_hex8(
            raport,
            klasa) ||
        !raport_dodaj_znak(
            raport,
            ':') ||
        !raport_dodaj_hex8(
            raport,
            podklasa) ||
        !raport_dodaj(
            raport,
            " prog_if=") ||
        !raport_dodaj_hex8(
            raport,
            prog_if) ||
        !raport_dodaj(
            raport,
            " rev=") ||
        !raport_dodaj_hex8(
            raport,
            revision) ||
        !raport_dodaj(
            raport,
            " cmd=") ||
        !raport_dodaj_hex16(
            raport,
            command) ||
        !raport_dodaj(
            raport,
            " ") ||
        !raport_dodaj(
            raport,
            pci_nazwa_klasy(
                klasa,
                podklasa,
                prog_if
            )) ||
        !raport_dodaj_znak(
            raport,
            '\n')) {

        return true;
    }

    return true;
}

/* =========================================================================
 * 11. FINALIZACJA UCIECIA RAPORTU
 * ========================================================================= */

void pci_oznacz_uciety_raport(
    Raport* raport
) {
    if (!raport ||
        !raport->uciety ||
        !raport->dane ||
        raport->pojemnosc == 0) {

        return;
    }

    static constexpr char ZNACZNIK[] =
        "\n[PCI] Raport uciety - osiagnieto limit bufora.\n";

    const size_t znacznik_len =
        sizeof(ZNACZNIK) - 1U;

    if (raport->pojemnosc <=
        znacznik_len + 1U) {

        raport->dane[
            raport->pojemnosc - 1U] =
            '\0';

        raport->dlugosc =
            raport->pojemnosc - 1U;

        return;
    }

    /*
     * Cofamy koniec raportu tak, aby znacznik zawsze sie zmiescil.
     */
    size_t start =
        raport->dlugosc;

    const size_t maks_start =
        raport->pojemnosc -
        znacznik_len -
        1U;

    if (start >
        maks_start) {

        start =
            maks_start;
    }

    for (size_t i = 0;
         i <
            znacznik_len;
         ++i) {

        raport->dane[
            start + i] =
            ZNACZNIK[i];
    }

    raport->dlugosc =
        start +
        znacznik_len;

    raport->dane[
        raport->dlugosc] =
        '\0';
}

} // namespace

/* =========================================================================
 * 12. PUBLICZNY ODCZYT PCI
 * ========================================================================= */

extern "C" uint32_t pci_odczytaj_dword(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset
) {
    return
        pci_odczytaj_dword_internal(
            bus,
            slot,
            func,
            offset
        );
}

/* =========================================================================
 * 13. PUBLICZNY ZAPIS PCI
 * ========================================================================= */

extern "C" void pci_zapisz_dword(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset,
    uint32_t data
) {
    pci_zapisz_dword_internal(
        bus,
        slot,
        func,
        offset,
        data
    );
}

/* =========================================================================
 * 14. SKANER PCI
 * ========================================================================= */

extern "C" void skanuj_magistrale_pci() {
    wypisz_log(
        "[PCI] Rozpoczynam skanowanie PCI..."
    );

    Raport raport{
        raport_pci,
        sizeof(raport_pci),
        0,
        false
    };

    raport_reset(
        &raport
    );

    (void)raport_dodaj(
        &raport,
        "Bursztyn OS - raport PCI\n"
        "BDF vendor device class prog_if rev command opis\n"
        "------------------------------------------------------------\n"
    );

    uint32_t liczba_funkcji =
        0;

    uint32_t liczba_slotow =
        0;

    for (uint16_t bus16 = 0;
         bus16 < 256U;
         ++bus16) {

        const uint8_t bus =
            static_cast<uint8_t>(
                bus16
            );

        for (uint8_t slot = 0;
             slot <=
                PCI_MAKS_SLOT;
             ++slot) {

            /*
             * Funkcja 0 okresla, czy slot istnieje oraz czy urzadzenie jest
             * multifunction.
             */
            if (!pci_funkcja_istnieje(
                    bus,
                    slot,
                    0)) {

                continue;
            }

            ++liczba_slotow;

            const uint8_t header_type =
                pci_header_type(
                    bus,
                    slot,
                    0
                );

            const uint8_t maks_func =
                (header_type &
                 PCI_HEADER_MULTIFUNCTION) != 0
                    ? PCI_MAKS_FUNC
                    : 0U;

            for (uint8_t func = 0;
                 func <=
                    maks_func;
                 ++func) {

                if (!pci_funkcja_istnieje(
                        bus,
                        slot,
                        func)) {

                    continue;
                }

                ++liczba_funkcji;

                (void)pci_dodaj_funkcje_do_raportu(
                    &raport,
                    bus,
                    slot,
                    func
                );

                if (raport.uciety) {
                    /*
                     * Nie ma sensu wykonywac kolejnych tysiecy operacji I/O,
                     * skoro raport i tak nie pomiesci nowych rekordow.
                     */
                    break;
                }
            }

            if (raport.uciety) {
                break;
            }
        }

        if (raport.uciety) {
            break;
        }
    }

    pci_oznacz_uciety_raport(
        &raport
    );

    if (liczba_funkcji == 0) {
        wypisz_log(
            "[PCI] Nie wykryto zadnych funkcji PCI."
        );

        return;
    }

    /*
     * Dodaj podsumowanie, o ile raport nie zostal juz uciety.
     */
    if (!raport.uciety) {
        (void)raport_dodaj(
            &raport,
            "------------------------------------------------------------\n"
            "Sloty: "
        );

        (void)raport_dodaj_dec(
            &raport,
            liczba_slotow
        );

        (void)raport_dodaj(
            &raport,
            "\nFunkcje: "
        );

        (void)raport_dodaj_dec(
            &raport,
            liczba_funkcji
        );

        (void)raport_dodaj_znak(
            &raport,
            '\n'
        );

        if (raport.uciety) {
            pci_oznacz_uciety_raport(
                &raport
            );
        }
    }

    if (!psf_czy_gotowy()) {
        return;
    }

    if (raport.dlugosc >
        UINT32_MAX) {

        wypisz_log(
            "[PCI] Raport jest zbyt duzy dla API systemu plikow."
        );

        return;
    }

    (void)utworz_plik(
        PCI_SCIEZKA_RAPORTU
    );

    if (!zapisz_do_pliku(
            PCI_SCIEZKA_RAPORTU,
            raport.dane,
            static_cast<uint32_t>(
                raport.dlugosc
            ))) {

        wypisz_log(
            "[PCI] Nie udalo sie zapisac /logi/pci.txt."
        );

        return;
    }

    if (raport.uciety) {
        wypisz_log(
            "[PCI] Skan zakonczony; raport zapisany, ale zostal uciety."
        );
    } else {
        wypisz_log(
            "[PCI] Skan zakonczony; raport zapisany do /logi/pci.txt."
        );
    }
}
#endif // !BURSZTYN_VIRTIO_HOST_TEST

#if BURSZTYNDIAGVIRTIOINPUTINIT
uint32_t pci_diagnostic_lock_state() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return 0;
#else
    return __atomic_load_n(&blokada_pci, __ATOMIC_RELAXED);
#endif
}
bool pci_diagnostic_try_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t* value) {
    if (!value || slot > 31 || func > 7) return false;
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    *value = host_pci_read(bus, slot, func, offset); return true;
#else
    const auto flags = pci_zapisz_i_wylacz_irq();
    // One attempt: diagnostics must not wait on the lock being diagnosed.
    if (__atomic_exchange_n(&blokada_pci, 1U, __ATOMIC_ACQUIRE)) {
        pci_przywroc_irq(flags); return false;
    }
    pci_outl(PCI_PORT_CONFIG_ADDRESS, pci_zbuduj_adres(bus, slot, func, offset));
    *value = pci_inl(PCI_PORT_CONFIG_DATA);
    pci_odblokuj(); pci_przywroc_irq(flags); return true;
#endif
}
#endif

/* =========================================================================
 * 6. OBSLUGA CAPABILITIES ORAZ GENERYCZNE MSI / MSI-X
 * ========================================================================= */

extern "C" uint8_t pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id) {
    if (!pci_funkcja_obecna(bus, slot, func)) {
        return 0;
    }
    const uint16_t status = pci_odczytaj_word(bus, slot, func, static_cast<uint8_t>(PCI_OFFSET_COMMAND_STATUS + 2));
    if ((status & PCI_STATUS_CAP_LIST) == 0) {
        return 0;
    }

    const uint8_t raw_cap_ptr = pci_odczytaj_byte(bus, slot, func, PCI_CAP_POINTER_OFFSET);
    if ((raw_cap_ptr & 0x03U) != 0 || raw_cap_ptr < 0x40 || raw_cap_ptr > 0xFC) {
        return 0;
    }

    uint8_t cap_ptr = raw_cap_ptr;
    uint64_t visited_mask = 0;
    uint32_t hops = 0;

    while (cap_ptr >= 0x40 && cap_ptr <= 0xFC && (cap_ptr & 0x03U) == 0 && hops < 48) {
        const uint32_t bit_idx = cap_ptr >> 2;
        if ((visited_mask & (UINT64_C(1) << bit_idx)) != 0) {
            return 0; // Wykryto cykl / petle
        }
        visited_mask |= (UINT64_C(1) << bit_idx);
        hops++;

        const uint8_t id = pci_odczytaj_byte(bus, slot, func, cap_ptr);
        if (id == cap_id) {
            return cap_ptr;
        }

        const uint8_t raw_next = pci_odczytaj_byte(bus, slot, func, static_cast<uint8_t>(cap_ptr + 1));
        if (raw_next == 0) {
            break;
        }
        if ((raw_next & 0x03U) != 0 || raw_next < 0x40 || raw_next > 0xFC) {
            return 0; // Nieprawidlowy wskaznik nastepnej capability
        }
        cap_ptr = raw_next;
    }

    return 0;
}

// Validate the whole conventional capability chain before programming a message
// capability. Reject another header inside its payload and cycles, including
// malformed links after the requested capability.
static bool pci_capability_extent(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off,uint32_t bytes) {
    const uint32_t end=uint32_t(off)+((bytes+3U)&~3U);
    if(off<0x40 || (off&3U) || end>256)return false;
    uint8_t p=pci_odczytaj_byte(bus,slot,func,0x34);
    uint64_t visited=0; bool found=false;
    for(unsigned hops=0;p && hops<48;++hops) {
        if(p<0x40 || (p&3U) || (visited&(UINT64_C(1)<<(p/4))))return false;
        visited|=UINT64_C(1)<<(p/4);
        if(p==off)found=true;
        else if(p>off && uint32_t(p)<end)return false;
        p=pci_odczytaj_byte(bus,slot,func,uint8_t(p+1));
    }
    return !p && found;
}

extern "C" bool pci_find_msi(uint8_t bus, uint8_t slot, uint8_t func, PciMsiCapability* out_msi) {
    if (!out_msi) return false;
    const uint8_t cap_off = pci_find_capability(bus, slot, func, PCI_CAP_ID_MSI);
    if (cap_off == 0) return false;

    const uint16_t msg_ctrl = pci_odczytaj_word(bus, slot, func, static_cast<uint8_t>(cap_off + 2));
    const bool is_64bit = (msg_ctrl & PCI_MSI_CTRL_64BIT) != 0;
    const bool has_pvm = (msg_ctrl & PCI_MSI_CTRL_PVM) != 0;

    uint32_t required_size = 10;
    if (is_64bit && !has_pvm) required_size = 14;
    else if (!is_64bit && has_pvm) required_size = 20;
    else if (is_64bit && has_pvm) required_size = 24;

    if (!pci_capability_extent(bus,slot,func,cap_off,required_size)) {
        return false;
    }

    uint8_t mmc_bits = static_cast<uint8_t>((msg_ctrl & PCI_MSI_CTRL_MMC_MASK) >> PCI_MSI_CTRL_MMC_SHIFT);
    if (mmc_bits > 5) mmc_bits = 5;

    out_msi->cap_offset = cap_off;
    out_msi->msg_control = msg_ctrl;
    out_msi->is_64bit = is_64bit;
    out_msi->has_pvm = has_pvm;
    out_msi->multiple_msg_capable = static_cast<uint8_t>(1U << mmc_bits);
    return true;
}

extern "C" bool pci_find_msix(uint8_t bus, uint8_t slot, uint8_t func, PciMsixCapability* out_msix) {
    if (!out_msix) return false;
    const uint8_t cap_off = pci_find_capability(bus, slot, func, PCI_CAP_ID_MSIX);
    if (cap_off == 0) return false;

    if (!pci_capability_extent(bus,slot,func,cap_off,12)) {
        return false;
    }

    const uint16_t msg_ctrl = pci_odczytaj_word(bus, slot, func, static_cast<uint8_t>(cap_off + 2));
    const uint16_t table_size = static_cast<uint16_t>((msg_ctrl & PCI_MSIX_CTRL_TABLE_SIZE) + 1U);
    if (table_size == 0 || table_size > 2048) {
        return false;
    }

    const uint32_t table_reg = pci_odczytaj_dword(bus, slot, func, static_cast<uint8_t>(cap_off + 4));
    const uint8_t table_bir = static_cast<uint8_t>(table_reg & PCI_MSIX_BIR_MASK);
    const uint32_t table_off = table_reg & PCI_MSIX_OFFSET_MASK;

    const uint32_t pba_reg = pci_odczytaj_dword(bus, slot, func, static_cast<uint8_t>(cap_off + 8));
    const uint8_t pba_bir = static_cast<uint8_t>(pba_reg & PCI_MSIX_BIR_MASK);
    const uint32_t pba_off = pba_reg & PCI_MSIX_OFFSET_MASK;

    if (table_bir >= 6 || pba_bir >= 6) {
        return false;
    }

    out_msix->cap_offset = cap_off;
    out_msix->msg_control = msg_ctrl;
    out_msix->table_size = table_size;
    out_msix->table_bir = table_bir;
    out_msix->table_offset = table_off;
    out_msix->pba_bir = pba_bir;
    out_msix->pba_offset = pba_off;
    return true;
}

extern "C" bool pci_msix_validate_bar_bounds(const PciMsixCapability* msix, uint64_t table_bar_size, uint64_t pba_bar_size) {
    if (!msix || msix->table_size == 0 || msix->table_size > 2048) {
        return false;
    }
    if (msix->table_bir >= 6 || msix->pba_bir >= 6) {
        return false;
    }
    if (table_bar_size == 0) {
        return false;
    }

    const uint64_t entry_size = sizeof(PciMsixTableEntry);
    const uint64_t table_bytes = static_cast<uint64_t>(msix->table_size) * entry_size;
    if (table_bytes / entry_size != msix->table_size) {
        return false;
    }

    const uint64_t table_offset = static_cast<uint64_t>(msix->table_offset);
    if (table_offset > table_bar_size) {
        return false;
    }
    if (table_bar_size - table_offset < table_bytes) {
        return false;
    }

    if (pba_bar_size > 0) {
        const uint64_t pba_bytes = ((static_cast<uint64_t>(msix->table_size) + 63ULL) / 64ULL) * 8ULL;
        const uint64_t pba_offset = static_cast<uint64_t>(msix->pba_offset);
        if (pba_offset > pba_bar_size) {
            return false;
        }
        if (pba_bar_size - pba_offset < pba_bytes) {
            return false;
        }
    }

    return true;
}

extern "C" bool pci_msi_enable(uint8_t bus, uint8_t slot, uint8_t func,
                               const PciMsiCapability* msi, uint8_t apic_id, uint8_t vector) {
    if (!msi || !pci_capability_extent(bus,slot,func,msi->cap_offset,
        msi->is_64bit ? (msi->has_pvm?24:14) : (msi->has_pvm?20:10))) return false;

    const uint32_t cmd_status = pci_odczytaj_dword(bus, slot, func, PCI_OFFSET_COMMAND_STATUS);
    const uint16_t cmd = static_cast<uint16_t>(cmd_status & 0xFFFFU) | PCI_COMMAND_INT_DISABLE;
    pci_zapisz_dword(bus, slot, func, PCI_OFFSET_COMMAND_STATUS, cmd);

    PciMsixCapability msix{};
    if (pci_find_msix(bus, slot, func, &msix)) {
        pci_msix_disable(bus, slot, func, &msix);
    }

    const uint32_t addr_low = 0xFEE00000U | (static_cast<uint32_t>(apic_id) << 12);
    pci_zapisz_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 4), addr_low);

    if (msi->is_64bit) {
        pci_zapisz_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 8), 0x00000000U);
        const uint32_t cur = pci_odczytaj_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 12));
        pci_zapisz_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 12),
                         (cur & 0xFFFF0000U) | static_cast<uint32_t>(vector));

        if (msi->has_pvm) {
            pci_zapisz_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 16), 0x00000000U);
        }
    } else {
        const uint32_t cur = pci_odczytaj_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 8));
        pci_zapisz_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 8),
                         (cur & 0xFFFF0000U) | static_cast<uint32_t>(vector));

        if (msi->has_pvm) {
            pci_zapisz_dword(bus, slot, func, static_cast<uint8_t>(msi->cap_offset + 12), 0x00000000U);
        }
    }

    const uint32_t dword0 = pci_odczytaj_dword(bus, slot, func, msi->cap_offset);
    uint16_t ctrl = static_cast<uint16_t>((dword0 >> 16) & 0xFFFFU);
    ctrl &= ~PCI_MSI_CTRL_MME_MASK;
    ctrl |= PCI_MSI_CTRL_ENABLE;
    pci_zapisz_dword(bus, slot, func, msi->cap_offset, (dword0 & 0x0000FFFFU) | (static_cast<uint32_t>(ctrl) << 16));

    return true;
}

extern "C" void pci_msi_disable(uint8_t bus, uint8_t slot, uint8_t func, const PciMsiCapability* msi) {
    if (!msi || msi->cap_offset == 0) return;

    const uint32_t dword0 = pci_odczytaj_dword(bus, slot, func, msi->cap_offset);
    uint16_t ctrl = static_cast<uint16_t>((dword0 >> 16) & 0xFFFFU);
    ctrl &= ~PCI_MSI_CTRL_ENABLE;
    pci_zapisz_dword(bus, slot, func, msi->cap_offset, (dword0 & 0x0000FFFFU) | (static_cast<uint32_t>(ctrl) << 16));

    if (msi->has_pvm) {
        pci_msi_set_mask(bus, slot, func, msi, true);
    }
}

extern "C" void pci_msi_set_mask(uint8_t bus, uint8_t slot, uint8_t func, const PciMsiCapability* msi, bool mask) {
    if (!msi || msi->cap_offset == 0 || !msi->has_pvm) return;
    const uint8_t mask_off = msi->is_64bit ? static_cast<uint8_t>(msi->cap_offset + 16) : static_cast<uint8_t>(msi->cap_offset + 12);
    pci_zapisz_dword(bus, slot, func, mask_off, mask ? 0xFFFFFFFFU : 0x00000000U);
}

extern "C" bool pci_msix_enable(uint8_t bus, uint8_t slot, uint8_t func, const PciMsixCapability* msix) {
    if (!msix || !pci_capability_extent(bus,slot,func,msix->cap_offset,12)) return false;

    const uint32_t cmd_status = pci_odczytaj_dword(bus, slot, func, PCI_OFFSET_COMMAND_STATUS);
    const uint16_t cmd = static_cast<uint16_t>(cmd_status & 0xFFFFU) | PCI_COMMAND_INT_DISABLE;
    pci_zapisz_dword(bus, slot, func, PCI_OFFSET_COMMAND_STATUS, cmd);

    PciMsiCapability msi{};
    if (pci_find_msi(bus, slot, func, &msi)) {
        pci_msi_disable(bus, slot, func, &msi);
    }

    const uint32_t dword0 = pci_odczytaj_dword(bus, slot, func, msix->cap_offset);
    uint16_t ctrl = static_cast<uint16_t>((dword0 >> 16) & 0xFFFFU);
    ctrl &= ~PCI_MSIX_CTRL_FUNC_MASK;
    ctrl |= PCI_MSIX_CTRL_ENABLE;
    pci_zapisz_dword(bus, slot, func, msix->cap_offset, (dword0 & 0x0000FFFFU) | (static_cast<uint32_t>(ctrl) << 16));

    return true;
}

extern "C" void pci_msix_disable(uint8_t bus, uint8_t slot, uint8_t func, const PciMsixCapability* msix) {
    if (!msix || msix->cap_offset == 0) return;

    const uint32_t dword0 = pci_odczytaj_dword(bus, slot, func, msix->cap_offset);
    uint16_t ctrl = static_cast<uint16_t>((dword0 >> 16) & 0xFFFFU);
    ctrl |= PCI_MSIX_CTRL_FUNC_MASK;
    ctrl &= ~PCI_MSIX_CTRL_ENABLE;
    pci_zapisz_dword(bus, slot, func, msix->cap_offset, (dword0 & 0x0000FFFFU) | (static_cast<uint32_t>(ctrl) << 16));
}

extern "C" bool pci_msix_program_entry(volatile PciMsixTableEntry* table, uint16_t table_size,
                                       uint16_t entry_index, uint8_t apic_id, uint8_t vector, bool masked) {
    if (!table || entry_index >= table_size) return false;

    volatile PciMsixTableEntry* entry = &table[entry_index];

    entry->vector_ctrl = 1U;
    asm volatile("mfence" ::: "memory");

    const uint32_t addr_low = 0xFEE00000U | (static_cast<uint32_t>(apic_id) << 12);
    entry->msg_addr_low = addr_low;
    entry->msg_addr_high = 0x00000000U;
    entry->msg_data = static_cast<uint32_t>(vector);
    asm volatile("mfence" ::: "memory");

    entry->vector_ctrl = masked ? 1U : 0U;
    asm volatile("mfence" ::: "memory");

    return true;
}

extern "C" bool pci_msix_mask_entry(volatile PciMsixTableEntry* table, uint16_t table_size, uint16_t entry_index) {
    if (!table || entry_index >= table_size) return false;
    table[entry_index].vector_ctrl = 1U;
    asm volatile("mfence" ::: "memory");
    return true;
}

extern "C" bool pci_msix_unmask_entry(volatile PciMsixTableEntry* table, uint16_t table_size, uint16_t entry_index) {
    if (!table || entry_index >= table_size) return false;
    table[entry_index].vector_ctrl = 0U;
    asm volatile("mfence" ::: "memory");
    return true;
}

extern "C" bool pci_msix_is_entry_masked(volatile const PciMsixTableEntry* table, uint16_t table_size, uint16_t entry_index) {
    if (!table || entry_index >= table_size) return true;
    return (table[entry_index].vector_ctrl & PCI_MSIX_VECTOR_CTRL_MASK) != 0;
}

