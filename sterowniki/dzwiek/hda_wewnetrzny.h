/*
 * Bursztyn OS - wewnetrzne API transportu HDA (jadar tylko)
 *
 * Legacy API odtwarzania QEMU i stanu strumienia.
 * Skaner uzywa wylacznie asynchronicznego hda_transport.h.
 * Nie ujawnia MMIO/verbów w Ring3.
 *
 * Nie dolaczaj z aplikacji Ring 3.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * Wysyla surowa komende Immediate Command (ICOI) i zwraca odpowiedz (ICII).
 * Timeout-bounded. Nie publikowane przez BWS.
 */
bool hda_wew_wyslij_immediate(uint32_t komenda, uint32_t* odpowiedz);

/*
 * Bezpieczne wyslanie komendy read-only ze statusem transportu.
 */
bool hda_wew_send_verb_readonly(uint32_t verb, uint32_t* response);

/* Budowa slowa komendy (12-bit verb z 8-bit payload). */
uint32_t hda_wew_verb12(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t payload);

/* Budowa slowa komendy (4-bit verb z 16-bit payload). */
uint32_t hda_wew_verb4(uint8_t cad, uint8_t nid, uint8_t verb, uint16_t payload);

/* Czy kontroler HDA jest zmapowany i gotowy do komend. */
bool hda_wew_kontroler_gotowy();

/*
 * Odczyt surowych pol kontrolera do migawki diagnostycznej.
 * Zwraca false, gdy kontroler niedostepny.
 */
struct HdaWewStanKontrolera {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t revision;
    uint16_t command;
    uint64_t bar0;
    uint32_t bar0_dlugosc;
    uint8_t hda_vmin;
    uint8_t hda_vmaj;
    uint16_t gcap;
    uint32_t gctl;
    uint16_t statests;
    uint16_t outpay;
    uint16_t wakeen;
    uint32_t intsts;
    uint8_t oss;
    uint8_t iss;
    uint8_t bss;
    uint8_t dma64;
    uint16_t initial_statests;
    uint16_t discovered_codec_mask;
    uint16_t responsive_codec_mask;
};

bool hda_wew_pobierz_stan_kontrolera(HdaWewStanKontrolera* out);

/*
 * Odczyt stanu Output Stream do celow diagnostycznych.
 */
struct HdaWewStanStrumienia {
    uint8_t numer;
    uint8_t stream_tag;
    uint16_t format_raw;
    uint32_t cbl;
    uint32_t lvi;
    uint32_t lpib;
    uint32_t ctl;
    uint32_t sts;
    uint8_t bcis;
    uint8_t fifoe;
    uint8_t dese;
    uint8_t fifordy;
    uint8_t run;
    uint32_t bdl_wpisow;
    uint32_t bdl_maska_fiz; /* zmaskowany */
    uint8_t bdl_uzywa_64;
};

bool hda_wew_pobierz_stan_strumienia(HdaWewStanStrumienia* out);

struct HdaWewStanTonu {
    uint64_t deadline_ns;
    uint32_t stop_reason; // HdaTonStopPowod w hda_diag_format.h
    uint8_t active;
    uint8_t run_after_stop; // rzeczywisty odczyt SDnCTL po ostatnim STOP
    uint64_t last_running_ns; // ostatni produkcyjny odczyt RUN=1 przed deadline
    uint64_t stopped_ns; // czas potwierdzonego RUN=0, nie czas obserwatora
};

void hda_wew_pobierz_stan_tonu(HdaWewStanTonu* out);

/*
 * Aktualna konfiguracja sciezki odtwarzania (stala QEMU lub odkryta).
 */
struct HdaWewSciezkaTestu {
    uint8_t cad;
    uint8_t konwerter_nid;
    uint8_t pin_nid;
    uint8_t routing_fizyczny; /* 0 = stala sciezka QEMU */
};

void hda_wew_pobierz_sciezke_testu(HdaWewSciezkaTestu* out);

/*
 * Rozszerzony generator tonow diagnostycznych.
 * tryb: 1=ton, 2=sweep, 3=lewy, 4=prawy, 5=oba
 * Zwraca true gdy stream wystartowal.
 */
bool hda_wew_test_start(uint32_t tryb,
                        uint32_t hz,
                        uint32_t czas_ms,
                        uint8_t kanaly);

void hda_wew_test_stop();

/* Czy testowy strumien gra. Auto-stop po czas_ms. */
bool hda_wew_test_tick(uint64_t teraz_ms);

/*
 * Aktywne sondowanie pojedynczego adresu kodeka (0..14).
 * Bounded work: wysyla dokladnie 1 zapytanie GetParameter VendorID.
 */
bool hda_wew_sonda_pojedynczego_kodeka(
    uint8_t cad,
    bool* out_odpowiada,
    uint32_t* out_vendor
);

/*
 * Aktywne sondowanie wszystkich adresow kodekow (0..14).
 * Zwraca maske bitowa responsive kodeków w *responsive_mask.
 * Nie modyfikuje routingu ani stanu kodeka.
 */
bool hda_wew_aktywne_sondowanie_kodekow(
    uint16_t* responsive_mask
);


uint32_t hda_wew_kontroler_odtwarzania();
