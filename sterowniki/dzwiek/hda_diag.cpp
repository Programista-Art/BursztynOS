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
    stan_test.wersja = HDA_DIAG_WERSJA_ABI;
    stan_test.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStanTestu));
    return true;
}

bool hda_diag_startuj_skan() {
    if (stan_skan.aktywny) {
        return false; /* juz trwa */
    }
    wyzeruj_snapshot(&skan_docelowy);
    stan_skan = HdaDiagSkan{};
    stan_skan.aktywny = 1;
    stan_skan.etap = HDA_DIAG_ETAP_KONTROLER;
    skan_docelowy.etap = HDA_DIAG_ETAP_KONTROLER;
    skan_docelowy.flagi = HDA_DIAG_FLAGA_SKAN_W_TOKU;

    if (!hda_wew_kontroler_gotowy()) {
        /* Sprobuj zainicjalizowac kontroler do celow diagnostycznych.
         * inicjalizuj_hda() jest juz bezpieczna do ponownego wywolania. */
        extern bool inicjalizuj_hda();
        if (!inicjalizuj_hda()) {
            /* Kontroler moze nie miec obslugiwanego kodeka (np. Realtek),
             * ale sam kontroler PCI moze byc widoczny. Kontynuujemy skan
             * rejestrow kontrolera nawet gdy playback nie jest gotowy. */
            if (!hda_wew_kontroler_gotowy()) {
                stan_skan.etap = HDA_DIAG_ETAP_BLAD;
                stan_skan.blad = HDA_DIAG_BLAD_BRAK_KONTROLERA;
                stan_skan.aktywny = 0;
                skan_docelowy.etap = HDA_DIAG_ETAP_BLAD;
                skan_docelowy.blad = HDA_DIAG_BLAD_BRAK_KONTROLERA;
                skan_docelowy.flagi |= HDA_DIAG_FLAGA_BRAK_KONTROLERA;
                return true; /* skan zakonczony (błędem) */
            }
        }
    }

    stan_skan.postep_0_1000 = 10;
    return true;
}

bool hda_diag_krok_skanu() {
    if (!stan_skan.aktywny) {
        return true;
    }

    uint32_t budzet = HDA_DIAG_SLOW_NA_KROK;

    switch (stan_skan.etap) {
        case HDA_DIAG_ETAP_KONTROLER: {
            HdaWewStanKontrolera kc{};
            if (!hda_wew_pobierz_stan_kontrolera(&kc)) {
                stan_skan.etap = HDA_DIAG_ETAP_BLAD;
                stan_skan.blad = HDA_DIAG_BLAD_MMIO;
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
            skan_docelowy.liczba_kontrolerow = 1;
            skan_docelowy.flagi |= HDA_DIAG_FLAGA_TRANSPORT_IC;

            /* STATESTS: bity 14..0 = obecnosc kodeka CAD 0..14. */
            const uint16_t states = static_cast<uint16_t>(kc.statests & 0x7FFFU);
            uint8_t ile = 0;
            for (uint8_t cad = 0; cad < 15 && ile < HDA_DIAG_MAKS_KODEKOW; ++cad) {
                if ((states & (1U << cad)) == 0) continue;
                HdaDiagKodek& k = skan_docelowy.kodeki[0][ile];
                k.wersja = HDA_DIAG_WERSJA_ABI;
                k.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagKodek));
                k.cad = cad;
                k.odpowiada = 1;
                ++ile;
            }
            c.liczba_kodekow = ile;
            if (ile == 0) {
                /* Brak bitow w STATESTS - oznaczamy brak kodeka. */
                skan_docelowy.flagi |= HDA_DIAG_FLAGA_SKAN_BLAD;
                stan_skan.blad = HDA_DIAG_BLAD_BRAK_KODEKA;
            }
            stan_skan.etap = HDA_DIAG_ETAP_KODEK;
            stan_skan.kodek_idx = 0;
            stan_skan.postep_0_1000 = 200;
            --budzet;
            break;
        }

        case HDA_DIAG_ETAP_KODEK: {
            if (skan_docelowy.liczba_kontrolerow == 0 ||
                skan_docelowy.kontrolery[0].liczba_kodekow == 0) {
                stan_skan.etap = HDA_DIAG_ETAP_SCIEZKI;
                break;
            }
            const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
            while (stan_skan.kodek_idx < nk && budzet > 0) {
                HdaDiagKodek& k = skan_docelowy.kodeki[0][stan_skan.kodek_idx];
                const uint8_t cad = k.cad;
                uint32_t vendor = 0;
                if (!pobierz_parametr(cad, 0, HDA_PAR_VENDOR_ID, &vendor)) {
                    k.odpowiada = 0;
                    k.blad = HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA;
                    ++stan_skan.kodek_idx;
                    --budzet;
                    continue;
                }
                k.vendor_id = hda_vendor_id(vendor);
                k.device_id = hda_device_id(vendor);

                uint32_t rev = 0;
                if (pobierz_parametr(cad, 0, HDA_PAR_REV_ID, &rev)) {
                    k.revision_id = rev;
                }

                uint32_t sub = 0;
                if (!pobierz_parametr(cad, 0, HDA_PAR_SUB_NODE_COUNT, &sub)) {
                    k.blad = HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
                    ++stan_skan.kodek_idx;
                    --budzet;
                    continue;
                }
                k.root_sub_count = sub;

                const uint8_t start = hda_sub_start(sub);
                const uint8_t count = hda_sub_count(sub);
                if (!hda_zakres_wezlow_poprawny(start, count)) {
                    k.blad = HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;
                    ++stan_skan.kodek_idx;
                    --budzet;
                    continue;
                }

                /* Enumeruj Function Groups. */
                uint8_t fg_licznik = 0;
                for (uint8_t i = 0; i < count && fg_licznik < HDA_DIAG_MAKS_FG; ++i) {
                    const uint8_t nid = static_cast<uint8_t>(start + i);
                    uint32_t fgt = 0;
                    if (!pobierz_parametr(cad, nid, HDA_PAR_FGROUP_TYPE, &fgt)) {
                        continue;
                    }
                    if (!hda_fg_jest_audio(fgt)) {
                        /* Zapisujemy tylko Audio Function Groups. */
                        continue;
                    }
                    HdaDiagFG& g = skan_docelowy.fg[0][stan_skan.kodek_idx][fg_licznik];
                    g.wersja = HDA_DIAG_WERSJA_ABI;
                    g.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagFG));
                    g.nid = nid;
                    g.typ = hda_fg_typ(fgt);
                    g.audio = 1;

                    uint32_t fgc = 0;
                    if (pobierz_parametr(cad, nid, HDA_PAR_AFG_CAPS, &fgc)) {
                        g.fg_caps = fgc;
                    }

                    uint32_t subg = 0;
                    if (pobierz_parametr(cad, nid, HDA_PAR_SUB_NODE_COUNT, &subg)) {
                        g.sub_start = hda_sub_start(subg);
                        g.sub_count = hda_sub_count(subg);
                        if (hda_zakres_wezlow_poprawny(
                                static_cast<uint8_t>(g.sub_start),
                                static_cast<uint8_t>(g.sub_count))) {
                            g.widget_start = g.sub_start;
                            g.widget_count = g.sub_count;
                        } else {
                            g.blad = HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;
                        }
                    } else {
                        g.blad = HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
                    }
                    ++fg_licznik;
                    --budzet;
                }
                k.liczba_fg = fg_licznik;
                if (fg_licznik == 0) {
                    k.blad = HDA_DIAG_BLAD_BRAK_AFG;
                }
                ++stan_skan.kodek_idx;
            }
            if (stan_skan.kodek_idx >= nk) {
                stan_skan.etap = HDA_DIAG_ETAP_WIDGETY;
                stan_skan.kodek_idx = 0;
                stan_skan.fg_idx = 0;
                stan_skan.widget_idx = 0;
                stan_skan.postep_0_1000 = 450;
            }
            break;
        }

        case HDA_DIAG_ETAP_WIDGETY: {
            const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
            bool gotowe = true;
            for (uint32_t ki = 0; ki < nk; ++ki) {
                const uint32_t nf = skan_docelowy.kodeki[0][ki].liczba_fg;
                for (uint32_t fi = 0; fi < nf && budzet > 0; ++fi) {
                    HdaDiagFG& g = skan_docelowy.fg[0][ki][fi];
                    if (g.widget_count == 0) continue;
                    const uint8_t cad = skan_docelowy.kodeki[0][ki].cad;
                    uint32_t widx = 0;
                    if (!(ki == stan_skan.kodek_idx && fi == stan_skan.fg_idx)) {
                        /* pomin juz przetworzone */
                        if (ki < stan_skan.kodek_idx ||
                            (ki == stan_skan.kodek_idx && fi < stan_skan.fg_idx)) {
                            continue;
                        }
                    }
                    widx = (ki == stan_skan.kodek_idx && fi == stan_skan.fg_idx)
                               ? stan_skan.widget_idx
                               : 0;

                    while (widx < g.widget_count && budzet > 0) {
                        const uint8_t nid =
                            static_cast<uint8_t>(g.widget_start + widx);
                        HdaDiagWidget& w =
                            skan_docelowy.widgety[0][ki][fi][widx < HDA_DIAG_MAKS_WIDGETOW
                                                                  ? widx
                                                                  : 0];
                        if (widx >= HDA_DIAG_MAKS_WIDGETOW) {
                            skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
                            ++widx;
                            continue;
                        }
                        w.wersja = HDA_DIAG_WERSJA_ABI;
                        w.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagWidget));
                        w.nid = nid;

                        uint32_t awcap = 0;
                        if (!pobierz_parametr(cad, nid, HDA_PAR_WIDGET_CAP, &awcap)) {
                            w.flagi = 2; /* blad odczytu */
                            w.blad = HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
                            ++widx;
                            --budzet;
                            continue;
                        }
                        w.awcap = awcap;
                        w.typ = hda_wcap_typ(awcap);

                        /* Pin Caps + Default Config tylko dla Pin Complex. */
                        if (hda_typ_wolno_pin_caps(w.typ)) {
                            uint32_t pc = 0;
                            if (pobierz_parametr(cad, nid, HDA_PAR_PIN_CAP, &pc)) {
                                w.pin_caps = pc;
                            }
                            uint32_t dc = 0;
                            if (pobierz_pin_config(cad, nid, &dc)) {
                                w.default_config = dc;
                            }
                        }

                        /* PCM tylko dla konwerterow. */
                        if (hda_typ_wolno_pcm_caps(w.typ)) {
                            uint32_t ps = 0;
                            if (pobierz_parametr(cad, nid, HDA_PAR_PCM_SIZE_RATE, &ps)) {
                                w.pcm_size_rate = ps;
                            }
                            uint32_t sf = 0;
                            if (pobierz_parametr(cad, nid, HDA_PAR_STREAM_FORMATS, &sf)) {
                                w.stream_formats = sf;
                            }
                        }

                        /* Amp caps gdy zaznaczone w AWCAP. */
                        if (hda_typ_wolno_amp_out(awcap)) {
                            uint32_t ac = 0;
                            if (pobierz_parametr(cad, nid, HDA_PAR_AMP_OUT_CAP, &ac)) {
                                w.out_amp_caps = ac;
                            }
                        }
                        if (hda_typ_wolno_amp_in(awcap)) {
                            uint32_t ac = 0;
                            if (pobierz_parametr(cad, nid, HDA_PAR_AMP_IN_CAP, &ac)) {
                                w.in_amp_caps = ac;
                            }
                        }

                        /* Power state: read-only, bezpieczne. */
                        uint8_t ps = 0;
                        if (pobierz_power_state(cad, nid, &ps)) {
                            w.power_current = ps;
                        }
                        uint32_t psc = 0;
                        if (pobierz_parametr(cad, nid, HDA_PAR_POWER_STATES, &psc)) {
                            w.power_states = psc;
                        }

                        /* Connection list. */
                        if (hda_typ_wolno_conn_list(awcap)) {
                            uint32_t cl = 0;
                            if (pobierz_parametr(cad, nid, HDA_PAR_CONNLIST_LEN, &cl)) {
                                w.connlist_len_raw = cl;
                                const uint8_t ile = hda_connlist_liczba(cl);
                                const bool lf = hda_connlist_long_form(cl);
                                w.conn_long_form = lf ? 1 : 0;
                                if (ile > 0 && ile <= HDA_DIAG_MAKS_POLACZEN) {
                                    uint32_t slowa[4] = {0, 0, 0, 0};
                                    uint32_t liczba_slow = 0;
                                    const uint32_t na_slowo = lf ? 2U : 4U;
                                    for (uint8_t idx = 0; idx < ile;) {
                                        if (liczba_slow >= 4) break;
                                        /* Get Connection List Entry: verb 0xF02. */
                                        const uint32_t cmd =
                                            hda_wew_verb12(cad, nid, 0xF02U, idx);
                                        uint32_t resp = 0;
                                        if (!hda_wew_wyslij_immediate(cmd, &resp) ||
                                            odpowiedz_nie_poprawna(resp)) {
                                            w.flagi = 2;
                                            w.blad = HDA_DIAG_BLAD_LISTA_POLACZEN;
                                            break;
                                        }
                                        slowa[liczba_slow++] = resp;
                                        idx = static_cast<uint8_t>(idx + na_slowo);
                                        --budzet;
                                    }
                                    uint32_t out_n = 0;
                                    bool trunc = false;
                                    uint8_t tmp[HDA_DIAG_MAKS_POLACZEN] = {0};
                                    if (hda_rozszerz_liste_polaczen(
                                            slowa, liczba_slow, lf, ile, tmp,
                                            HDA_DIAG_MAKS_POLACZEN, &out_n, &trunc)) {
                                        w.liczba_polaczen = static_cast<uint8_t>(out_n);
                                        for (uint32_t ci = 0; ci < out_n; ++ci) {
                                            w.polaczenia[ci] = tmp[ci];
                                            w.polaczenia_surowe[ci] = tmp[ci];
                                        }
                                        if (trunc) {
                                            skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
                                        }
                                    } else {
                                        w.blad = HDA_DIAG_BLAD_LISTA_POLACZEN;
                                    }
                                } else if (ile > HDA_DIAG_MAKS_POLACZEN) {
                                    w.flagi = 1;
                                    skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
                                }
                            }
                        }

                        ++widx;
                        --budzet;
                    }
                    stan_skan.widget_idx = widx;
                    stan_skan.fg_idx = fi;
                    stan_skan.kodek_idx = ki;
                    if (widx < g.widget_count) {
                        gotowe = false;
                    }
                }
            }
            if (gotowe) {
                stan_skan.etap = HDA_DIAG_ETAP_SCIEZKI;
                stan_skan.postep_0_1000 = 800;
            }
            break;
        }

        case HDA_DIAG_ETAP_SCIEZKI: {
            /*
             * Sciezki budujemy dla kazdego AFG osobno, potem sklejamy
             * do wspolnej tablicy snapshotu.
             */
            uint32_t ile_sciezek = 0;
            bool trunc = false;
            const uint32_t nk = skan_docelowy.kontrolery[0].liczba_kodekow;
            for (uint32_t ki = 0; ki < nk; ++ki) {
                const uint32_t nf = skan_docelowy.kodeki[0][ki].liczba_fg;
                for (uint32_t fi = 0; fi < nf; ++fi) {
                    HdaDiagWidget lokalne[HDA_DIAG_MAKS_WIDGETOW];
                    uint32_t n = 0;
                    for (uint32_t wi = 0; wi < HDA_DIAG_MAKS_WIDGETOW; ++wi) {
                        const HdaDiagWidget& src =
                            skan_docelowy.widgety[0][ki][fi][wi];
                        if (src.awcap == 0 && src.nid == 0) continue;
                        if (n >= HDA_DIAG_MAKS_WIDGETOW) break;
                        lokalne[n++] = src;
                    }
                    if (n == 0) continue;
                    HdaDiagSciezka tmp[HDA_DIAG_MAKS_SCIEZEK];
                    uint32_t ns = 0;
                    bool obc = false;
                    if (!hda_zbuduj_sciezki(lokalne, n, tmp, HDA_DIAG_MAKS_SCIEZEK,
                                            &ns, &obc)) {
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
            if (trunc) {
                skan_docelowy.flagi |= HDA_DIAG_FLAGA_OBECIETA;
            }
            /* Nie ma pewnego kandydata glosnika - nie wymuszamy routingu. */
            if (!hda_sciezka_glosnik_istnieje(skan_docelowy.sciezki, ile_sciezek)) {
                stan_skan.blad = HDA_DIAG_BLAD_BRAK_GLOSNIKA;
            }
            stan_skan.etap = HDA_DIAG_ETAP_GOTOWY;
            stan_skan.postep_0_1000 = 1000;
            break;
        }

        case HDA_DIAG_ETAP_GOTOWY:
        case HDA_DIAG_ETAP_BLAD:
        default:
            break;
    }

    if (stan_skan.etap == HDA_DIAG_ETAP_GOTOWY ||
        stan_skan.etap == HDA_DIAG_ETAP_BLAD) {
        /* Publikacja atomowa: kopiujemy do nieaktywnego bufora. */
        HdaDiagSnapshot* cel =
            (opublikowana == &bufor_a) ? &bufor_b : &bufor_a;
        skan_docelowy.wersja = HDA_DIAG_WERSJA_ABI;
        skan_docelowy.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagSnapshot));
        skan_docelowy.generacja = licznik_generacji + 1;
        skan_docelowy.etap = stan_skan.etap;
        skan_docelowy.blad = stan_skan.blad;
        skan_docelowy.postep_0_1000 = stan_skan.postep_0_1000;
        if (stan_skan.etap == HDA_DIAG_ETAP_GOTOWY) {
            skan_docelowy.flagi &= ~HDA_DIAG_FLAGA_SKAN_W_TOKU;
            skan_docelowy.flagi |= HDA_DIAG_FLAGA_SKAN_GOTOWY;
        } else {
            skan_docelowy.flagi &= ~HDA_DIAG_FLAGA_SKAN_W_TOKU;
            skan_docelowy.flagi |= HDA_DIAG_FLAGA_SKAN_BLAD;
        }

        /* Aktualizuj stan strumienia i testu w opublikowanej kopii. */
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
        /* Publikacja: jeden zapis wskaznika. */
        __atomic_store_n(&opublikowana, cel, __ATOMIC_RELEASE);
        __atomic_store_n(&generacja_publikacji, skan_docelowy.generacja,
                         __ATOMIC_RELEASE);
        licznik_generacji = skan_docelowy.generacja;

        stan_skan.aktywny = 0;
        stan_skan.gotowy = (stan_skan.etap == HDA_DIAG_ETAP_GOTOWY) ? 1 : 0;
        return true;
    }

    return false;
}

void hda_diag_pobierz_stan(HdaDiagSkan* out) {
    if (!out) return;
    *out = stan_skan;
    out->postep_0_1000 = stan_skan.postep_0_1000;
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
