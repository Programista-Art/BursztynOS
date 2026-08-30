/*
 * Bursztyn OS - BursztynUI Demo Application (Ring 3)
 * Pokaz możliwości nowoczesnego toolkitu GUI:
 *  - Drzewo widgetów w retained-mode (Panel/Column/Row, Label, TextBox, CheckBox, Button)
 *  - Automatyczny układ (Layout pass: measure & arrange)
 *  - Odrysowywanie dirty-region (Paint pass)
 *  - Obsługa zdarzeń i skupienia (Focus & Event dispatch)
 *  - Pełne wsparcie dla polskich znaków UTF-8 w TextBox
 */

#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

// =============================================================================
// Nagłówek formatu .bur
// =============================================================================

extern "C" void _start();

static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0xC000ULL; // 48 KiB
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0xD000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x100D000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 131072ULL;

extern "C" {
    __attribute__((section(".naglowek"), used))
    NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        BUR_TEKST_PRZESUNIECIE,
        BUR_TEKST_ROZMIAR,
        BUR_TEKST_WIRTUALNY,
        BUR_DANE_PRZESUNIECIE,
        BUR_DANE_ROZMIAR,
        BUR_DANE_WIRTUALNY
    };
}

// =============================================================================
// Stan Aplikacji Demo
// =============================================================================

struct AppState {
    Label* lbl_status{nullptr};
    TextBox* txt_input{nullptr};
    CheckBox* chk_option{nullptr};
    Button* btn_action{nullptr};
    int click_count{0};
};

static AppState g_app_state;

static void on_btn_click(Button* sender, void* user_data) {
    (void)sender;
    AppState* state = static_cast<AppState*>(user_data);
    if (!state || !state->lbl_status || !state->txt_input) return;

    state->click_count++;

    const char* txt = state->txt_input->get_text();
    char buf[256];
    if (txt && txt[0] != '\0') {
        // Skopiuj tekst z pola
        size_t i = 0;
        const char* prefix = "Wpisano: ";
        while (prefix[i] != '\0' && i < sizeof(buf) - 1) {
            buf[i] = prefix[i];
            i++;
        }
        size_t j = 0;
        while (txt[j] != '\0' && i < sizeof(buf) - 1) {
            buf[i++] = txt[j++];
        }
        buf[i] = '\0';
    } else {
        // Domyślny licznik kliknięć
        buf[0] = 'K'; buf[1] = 'l'; buf[2] = 'i'; buf[3] = 'k';
        buf[4] = 'n'; buf[5] = 'i'; buf[6] = 'e'; buf[7] = 't';
        buf[8] = 'o'; buf[9] = ':'; buf[10] = ' ';
        // Prosta konwersja liczby
        int n = state->click_count;
        int digits[10];
        int dcnt = 0;
        if (n == 0) digits[dcnt++] = 0;
        while (n > 0) {
            digits[dcnt++] = n % 10;
            n /= 10;
        }
        size_t pos = 11;
        for (int k = dcnt - 1; k >= 0 && pos < sizeof(buf) - 1; --k) {
            buf[pos++] = '0' + digits[k];
        }
        buf[pos] = '\0';
    }

    state->lbl_status->set_text(buf);
}

static void on_chk_toggled(CheckBox* sender, bool is_checked, void* user_data) {
    (void)sender;
    AppState* state = static_cast<AppState*>(user_data);
    if (!state || !state->lbl_status) return;

    if (is_checked) {
        state->lbl_status->set_text("Opcja wlaczona (zaznaczono CheckBox)");
    } else {
        state->lbl_status->set_text("Opcja wylaczona");
    }
}

// =============================================================================
// Punkt Wejścia
// =============================================================================

extern "C" void _start() {
    GuiApplication app;

    Window win("BursztynUI Demo", 460, 300);
    win.set_position(160, 120);

    // Główny panel pionowy (Column)
    Column* main_panel = new Column();
    main_panel->set_padding(Thickness(16, 16, 16, 16));
    main_panel->set_spacing(10);

    // 1. Tytuł / Etykieta nagłówka
    Label* lbl_title = new Label("BursztynUI v0.1 - Zestaw Kontrolek");
    lbl_title->set_font_scale(1);
    lbl_title->set_text_color(Colors::AccentGold);
    lbl_title->set_margin(Thickness(0, 0, 0, 4));
    main_panel->add_child(lbl_title);

    // 2. Etykieta dynamicznego statusu [ Label ]
    Label* lbl_status = new Label("Wpisz polski tekst i kliknij przycisk:");
    lbl_status->set_text_color(Colors::TextMuted);
    lbl_status->set_margin(Thickness(0, 0, 0, 6));
    main_panel->add_child(lbl_status);
    g_app_state.lbl_status = lbl_status;

    // 3. Pole edycyjne [ TextBox ]
    TextBox* txt_box = new TextBox("Wpisz np. Zażółć gęślą jaźń...");
    txt_box->set_margin(Thickness(0, 0, 0, 8));
    txt_box->set_horizontal_alignment(HorizontalAlignment::Stretch);
    txt_box->set_on_submit([](TextBox*, const char*, void* user_data) {
        on_btn_click(nullptr, user_data);
    }, &g_app_state);
    main_panel->add_child(txt_box);
    g_app_state.txt_input = txt_box;

    // 4. Pole wyboru [ CheckBox ]
    CheckBox* chk = new CheckBox("Wlacz zaawansowane formatowanie", false);
    chk->set_margin(Thickness(0, 0, 0, 10));
    chk->set_on_toggled(on_chk_toggled, &g_app_state);
    main_panel->add_child(chk);
    g_app_state.chk_option = chk;

    // 5. Przycisk akcji [ Button ]
    Button* btn = new Button("Zatwierdz tekst (Aktualizuj etykiete)");
    btn->set_preferred_size(Size(280, 32));
    btn->set_horizontal_alignment(HorizontalAlignment::Stretch);
    btn->set_on_click(on_btn_click, &g_app_state);
    main_panel->add_child(btn);
    g_app_state.btn_action = btn;

    win.set_root(main_panel);
    win.set_focus(txt_box);

    app.set_main_window(&win);
    app.run();

    bws_zakoncz_proces();
}
