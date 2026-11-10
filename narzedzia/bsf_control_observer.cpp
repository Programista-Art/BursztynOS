#include <cstdio>
#include <cstddef>
#define BSF_NATIVE 1
#define private public
#define protected public
#include "bursztyn_ui/bursztyn_ui.h"
#undef private
#undef protected
#include "programy/ustawienia/themes_ui.h"
int main(){printf("{\"visible\":%zu,\"text\":%zu,\"theme_status\":%zu,\"theme_seconds\":%zu}\n",offsetof(BursztynUI::Kontrolka,czy_widoczna_),offsetof(BursztynUI::Kontrolka,tekst_),offsetof(SettingsUI::ThemesPage,status),offsetof(SettingsUI::ThemesPage,shown_seconds));}
