/*
 * Bursztyn OS - HDA diagnostyka: czysty format i parsowanie
 *
 * Naglowek host-testowalny. Nie dotyka MMIO, DMA ani BWS.
 * Zawiera:
 *   - stale ABI migawki diagnostycznej (wersja, limity, bledy),
 *   - dekodowanie Audio Widget Capabilities,
 *   - dekodowanie Pin Default Config,
 *   - parsowanie list polaczen (short/long/range),
 *   - budowe grafu topologii z detekcja cykli,
 *   - klasyfikacje sciezek wyjsciowych,
 *   - formatowanie tekstu raportu.
 *
 * Wszystkie liczby publiczne maja stala szerokosc. Kazda struktura
 * publiczna zawiera pola wersja i rozmiar.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. WERSJA I LIMITY ABI
 * ========================================================================= */

/*
 * Wersja schematu migawki diagnostycznej HDA.
 * Zwiekszana przy niekompatybilnej zmianie layoutu.
 */
#define HDA_DIAG_WERSJA_ABI UINT32_C(2)

/*
 * Znaczniki obciazenia / obciecia migawki.
 */
#define HDA_DIAG_FLAGA_OBECIETA        (UINT32_C(1) << 0)
#define HDA_DIAG_FLAGA_SKAN_W_TOKU     (UINT32_C(1) << 1)
#define HDA_DIAG_FLAGA_SKAN_GOTOWY     (UINT32_C(1) << 2)
#define HDA_DIAG_FLAGA_SKAN_BLAD       (UINT32_C(1) << 3)
#define HDA_DIAG_FLAGA_BRAK_KONTROLERA (UINT32_C(1) << 4)
#define HDA_DIAG_FLAGA_TRANSPORT_IC    (UINT32_C(1) << 5)

/*
 * Maksima reprezentowane w jednej migawce.
 * Przekroczenie ustawia HDA_DIAG_FLAGA_OBECIETA zamiast przelewac pamiec.
 */
#define HDA_DIAG_MAKS_KONTROLEROW          UINT32_C(4)
#define HDA_DIAG_MAKS_KODEKOW              UINT32_C(4)
#define HDA_DIAG_MAKS_FG                   UINT32_C(4)
#define HDA_DIAG_MAKS_WIDGETOW             UINT32_C(96)
#define HDA_DIAG_MAKS_POLACZEN             UINT32_C(32)
#define HDA_DIAG_MAKS_SCIEZEK              UINT32_C(64)
#define HDA_DIAG_MAKS_DLUGOSC_SCIEZKI      UINT32_C(16)
#define HDA_DIAG_MAKS_BLEDOW_STRUMIENIA    UINT32_C(8)
#define HDA_DIAG_MAKS_HISTORII             UINT32_C(8)

/*
 * Twardy limit bajtow tekstu raportu generowanego z migawki.
 */
#define HDA_DIAG_MAKS_RAPORTU              UINT32_C(1048576)

/*
 * Maks. liczba slow/verbów pochodzenia w jednym kroku skanu (bounded work).
 * Jedna aktywna transakcja na kontroler; krok nigdy nie czeka na sprzet.
 */
#define HDA_DIAG_LIMIT_VERBOW_NA_KROK      UINT32_C(1)
#define HDA_DIAG_SLOW_NA_KROK              HDA_DIAG_LIMIT_VERBOW_NA_KROK

/* =========================================================================
 * 2. KODY BLEDOW DIAGNOSTYCZNYCH
 * ========================================================================= */

enum HdaDiagBlad : uint32_t {
    HDA_DIAG_OK = 0,
    HDA_DIAG_BLAD_BRAK_KONTROLERA = 1,
    HDA_DIAG_BLAD_MMIO = 2,
    HDA_DIAG_BLAD_RESET = 3,
    HDA_DIAG_BLAD_BRAK_KODEKA = 4,
    HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA = 5,
    HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW = 6,
    HDA_DIAG_BLAD_LISTA_POLACZEN = 7,
    HDA_DIAG_BLAD_BRAK_AFG = 8,
    HDA_DIAG_BLAD_BRAK_WYJSC = 9,
    HDA_DIAG_BLAD_BRAK_GLOSNIKA = 10,
    HDA_DIAG_BLAD_STRUMIEN = 11,
    HDA_DIAG_BLAD_TIMEOUT_ODTWORZENIA = 12,
    HDA_DIAG_BLAD_NIEOBSLUGIWANY_SPRZET = 13,
    HDA_DIAG_BLAD_PRZEKROCZENIE_LIMITU = 14,
    HDA_DIAG_BLAD_ZLA_ODPOWIEDZ = 15,
    HDA_DIAG_BLAD_TIMEOUT = 16,
    HDA_DIAG_BLAD_WSKAZNIK = 17,
    HDA_DIAG_BLAD_UPOWAZNIENIE = 18,
    HDA_DIAG_BLAD_WERSJA = 19,
    HDA_DIAG_BLAD_W_TRAKCIE = 20,
    HDA_DIAG_BLAD_BRAK_SNAPSHOTU = 21,
    HDA_DIAG_BLAD_BRAK_MIEJSCA = 22,
    HDA_DIAG_BLAD_NIEBEZPIECZNY_ZAKRES = 23
};

/*
 * Etapy skanu widoczne dla aplikacji (granularna maszyna stanow).
 */
enum HdaDiagEtap : uint32_t {
    HDA_DIAG_ETAP_BEZCZYNNY = 0,             /* Bezczynny / Uruchamianie */
    HDA_DIAG_ETAP_KONTROLER = 1,             /* Skanowanie kontrolera HDA */
    HDA_DIAG_ETAP_SONDOWANIE_KODEKOW = 2,     /* Sondowanie kodeków (CAD 0..14) */
    HDA_DIAG_ETAP_PARAMETRY_KORZENIA = 3,     /* Odczytywanie parametrów korzenia kodeka */
    HDA_DIAG_ETAP_GRUPY_FUNKCYJNE = 4,        /* Odczytywanie grup funkcyjnych */
    HDA_DIAG_ETAP_PARAMETRY_AFG = 5,          /* Odczytywanie parametrów AFG */
    HDA_DIAG_ETAP_WIDGET_ODKRYCIE = 6,        /* Skanowanie widgetów (AWCAP) */
    HDA_DIAG_ETAP_WIDGET_PARAMETRY = 7,       /* Odczytywanie parametrów widgetów / pinów */
    HDA_DIAG_ETAP_POLACZENIA = 8,             /* Odczytywanie połączeń */
    HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK = 9,      /* Budowanie topologii */
    HDA_DIAG_ETAP_PUBLIKACJA = 10,            /* Publikacja raportu i migawki */
    HDA_DIAG_ETAP_GOTOWY = 11,                /* Gotowe */
    HDA_DIAG_ETAP_BLAD = 12                   /* Błąd diagnostyki HDA */
};

/* Zgodnosc wsteczna ze starymi stalymi etapow */
constexpr uint32_t HDA_DIAG_ETAP_KODEK = HDA_DIAG_ETAP_PARAMETRY_KORZENIA;
constexpr uint32_t HDA_DIAG_ETAP_FG = HDA_DIAG_ETAP_GRUPY_FUNKCYJNE;
constexpr uint32_t HDA_DIAG_ETAP_WIDGETY = HDA_DIAG_ETAP_WIDGET_ODKRYCIE;
constexpr uint32_t HDA_DIAG_ETAP_SCIEZKI = HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK;

inline const char* hda_diag_etap_nazwa(uint32_t etap) {
    switch (etap) {
        case HDA_DIAG_ETAP_BEZCZYNNY: return "Uruchamianie";
        case HDA_DIAG_ETAP_KONTROLER: return "Skanowanie kontrolera HDA";
        case HDA_DIAG_ETAP_SONDOWANIE_KODEKOW: return "Sondowanie kodeków";
        case HDA_DIAG_ETAP_PARAMETRY_KORZENIA: return "Odczytywanie grup funkcyjnych";
        case HDA_DIAG_ETAP_GRUPY_FUNKCYJNE: return "Odczytywanie grup funkcyjnych";
        case HDA_DIAG_ETAP_PARAMETRY_AFG: return "Odczytywanie grup funkcyjnych";
        case HDA_DIAG_ETAP_WIDGET_ODKRYCIE: return "Skanowanie widgetów";
        case HDA_DIAG_ETAP_WIDGET_PARAMETRY: return "Skanowanie widgetów";
        case HDA_DIAG_ETAP_POLACZENIA: return "Odczytywanie połączeń";
        case HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK: return "Budowanie topologii";
        case HDA_DIAG_ETAP_PUBLIKACJA: return "Budowanie topologii";
        case HDA_DIAG_ETAP_GOTOWY: return "Gotowe";
        case HDA_DIAG_ETAP_BLAD: return "Błąd diagnostyki HDA";
        default: return "Nieznany";
    }
}

/*
 * Stan strumienia odtwarzania.
 */
enum HdaDiagStanStrumienia : uint32_t {
    HDA_STRUMIEN_BEZCZYNNY = 0,
    HDA_STRUMIEN_SKONFIGUROWANY = 1,
    HDA_STRUMIEN_GRA = 2,
    HDA_STRUMIEN_ZATRZYMANY = 3,
    HDA_STRUMIEN_BLAD = 4
};

/*
 * Tryby kontrolowanego testu odtwarzania.
 */
enum HdaDiagTrybTestu : uint32_t {
    HDA_TEST_BRAK = 0,
    HDA_TEST_TON = 1,
    HDA_TEST_SWEEP = 2,
    HDA_TEST_LEWY = 3,
    HDA_TEST_PRAWY = 4,
    HDA_TEST_OBA = 5
};

/*
 * Klasyfikacja pinu na podstawie Default Config.
 */
enum HdaDiagCelPinu : uint32_t {
    HDA_CEL_NIEZNANY = 0,
    HDA_CEL_GLOSNIK = 1,
    HDA_CEL_SLUCHAWKI = 2,
    HDA_CEL_WYJSCIE_LINIOWE = 3,
    HDA_CEL_HDMI = 4,
    HDA_CEL_WEJSCIE_LINIOWE = 5,
    HDA_CEL_MIC_IN = 6,
    HDA_CEL_SPDIF = 7,
    HDA_CEL_NIEPODLACZONY = 8,
    HDA_CEL_INNY = 9
};

/* =========================================================================
 * 3. DEKODOWANIE AUDIO WIDGET CAPABILITIES
 * ========================================================================= */

/*
 * Layout Audio Widget Capabilities (GetParameter 0x05) zgodny z HDA 1.0
 * i praktyka sterownikow (Linux ALSA / realne zrzuty Realtek):
 *
 *   bit 0      Stereo
 *   bit 1      Input Amplifier Present
 *   bit 2      Output Amplifier Present
 *   bit 3      Amplifier Parameter Override
 *   bit 4      Format Override
 *   bit 5      Stripe
 *   bit 6      Processing Widget
 *   bit 7      Unsolicited Response Capable
 *   bit 8      Connection List Present
 *   bit 9      Digital
 *   bits 11:8  Delay (gdy uzywane jako pole Delay)
 *   bits 23:20 Widget Type
 *   bits 31:24 Reserved / vendor
 *
 * Widget Type jest w bitach 23:20. Potwierdzaja to zrzuty /proc/asound
 * (np. wcaps 0x0040018F -> type 4 = Pin Complex).
 */
#define HDA_WCAP_TYPE_MASK       UINT32_C(0x00F00000)
#define HDA_WCAP_TYPE_SHIFT      UINT32_C(20)
#define HDA_WCAP_STEREO          (UINT32_C(1) << 0)
#define HDA_WCAP_IN_AMP          (UINT32_C(1) << 1)
#define HDA_WCAP_OUT_AMP         (UINT32_C(1) << 2)
#define HDA_WCAP_AMP_OVRD        (UINT32_C(1) << 3)
#define HDA_WCAP_FORMAT_OVRD     (UINT32_C(1) << 4)
#define HDA_WCAP_STRIPE          (UINT32_C(1) << 5)
#define HDA_WCAP_PROC_WID        (UINT32_C(1) << 6)
#define HDA_WCAP_UNSOL_CAP       (UINT32_C(1) << 7)
#define HDA_WCAP_CONN_LIST       (UINT32_C(1) << 8)
#define HDA_WCAP_DIGITAL         (UINT32_C(1) << 9)
#define HDA_WCAP_DELAY_MASK      UINT32_C(0x000F0000)
#define HDA_WCAP_DELAY_SHIFT     UINT32_C(16)

enum HdaTypWidgetu : uint8_t {
    HDA_WID_OUT = 0x0,
    HDA_WID_IN = 0x1,
    HDA_WID_MIX = 0x2,
    HDA_WID_SEL = 0x3,
    HDA_WID_PIN = 0x4,
    HDA_WID_POWER = 0x5,
    HDA_WID_VOLKNOB = 0x6,
    HDA_WID_BEEP = 0x7,
    HDA_WID_VENDOR = 0xF
};

inline uint8_t hda_wcap_typ(uint32_t wcap) {
    return static_cast<uint8_t>(
        (wcap & HDA_WCAP_TYPE_MASK) >> HDA_WCAP_TYPE_SHIFT);
}

inline uint8_t hda_wcap_delay(uint32_t wcap) {
    return static_cast<uint8_t>(
        (wcap & HDA_WCAP_DELAY_MASK) >> HDA_WCAP_DELAY_SHIFT);
}

/*
 * Czy typ widgetu moze miec liste polaczen jako zrodlo w sciezce audio.
 */
inline bool hda_typ_widgetu_jest_audio(uint8_t typ) {
    return typ == HDA_WID_OUT || typ == HDA_WID_IN ||
           typ == HDA_WID_MIX || typ == HDA_WID_SEL;
}

inline bool hda_typ_widgetu_jest_wyjsciem(uint8_t typ) {
    return typ == HDA_WID_OUT;
}

/*
 * Czy widget moze byc posrednikiem sciezki wyjsciowej.
 */
inline bool hda_typ_widgetu_posrednik(uint8_t typ) {
    return typ == HDA_WID_MIX || typ == HDA_WID_SEL;
}

/*
 * Czy widget to Pin Complex.
 */
inline bool hda_typ_widgetu_pin(uint8_t typ) {
    return typ == HDA_WID_PIN;
}

/*
 * Czy na podstawie typu wolno bezpiecznie odczytac Pin Caps / Default Config.
 * Tylko Pin Complex. QEMU toleruje nadmiarowe verby, prawdziwy sprzet nie.
 */
inline bool hda_typ_wolno_pin_caps(uint8_t typ) {
    return typ == HDA_WID_PIN;
}

/*
 * Czy na podstawie typu wolno bezpiecznie odczytac PCM size/rate.
 * Tylko konwertery (Audio Output / Audio Input).
 */
inline bool hda_typ_wolno_pcm_caps(uint8_t typ) {
    return typ == HDA_WID_OUT || typ == HDA_WID_IN;
}

/*
 * Czy wolno odczytac Connection List Length.
 * Widgety bez Connection List w AWCAP nie maja listy.
 */
inline bool hda_typ_wolno_conn_list(uint32_t wcap) {
    return (wcap & HDA_WCAP_CONN_LIST) != 0;
}

/*
 * Czy wolno odczytac amplitudy (In/Out Amp Capabilities).
 */
inline bool hda_typ_wolno_amp_in(uint32_t wcap) {
    return (wcap & HDA_WCAP_IN_AMP) != 0;
}

inline bool hda_typ_wolno_amp_out(uint32_t wcap) {
    return (wcap & HDA_WCAP_OUT_AMP) != 0;
}

/*
 * Czy wolno odczytac biezacy stan zasilania.
 * Spec: Get Power State dla kazdego widgetu jest bezpieczny (read-only).
 */
inline bool hda_typ_wolno_power_state(uint8_t typ) {
    (void)typ;
    return true;
}

/*
 * Nazwa typu widgetu (UTF-8, stale ASCII + polskie w aplikacji).
 */
inline const char* hda_nazwa_typu_widgetu(uint8_t typ) {
    switch (typ) {
        case HDA_WID_OUT: return "Audio Output";
        case HDA_WID_IN: return "Audio Input";
        case HDA_WID_MIX: return "Audio Mixer";
        case HDA_WID_SEL: return "Audio Selector";
        case HDA_WID_PIN: return "Pin Complex";
        case HDA_WID_POWER: return "Power Widget";
        case HDA_WID_VOLKNOB: return "Volume Knob";
        case HDA_WID_BEEP: return "Beep Generator";
        case HDA_WID_VENDOR: return "Vendor Defined";
        default: return "Reserved";
    }
}

/* =========================================================================
 * 4. DEKODOWANIE PIN DEFAULT CONFIG
 * ========================================================================= */

/*
 * Pin Default Config (Get Configuration Default, verb 0xF1C).
 *
 *   bits 31:30 Port Connectivity
 *   bits 29:24 Location
 *   bits 23:20 Default Device
 *   bits 19:16 Connection Type
 *   bits 15:12 Color
 *   bits 11:8  Misc
 *   bits 7:4   Default Association
 *   bits 3:0   Sequence
 */
#define HDA_PINCFG_PORT_MASK     UINT32_C(0xC0000000)
#define HDA_PINCFG_PORT_SHIFT    UINT32_C(30)
#define HDA_PINCFG_LOC_MASK      UINT32_C(0x3F000000)
#define HDA_PINCFG_LOC_SHIFT     UINT32_C(24)
#define HDA_PINCFG_DEV_MASK      UINT32_C(0x00F00000)
#define HDA_PINCFG_DEV_SHIFT     UINT32_C(20)
#define HDA_PINCFG_CON_MASK      UINT32_C(0x000F0000)
#define HDA_PINCFG_CON_SHIFT     UINT32_C(16)
#define HDA_PINCFG_COLOR_MASK    UINT32_C(0x0000F000)
#define HDA_PINCFG_COLOR_SHIFT   UINT32_C(12)
#define HDA_PINCFG_MISC_MASK     UINT32_C(0x00000F00)
#define HDA_PINCFG_MISC_SHIFT    UINT32_C(8)
#define HDA_PINCFG_ASSOC_MASK    UINT32_C(0x000000F0)
#define HDA_PINCFG_ASSOC_SHIFT   UINT32_C(4)
#define HDA_PINCFG_SEQ_MASK      UINT32_C(0x0000000F)

inline uint8_t hda_pincfg_port(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_PORT_MASK) >> HDA_PINCFG_PORT_SHIFT);
}
inline uint8_t hda_pincfg_lokacja(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_LOC_MASK) >> HDA_PINCFG_LOC_SHIFT);
}
inline uint8_t hda_pincfg_dev(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_DEV_MASK) >> HDA_PINCFG_DEV_SHIFT);
}
inline uint8_t hda_pincfg_con(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_CON_MASK) >> HDA_PINCFG_CON_SHIFT);
}
inline uint8_t hda_pincfg_kolor(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_COLOR_MASK) >> HDA_PINCFG_COLOR_SHIFT);
}
inline uint8_t hda_pincfg_misc(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_MISC_MASK) >> HDA_PINCFG_MISC_SHIFT);
}
inline uint8_t hda_pincfg_asocjacja(uint32_t c) {
    return static_cast<uint8_t>((c & HDA_PINCFG_ASSOC_MASK) >> HDA_PINCFG_ASSOC_SHIFT);
}
inline uint8_t hda_pincfg_sekwencja(uint32_t c) {
    return static_cast<uint8_t>(c & HDA_PINCFG_SEQ_MASK);
}

inline const char* hda_nazwa_portu(uint8_t port) {
    switch (port) {
        case 0: return "Jack";
        case 1: return "No Physical Connection";
        case 2: return "Fixed Function";
        case 3: return "Jack and Internal";
        default: return "Unknown";
    }
}

inline const char* hda_nazwa_lokacji(uint8_t loc) {
    /*
     * bits 5:4 pola Location = Gross Location:
     *   00 External, 01 Separate chassis, 10 Other chassis, 11 Internal.
     */
    const uint8_t gross = static_cast<uint8_t>((loc >> 4) & 0x3U);
    switch (gross) {
        case 0: return "External";
        case 1: return "Internal";
        case 2: return "Separate chassis";
        case 3: return "Other chassis";
        default: return "Unknown";
    }
}

inline bool hda_lokacja_wewnetrzna(uint8_t loc) {
    return ((loc >> 4) & 0x3U) == 0x1U;
}

inline const char* hda_nazwa_domyslnego_urzadzenia(uint8_t dev) {
    switch (dev) {
        case 0x0: return "Line Out";
        case 0x1: return "Speaker";
        case 0x2: return "Headphone Out";
        case 0x3: return "CD";
        case 0x4: return "SPDIF Out";
        case 0x5: return "Digital Other Out";
        case 0x6: return "Modem Line Side";
        case 0x7: return "Modem Handset Side";
        case 0x8: return "Line In";
        case 0x9: return "AUX";
        case 0xA: return "Mic In";
        case 0xB: return "Telephony";
        case 0xC: return "SPDIF In";
        case 0xD: return "Digital Other In";
        case 0xE: return "Reserved";
        case 0xF: return "Other";
        default: return "Unknown";
    }
}

inline const char* hda_nazwa_typu_polaczenia(uint8_t con) {
    switch (con) {
        case 0x0: return "Unknown";
        case 0x1: return "1/8\" jack";
        case 0x2: return "1/4\" jack";
        case 0x3: return "ATAPI internal";
        case 0x4: return "RCA";
        case 0x5: return "Optical";
        case 0x6: return "Other Digital";
        case 0x7: return "Other Analog";
        case 0x8: return "Multichannel DIN";
        case 0x9: return "XLR/Professional";
        case 0xA: return "11/3\" jack";
        case 0xB: return "Combination";
        default: return "Other";
    }
}

inline const char* hda_nazwa_koloru(uint8_t color) {
    switch (color) {
        case 0x0: return "Unknown";
        case 0x1: return "Black";
        case 0x2: return "Grey";
        case 0x3: return "Blue";
        case 0x4: return "Green";
        case 0x5: return "Red";
        case 0x6: return "Orange";
        case 0x7: return "Yellow";
        case 0x8: return "Purple";
        case 0x9: return "Pink";
        case 0xA: return "White";
        case 0xB: return "Other";
        default: return "Other";
    }
}

inline const char* hda_nazwa_celu_pinu(uint32_t cel) {
    switch (cel) {
        case HDA_CEL_GLOSNIK: return "Speaker";
        case HDA_CEL_SLUCHAWKI: return "Headphone Out";
        case HDA_CEL_WYJSCIE_LINIOWE: return "Line Out";
        case HDA_CEL_HDMI: return "HDMI";
        case HDA_CEL_WEJSCIE_LINIOWE: return "Line In";
        case HDA_CEL_MIC_IN: return "Mic In";
        case HDA_CEL_SPDIF: return "SPDIF Out";
        case HDA_CEL_NIEPODLACZONY: return "Not Connected";
        case HDA_CEL_INNY: return "Other";
        default: return "Unknown";
    }
}

/*
 * Klasyfikacja pinu. Nie zgadujemy po numerze NID.
 * Uzywamy wylacznie Default Config (port + device + location).
 */
inline uint32_t hda_klasyfikuj_pin(uint32_t default_config) {
    const uint8_t port = hda_pincfg_port(default_config);
    const uint8_t dev = hda_pincfg_dev(default_config);
    const uint8_t loc = hda_pincfg_lokacja(default_config);

    /* Port 1 = No Physical Connection. */
    if (port == 1) {
        return HDA_CEL_NIEPODLACZONY;
    }

    switch (dev) {
        case 0x1: return HDA_CEL_GLOSNIK;
        case 0x2: return HDA_CEL_SLUCHAWKI;
        case 0x0: return HDA_CEL_WYJSCIE_LINIOWE;
        case 0x4: return HDA_CEL_SPDIF;
        case 0x5: return HDA_CEL_HDMI;
        case 0x8: return HDA_CEL_WEJSCIE_LINIOWE;
        case 0x9: return HDA_CEL_WEJSCIE_LINIOWE;
        case 0xA: return HDA_CEL_MIC_IN;
        case 0xC: return HDA_CEL_SPDIF;
        case 0xD: return HDA_CEL_HDMI;
        default: break;
    }

    /*
     * HDMI / DisplayPort bywa tez sygnalizowane Connection Type
     * Optical / Other Digital przy Device Digital Other Out.
     */
    const uint8_t con = hda_pincfg_con(default_config);
    if (dev == 0x5 || dev == 0xD) {
        return HDA_CEL_HDMI;
    }
    if (con == 0x5 || con == 0x6) {
        return HDA_CEL_HDMI;
    }

    (void)loc;
    return HDA_CEL_INNY;
}

inline bool hda_cel_jest_wyjsciem(uint32_t cel) {
    return cel == HDA_CEL_GLOSNIK || cel == HDA_CEL_SLUCHAWKI ||
           cel == HDA_CEL_WYJSCIE_LINIOWE || cel == HDA_CEL_HDMI ||
           cel == HDA_CEL_SPDIF;
}

inline bool hda_cel_jest_glosnikiem(uint32_t cel) {
    return cel == HDA_CEL_GLOSNIK;
}

inline bool hda_cel_jest_sluchawkami(uint32_t cel) {
    return cel == HDA_CEL_SLUCHAWKI;
}

inline bool hda_cel_jest_hdmi(uint32_t cel) {
    return cel == HDA_CEL_HDMI;
}

inline bool hda_cel_jest_niepodlaczony(uint32_t cel) {
    return cel == HDA_CEL_NIEPODLACZONY;
}

/* =========================================================================
 * 5. LISTA POLACZEN
 * ========================================================================= */

/*
 * Dlugosc listy (GetParameter 0x0A):
 *   bits 7:0  = liczba wpisow
 *   bit 8     = Long Form (1 = 16-bitowe wpisy)
 *
 * Short form (1 bajt): bits 6:0 = NID.
 * Long form (2 bajty):
 *   bit 15     = Range
 *   bits 14:8  = Range end NID (gdy Range=1)
 *   bits 7:0   = NID (poczatek zakresu)
 */
#define HDA_CONNLIST_LEN_MASK    UINT32_C(0x7F)
#define HDA_CONNLIST_LONG        (UINT32_C(1) << 7)

inline uint8_t hda_connlist_liczba(uint32_t param) {
    return static_cast<uint8_t>(param & HDA_CONNLIST_LEN_MASK);
}

inline bool hda_connlist_long_form(uint32_t param) {
    return (param & HDA_CONNLIST_LONG) != 0;
}

/*
 * Pojedynczy zdekodowany wpis listy polaczen.
 */
struct HdaWpisPolaczenia {
    uint8_t nid_start;
    uint8_t nid_koniec; /* == nid_start gdy brak range */
    bool zakres;        /* wpis range z long form */
    bool poprawny;
};

/*
 * Dekoduje surowy wpis short (8-bit) lub long (16-bit).
 * Odrzuca NID spoza 0x00..0x7F oraz puste zakresy.
 */
// HDA 1.0a 7.3.3.1: range marks the END NID; start is the preceding entry.
inline HdaWpisPolaczenia hda_dekoduj_wpis_polaczenia(uint32_t raw, bool long_form) {
    HdaWpisPolaczenia w{};
    const uint32_t range = long_form ? 0x8000U : 0x80U;
    const uint32_t nid = raw & (range - 1U);
    w.poprawny = nid > 0 && nid <= 127 && raw <= (long_form ? 65535U : 255U);
    w.zakres = (raw & range) != 0;
    w.nid_start = w.nid_koniec = static_cast<uint8_t>(nid);
    return w;
}

inline bool hda_rozszerz_liste_polaczen(const uint32_t* words, uint32_t word_count,
    bool long_form, uint8_t entries, uint8_t* out, uint32_t capacity,
    uint32_t* count, bool* truncated) {
    if (!count) return false;
    *count = 0;
    if (truncated) *truncated = false;
    if (!words || !out || !capacity || entries > 127) return false;
    const uint32_t per_word = long_form ? 2U : 4U;
    if (word_count < (entries + per_word - 1) / per_word) return false;
    uint32_t stored = 0, previous = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        const uint32_t raw = (words[i / per_word] >> ((i % per_word) * (long_form ? 16 : 8))) &
            (long_form ? 65535U : 255U);
        const auto e = hda_dekoduj_wpis_polaczenia(raw, long_form);
        if (!e.poprawny || (e.zakres && (!previous || e.nid_koniec <= previous))) return false;
        const uint32_t start = e.zakres ? previous + 1 : e.nid_start;
        for (uint32_t nid = start; nid <= e.nid_koniec; ++nid) {
            if (stored < capacity) out[stored++] = static_cast<uint8_t>(nid);
            else if (truncated) *truncated = true;
        }
        previous = e.nid_koniec;
    }
    *count = stored;
    return true;
}

/* =========================================================================
 * 6. PARAMETRY GETPARAMETER (dekodowanie bitowe)
 * ========================================================================= */

/* Param ID z HDA 1.0 Table 72 i Table 140. */
#define HDA_PAR_VENDOR_ID        UINT8_C(0x00)
#define HDA_PAR_SUBSYSTEM_ID     UINT8_C(0x01)
#define HDA_PAR_REV_ID           UINT8_C(0x02)
#define HDA_PAR_SUB_NODE_COUNT   UINT8_C(0x04)
#define HDA_PAR_FGROUP_TYPE      UINT8_C(0x05)
#define HDA_PAR_AFG_CAPS         UINT8_C(0x08)
#define HDA_PAR_WIDGET_CAP       UINT8_C(0x09)
#define HDA_PAR_PCM_SIZE_RATE    UINT8_C(0x0A)
#define HDA_PAR_STREAM_FORMATS   UINT8_C(0x0B)
#define HDA_PAR_PIN_CAP          UINT8_C(0x0C)
#define HDA_PAR_AMP_IN_CAP       UINT8_C(0x0D)
#define HDA_PAR_CONNLIST_LEN     UINT8_C(0x0E)
#define HDA_PAR_POWER_STATES     UINT8_C(0x0F)
#define HDA_PAR_PROCESSING_CAP   UINT8_C(0x10)
#define HDA_PAR_GPIO_COUNT       UINT8_C(0x11)
#define HDA_PAR_AMP_OUT_CAP      UINT8_C(0x12)
#define HDA_PAR_VOLKNOB_CAP      UINT8_C(0x13)

/* Vendor ID: bits 31:16 vendor, 15:0 device. */
inline uint16_t hda_vendor_id(uint32_t p) {
    return static_cast<uint16_t>(p >> 16);
}
inline uint16_t hda_device_id(uint32_t p) {
    return static_cast<uint16_t>(p & 0xFFFFU);
}

/* Subordinate Node Count: bits 23:16 start NID, 7:0 count. */
inline uint8_t hda_sub_start(uint32_t p) {
    return static_cast<uint8_t>((p >> 16) & 0xFFU);
}
inline uint8_t hda_sub_count(uint32_t p) {
    return static_cast<uint8_t>(p & 0xFFU);
}

/*
 * Walidacja zakresu wezlow podporzadkowanych.
 * Odrzuca start > 0x7F, count == 0 z start != 0, overflow zakresu.
 */
inline bool hda_zakres_wezlow_poprawny(uint8_t start, uint8_t count) {
    if (count == 0) return false;
    if (start > 0x7FU) return false;
    const uint32_t koniec = static_cast<uint32_t>(start) + count;
    if (koniec > 0x80U) return false;
    return true;
}

/* Function Group Type: bits 7:0 typ (1=Audio, 2=Modem, 0/3=Vendor). */
inline uint8_t hda_fg_typ(uint32_t p) {
    return static_cast<uint8_t>(p & 0xFFU);
}
inline bool hda_fg_jest_audio(uint32_t p) {
    return hda_fg_typ(p) == 1;
}

/* =========================================================================
 * 6B. BUDOWA I WALIDACJA KOMEND ORAZ TRANSPORT HDA
 * ========================================================================= */

enum HdaTransportStatus : uint32_t {
    HDA_TRANSPORT_SUCCESS = 0,
    HDA_TRANSPORT_NOT_READY = 1,
    HDA_TRANSPORT_BUSY_STUCK = 2,
    HDA_TRANSPORT_TIMEOUT = 3,
    HDA_TRANSPORT_NO_VALID_RESPONSE = 4,
    HDA_TRANSPORT_INVALID_ARGUMENT = 5,
    HDA_TRANSPORT_PENDING = 6,
    HDA_TRANSPORT_BUSY = 7,
    HDA_TRANSPORT_CORB_FULL = 8,
    HDA_TRANSPORT_RIRB_NO_RESPONSE = 9,
    HDA_TRANSPORT_CODEC_NO_RESPONSE = 10,
    HDA_TRANSPORT_MALFORMED = 11,
    HDA_TRANSPORT_RESET_REQUIRED = 12,
    HDA_TRANSPORT_CANCELLED = 13,

};

inline bool hda_waliduj_cad(uint8_t cad) {
    return cad <= 14U;
}

inline bool hda_waliduj_nid(uint8_t nid) {
    return nid <= 127U;
}

inline bool hda_zbuduj_verb12(
    uint8_t cad,
    uint8_t nid,
    uint16_t verb,
    uint8_t payload,
    uint32_t* out_cmd
) {
    if (!out_cmd || !hda_waliduj_cad(cad) || !hda_waliduj_nid(nid) || (verb > 0x0FFFU)) {
        if (out_cmd) *out_cmd = 0;
        return false;
    }
    *out_cmd = (static_cast<uint32_t>(cad & 0x0FU) << 28) |
               (static_cast<uint32_t>(nid & 0x7FU) << 20) |
               (static_cast<uint32_t>(verb & 0x0FFFU) << 8) |
               static_cast<uint32_t>(payload);
    return true;
}

inline bool hda_zbuduj_verb4(
    uint8_t cad,
    uint8_t nid,
    uint8_t verb,
    uint16_t payload,
    uint32_t* out_cmd
) {
    if (!out_cmd || !hda_waliduj_cad(cad) || !hda_waliduj_nid(nid) || (verb > 0x0FU)) {
        if (out_cmd) *out_cmd = 0;
        return false;
    }
    *out_cmd = (static_cast<uint32_t>(cad & 0x0FU) << 28) |
               (static_cast<uint32_t>(nid & 0x7FU) << 20) |
               (static_cast<uint32_t>(verb & 0x0FU) << 16) |
               static_cast<uint32_t>(payload);
    return true;
}

inline bool hda_zbuduj_get_param(
    uint8_t cad,
    uint8_t nid,
    uint8_t param_id,
    uint32_t* out_cmd
) {
    return hda_zbuduj_verb12(cad, nid, 0x0F00U, param_id, out_cmd);
}

/* Amp Capabilities: ALSA-compatible layout. */
#define HDA_AMP_MUTE             (UINT32_C(1) << 31)
#define HDA_AMP_OFFSET_MASK      UINT32_C(0x7F)
#define HDA_AMP_NUM_STEPS_MASK   UINT32_C(0x7F00)
#define HDA_AMP_NUM_STEPS_SHIFT  UINT32_C(8)
#define HDA_AMP_STEP_SIZE_MASK   UINT32_C(0x7F0000)
#define HDA_AMP_STEP_SIZE_SHIFT  UINT32_C(16)

inline bool hda_amp_mute(uint32_t caps) {
    return (caps & HDA_AMP_MUTE) != 0;
}
inline uint8_t hda_amp_offset(uint32_t caps) {
    return static_cast<uint8_t>(caps & HDA_AMP_OFFSET_MASK);
}
inline uint8_t hda_amp_kroki(uint32_t caps) {
    return static_cast<uint8_t>(
        (caps & HDA_AMP_NUM_STEPS_MASK) >> HDA_AMP_NUM_STEPS_SHIFT);
}
inline uint8_t hda_amp_rozmiar_kroku_25(uint32_t caps) {
    /* Encoded step size; physical step is (value + 1) * 0.25 dB. */
    return static_cast<uint8_t>(
        (caps & HDA_AMP_STEP_SIZE_MASK) >> HDA_AMP_STEP_SIZE_SHIFT);
}

/* Pin Capabilities (wybrane bity). */
#define HDA_PINCAP_EAPD          (UINT32_C(1) << 16)
#define HDA_PINCAP_VREF_HIZ      (UINT32_C(1) << 8)
#define HDA_PINCAP_VREF_50       (UINT32_C(1) << 9)
#define HDA_PINCAP_VREF_GND      (UINT32_C(1) << 10)
#define HDA_PINCAP_VREF_80       (UINT32_C(1) << 12)
#define HDA_PINCAP_VREF_100      (UINT32_C(1) << 13)
#define HDA_PINCAP_BALANCED      (UINT32_C(1) << 6)
#define HDA_PINCAP_INPUT         (UINT32_C(1) << 5)
#define HDA_PINCAP_OUTPUT        (UINT32_C(1) << 4)
#define HDA_PINCAP_HP_DRV        (UINT32_C(1) << 3)
#define HDA_PINCAP_PRESDET       (UINT32_C(1) << 2)
#define HDA_PINCAP_TRIGGER       (UINT32_C(1) << 1)
#define HDA_PINCAP_DP            (UINT32_C(1) << 24)
#define HDA_PINCAP_HDMI          (UINT32_C(1) << 7)

/* PCM Size / Rate. */
#define HDA_PCM_8BIT             (UINT32_C(1) << 16)
#define HDA_PCM_16BIT            (UINT32_C(1) << 17)
#define HDA_PCM_20BIT            (UINT32_C(1) << 18)
#define HDA_PCM_24BIT            (UINT32_C(1) << 19)
#define HDA_PCM_32BIT            (UINT32_C(1) << 20)
#define HDA_RATE_8K              (UINT32_C(1) << 0)
#define HDA_RATE_11K             (UINT32_C(1) << 1)
#define HDA_RATE_16K             (UINT32_C(1) << 2)
#define HDA_RATE_22K             (UINT32_C(1) << 3)
#define HDA_RATE_32K             (UINT32_C(1) << 4)
#define HDA_RATE_44K             (UINT32_C(1) << 5)
#define HDA_RATE_48K             (UINT32_C(1) << 6)
#define HDA_RATE_88K             (UINT32_C(1) << 7)
#define HDA_RATE_96K             (UINT32_C(1) << 8)
#define HDA_RATE_176K            (UINT32_C(1) << 9)
#define HDA_RATE_192K            (UINT32_C(1) << 10)


/*
 * Stan skanu diagnostycznego (widoczny dla Ring 3 przez BWS).
 * Zawiera jawna wersje, rozmiar, generacje i granularny stan postepu.
 */
struct HdaDiagSkan {
    uint32_t wersja;                    /* HDA_DIAG_WERSJA_ABI */
    uint32_t rozmiar;                   /* sizeof(HdaDiagSkan) */
    uint32_t generacja_skanu;           /* numer generacji biezacego skanu */
    uint32_t etap;                      /* stan skanu (HdaDiagEtap) */
    uint32_t blad;                      /* ostatni kod bledu (HdaDiagBlad) */
    uint32_t postep_biezacy;            /* postep biezacy (kroki/indeks) */
    uint32_t postep_maksymalny;         /* postep calkowity (szacowany) */
    uint32_t postep_0_1000;             /* postep promilowy 0..1000 */
    uint32_t kontroler_idx;             /* indeks kontrolera (0) */
    uint32_t biezacy_cad;               /* biezacy badany CAD kodeka (0..14) */
    uint32_t biezacy_nid;               /* biezacy badany NID wezla (0..255) */
    uint32_t kodek_idx;                 /* indeks w tablicy kodekow */
    uint32_t fg_idx;                    /* indeks Function Group */
    uint32_t widget_idx;                /* indeks widgetu */
    uint32_t slow_w_kroku;              /* slow/verbow wykonanych w kroku */
    uint32_t generacja_opublikowana;    /* numer ostatnio ukonczonej migawki */
    uint8_t aktywny;                    /* 1 gdy skan w toku */
    uint8_t gotowy;                     /* 1 gdy skan ukonczony sukcesem */
    uint8_t migawka_dostepna;           /* 1 gdy opublikowano pelna migawke */
    uint8_t przerwany;                  /* 1 gdy przerwany / anulowany */
    uint8_t rezerwa[4];                 /* wyrownanie do 8 bajtow */
};

static_assert(sizeof(HdaDiagSkan) == 72, "HdaDiagSkan layout");
static_assert(__builtin_offsetof(HdaDiagSkan, wersja) == 0, "HdaDiagSkan wersja offset");
static_assert(__builtin_offsetof(HdaDiagSkan, generacja_skanu) == 8, "HdaDiagSkan generacja_skanu offset");
static_assert(__builtin_offsetof(HdaDiagSkan, etap) == 12, "HdaDiagSkan etap offset");
static_assert(__builtin_offsetof(HdaDiagSkan, blad) == 16, "HdaDiagSkan blad offset");
static_assert(__builtin_offsetof(HdaDiagSkan, postep_0_1000) == 28, "HdaDiagSkan postep_0_1000 offset");
static_assert(__builtin_offsetof(HdaDiagSkan, biezacy_cad) == 36, "HdaDiagSkan biezacy_cad offset");
static_assert(__builtin_offsetof(HdaDiagSkan, biezacy_nid) == 40, "HdaDiagSkan biezacy_nid offset");
static_assert(__builtin_offsetof(HdaDiagSkan, generacja_opublikowana) == 60, "HdaDiagSkan generacja_opublikowana offset");
static_assert(__builtin_offsetof(HdaDiagSkan, aktywny) == 64, "HdaDiagSkan aktywny offset");
static_assert(__builtin_offsetof(HdaDiagSkan, gotowy) == 65, "HdaDiagSkan gotowy offset");
static_assert(__builtin_offsetof(HdaDiagSkan, migawka_dostepna) == 66, "HdaDiagSkan migawka_dostepna offset");


/* =========================================================================
 * 7. STRUKTURY MIGAWKI (publiczne ABI, wersja+rozmiar)
 * ========================================================================= */

/*
 * Naglowek wspolny kazdej struktury publicznej.
 */
struct HdaDiagNaglowek {
    uint32_t wersja;
    uint32_t rozmiar;
};

/*
 * Sanitized BDL info: bez uzytkowalnego adresu fizycznego.
 * Ujawniamy tylko maskowane bity / tekst kernela.
 */
struct HdaDiagBdlInfo {
    uint32_t wersja;
    uint32_t rozmiar;
    uint32_t liczba_wpisow;
    uint32_t maska_fizycznego; /* dolne 12 bitow = 0, reszta zamaskowana */
    uint8_t flagi;             /* bit0: 64-bit DMA uzywany */
    uint8_t rezerwa[3];
};

struct HdaDiagKontroler {
    uint32_t wersja;
    uint32_t rozmiar;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t rezerwa0;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t revision;
    uint16_t command;
    uint64_t bar0;            /* surowy BAR (diagnostyka, nie mapowanie) */
    uint32_t bar0_dlugosc;    /* zwalidowana dlugosc MMIO gdy znana */
    uint32_t rezerwa1;
    uint8_t hda_vmin;
    uint8_t hda_vmaj;
    uint16_t gcap;
    uint32_t gctl;
    uint16_t statests;
    uint16_t outpay;
    uint16_t wakeen;
    uint16_t rezerwa2;
    uint32_t intsts;
    uint32_t flagi;           /* HDA_DIAG_FLAGA_* */
    uint32_t blad;            /* HdaDiagBlad */
    uint8_t oss;              /* output streams */
    uint8_t iss;              /* input streams */
    uint8_t bss;              /* bidirectional streams */
    uint8_t dma64;            /* GCAP 64OK */
    uint8_t liczba_kodekow;
    uint8_t transport;        /* 1 = Immediate Command */
    uint8_t rezerwa3;
    uint16_t initial_statests;
    uint16_t discovered_codec_mask;
    uint16_t responsive_codec_mask;
    uint8_t bar_typ;          /* 32 or 64 */
    uint8_t mmio_zmapowane;
    uint8_t corb_caps;
    uint8_t rirb_caps;
    uint16_t corb_rozmiar;
    uint16_t rirb_rozmiar;
    uint8_t corb_run;
    uint8_t rirb_run;
    uint8_t gotowy;
    uint8_t test_dostepny;
    uint32_t inicjalizacja;
    uint32_t etap_bledu;
    uint32_t stan_transakcji;
    uint32_t status_transportu;
    uint32_t corb_stan;
    uint32_t rirb_stan;
    uint32_t odpowiedzi_niezamowione;
    uint32_t odpowiedzi_obce;
    uint32_t odzyskiwania;
    uint64_t maks_krok_ns;
    uint32_t generacja;
    uint32_t subsystem_id;
};

struct HdaDiagKodek {
    uint32_t wersja;
    uint32_t rozmiar;
    uint8_t cad;              /* codec address 0..14 */
    uint8_t odpowiada;
    uint8_t liczba_fg;
    uint8_t flagi;            /* obciety itd. */
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t revision_id;
    uint32_t root_sub_count;  /* surowy GetParameter 0x02 root */
    uint32_t blad;
    uint32_t rezerwa;
};

struct HdaDiagFG {
    uint32_t wersja;
    uint32_t rozmiar;
    uint8_t nid;
    uint8_t typ;              /* 1=Audio, 2=Modem, ... */
    uint8_t audio;
    uint8_t flagi;
    uint32_t fg_caps;         /* GetParameter 0x04 */
    uint32_t sub_start;
    uint32_t sub_count;
    uint32_t widget_start;
    uint32_t widget_count;
    uint32_t blad;
    uint32_t pcm_size_rate;
    uint32_t stream_formats;
    uint32_t in_amp_caps;
    uint32_t out_amp_caps;
    uint32_t power_states;
};

struct HdaDiagWidget {
    uint32_t wersja;
    uint32_t rozmiar;
    uint8_t nid;
    uint8_t typ;
    uint8_t flagi;            /* bit0: obciety, bit1: blad odczytu */
    uint8_t liczba_polaczen;
    uint32_t awcap;           /* surowy Audio Widget Capabilities */
    uint32_t pcm_size_rate;   /* surowy, gdy istotny */
    uint32_t stream_formats;  /* surowy, gdy istotny */
    uint32_t pin_caps;        /* surowy, gdy Pin */
    uint32_t default_config;  /* surowy 32-bit, gdy Pin */
    uint32_t out_amp_caps;
    uint32_t in_amp_caps;
    uint32_t power_states;
    uint8_t power_current;
    uint8_t conn_long_form;
    uint16_t rezerwa0;
    uint32_t connlist_len_raw;
    uint8_t polaczenia[HDA_DIAG_MAKS_POLACZEN];
    uint8_t polaczenia_surowe[HDA_DIAG_MAKS_POLACZEN];
    uint32_t blad;
    uint32_t conn_select;
    uint32_t eapd;
    uint32_t conn_slowa[64];   /* original packed responses, long form included */
    uint8_t liczba_slow_polaczen;
    uint8_t rezerwa1[3];
};

struct HdaDiagSciezka {
    uint32_t wersja;
    uint32_t rozmiar;
    uint8_t konwerter_nid;
    uint8_t pin_nid;
    uint8_t dlugosc;          /* liczba NID-ow w nids[] */
    uint8_t cel;              /* HdaDiagCelPinu */
    uint8_t nids[HDA_DIAG_MAKS_DLUGOSC_SCIEZKI];
    uint8_t flagi;            /* bit0: niejednoznaczna, bit1: cykl */
    uint8_t rezerwa[2];
    uint32_t default_config_pinu;
    uint8_t kontroler_idx;
    uint8_t codec_cad;
    uint8_t fg_nid;
    uint8_t ocena;
    uint8_t selektory[HDA_DIAG_MAKS_DLUGOSC_SCIEZKI];
};

struct HdaDiagStrumien {
    uint32_t wersja;
    uint32_t rozmiar;
    uint8_t numer;            /* deskryptor / numer strumienia */
    uint8_t stream_tag;
    uint8_t stan;             /* HdaDiagStanStrumienia */
    uint8_t kanaly;
    uint16_t format_raw;
    uint16_t rezerwa0;
    uint32_t sample_rate;
    uint16_t bits;
    uint16_t rezerwa1;
    uint32_t cbl;
    uint32_t lvi;
    uint32_t lpib;
    uint32_t ctl;             /* SDnCTL surowy */
    uint32_t sts;             /* SDnSTS surowy */
    uint8_t bcis;
    uint8_t fifoe;
    uint8_t dese;
    uint8_t fifordy;
    uint8_t run;
    uint8_t rezerwa2[3];
    HdaDiagBdlInfo bdl;
    uint32_t ostatnie_bledy[HDA_DIAG_MAKS_BLEDOW_STRUMIENIA];
    uint8_t liczba_bledow;
    uint8_t rezerwa3[3];
    uint32_t historia[HDA_DIAG_MAKS_HISTORII];
    uint8_t historia_typ[HDA_DIAG_MAKS_HISTORII];
    uint8_t liczba_historii;
    uint8_t rezerwa4[3];
};

struct HdaDiagStanTestu {
    uint32_t wersja;
    uint32_t rozmiar;
    uint32_t tryb;            /* HdaDiagTrybTestu */
    uint32_t czestotliwosc_hz;
    uint32_t czas_ms;
    uint32_t pozostalo_ms;
    uint8_t kanaly;           /* bit0=L, bit1=R */
    uint8_t aktywny;
    uint8_t rezerwa[2];
    uint32_t ostatni_blad;
    /*
     * Aktualnie wybrana sciezka odtwarzania (stala QEMU lub odkryta).
     */
    uint8_t kontroler_idx;
    uint8_t codec_cad;
    uint8_t konwerter_nid;
    uint8_t pin_nid;
    uint8_t routing_fizyczny; /* 0 = stala sciezka QEMU, 1 = odkryta */
    uint8_t rezerwa2[3];
};

/*
 * Glowna migawka diagnostyczna.
 */
struct HdaDiagSnapshot {
    uint32_t wersja;
    uint32_t rozmiar;
    uint32_t generacja;
    uint32_t flagi;           /* HDA_DIAG_FLAGA_* */
    uint32_t etap;            /* HdaDiagEtap */
    uint32_t blad;            /* HdaDiagBlad */
    uint32_t postep_0_1000;   /* 0..1000 */
    uint32_t liczba_kontrolerow;
    uint32_t liczba_sciezek;
    uint32_t rezerwa0;
    uint32_t rezerwa1;
    HdaDiagStrumien strumien;
    HdaDiagStanTestu test;
    HdaDiagKontroler kontrolery[HDA_DIAG_MAKS_KONTROLEROW];
    HdaDiagKodek kodeki[HDA_DIAG_MAKS_KONTROLEROW][HDA_DIAG_MAKS_KODEKOW];
    HdaDiagFG fg[HDA_DIAG_MAKS_KONTROLEROW][HDA_DIAG_MAKS_KODEKOW][HDA_DIAG_MAKS_FG];
    HdaDiagWidget widgety[HDA_DIAG_MAKS_KONTROLEROW][HDA_DIAG_MAKS_KODEKOW][HDA_DIAG_MAKS_FG][HDA_DIAG_MAKS_WIDGETOW];
    HdaDiagSciezka sciezki[HDA_DIAG_MAKS_SCIEZEK];
};

/* Sanity: struktury maja stale rozmiary ABI. */
static_assert(sizeof(void*) >= 4, "wymagany 32/64-bit");
static_assert(HDA_DIAG_MAKS_POLACZEN >= 1, "lista polaczen musi istniec");
static_assert(HDA_DIAG_MAKS_DLUGOSC_SCIEZKI >= 2, "sciezka min. 2 wezly");
static_assert(sizeof(HdaDiagNaglowek) == 8, "naglowek ABI = 8 B");

/* =========================================================================
 * 8. GRAF TOPOLOGII (host-testowalny)
 * ========================================================================= */

/*
 * Buduje liste sciezek z konwerterow Audio Output do pinow wyjsciowych.
 * Graf jest przeszukiwany DFS z visited set i detekcja cykli.
 *
 * widgety[]     - tablica widgetow jednego kodeka/FG
 * liczba_widgetow - ile wpisow w widgety[]
 * sciezki[]     - wyjscie
 * max_sciezek   - pojemnosc sciezki[]
 * liczba_sciezek - ile zapisano
 *
 * Zwraca false przy bledzie wewnetrznym (zly wskaznik).
 * Cykle sa oznaczane flaga, nie zawieszaja przeszukiwania.
 */
inline bool hda_zbuduj_sciezki(
    const HdaDiagWidget* widgety,
    uint32_t liczba_widgetow,
    HdaDiagSciezka* sciezki,
    uint32_t max_sciezek,
    uint32_t* liczba_sciezek,
    bool* obciete
) {
    if (!liczba_sciezek) return false;
    *liczba_sciezek = 0;
    if (obciete) *obciete = false;
    if (!widgety || !sciezki || max_sciezek == 0) return false;
    if (liczba_widgetow > HDA_DIAG_MAKS_WIDGETOW * HDA_DIAG_MAKS_FG) {
        return false;
    }

    /*
     * Indeks widgetu po NID (NID jest 7-bitowy).
     */
    int16_t idx_po_nid[128];
    for (int i = 0; i < 128; ++i) idx_po_nid[i] = -1;
    for (uint32_t i = 0; i < liczba_widgetow; ++i) {
        const uint8_t nid = widgety[i].nid;
        if(!nid || nid>=128 || widgety[i].blad)continue;
        if(idx_po_nid[nid]>=0)return false;
        idx_po_nid[nid] = static_cast<int16_t>(i);
    }
    uint32_t operacje = 0;
    constexpr uint32_t MAKS_OPERACJI = 8192;

    /*
     * Sygnal w HDA plynie: zrodlowe wezly -> ... -> Pin.
     * Connection List widgetu wymienia jego WEJSCIA (zrodla).
     * Krawedz skierowana: zrodlo -> odbiorca (ten, kto ma zrodlo na liscie).
     *
     * DFS z konwertera Audio Output po krawedziach zrodlo->odbiorca
     * dociera do Pin Complex. Bez rekursji: stos o glebokosci limitu
     * sciezki + visited set w biezacej sciezce (detekcja cykli).
     */
    for (uint32_t w = 0; w < liczba_widgetow; ++w) {
        const HdaDiagWidget& konw = widgety[w];
        if (konw.nid > 127 || !konw.nid || konw.blad || hda_wcap_typ(konw.awcap) != HDA_WID_OUT) continue;

        struct Ramka {
            uint8_t nid;
            uint8_t glebokosc;
            uint16_t kandydat;
        };
        Ramka stos[HDA_DIAG_MAKS_DLUGOSC_SCIEZKI + 2];
        uint8_t sciezka_nid[HDA_DIAG_MAKS_DLUGOSC_SCIEZKI];
        bool w_sciezce[128];
        for (int i = 0; i < 128; ++i) w_sciezce[i] = false;

        int sp = 1;
        stos[0].nid = konw.nid;
        stos[0].glebokosc = 0;
        stos[0].kandydat = 0;
        w_sciezce[konw.nid] = true;
        sciezka_nid[0] = konw.nid;
        bool cykl = false;

        while (sp > 0) {
            Ramka& top = stos[sp - 1];
            bool posunieto = false;

            for (uint16_t kandydat = top.kandydat;
                 kandydat < liczba_widgetow;
                 ++kandydat) {
                if (++operacje > MAKS_OPERACJI) { if (obciete) *obciete = true; return true; }
                const HdaDiagWidget& odb = widgety[kandydat];
                if(odb.blad || (odb.flagi&1))continue;
                bool polaczony = false;
                for (uint8_t c = 0;
                     c < odb.liczba_polaczen && c < HDA_DIAG_MAKS_POLACZEN;
                     ++c) {
                    if (odb.polaczenia[c] == top.nid) {
                        polaczony = true;
                        break;
                    }
                }
                if (!polaczony) continue;

                top.kandydat = static_cast<uint16_t>(kandydat + 1);

                const uint8_t nidl = odb.nid;
                if (!nidl || nidl >= 128) continue;

                if (w_sciezce[nidl]) {
                    cykl = true;
                    continue;
                }

                if (static_cast<uint32_t>(top.glebokosc) + 1U >= HDA_DIAG_MAKS_DLUGOSC_SCIEZKI) {
                    if (obciete) *obciete = true;
                    continue;
                }

                const uint8_t typ_odb = hda_wcap_typ(odb.awcap);

                if (typ_odb == HDA_WID_PIN && (odb.pin_caps&HDA_PINCAP_OUTPUT) && hda_cel_jest_wyjsciem(hda_klasyfikuj_pin(odb.default_config))) {
                    if (*liczba_sciezek >= max_sciezek) {
                        if (obciete) *obciete = true;
                        continue;
                    }
                    sciezka_nid[top.glebokosc + 1] = nidl;
                    HdaDiagSciezka& s = sciezki[*liczba_sciezek];
                    s.wersja = HDA_DIAG_WERSJA_ABI;
                    s.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSciezka));
                    s.konwerter_nid = konw.nid;
                    s.pin_nid = nidl;
                    s.dlugosc = static_cast<uint8_t>(top.glebokosc + 2);
                    for (uint8_t i = 0; i < s.dlugosc &&
                                        i < HDA_DIAG_MAKS_DLUGOSC_SCIEZKI; ++i) {
                        s.nids[i] = sciezka_nid[i];
                    }
                    s.default_config_pinu = odb.default_config;
                    s.cel = static_cast<uint8_t>(
                        hda_klasyfikuj_pin(odb.default_config));
                    s.flagi = static_cast<uint8_t>(cykl ? 2 : 0);
                    s.rezerwa[0] = 0;
                    s.rezerwa[1] = 0;
                    s.kontroler_idx = 0;
                    s.codec_cad = 0;
                    s.fg_nid = 0;
                    s.ocena = s.cel == HDA_CEL_GLOSNIK && hda_lokacja_wewnetrzna(hda_pincfg_lokacja(odb.default_config)) ? 100 :
                        s.cel == HDA_CEL_SLUCHAWKI ? 60 : s.cel == HDA_CEL_WYJSCIE_LINIOWE ? 40 : 10;
                    for (uint8_t h = 1; h < s.dlugosc; ++h) {
                        const int index = idx_po_nid[s.nids[h]];
                        s.selektory[h] = 255;
                        if (index >= 0) for (uint8_t ci = 0; ci < widgety[index].liczba_polaczen && ci < HDA_DIAG_MAKS_POLACZEN; ++ci)
                            if (widgety[index].polaczenia[ci] == s.nids[h-1]) { s.selektory[h] = ci; break; }
                    }
                    ++(*liczba_sciezek);
                    continue;
                }

                if (hda_typ_widgetu_posrednik(typ_odb) ||
                    typ_odb == HDA_WID_OUT) {
                    if (sp >= static_cast<int>(sizeof(stos) / sizeof(stos[0]))) {
                        if (obciete) *obciete = true;
                        continue;
                    }
                    sciezka_nid[top.glebokosc + 1] = nidl;
                    w_sciezce[nidl] = true;
                    stos[sp].nid = nidl;
                    stos[sp].glebokosc = static_cast<uint8_t>(top.glebokosc + 1);
                    stos[sp].kandydat = 0;
                    ++sp;
                    posunieto = true;
                    break;
                }
            }

            if (!posunieto) {
                w_sciezce[top.nid] = false;
                --sp;
            }
        }
    }

    for(uint32_t i=1;i<*liczba_sciezek;++i){
        const auto route=sciezki[i];uint32_t j=i;
        while(j&&sciezki[j-1].ocena<route.ocena){sciezki[j]=sciezki[j-1];--j;}
        sciezki[j]=route;
    }
    return true;
}

/*
 * Czy w tablicy sciezek istnieje conajmniej jedna sciezka do glosnika.
 */
inline bool hda_sciezka_glosnik_istnieje(const HdaDiagSciezka* s, uint32_t n) {
    for (uint32_t i = 0; i < n && s; ++i) {
        if (hda_cel_jest_glosnikiem(s[i].cel)) return true;
    }
    return false;
}

inline bool hda_sciezka_sluchawki_istnieje(const HdaDiagSciezka* s, uint32_t n) {
    for (uint32_t i = 0; i < n && s; ++i) {
        if (hda_cel_jest_sluchawkami(s[i].cel)) return true;
    }
    return false;
}

inline bool hda_sciezka_line_out_istnieje(const HdaDiagSciezka* s, uint32_t n) {
    for (uint32_t i = 0; i < n && s; ++i) {
        if (s[i].cel == HDA_CEL_WYJSCIE_LINIOWE) return true;
    }
    return false;
}

inline bool hda_sciezka_hdmi_istnieje(const HdaDiagSciezka* s, uint32_t n) {
    for (uint32_t i = 0; i < n && s; ++i) {
        if (hda_cel_jest_hdmi(s[i].cel)) return true;
    }
    return false;
}

inline uint32_t hda_liczba_sciezek_glosnik(const HdaDiagSciezka* s, uint32_t n) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < n && s; ++i) {
        if (hda_cel_jest_glosnikiem(s[i].cel)) ++c;
    }
    return c;
}

/*
 * Zwraca czytelny stan diagnostyczny wyjscia audio wg 11 wymaganych stanow:
 *  - Brak kontrolera HDA
 *  - Kontroler HDA wykryty
 *  - Brak odpowiedzi kodeka
 *  - Kodek wykryty
 *  - Brak Audio Function Group
 *  - Brak wykrytego wyjścia audio
 *  - Wyjście Głośniki
 *  - Wyjście Słuchawki
 *  - Wyjście Line Out
 *  - Wyjście HDMI DisplayPort
 *  - Nieznany typ wyjścia
 */
inline const char* hda_diag_stan_wyjscia_tekst(
    const HdaDiagSnapshot* snap,
    const HdaDiagSkan* scan
) {
    if (!snap || snap->liczba_kontrolerow == 0 ||
        (scan && scan->blad == HDA_DIAG_BLAD_BRAK_KONTROLERA)) {
        return "Brak kontrolera HDA";
    }

    uint32_t liczba_odp = 0;
    for (uint32_t k = 0; k < snap->liczba_kontrolerow && k < HDA_DIAG_MAKS_KONTROLEROW; ++k) {
        for (uint32_t c = 0; c < HDA_DIAG_MAKS_KODEKOW; ++c) {
            if (snap->kodeki[k][c].odpowiada) ++liczba_odp;
        }
    }

    if (liczba_odp == 0) {
        if (scan && (scan->blad == HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA ||
                     scan->blad == HDA_DIAG_BLAD_BRAK_KODEKA ||
                     scan->etap == HDA_DIAG_ETAP_BLAD ||
                     scan->gotowy)) {
            return "Brak odpowiedzi kodeka";
        }
        return "Kontroler HDA wykryty";
    }

    uint32_t liczba_afg = 0;
    for (uint32_t k = 0; k < snap->liczba_kontrolerow && k < HDA_DIAG_MAKS_KONTROLEROW; ++k) {
        for (uint32_t c = 0; c < HDA_DIAG_MAKS_KODEKOW; ++c) {
            if (!snap->kodeki[k][c].odpowiada) continue;
            for (uint32_t f = 0; f < snap->kodeki[k][c].liczba_fg && f < HDA_DIAG_MAKS_FG; ++f) {
                if (snap->fg[k][c][f].audio) ++liczba_afg;
            }
        }
    }

    if (liczba_afg == 0) {
        if (scan && (scan->blad == HDA_DIAG_BLAD_BRAK_AFG ||
                     scan->gotowy ||
                     scan->etap == HDA_DIAG_ETAP_GOTOWY)) {
            return "Brak Audio Function Group";
        }
        return "Kodek wykryty";
    }

    if (snap->liczba_sciezek == 0) {
        if (scan && (scan->blad == HDA_DIAG_BLAD_BRAK_WYJSC ||
                     scan->gotowy ||
                     scan->etap == HDA_DIAG_ETAP_GOTOWY)) {
            return "Brak wykrytego wyjścia audio";
        }
        return "Kodek wykryty";
    }

    /* Wykryto sciezki: klasyfikujemy urzadzenie wyjsciowe */
    if (hda_sciezka_glosnik_istnieje(snap->sciezki, snap->liczba_sciezek)) {
        return "Wyjście Głośniki";
    }
    if (hda_sciezka_sluchawki_istnieje(snap->sciezki, snap->liczba_sciezek)) {
        return "Wyjście Słuchawki";
    }
    if (hda_sciezka_line_out_istnieje(snap->sciezki, snap->liczba_sciezek)) {
        return "Wyjście Line Out";
    }
    if (hda_sciezka_hdmi_istnieje(snap->sciezki, snap->liczba_sciezek)) {
        return "Wyjście HDMI DisplayPort";
    }
    return "Nieznany typ wyjścia";
}

/* =========================================================================
 * 9. TEKST RAPORTU (czyste formatowanie)
 * ========================================================================= */

/*
 * Bufor tekstowy raportu z kontrola pojemnosci.
 */
struct HdaRaportBufor {
    char* dane;
    uint32_t pojemnosc;
    uint32_t dlugosc;
    bool obciety;
};

inline void hda_raport_init(HdaRaportBufor* b, char* dane, uint32_t pojemnosc) {
    if (!b) return;
    b->dane = dane;
    b->pojemnosc = pojemnosc;
    b->dlugosc = 0;
    b->obciety = false;
    if (dane && pojemnosc > 0) dane[0] = '\0';
}

inline void hda_raport_dopisz(HdaRaportBufor* b, const char* tekst) {
    if (!b || !b->dane || !tekst) return;
    for (size_t i = 0; tekst[i]; ++i) {
        if (b->dlugosc + 1 >= b->pojemnosc) {
            b->obciety = true;
            b->dane[b->dlugosc] = '\0';
            return;
        }
        b->dane[b->dlugosc++] = tekst[i];
    }
    b->dane[b->dlugosc] = '\0';
}

inline void hda_raport_liczba(HdaRaportBufor* b, uint64_t v, uint8_t baza) {
    char tmp[24];
    int i = 0;
    if (baza < 2 || baza > 16) baza = 10;
    if (v == 0) {
        hda_raport_dopisz(b, "0");
        return;
    }
    const char* cyfry = "0123456789ABCDEF";
    while (v > 0 && i < 23) {
        tmp[i++] = cyfry[v % baza];
        v /= baza;
    }
    char odw[24];
    int j = 0;
    while (i > 0) odw[j++] = tmp[--i];
    odw[j] = '\0';
    hda_raport_dopisz(b, odw);
}

inline void hda_raport_hex32(HdaRaportBufor* b, uint32_t v) {
    hda_raport_dopisz(b, "0x");
    char tmp[9];
    for (int i = 7; i >= 0; --i) {
        const uint8_t n = static_cast<uint8_t>((v >> (i * 4)) & 0xF);
        tmp[7 - i] = static_cast<char>(n < 10 ? ('0' + n) : ('A' + n - 10));
    }
    tmp[8] = '\0';
    hda_raport_dopisz(b, tmp);
}

inline void hda_raport_hex16(HdaRaportBufor* b, uint16_t v) {
    hda_raport_dopisz(b, "0x");
    char tmp[5];
    for (int i = 3; i >= 0; --i) {
        const uint8_t n = static_cast<uint8_t>((v >> (i * 4)) & 0xF);
        tmp[3 - i] = static_cast<char>(n < 10 ? ('0' + n) : ('A' + n - 10));
    }
    tmp[4] = '\0';
    hda_raport_dopisz(b, tmp);
}

inline void hda_raport_hex8(HdaRaportBufor* b, uint8_t v) {
    hda_raport_dopisz(b, "0x");
    char tmp[3];
    for (int i = 1; i >= 0; --i) {
        const uint8_t n = static_cast<uint8_t>((v >> (i * 4)) & 0xF);
        tmp[1 - i] = static_cast<char>(n < 10 ? ('0' + n) : ('A' + n - 10));
    }
    tmp[2] = '\0';
    hda_raport_dopisz(b, tmp);
}

inline void hda_raport_linia(HdaRaportBufor* b, const char* tekst) {
    hda_raport_dopisz(b, tekst);
    hda_raport_dopisz(b, "\n");
}

inline void hda_raport_klucz_wartosc(HdaRaportBufor* b,
                                     const char* klucz,
                                     const char* wartosc) {
    hda_raport_dopisz(b, klucz);
    hda_raport_dopisz(b, " ");
    hda_raport_dopisz(b, wartosc);
    hda_raport_dopisz(b, "\n");
}

inline void hda_raport_klucz_hex32(HdaRaportBufor* b,
                                   const char* klucz,
                                   uint32_t v) {
    hda_raport_dopisz(b, klucz);
    hda_raport_dopisz(b, " ");
    hda_raport_hex32(b, v);
    hda_raport_dopisz(b, "\n");
}

inline void hda_raport_klucz_hex16(HdaRaportBufor* b,
                                   const char* klucz,
                                   uint16_t v) {
    hda_raport_dopisz(b, klucz);
    hda_raport_dopisz(b, " ");
    hda_raport_hex16(b, v);
    hda_raport_dopisz(b, "\n");
}

inline void hda_raport_klucz_hex8(HdaRaportBufor* b,
                                  const char* klucz,
                                  uint8_t v) {
    hda_raport_dopisz(b, klucz);
    hda_raport_dopisz(b, " ");
    hda_raport_hex8(b, v);
    hda_raport_dopisz(b, "\n");
}

inline void hda_raport_klucz_liczba(HdaRaportBufor* b,
                                    const char* klucz,
                                    uint64_t v) {
    hda_raport_dopisz(b, klucz);
    hda_raport_dopisz(b, " ");
    hda_raport_liczba(b, v, 10);
    hda_raport_dopisz(b, "\n");
}

inline const char* hda_nazwa_transportu(uint32_t t){return t==1?"Immediate Command (IC)":t==2?"CORB/RIRB":"not selected";}
inline const char* hda_nazwa_statusu_transportu(uint32_t s){
    const char* names[]={"success","controller unavailable","ICB stuck","timeout","IRV missing","invalid argument",
        "pending","transport busy","CORB full","RIRB no response","codec no response","malformed state","reset required","cancelled"};
    return s<sizeof(names)/sizeof(names[0])?names[s]:"unknown status";
}
inline const char* hda_nazwa_etapu_bledu(uint32_t stage) {
    const char* names[]={"OK","BAR nieprawidłowy","MMIO: mapowanie nieudane","PCI Memory Space wyłączone",
        "PCI Bus Master wyłączony","GCAP nieprawidłowy","Nieobsługiwana wersja HDA","Brak zegara HPET",
        "Timeout zatrzymywania DMA","Timeout CRST=0","Timeout CRST=1","CORB: brak obsługiwanego rozmiaru",
        "RIRB: brak obsługiwanego rozmiaru","CORB: alokacja DMA nieudana","RIRB: alokacja DMA nieudana",
        "CORB: reset wskaźnika nieudany","RIRB: reset wskaźnika nieudany","CORB/RIRB: start nieudany",
        "Kontroler zresetowany lub transport wymaga resetu"};
    return stage<sizeof(names)/sizeof(names[0])?names[stage]:"Nieznany etap";
}
inline void hda_raport_pcm(HdaRaportBufor* out,uint32_t caps,uint32_t formats) {
    hda_raport_klucz_hex32(out,"PCMSizeRate raw",caps);
    const char* rates[]={"8000","11025","16000","22050","32000","44100","48000","88200","96000","176400","192000","384000"};
    hda_raport_dopisz(out,"Rates [Hz]");for(uint32_t i=0;i<12;++i)if(caps&(1U<<i)){hda_raport_dopisz(out," ");hda_raport_dopisz(out,rates[i]);}
    hda_raport_dopisz(out,"\nBit depths");const uint8_t bits[]={8,16,20,24,32};
    for(uint32_t i=0;i<5;++i)if(caps&(1U<<(16+i))){hda_raport_dopisz(out," ");hda_raport_liczba(out,bits[i],10);}
    hda_raport_dopisz(out,"\n");hda_raport_klucz_hex32(out,"StreamFormats raw",formats);
    hda_raport_dopisz(out,"Formats");if(formats&1)hda_raport_dopisz(out," PCM");if(formats&2)hda_raport_dopisz(out," Float32");if(formats&4)hda_raport_dopisz(out," AC3");hda_raport_dopisz(out,"\n");
}
inline void hda_raport_amp(HdaRaportBufor* out,const char* name,uint32_t caps) {
    hda_raport_klucz_hex32(out,name,caps);
    hda_raport_klucz_liczba(out,"  offset (0 dB step)",hda_amp_offset(caps));
    hda_raport_klucz_liczba(out,"  number of steps",hda_amp_kroki(caps));
    hda_raport_klucz_liczba(out,"  step size [0.25 dB]",uint32_t(hda_amp_rozmiar_kroku_25(caps))+1);
    hda_raport_klucz_liczba(out,"  mute supported",hda_amp_mute(caps));
}

/*
 * Formatuje pelny raport tekstowy z migawki.
 * Surowe wartosci sa oznaczane sufiksem "raw", dekodowane osobno.
 */
inline bool hda_sformatuj_raport(
    const HdaDiagSnapshot* snap,
    const char* wersja_systemu,
    HdaRaportBufor* out
) {
    if (!snap || !out) return false;

    hda_raport_linia(out, "=== Raport diagnostyczny HDA ===");
    hda_raport_klucz_wartosc(out, "System", wersja_systemu ? wersja_systemu : "nieznany");
    hda_raport_dopisz(out, "Schema ");
    hda_raport_liczba(out, snap->wersja, 10);
    hda_raport_dopisz(out, " rozmiar ");
    hda_raport_liczba(out, snap->rozmiar, 10);
    hda_raport_dopisz(out, " generacja ");
    hda_raport_liczba(out, snap->generacja, 10);
    hda_raport_dopisz(out, "\n");
    hda_raport_klucz_hex32(out, "Flagi", snap->flagi);
    if (snap->flagi & HDA_DIAG_FLAGA_OBECIETA) {
        hda_raport_linia(out, "UWAGA: migawka obcieta (limit ABI).");
    }

    if (snap->liczba_kontrolerow == 0) {
        hda_raport_linia(out, "Nie znaleziono kontrolera HDA.");
    }

    for (uint32_t k = 0; k < snap->liczba_kontrolerow &&
                        k < HDA_DIAG_MAKS_KONTROLEROW; ++k) {
        const HdaDiagKontroler& c = snap->kontrolery[k];
        hda_raport_dopisz(out, "HDA Controller ");
        hda_raport_liczba(out, k, 10);
        hda_raport_dopisz(out, "\n");
        hda_raport_dopisz(out, "PCI ");
        hda_raport_liczba(out, c.bus, 16);
        hda_raport_dopisz(out, " ");
        hda_raport_liczba(out, c.slot, 16);
        hda_raport_dopisz(out, ".");
        hda_raport_liczba(out, c.func, 16);
        hda_raport_dopisz(out, "\n");
        hda_raport_dopisz(out, "Vendor ");
        hda_raport_liczba(out, c.vendor_id, 16);
        hda_raport_dopisz(out, "\nDevice ");
        hda_raport_liczba(out, c.device_id, 16);
        hda_raport_dopisz(out, "\nRevision ");
        hda_raport_liczba(out, c.revision, 16);
        hda_raport_dopisz(out, "\n");
        hda_raport_klucz_hex32(out, "Command raw", c.command);
        hda_raport_dopisz(out, "BAR0 raw ");
        hda_raport_liczba(out, c.bar0, 16);
        hda_raport_dopisz(out, "\n");
        hda_raport_klucz_liczba(out, "BAR0_MMIO_LEN", c.bar0_dlugosc);
        hda_raport_klucz_liczba(out,"BAR width",c.bar_typ);
        hda_raport_klucz_liczba(out,"MMIO mapped",c.mmio_zmapowane);
        hda_raport_klucz_hex32(out,"Subsystem PCI ID",c.subsystem_id);
        hda_raport_klucz_liczba(out,"Initialization state",c.inicjalizacja);
        hda_raport_klucz_liczba(out,"Initialization ready",c.gotowy);
        hda_raport_klucz_liczba(out,"Initialization failure stage",c.etap_bledu);
        hda_raport_klucz_wartosc(out,"Initialization detail",hda_nazwa_etapu_bledu(c.etap_bledu));
        hda_raport_klucz_liczba(out,"Diagnostic generation",c.generacja);
        hda_raport_klucz_liczba(out,"Transaction phase",c.stan_transakcji);
        hda_raport_klucz_liczba(out,"Transport status",c.status_transportu);
        hda_raport_klucz_wartosc(out,"Transport detail",hda_nazwa_statusu_transportu(c.status_transportu));
        hda_raport_klucz_hex32(out,"CORB capabilities",c.corb_caps);
        hda_raport_klucz_liczba(out,"CORB selected size",c.corb_rozmiar);
        hda_raport_klucz_liczba(out,"CORB initialization",c.corb_stan);
        hda_raport_klucz_liczba(out,"CORB running",c.corb_run);
        hda_raport_klucz_hex32(out,"RIRB capabilities",c.rirb_caps);
        hda_raport_klucz_liczba(out,"RIRB selected size",c.rirb_rozmiar);
        hda_raport_klucz_liczba(out,"RIRB initialization",c.rirb_stan);
        hda_raport_klucz_liczba(out,"RIRB running",c.rirb_run);
        hda_raport_klucz_liczba(out,"Unsolicited responses separated",c.odpowiedzi_niezamowione);
        hda_raport_klucz_liczba(out,"Foreign/stale responses separated",c.odpowiedzi_obce);
        hda_raport_klucz_liczba(out,"Bounded recoveries",c.odzyskiwania);
        hda_raport_klucz_liczba(out,"Maximum transport service [ns]",c.maks_krok_ns);
        hda_raport_klucz_wartosc(out,"Test tone",c.test_dostepny?"available (proven QEMU path)":"unavailable (physical route not configured)");

        hda_raport_dopisz(out, "HDA ");
        hda_raport_liczba(out, c.hda_vmaj, 10);
        hda_raport_dopisz(out, ".");
        hda_raport_liczba(out, c.hda_vmin, 10);
        hda_raport_dopisz(out, "\n");
        hda_raport_klucz_hex16(out, "GCAP raw", c.gcap);
        hda_raport_klucz_hex32(out, "GCTL raw", c.gctl);
        hda_raport_klucz_hex16(out, "STATESTS raw", c.statests);
        hda_raport_klucz_hex16(out, "STATESTS initial", c.initial_statests);
        hda_raport_klucz_hex16(out, "Discovered codec mask", c.discovered_codec_mask);
        hda_raport_klucz_hex16(out, "Responsive codec mask", c.responsive_codec_mask);
        hda_raport_klucz_liczba(out, "OSS", c.oss);
        hda_raport_klucz_liczba(out, "ISS", c.iss);
        hda_raport_klucz_liczba(out, "BSS", c.bss);
        hda_raport_klucz_liczba(out, "DMA64", c.dma64);
        hda_raport_klucz_liczba(out, "Codecs", c.liczba_kodekow);
        hda_raport_dopisz(out, "Transport ");
        hda_raport_dopisz(out, hda_nazwa_transportu(c.transport));
        hda_raport_dopisz(out, "\n");

        for (uint32_t kd = 0; kd < HDA_DIAG_MAKS_KODEKOW; ++kd) {
            const HdaDiagKodek& ko = snap->kodeki[k][kd];
            if (!ko.odpowiada && ko.vendor_id == 0 && kd > 0) continue;
            if (!ko.odpowiada && kd > 0) continue;

            hda_raport_dopisz(out, "Codec address ");
            hda_raport_liczba(out, ko.cad, 10);
            hda_raport_dopisz(out, "\n");
            if (!ko.odpowiada) {
                hda_raport_linia(out, "  nie odpowiada");
                continue;
            }
            hda_raport_dopisz(out, "Codec Vendor ");
            hda_raport_liczba(out, ko.vendor_id, 16);
            hda_raport_dopisz(out, "\nCodec Device ");
            hda_raport_liczba(out, ko.device_id, 16);
            hda_raport_dopisz(out, "\n");
            hda_raport_klucz_hex32(out, "RevisionID raw", ko.revision_id);
            hda_raport_klucz_hex32(out, "RootSubCount raw", ko.root_sub_count);
            hda_raport_klucz_liczba(out,"Root subordinate start",hda_sub_start(ko.root_sub_count));
            hda_raport_klucz_liczba(out,"Root subordinate count",hda_sub_count(ko.root_sub_count));
            hda_raport_klucz_liczba(out,"Function Groups",ko.liczba_fg);
            hda_raport_klucz_liczba(out,"Codec read error",ko.blad);

            for (uint32_t f = 0; f < HDA_DIAG_MAKS_FG; ++f) {
                const HdaDiagFG& g = snap->fg[k][kd][f];
                if (g.nid == 0 && g.widget_count == 0) continue;
                hda_raport_dopisz(out, "AFG NID ");
                hda_raport_hex8(out, g.nid);
                hda_raport_dopisz(out, "\n");
                hda_raport_klucz_liczba(out, "  FGType", g.typ);
                hda_raport_klucz_hex32(out, "  FGCaps raw", g.fg_caps);
                hda_raport_klucz_liczba(out, "  WidgetStart", g.widget_start);
                hda_raport_klucz_liczba(out, "  WidgetCount", g.widget_count);
                hda_raport_pcm(out,g.pcm_size_rate,g.stream_formats);
                hda_raport_amp(out,"AFG input amplifier",g.in_amp_caps);
                hda_raport_amp(out,"AFG output amplifier",g.out_amp_caps);
                hda_raport_klucz_hex32(out,"AFG power capabilities",g.power_states);
                hda_raport_klucz_liczba(out,"FG read error",g.blad);

                for (uint32_t wi = 0; wi < HDA_DIAG_MAKS_WIDGETOW; ++wi) {
                    const HdaDiagWidget& w = snap->widgety[k][kd][f][wi];
                    if (w.nid == 0 && w.awcap == 0 && wi > 0) continue;

                    hda_raport_dopisz(out, "Widget NID ");
                    hda_raport_hex8(out, w.nid);
                    hda_raport_dopisz(out, "\n");
                    hda_raport_dopisz(out, "Type ");
                    hda_raport_dopisz(out, hda_nazwa_typu_widgetu(w.typ));
                    hda_raport_dopisz(out, "\n");
                    hda_raport_klucz_hex32(out, "AWCAP raw", w.awcap);
                    hda_raport_klucz_liczba(out,"Stereo",(w.awcap&HDA_WCAP_STEREO)!=0);
                    hda_raport_klucz_liczba(out,"Digital",(w.awcap&HDA_WCAP_DIGITAL)!=0);
                    hda_raport_klucz_liczba(out,"Input amp present",(w.awcap&HDA_WCAP_IN_AMP)!=0);
                    hda_raport_klucz_liczba(out,"Output amp present",(w.awcap&HDA_WCAP_OUT_AMP)!=0);
                    hda_raport_klucz_liczba(out,"Amp override",(w.awcap&HDA_WCAP_AMP_OVRD)!=0);
                    hda_raport_klucz_liczba(out,"Format override",(w.awcap&HDA_WCAP_FORMAT_OVRD)!=0);
                    hda_raport_klucz_liczba(out,"Widget read error",w.blad);
                    hda_raport_klucz_hex32(out,"Power capabilities",w.power_states);
                    hda_raport_klucz_liczba(out,"Power current",w.power_current);
                    hda_raport_klucz_hex32(out,"Connection list length raw",w.connlist_len_raw);
                    hda_raport_klucz_liczba(out,"Connection list entries",hda_connlist_liczba(w.connlist_len_raw));
                    hda_raport_klucz_wartosc(out,"Connection list form",w.conn_long_form?"long":"short");
                    hda_raport_klucz_liczba(out,"Current connection selector",w.conn_select);
                    if(w.flagi&1)hda_raport_linia(out,"WARNING: widget connections truncated by explicit limit");
                    if(w.liczba_slow_polaczen<=64){
                        hda_raport_dopisz(out,"Packed connection responses");
                        for(uint32_t n=0;n<w.liczba_slow_polaczen;++n){hda_raport_dopisz(out," ");hda_raport_hex32(out,w.conn_slowa[n]);}
                        hda_raport_dopisz(out,"\n");
                    }

                    if (hda_typ_widgetu_pin(w.typ)) {
                        hda_raport_klucz_hex32(out, "DefaultConfig raw", w.default_config);
                        hda_raport_dopisz(out, "DefaultDevice ");
                        hda_raport_dopisz(
                            out, hda_nazwa_domyslnego_urzadzenia(
                                     hda_pincfg_dev(w.default_config)));
                        hda_raport_dopisz(out, "\nLocation ");
                        hda_raport_dopisz(
                            out, hda_nazwa_lokacji(
                                     hda_pincfg_lokacja(w.default_config)));
                        hda_raport_dopisz(out, "\nPort ");
                        hda_raport_dopisz(
                            out, hda_nazwa_portu(
                                     hda_pincfg_port(w.default_config)));
                        hda_raport_dopisz(out, "\n");
                        hda_raport_klucz_hex32(out, "PinCaps raw", w.pin_caps);
                        hda_raport_klucz_wartosc(out,"Connection type",hda_nazwa_typu_polaczenia(hda_pincfg_con(w.default_config)));
                        hda_raport_klucz_wartosc(out,"Color",hda_nazwa_koloru(hda_pincfg_kolor(w.default_config)));
                        hda_raport_klucz_liczba(out,"Association",hda_pincfg_asocjacja(w.default_config));
                        hda_raport_klucz_liczba(out,"Sequence",hda_pincfg_sekwencja(w.default_config));
                        hda_raport_klucz_liczba(out,"EAPD capability",(w.pin_caps&HDA_PINCAP_EAPD)!=0);
                        if(w.pin_caps&HDA_PINCAP_EAPD)hda_raport_klucz_hex32(out,"EAPD current raw",w.eapd);
                        hda_raport_klucz_liczba(out,"Pin output capability",(w.pin_caps&HDA_PINCAP_OUTPUT)!=0);
                        hda_raport_klucz_liczba(out,"Pin input capability",(w.pin_caps&HDA_PINCAP_INPUT)!=0);
                    }

                    if (hda_typ_wolno_pcm_caps(w.typ)) {
                        hda_raport_pcm(out,w.pcm_size_rate,w.stream_formats);
                    }

                    if (w.out_amp_caps || w.in_amp_caps) {
                        hda_raport_amp(out,"OutAmpCaps raw",w.out_amp_caps);
                        hda_raport_amp(out,"InAmpCaps raw",w.in_amp_caps);
                    }

                    if (w.liczba_polaczen > 0) {
                        hda_raport_dopisz(out, "Connections");
                        for (uint8_t ci = 0; ci < w.liczba_polaczen && ci < HDA_DIAG_MAKS_POLACZEN; ++ci) {
                            hda_raport_dopisz(out, " ");
                            hda_raport_hex8(out, w.polaczenia[ci]);
                        }
                        hda_raport_dopisz(out, "\n");
                        hda_raport_dopisz(out, "ConnectionsRaw");
                        for (uint8_t ci = 0; ci < w.liczba_polaczen && ci < HDA_DIAG_MAKS_POLACZEN; ++ci) {
                            hda_raport_dopisz(out, " ");
                            hda_raport_hex8(out, w.polaczenia_surowe[ci]);
                        }
                        hda_raport_dopisz(out, "\n");
                    }
                }
            }
        }

        /* Sciezki naleza do calego snapshotu, ale raportujemy je
         * przy pierwszym kontrolerze z kontekstem. */
        {
            hda_raport_linia(out, "Audio paths");
            for (uint32_t s = 0; s < snap->liczba_sciezek &&
                                s < HDA_DIAG_MAKS_SCIEZEK; ++s) {
                const HdaDiagSciezka& sc = snap->sciezki[s];
                if(sc.kontroler_idx!=k)continue;
                hda_raport_dopisz(out, "Path ");
                hda_raport_liczba(out, s, 10);
                hda_raport_dopisz(out," CAD ");hda_raport_liczba(out,sc.codec_cad,10);
                hda_raport_dopisz(out," FG ");hda_raport_hex8(out,sc.fg_nid);
                hda_raport_dopisz(out," score ");hda_raport_liczba(out,sc.ocena,10);
                hda_raport_dopisz(out, " converter ");
                hda_raport_hex8(out, sc.konwerter_nid);
                hda_raport_dopisz(out, " pin ");
                hda_raport_hex8(out, sc.pin_nid);
                hda_raport_dopisz(out, " cel ");
                hda_raport_dopisz(out, hda_nazwa_celu_pinu(sc.cel));
                hda_raport_dopisz(out, " hop ");
                for (uint8_t i = 0; i < sc.dlugosc; ++i) {
                    hda_raport_hex8(out, sc.nids[i]);
                    hda_raport_dopisz(out, " ");
                }
                hda_raport_dopisz(out, "\nSelector indices");
                for(uint8_t h=1;h<sc.dlugosc&&h<HDA_DIAG_MAKS_DLUGOSC_SCIEZKI;++h){hda_raport_dopisz(out," ");hda_raport_liczba(out,sc.selektory[h],10);}
                hda_raport_dopisz(out, "\nOutput route discovered; physical activation awaits real hardware evidence.\n");
            }
        }
    }

    hda_raport_linia(out, "Stream state");
    hda_raport_klucz_liczba(out, "State", snap->strumien.stan);
    hda_raport_klucz_liczba(out, "Tag", snap->strumien.stream_tag);
    hda_raport_klucz_liczba(out, "Rate", snap->strumien.sample_rate);
    hda_raport_klucz_liczba(out, "Bits", snap->strumien.bits);
    hda_raport_klucz_liczba(out, "Channels", snap->strumien.kanaly);
    hda_raport_klucz_liczba(out, "CBL", snap->strumien.cbl);
    hda_raport_klucz_liczba(out, "LVI", snap->strumien.lvi);
    hda_raport_klucz_liczba(out, "LPIB", snap->strumien.lpib);
    hda_raport_klucz_hex32(out, "CTL raw", snap->strumien.ctl);
    hda_raport_klucz_hex32(out, "STS raw", snap->strumien.sts);

    hda_raport_linia(out, "Test path");
    hda_raport_klucz_liczba(out, "Tryb", snap->test.tryb);
    hda_raport_klucz_liczba(out, "Hz", snap->test.czestotliwosc_hz);
    hda_raport_klucz_liczba(out, "Konwerter", snap->test.konwerter_nid);
    hda_raport_klucz_liczba(out, "Pin", snap->test.pin_nid);
    hda_raport_klucz_liczba(out, "RoutingFizyczny", snap->test.routing_fizyczny);

    if (out->obciety) {
        hda_raport_linia(out, "UWAGA: raport obciety do pojemnosci bufora.");
    }
    return true;
}

/* =========================================================================
 * 10. STALE TESTU TONOW (polityka bezpieczenstwa)
 * ========================================================================= */

/*
 * Amplituda konserwatywna (ok. 50% skali 16-bit, bez przyciecia).
 * Generator nie uzywa zmiennoprzecinkowych.
 */
#define HDA_TEST_AMPLITUDA INT32_C(16384)

/*
 * Domyslny czas tonu diagnostycznego: 1.5 s (auto-stop).
 */
#define HDA_TEST_DOMYSLNY_CZAS_MS UINT32_C(1500)

/*
 * Sweep bezpieczny: 200 Hz .. 4000 Hz.
 */
#define HDA_TEST_SWEEP_MIN_HZ UINT32_C(200)
#define HDA_TEST_SWEEP_MAX_HZ UINT32_C(4000)
#define HDA_TEST_SWEEP_CZAS_MS UINT32_C(2000)

/*
 * Zwykly ton: 440 Hz / 1000 Hz.
 */
#define HDA_TEST_TON_A4_HZ UINT32_C(440)
#define HDA_TEST_TON_1K_HZ UINT32_C(1000)

