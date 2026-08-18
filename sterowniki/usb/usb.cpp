#include "sterowniki/usb/usb.h"
#include "sterowniki/usb/xhci.h"

void wypisz_log(const char* tekst);

void usb_inicjalizuj() {
    wypisz_log("[USB] Inicjalizacja podsystemu USB.");
    if (xhci_inicjalizuj_pierwszy())
        wypisz_log("[USB] xHCI gotowy.");
    else
        wypisz_log("[USB] Brak kontrolera xHCI; USB niedostepne.");
}
