/*
 * Bursztyn OS - HDA diagnostyka: inkrementalny skan i migawka
 *
 * Kernel-owned. Kazdy krok skanu ma ograniczona liczbe transakcji verb.
 * Publikacja migawki jest atomowa (podwojny bufor + generacja).
 *
 * Nie wykonuj hda_diag_startuj_skan() na sciezce krytycznego bootu.
 */

#include "hda_diag.h"
#include "hda_wewnetrzny.h"
#include "hda.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

uint64_t czas_monotoniczny_ns();
bool hpet_dostepny();

namespace {

/* --------------------------------------------------------------------- */
/* Podwojny bufor migawki (publikacja atomowa)                            */
/* --------------------------------------------------------------------- */

HdaDiagSnapshot bufor_a;
HdaDiagSnapshot bufor_b;
HdaDiagSnapshot* opublikowana = nullptr;
uint32_t generacja_publikacji = 0;
uint32_t licznik_generacji = 0;

/* Bufor roboczy skanu (nie widoczny dla Ring3 az do publikacji). */
HdaDiagSnapshot skan_docelowy;

HdaDiagSkan stan_skan{};

/* --------------------------------------------------------------------- */
/* Stan testu tonu                                                        */
/* --------------------------------------------------------------------- */

HdaDiagStanTestu stan_test{};
uint64_t test_start_ms = 0;
uint64_t test_czas_ms = 0;
bool test_aktywny = false;

uint64_t teraz_ms_monotoniczny() {
    if (hpet_dostepny()) {
        return czas_monotoniczny_ns() / 1000000ULL;
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* Pomocnicze                                                             */
/* --------------------------------------------------------------------- */

void wyzeruj_snapshot(HdaDiagSnapshot* s) {
    if (!s) return;
    uint8_t* p = reinterpret_cast<uint8_t*>(s);
    for (size_t i = 0; i < sizeof(HdaDiagSnapshot); ++i) p[i] = 0;
    s->wersja = HDA_DIAG_WERSJA_ABI;
    s->rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSnapshot));
}

void skopiuj_snapshot(HdaDiagSnapshot* cel, const HdaDiagSnapshot* zrodlo) {
    if (!cel || !zrodlo) return;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(zrodlo);
    uint8_t* d = reinterpret_cast<uint8_t*>(cel);
    for (size_t i = 0; i < sizeof(HdaDiagSnapshot); ++i) d[i] = s[i];
}

bool odpowiedz_nie_poprawna(uint32_t r) {
    /*
     * Niektore implementacje zwracaja 0xFFFFFFFF / 0x00000000 dla
     * nieobslugiwanych verbów. Traktujemy to jako brak odpowiedzi.
     */
    return r == 0xFFFFFFFFU;
}

bool pobierz_parametr(uint8_t cad, uint8_t nid, uint8_t param, uint32_t* out) {
    if (!out) return false;
    const uint32_t cmd = hda_wew_verb12(cad, nid, 0xF00U, param);
    uint32_t resp = 0;
    if (!hda_wew_wyslij_immediate(cmd, &resp)) return false;
    if (odpowiedz_nie_poprawna(resp)) return false;
    *out = resp;
    return true;
}

bool pobierz_pin_config(uint8_t cad, uint8_t nid, uint32_t* out) {
    if (!out) return false;
    /* Get Configuration Default: verb 0xF1C (12-bit, payload 0). */
    const uint32_t cmd = hda_wew_verb12(cad, nid, 0xF1CU, 0);
    uint32_t resp = 0;
    if (!hda_wew_wyslij_immediate(cmd, &resp)) return false;
    if (odpowiedz_nie_poprawna(resp)) return false;
    *out = resp;
    return true;
}

bool pobierz_power_state(uint8_t cad, uint8_t nid, uint8_t* out) {
    if (!out) return false;
    /* Get Power State: verb 0xF05. */
    const uint32_t cmd = hda_wew_verb12(cad, nid, 0xF05U, 0);
    uint32_t resp = 0;
    if (!hda_wew_wyslij_immediate(cmd, &resp)) return false;
    if (odpowiedz_nie_poprawna(resp)) return false;
    *out = static_cast<uint8_t>(resp & 0x0FU);
    return true;
}

struct StanSkanuWew {
    uint8_t sonda_cad{0};
    uint16_t responsive_mask{0};
    uint8_t subnode_start{0};
    uint8_t subnode_count{0};
    uint8_t subnode_idx{0};
    uint8_t fg_licznik{0};
    uint8_t conn_idx{0};
    uint8_t conn_slowo_idx{0};
    uint32_t conn_slowa[4]{0};
};

static StanSkanuWew s_skan_wew{};

} // namespace

/* ========================================================================= */
/* Publiczne API                                                             */
/* ========================================================================= */

bool hda_diag_inicjalizuj() {
    wyzeruj_snapshot(&bufor_a);
    wyzeruj_snapshot(&bufor_b);
    wyzeruj_snapshot(&skan_docelowy);
    opublikowana = nullptr;
    generacja_publikacji = 0;
    licznik_generacji = 0;
    stan_skan = HdaDiagSkan{};
    stan_skan.wersja = HDA_DIAG_WERSJA_ABI;
    stan_skan.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSkan));
    stan_test.wersja = HDA_DIAG_WERSJA_ABI;
    stan_test.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStanTestu));
    s_skan_wew = StanSkanuWew{};
    return true;
}

bool hda_diag_startuj_skan() {
    // Rozpocznij nowa generacje skanu (jesli poprzedni trwal, oznacz go jako przestarzaly/nadpisany)
    stan_skan = HdaDiagSkan{};
    stan_skan.wersja = HDA_DIAG_WERSJA_ABI;
    stan_skan.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSkan));
    stan_skan.generacja_skanu = ++licznik_generacji;
    stan_skan.generacja_opublikowana = generacja_publikacji;
    stan_skan.migawka_dostepna = (opublikowana != nullptr) ? 1 : 0;
    stan_skan.aktywny = 1;
    stan_skan.gotowy = 0;
    stan_skan.przerwany = 0;
    stan_skan.blad = HDA_DIAG_OK;
    stan_skan.etap = HDA_DIAG_ETAP_KONTROLER;
    stan_skan.postep_0_1000 = 20;

    s_skan_wew = StanSkanuWew{};

    wyzeruj_snapshot(&skan_docelowy);
    skan_docelowy.generacja = stan_skan.generacja_skanu;
    skan_docelowy.etap = HDA_DIAG_ETAP_KONTROLER;
    skan_docelowy.flagi = HDA_DIAG_FLAGA_SKAN_W_TOKU;

    if (!hda_wew_kontroler_gotowy()) {
        /* Sprobuj zainicjalizowac kontroler do celow diagnostycznych. */
        extern bool inicjalizuj_hda();
        if (!inicjalizuj_hda()) {
            if (!hda_wew_kontroler_gotowy()) {
                stan_skan.etap = HDA_DIAG_ETAP_BLAD;
                stan_skan.blad = HDA_DIAG_BLAD_BRAK_KONTROLERA;
                stan_skan.aktywny = 0;
                skan_docelowy.etap = HDA_DIAG_ETAP_BLAD;
                skan_docelowy.blad = HDA_DIAG_BLAD_BRAK_KONTROLERA;
                skan_docelowy.flagi |= HDA_DIAG_FLAGA_BRAK_KONTROLERA;
                return true; /* skan zakonczony (bledem braku kontrolera) */
            }
        }
    }

    return true;
}

void hda_diag_anuluj_skan() {
    if (stan_skan.aktywny) {
        stan_skan.aktywny = 0;
        stan_skan.przerwany = 1;
        stan_skan.etap = HDA_DIAG_ETAP_BEZCZYNNY;
    }
}

const HdaDiagSnapshot* hda_diag_pobierz_opublikowana(uint32_t* out_generacja) {
    HdaDiagSnapshot* zrodlo = __atomic_load_n(&opublikowana, __ATOMIC_ACQUIRE);
    if (!zrodlo) return nullptr;
    if (out_generacja) {
        *out_generacja = __atomic_load_n(&generacja_publikacji, __ATOMIC_ACQUIRE);
    }
    return zrodlo;
}

bool hda_diag_krok_skanu() {
    if (!stan_skan.aktywny) {
        return true;
    }

    uint32_t budzet = HDA_DIAG_LIMIT_VERBOW_NA_KROK;
    uint32_t wykonane_slowa = 0;

    switch (stan_skan.etap) {
        case HDA_DIAG_ETAP_KONTROLER: {
            HdaWewStanKontrolera kc{};
            if (!hda_wew_pobierz_stan_kontrolera(&kc)) {
                stan_skan.etap = HDA_DIAG_ETAP_BLAD;
                stan_skan.blad = HDA_DIAG_BLAD_MMIO;
                stan_skan.aktywny = 0;
                break;
            }
            HdaDiagKontroler& c = skan_docelowy.kontrolery[0];
            c.wersja = HDA_DIAG_WERSJA_ABI;
            c.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagKontroler));
            c.bus = kc.bus;
            c.slot = kc.slot;
            c.func = kc.func;
            c.vendor_id = kc.vendor_id;
            c.device_id = kc.device_id;
            c.revision = kc.revision;
            c.command = kc.command;
            c.bar0 = kc.bar0;
            c.bar0_dlugosc = kc.bar0_dlugosc;
            c.hda_vmin = kc.hda_vmin;
            c.hda_vmaj = kc.hda_vmaj;
            c.gcap = kc.gcap;
            c.gctl = kc.gctl;
            c.statests = kc.statests;
            c.outpay = kc.outpay;
            c.wakeen = kc.wakeen;
            c.intsts = kc.intsts;
            c.oss = kc.oss;
            c.iss = kc.iss;
            c.bss = kc.bss;
            c.dma64 = kc.dma64;
            c.transport = 1; /* Immediate Command */
            c.flagi = HDA_DIAG_FLAGA_TRANSPORT_IC;
            c.blad = HDA_DIAG_OK;
            c.initial_statests = kc.initial_statests;
            c.discovered_codec_mask = kc.discovered_codec_mask;
            c.responsive_codec_mask = 0;
            skan_docelowy.liczba_kontrolerow = 1;
            skan_docelowy.flagi |= HDA_DIAG_FLAGA_TRANSPORT_IC;

            s_skan_wew.sonda_cad = 0;
            s_skan_wew.responsive_mask = 0;
            stan_skan.biezacy_cad = 0;
            stan_skan.biezacy_nid = 0;
            stan_skan.etap = HDA_DIAG_ETAP_SONDOWANIE_KODEKOW;
            stan_skan.postep_0_1000 = 30;
            break;
        }

        case HDA_DIAG_ETAP_SONDOWANIE_KODEKOW: {
            const uint8_t cad = s_skan_wew.sonda_cad;
            stan_skan.biezacy_cad = cad;
            stan_skan.biezacy_nid = 0;

            bool odp = false;
            uint32_t vendor = 0;
            hda_wew_sonda_pojedynczego_kodeka(cad, &odp, &vendor);
            wykonane_slowa += 1;
            --budzet;

            if (odp) {
                uint8_t ile = skan_docelowy.kontrolery[0].liczba_kodekow;
                if (ile < HDA_DIAG_MAKS_KODEKOW) {
                    HdaDiagKodek& k = skan_docelowy.kodeki[0][ile];
                    k.wersja = HDA_DIAG_WERSJA_ABI;
                    k.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagKodek));
                    k.cad = cad;
                    k.odpowiada = 1;
                    k.vendor_id = hda_vendor_id(vendor);
                    k.device_id = hda_device_id(vendor);
                    skan_docelowy.kontrolery[0].liczba_kodekow = ile + 1;
                }
                s_skan_wew.responsive_mask = static_cast<uint16_t>(s_skan_wew.responsive_mask | (1U << cad));
            }

            s_skan_wew.sonda_cad++;
            stan_skan.postep_0_1000 = 30 + (s_skan_wew.sonda_cad * 120) / 15;

            if (s_skan_wew.sonda_cad >= 15U) {
                skan_docelowy.kontrolery[0].responsive_codec_mask = s_skan_wew.responsive_mask;
                if (skan_docelowy.kontrolery[0].liczba_kodekow == 0) {
                    skan_docelowy.flagi |= HDA_DIAG_FLAGA_SKAN_BLAD;
                    stan_skan.blad = HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA;
                    stan_skan.etap = HDA_DIAG_ETAP_BLAD;
                    stan_skan.aktywny = 0;
                    break;
                }
                stan_skan.kodek_idx = 0;
                stan_skan.etap = HDA_DIAG_ETAP_PARAMETRY_KORZENIA;
            }
            break;
        }

        case HDA_DIAG_ETAP_PARAMETRY_KORZENIA: {
            const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
            if (stan_skan.kodek_idx >= nk) {
                stan_skan.kodek_idx = 0;
                stan_skan.fg_idx = 0;
                stan_skan.widget_idx = 0;
                stan_skan.etap = HDA_DIAG_ETAP_WIDGET_ODKRYCIE;
                break;
            }

            HdaDiagKodek& k = skan_docelowy.kodeki[0][stan_skan.kodek_idx];
            const uint8_t cad = k.cad;
            stan_skan.biezacy_cad = cad;
            stan_skan.biezacy_nid = 0;

            uint32_t vendor = 0;
            if (!pobierz_parametr(cad, 0, HDA_PAR_VENDOR_ID, &vendor)) {
                k.odpowiada = 0;
                k.blad = HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA;
                stan_skan.kodek_idx++;
                wykonane_slowa += 1;
                break;
            }
            k.vendor_id = hda_vendor_id(vendor);
            k.device_id = hda_device_id(vendor);
            wykonane_slowa += 1;

            uint32_t rev = 0;
            if (pobierz_parametr(cad, 0, HDA_PAR_REV_ID, &rev)) {
                k.revision_id = rev;
            }
            wykonane_slowa += 1;

            uint32_t sub = 0;
            if (!pobierz_parametr(cad, 0, HDA_PAR_SUB_NODE_COUNT, &sub)) {
                k.blad = HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
                stan_skan.kodek_idx++;
                wykonane_slowa += 1;
                break;
            }
            k.root_sub_count = sub;
            wykonane_slowa += 1;

            const uint8_t start = hda_sub_start(sub);
            const uint8_t count = hda_sub_count(sub);
            if (!hda_zakres_wezlow_poprawny(start, count)) {
                k.blad = HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;
                stan_skan.kodek_idx++;
                break;
            }

            s_skan_wew.subnode_start = start;
            s_skan_wew.subnode_count = count;
            s_skan_wew.subnode_idx = 0;
            s_skan_wew.fg_licznik = 0;
            stan_skan.etap = HDA_DIAG_ETAP_GRUPY_FUNKCYJNE;
            stan_skan.postep_0_1000 = 200;
            break;
        }

        case HDA_DIAG_ETAP_GRUPY_FUNKCYJNE: {
            HdaDiagKodek& k = skan_docelowy.kodeki[0][stan_skan.kodek_idx];
            const uint8_t cad = k.cad;

            while (s_skan_wew.subnode_idx < s_skan_wew.subnode_count &&
                   s_skan_wew.fg_licznik < HDA_DIAG_MAKS_FG && budzet > 0) {
                const uint8_t nid = static_cast<uint8_t>(s_skan_wew.subnode_start + s_skan_wew.subnode_idx);
                stan_skan.biezacy_cad = cad;
                stan_skan.biezacy_nid = nid;

                uint32_t fgt = 0;
                const bool ok = pobierz_parametr(cad, nid, HDA_PAR_FGROUP_TYPE, &fgt);
                wykonane_slowa += 1;
                --budzet;

                if (ok && hda_fg_jest_audio(fgt)) {
                    HdaDiagFG& g = skan_docelowy.fg[0][stan_skan.kodek_idx][s_skan_wew.fg_licznik];
                    g.wersja = HDA_DIAG_WERSJA_ABI;
                    g.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagFG));
                    g.nid = nid;
                    g.typ = hda_fg_typ(fgt);
                    g.audio = 1;
                    stan_skan.etap = HDA_DIAG_ETAP_PARAMETRY_AFG;
                    break;
                }
                s_skan_wew.subnode_idx++;
            }

            if (stan_skan.etap == HDA_DIAG_ETAP_PARAMETRY_AFG) {
                break;
            }

            if (s_skan_wew.subnode_idx >= s_skan_wew.subnode_count || s_skan_wew.fg_licznik >= HDA_DIAG_MAKS_FG) {
                k.liczba_fg = s_skan_wew.fg_licznik;
                if (k.liczba_fg == 0) {
                    k.blad = HDA_DIAG_BLAD_BRAK_AFG;
                }
                stan_skan.kodek_idx++;
                const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
                if (stan_skan.kodek_idx < nk) {
                    stan_skan.etap = HDA_DIAG_ETAP_PARAMETRY_KORZENIA;
                } else {
                    stan_skan.kodek_idx = 0;
                    stan_skan.fg_idx = 0;
                    stan_skan.widget_idx = 0;
                    stan_skan.etap = HDA_DIAG_ETAP_WIDGET_ODKRYCIE;
                    stan_skan.postep_0_1000 = 300;
                }
            }
            break;
        }

        case HDA_DIAG_ETAP_PARAMETRY_AFG: {
            HdaDiagKodek& k = skan_docelowy.kodeki[0][stan_skan.kodek_idx];
            HdaDiagFG& g = skan_docelowy.fg[0][stan_skan.kodek_idx][s_skan_wew.fg_licznik];
            const uint8_t cad = k.cad;
            const uint8_t nid = g.nid;

            uint32_t fgc = 0;
            if (pobierz_parametr(cad, nid, HDA_PAR_AFG_CAPS, &fgc)) {
                g.fg_caps = fgc;
            }
            wykonane_slowa += 1;

            uint32_t subg = 0;
            if (pobierz_parametr(cad, nid, HDA_PAR_SUB_NODE_COUNT, &subg)) {
                g.sub_start = hda_sub_start(subg);
                g.sub_count = hda_sub_count(subg);
                if (hda_zakres_wezlow_poprawny(static_cast<uint8_t>(g.sub_start), static_cast<uint8_t>(g.sub_count))) {
                    g.widget_start = g.sub_start;
                    g.widget_count = g.sub_count;
                } else {
                    g.blad = HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;
                }
            } else {
                g.blad = HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
            }
            wykonane_slowa += 1;

            s_skan_wew.fg_licznik++;
            s_skan_wew.subnode_idx++;
            stan_skan.etap = HDA_DIAG_ETAP_GRUPY_FUNKCYJNE;
            break;
        }

        case HDA_DIAG_ETAP_WIDGET_ODKRYCIE: {
            const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
            if (stan_skan.kodek_idx >= nk) {
                stan_skan.etap = HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK;
                break;
            }
            const uint32_t nf = skan_docelowy.kodeki[0][stan_skan.kodek_idx].liczba_fg;
            if (stan_skan.fg_idx >= nf) {
                stan_skan.kodek_idx++;
                stan_skan.fg_idx = 0;
                stan_skan.widget_idx = 0;
                break;
            }
            HdaDiagFG& g = skan_docelowy.fg[0][stan_skan.kodek_idx][stan_skan.fg_idx];
            if (g.widget_count == 0 || stan_skan.widget_idx >= g.widget_count) {
                stan_skan.fg_idx++;
                stan_skan.widget_idx = 0;
                break;
            }

            const uint8_t cad = skan_docelowy.kodeki[0][stan_skan.kodek_idx].cad;
            const uint8_t nid = static_cast<uint8_t>(g.widget_start + stan_skan.widget_idx);
            stan_skan.biezacy_cad = cad;
            stan_skan.biezacy_nid = nid;

            if (stan_skan.widget_idx >= HDA_DIAG_MAKS_WIDGETOW) {
                skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
                stan_skan.widget_idx++;
                break;
            }

            HdaDiagWidget& w = skan_docelowy.widgety[0][stan_skan.kodek_idx][stan_skan.fg_idx][stan_skan.widget_idx];
            w.wersja = HDA_DIAG_WERSJA_ABI;
            w.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagWidget));
            w.nid = nid;

            uint32_t awcap = 0;
            if (!pobierz_parametr(cad, nid, HDA_PAR_WIDGET_CAP, &awcap)) {
                w.flagi = 2;
                w.blad = HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
                stan_skan.widget_idx++;
                wykonane_slowa += 1;
                break;
            }
            w.awcap = awcap;
            w.typ = hda_wcap_typ(awcap);
            wykonane_slowa += 1;

            stan_skan.etap = HDA_DIAG_ETAP_WIDGET_PARAMETRY;
            break;
        }

        case HDA_DIAG_ETAP_WIDGET_PARAMETRY: {
            HdaDiagFG& g = skan_docelowy.fg[0][stan_skan.kodek_idx][stan_skan.fg_idx];
            HdaDiagWidget& w = skan_docelowy.widgety[0][stan_skan.kodek_idx][stan_skan.fg_idx][stan_skan.widget_idx];
            const uint8_t cad = skan_docelowy.kodeki[0][stan_skan.kodek_idx].cad;
            const uint8_t nid = w.nid;

            if (hda_typ_wolno_pin_caps(w.typ)) {
                uint32_t pc = 0;
                if (pobierz_parametr(cad, nid, HDA_PAR_PIN_CAP, &pc)) w.pin_caps = pc;
                uint32_t dc = 0;
                if (pobierz_pin_config(cad, nid, &dc)) w.default_config = dc;
                wykonane_slowa += 2;
            }
            if (hda_typ_wolno_pcm_caps(w.typ)) {
                uint32_t ps = 0;
                if (pobierz_parametr(cad, nid, HDA_PAR_PCM_SIZE_RATE, &ps)) w.pcm_size_rate = ps;
                uint32_t sf = 0;
                if (pobierz_parametr(cad, nid, HDA_PAR_STREAM_FORMATS, &sf)) w.stream_formats = sf;
                wykonane_slowa += 2;
            }
            if (hda_typ_wolno_amp_out(w.awcap)) {
                uint32_t ac = 0;
                if (pobierz_parametr(cad, nid, HDA_PAR_AMP_OUT_CAP, &ac)) w.out_amp_caps = ac;
                wykonane_slowa += 1;
            }
            if (hda_typ_wolno_amp_in(w.awcap)) {
                uint32_t ac = 0;
                if (pobierz_parametr(cad, nid, HDA_PAR_AMP_IN_CAP, &ac)) w.in_amp_caps = ac;
                wykonane_slowa += 1;
            }
            uint8_t ps = 0;
            if (pobierz_power_state(cad, nid, &ps)) w.power_current = ps;
            uint32_t psc = 0;
            if (pobierz_parametr(cad, nid, HDA_PAR_POWER_STATES, &psc)) w.power_states = psc;
            wykonane_slowa += 2;

            if (hda_typ_wolno_conn_list(w.awcap)) {
                uint32_t cl = 0;
                if (pobierz_parametr(cad, nid, HDA_PAR_CONNLIST_LEN, &cl)) {
                    w.connlist_len_raw = cl;
                    const uint8_t ile = hda_connlist_liczba(cl);
                    const bool lf = hda_connlist_long_form(cl);
                    w.conn_long_form = lf ? 1 : 0;
                    if (ile > 0 && ile <= HDA_DIAG_MAKS_POLACZEN) {
                        s_skan_wew.conn_idx = 0;
                        s_skan_wew.conn_slowo_idx = 0;
                        for (int i = 0; i < 4; ++i) s_skan_wew.conn_slowa[i] = 0;
                        stan_skan.etap = HDA_DIAG_ETAP_POLACZENIA;
                        wykonane_slowa += 1;
                        break;
                    } else if (ile > HDA_DIAG_MAKS_POLACZEN) {
                        w.flagi = 1;
                        skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
                    }
                }
                wykonane_slowa += 1;
            }

            stan_skan.widget_idx++;
            stan_skan.postep_0_1000 = 300 + (stan_skan.widget_idx * 500) / (g.widget_count ? g.widget_count : 1);
            stan_skan.etap = HDA_DIAG_ETAP_WIDGET_ODKRYCIE;
            break;
        }

        case HDA_DIAG_ETAP_POLACZENIA: {
            HdaDiagFG& g = skan_docelowy.fg[0][stan_skan.kodek_idx][stan_skan.fg_idx];
            HdaDiagWidget& w = skan_docelowy.widgety[0][stan_skan.kodek_idx][stan_skan.fg_idx][stan_skan.widget_idx];
            const uint8_t cad = skan_docelowy.kodeki[0][stan_skan.kodek_idx].cad;
            const uint8_t nid = w.nid;
            const uint8_t ile = hda_connlist_liczba(w.connlist_len_raw);
            const bool lf = w.conn_long_form != 0;
            const uint32_t na_slowo = lf ? 2U : 4U;

            while (s_skan_wew.conn_idx < ile && s_skan_wew.conn_slowo_idx < 4 && budzet > 0) {
                const uint32_t cmd = hda_wew_verb12(cad, nid, 0xF02U, s_skan_wew.conn_idx);
                uint32_t resp = 0;
                if (!hda_wew_wyslij_immediate(cmd, &resp) || odpowiedz_nie_poprawna(resp)) {
                    w.flagi = 2;
                    w.blad = HDA_DIAG_BLAD_LISTA_POLACZEN;
                    break;
                }
                s_skan_wew.conn_slowa[s_skan_wew.conn_slowo_idx++] = resp;
                s_skan_wew.conn_idx = static_cast<uint8_t>(s_skan_wew.conn_idx + na_slowo);
                wykonane_slowa += 1;
                --budzet;
            }

            if (s_skan_wew.conn_idx >= ile || s_skan_wew.conn_slowo_idx >= 4 || w.flagi == 2) {
                if (w.flagi != 2) {
                    uint32_t out_n = 0;
                    bool trunc = false;
                    uint8_t tmp[HDA_DIAG_MAKS_POLACZEN] = {0};
                    if (hda_rozszerz_liste_polaczen(s_skan_wew.conn_slowa, s_skan_wew.conn_slowo_idx, lf, ile, tmp,
                                                    HDA_DIAG_MAKS_POLACZEN, &out_n, &trunc)) {
                        w.liczba_polaczen = static_cast<uint8_t>(out_n);
                        for (uint32_t ci = 0; ci < out_n; ++ci) {
                            w.polaczenia[ci] = tmp[ci];
                            w.polaczenia_surowe[ci] = tmp[ci];
                        }
                        if (trunc) skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
                    } else {
                        w.blad = HDA_DIAG_BLAD_LISTA_POLACZEN;
                    }
                }
                stan_skan.widget_idx++;
                stan_skan.postep_0_1000 = 300 + (stan_skan.widget_idx * 500) / (g.widget_count ? g.widget_count : 1);
                stan_skan.etap = HDA_DIAG_ETAP_WIDGET_ODKRYCIE;
            }
            break;
        }

        case HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK: {
            uint32_t ile_sciezek = 0;
            bool trunc = false;
            const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
            for (uint32_t ki = 0; ki < nk; ++ki) {
                const uint32_t nf = skan_docelowy.kodeki[0][ki].liczba_fg;
                for (uint32_t fi = 0; fi < nf; ++fi) {
                    HdaDiagWidget lokalne[HDA_DIAG_MAKS_WIDGETOW];
                    uint32_t n = 0;
                    for (uint32_t wi = 0; wi < HDA_DIAG_MAKS_WIDGETOW; ++wi) {
                        const HdaDiagWidget& src = skan_docelowy.widgety[0][ki][fi][wi];
                        if (src.awcap == 0 && src.nid == 0) continue;
                        if (n >= HDA_DIAG_MAKS_WIDGETOW) break;
                        lokalne[n++] = src;
                    }
                    if (n == 0) continue;
                    HdaDiagSciezka tmp[HDA_DIAG_MAKS_SCIEZEK];
                    uint32_t ns = 0;
                    bool obc = false;
                    if (!hda_zbuduj_sciezki(lokalne, n, tmp, HDA_DIAG_MAKS_SCIEZEK, &ns, &obc)) {
                        continue;
                    }
                    for (uint32_t si = 0; si < ns; ++si) {
                        if (ile_sciezek >= HDA_DIAG_MAKS_SCIEZEK) {
                            trunc = true;
                            break;
                        }
                        skan_docelowy.sciezki[ile_sciezek] = tmp[si];
                        ++ile_sciezek;
                    }
                    if (obc) trunc = true;
                }
            }
            skan_docelowy.liczba_sciezek = ile_sciezek;
            if (trunc) skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
            if (ile_sciezek == 0) {
                stan_skan.blad = HDA_DIAG_BLAD_BRAK_WYJSC;
            } else {
                stan_skan.blad = HDA_DIAG_OK;
            }
            stan_skan.postep_0_1000 = 950;
            stan_skan.etap = HDA_DIAG_ETAP_PUBLIKACJA;
            break;
        }

        case HDA_DIAG_ETAP_PUBLIKACJA: {
            HdaDiagSnapshot* cel = (opublikowana == &bufor_a) ? &bufor_b : &bufor_a;
            skan_docelowy.wersja = HDA_DIAG_WERSJA_ABI;
            skan_docelowy.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSnapshot));
            skan_docelowy.generacja = stan_skan.generacja_skanu;
            skan_docelowy.etap = HDA_DIAG_ETAP_GOTOWY;
            skan_docelowy.blad = stan_skan.blad;
            skan_docelowy.postep_0_1000 = 1000;
            skan_docelowy.flagi &= ~HDA_DIAG_FLAGA_SKAN_W_TOKU;
            skan_docelowy.flagi |= HDA_DIAG_FLAGA_SKAN_GOTOWY;

            HdaWewStanStrumienia ws{};
            if (hda_wew_pobierz_stan_strumienia(&ws)) {
                HdaDiagStrumien& s = skan_docelowy.strumien;
                s.wersja = HDA_DIAG_WERSJA_ABI;
                s.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStrumien));
                s.numer = ws.numer;
                s.stream_tag = ws.stream_tag;
                s.format_raw = ws.format_raw;
                s.cbl = ws.cbl;
                s.lvi = ws.lvi;
                s.lpib = ws.lpib;
                s.ctl = ws.ctl;
                s.sts = ws.sts;
                s.bcis = ws.bcis;
                s.fifoe = ws.fifoe;
                s.dese = ws.dese;
                s.fifordy = ws.fifordy;
                s.run = ws.run;
                s.bdl.wersja = HDA_DIAG_WERSJA_ABI;
                s.bdl.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagBdlInfo));
                s.bdl.liczba_wpisow = ws.bdl_wpisow;
                s.bdl.maska_fizycznego = ws.bdl_maska_fiz;
                s.bdl.flagi = ws.bdl_uzywa_64;
                if (ws.run) s.stan = HDA_STRUMIEN_GRA;
                else if (ws.ctl != 0) s.stan = HDA_STRUMIEN_SKONFIGUROWANY;
                else s.stan = HDA_STRUMIEN_BEZCZYNNY;
            }

            skan_docelowy.test = stan_test;
            HdaWewSciezkaTestu sp{};
            hda_wew_pobierz_sciezke_testu(&sp);
            skan_docelowy.test.kontroler_idx = 0;
            skan_docelowy.test.codec_cad = sp.cad;
            skan_docelowy.test.konwerter_nid = sp.konwerter_nid;
            skan_docelowy.test.pin_nid = sp.pin_nid;
            skan_docelowy.test.routing_fizyczny = sp.routing_fizyczny;

            skopiuj_snapshot(cel, &skan_docelowy);
            __atomic_store_n(&opublikowana, cel, __ATOMIC_RELEASE);
            __atomic_store_n(&generacja_publikacji, skan_docelowy.generacja, __ATOMIC_RELEASE);

            stan_skan.generacja_opublikowana = skan_docelowy.generacja;
            stan_skan.migawka_dostepna = 1;
            stan_skan.aktywny = 0;
            stan_skan.gotowy = 1;
            stan_skan.etap = HDA_DIAG_ETAP_GOTOWY;
            stan_skan.postep_0_1000 = 1000;
            return true;
        }

        case HDA_DIAG_ETAP_GOTOWY:
        case HDA_DIAG_ETAP_BLAD:
        default:
            return true;
    }

    if (stan_skan.etap == HDA_DIAG_ETAP_BLAD) {
        HdaDiagSnapshot* cel = (opublikowana == &bufor_a) ? &bufor_b : &bufor_a;
        skan_docelowy.wersja = HDA_DIAG_WERSJA_ABI;
        skan_docelowy.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSnapshot));
        skan_docelowy.generacja = stan_skan.generacja_skanu;
        skan_docelowy.etap = HDA_DIAG_ETAP_BLAD;
        skan_docelowy.blad = stan_skan.blad;
        skan_docelowy.flagi &= ~HDA_DIAG_FLAGA_SKAN_W_TOKU;
        skan_docelowy.flagi |= HDA_DIAG_FLAGA_SKAN_BLAD;

        skopiuj_snapshot(cel, &skan_docelowy);
        __atomic_store_n(&opublikowana, cel, __ATOMIC_RELEASE);
        __atomic_store_n(&generacja_publikacji, skan_docelowy.generacja, __ATOMIC_RELEASE);

        stan_skan.generacja_opublikowana = skan_docelowy.generacja;
        stan_skan.migawka_dostepna = 1;
        stan_skan.aktywny = 0;
        stan_skan.gotowy = 0;
        return true;
    }

    stan_skan.slow_w_kroku = wykonane_slowa;
    return false;
}

void hda_diag_pobierz_stan(HdaDiagSkan* out) {
    if (!out) return;
    *out = stan_skan;
    out->wersja = HDA_DIAG_WERSJA_ABI;
    out->rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSkan));
    out->migawka_dostepna = (__atomic_load_n(&opublikowana, __ATOMIC_ACQUIRE) != nullptr) ? 1 : 0;
    out->generacja_opublikowana = __atomic_load_n(&generacja_publikacji, __ATOMIC_ACQUIRE);
}


bool hda_diag_pobierz_snapshot(
    HdaDiagSnapshot* out,
    uint32_t out_maks,
    uint32_t* zapisano,
    uint32_t* generacja
) {
    if (!out || !zapisano || out_maks < sizeof(HdaDiagSnapshot)) {
        return false;
    }
    HdaDiagSnapshot* zrodlo = __atomic_load_n(&opublikowana, __ATOMIC_ACQUIRE);
    if (!zrodlo) {
        return false;
    }
    skopiuj_snapshot(out, zrodlo);
    *zapisano = static_cast<uint32_t>(sizeof(HdaDiagSnapshot));
    if (generacja) {
        *generacja = __atomic_load_n(&generacja_publikacji, __ATOMIC_ACQUIRE);
    }
    return true;
}

bool hda_diag_pobierz_strumien(HdaDiagStrumien* out) {
    if (!out) return false;
    HdaDiagSnapshot* zrodlo = __atomic_load_n(&opublikowana, __ATOMIC_ACQUIRE);
    if (!zrodlo) {
        /* Probe live. */
        HdaWewStanStrumienia ws{};
        if (!hda_wew_pobierz_stan_strumienia(&ws)) return false;
        out->wersja = HDA_DIAG_WERSJA_ABI;
        out->rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStrumien));
        out->numer = ws.numer;
        out->stream_tag = ws.stream_tag;
        out->format_raw = ws.format_raw;
        out->cbl = ws.cbl;
        out->lvi = ws.lvi;
        out->lpib = ws.lpib;
        out->ctl = ws.ctl;
        out->sts = ws.sts;
        out->bcis = ws.bcis;
        out->fifoe = ws.fifoe;
        out->dese = ws.dese;
        out->fifordy = ws.fifordy;
        out->run = ws.run;
        return true;
    }
    *out = zrodlo->strumien;
    return true;
}

bool hda_diag_pobierz_test(HdaDiagStanTestu* out) {
    if (!out) return false;
    *out = stan_test;
    if (test_aktywny) {
        const uint64_t t = teraz_ms_monotoniczny();
        const uint64_t uplynelo = t >= test_start_ms ? t - test_start_ms : 0;
        const uint64_t pozostalo =
            uplynelo < test_czas_ms ? test_czas_ms - uplynelo : 0;
        out->pozostalo_ms = static_cast<uint32_t>(pozostalo);
        out->aktywny = 1;
    } else {
        out->pozostalo_ms = 0;
        out->aktywny = 0;
    }
    return true;
}

bool hda_diag_test_start(
    uint32_t tryb,
    uint32_t czestotliwosc_hz,
    uint32_t czas_ms,
    uint8_t kanaly
) {
    hda_diag_test_stop();

    if (tryb == HDA_TEST_BRAK) return false;
    if (czas_ms == 0 || czas_ms > HDA_API_MAX_CZAS_MS) return false;
    if (tryb == HDA_TEST_TON || tryb == HDA_TEST_LEWY ||
        tryb == HDA_TEST_PRAWY || tryb == HDA_TEST_OBA) {
        if (!hda_api_czestotliwosc_poprawna(czestotliwosc_hz)) return false;
    }
    if (tryb == HDA_TEST_SWEEP) {
        czestotliwosc_hz = HDA_TEST_SWEEP_MIN_HZ;
    }
    if (kanaly == 0) kanaly = 3;

    uint8_t kan = kanaly;
    if (tryb == HDA_TEST_LEWY) kan = 1;
    if (tryb == HDA_TEST_PRAWY) kan = 2;
    if (tryb == HDA_TEST_OBA) kan = 3;

    if (!hda_wew_test_start(tryb, czestotliwosc_hz, czas_ms, kan)) {
        stan_test.ostatni_blad = HDA_DIAG_BLAD_STRUMIEN;
        return false;
    }

    stan_test.wersja = HDA_DIAG_WERSJA_ABI;
    stan_test.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStanTestu));
    stan_test.tryb = tryb;
    stan_test.czestotliwosc_hz = czestotliwosc_hz;
    stan_test.czas_ms = czas_ms;
    stan_test.pozostalo_ms = czas_ms;
    stan_test.kanaly = kan;
    stan_test.aktywny = 1;
    stan_test.ostatni_blad = HDA_DIAG_OK;

    HdaWewSciezkaTestu sp{};
    hda_wew_pobierz_sciezke_testu(&sp);
    stan_test.kontroler_idx = 0;
    stan_test.codec_cad = sp.cad;
    stan_test.konwerter_nid = sp.konwerter_nid;
    stan_test.pin_nid = sp.pin_nid;
    stan_test.routing_fizyczny = sp.routing_fizyczny;

    test_aktywny = true;
    test_start_ms = teraz_ms_monotoniczny();
    test_czas_ms = czas_ms;
    return true;
}

void hda_diag_test_stop() {
    hda_wew_test_stop();
    test_aktywny = false;
    stan_test.aktywny = 0;
    stan_test.pozostalo_ms = 0;
    stan_test.tryb = HDA_TEST_BRAK;
}

bool hda_diag_test_tick(uint64_t teraz_ms) {
    if (!hda_wew_test_tick(teraz_ms)) {
        if (test_aktywny) {
            hda_diag_test_stop();
        }
        return false;
    }
    if (!test_aktywny) return false;
    const uint64_t uplynelo =
        teraz_ms >= test_start_ms ? teraz_ms - test_start_ms : 0;
    if (uplynelo >= test_czas_ms) {
        hda_diag_test_stop();
        return false;
    }
    stan_test.pozostalo_ms =
        static_cast<uint32_t>(test_czas_ms - uplynelo);
    return true;
}

void hda_diag_pobierz_sciezke_testu(
    uint8_t* kontroler,
    uint8_t* cad,
    uint8_t* konwerter,
    uint8_t* pin,
    uint8_t* routing_fizyczny
) {
    HdaWewSciezkaTestu sp{};
    hda_wew_pobierz_sciezke_testu(&sp);
    if (kontroler) *kontroler = 0;
    if (cad) *cad = sp.cad;
    if (konwerter) *konwerter = sp.konwerter_nid;
    if (pin) *pin = sp.pin_nid;
    if (routing_fizyczny) *routing_fizyczny = sp.routing_fizyczny;
}

bool hda_diag_wyslij_verb(uint32_t komenda, uint32_t* odpowiedz) {
    return hda_wew_wyslij_immediate(komenda, odpowiedz);
}

uint32_t hda_diag_verb12(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t payload) {
    return hda_wew_verb12(cad, nid, verb, payload);
}

uint32_t hda_diag_verb4(uint8_t cad, uint8_t nid, uint8_t verb, uint16_t payload) {
    return hda_wew_verb4(cad, nid, verb, payload);
}
