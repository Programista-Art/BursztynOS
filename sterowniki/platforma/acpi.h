#pragma once

#include <stdint.h>

/* Inicjalizuje jeden wspolny parser ACPI z tagow Multiboot2. */
bool acpi_inicjalizuj(uint64_t adres_info_multiboot);
bool acpi_pobierz_adres_hpet(uint64_t* adres_mmio);
bool acpi_uzyto_xsdt();

bool acpi_restart_dostepny();
bool acpi_wykonaj_restart();
bool acpi_shutdown_dostepny();
bool acpi_wykonaj_shutdown();
