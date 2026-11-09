# Inwentaryzacja UI / input / compositor / BWS — 2026-11-07

Zakres: architektura przed naprawą kursora i modernizacją UX (plan 1B+2A).
Źródła: kod, `docs/11_audyt_bws_nowoczesne.md`, `docs/raport-lokalna-weryfikacja-2026-11-06.md`, audyty pointer/cursor/desktop.

## Model wyświetlania (bez zmian architektonicznych)

```
Ring3 WM / apps → BWS GUI
  → skladacz_obrazu (warstwy PID, dirty rects)
  → backbuffer (grafika.cpp)
  → HAL: GOP / VESA / Bochs / VirtIO-GPU 2D (+ opcjonalny HW cursor)
```

Świadomie **odrzucamy** w tym torze: VirGL/3D, osobny compositor typu Wayland (koliduje z zaufanym składem w jądrze).

## Tabela obszarów

| Obszar | Stan | Akcja | Priorytet |
|--------|------|-------|-----------|
| Input/cursor VirtIO+PS/2+USB | Bramki `s_desktop_ready` + `powitanie_aktywne()`; QEMU domyślnie VirtIO mouse | Naprawa handshake + admission | **P0** |
| VirtIO-GPU present/cursor | `test-virtio-gpu-qemu` FAIL (raport 2026-11-06) | Naprawa present/overlay | **P0** |
| Stack jądra (`audit-stos-jadra`) | 3 ramki > 6144 B | Przenieść lokale poza stos | **P0** |
| Splash ↔ pointer | Splash ukrywa kursor i (dotąd) blokuje VirtIO admission | Rozdzielić fade od inputu; watchdog | **P0** |
| Compositor / dirty rects | Działa; pacing blokuje SW cursor | Utwardzenie | P1 |
| Pulpit: default + rearrange | Auto **row-major**; persist `/ustawienia/uklad-pulpitu.cfg` OK | Kolumnowy jak Windows + „Ułóż ikony” | P1 |
| HiDPI / Ekran / motywy | Działa (100–200%, motywy v2) | Regresja przy zmianach UX | P1 |
| BWS nowoczesne PARTIAL | Dialogi, powiadomienia, async I/O, sockets concurrency | Zalążki Ring 3 | P2 |
| Scheduler / sync sieć Ring0 | Może blokować GUI | Osobny tor | P2 |
| Procesy 16 slotów / BUR v2 ASLR | Limit skali; ASLR host-only | Później | P3 |
| VirGL / Wayland-compositor | Poza modelem | **Odrzucić** | — |

**Podsumowanie skali:** ~10 obszarów systemowych; w tej iteracji **4× P0** (input/cursor/splash/GPU present powiązane), **3× P1** UX/compositor, reszta P2/P3 lub odrzucone.

## Hipotezy P0 — kursor niewidoczny + brak klików (QEMU Windows)

1. **VirtIO Input odrzuca wskaźnik**, dopóki `interactive = geometry && s_desktop_ready && !powitanie_aktywne()` (`virtio_input.cpp`). `s_desktop_ready` ustawia tylko `system_potwierdz_gotowosc_pulpitu` po `gui_potwierdz_gotowosc_pulpitu()`.
2. **WM woła handshake tylko gdy `hash_pulpitu_wazny`** — nieudane pierwsze `odswiez_wpisy_pulpitu` opóźnia odblokowanie myszy (retry na timerze).
3. **Splash aktywny** (`powitanie_aktywne`) ukrywa HW/SW cursor w `grafika_prezentuj_scene_z_kursorem` i (przed poprawką) blokuje admission VirtIO; zawieszenie w `zakonczenie_oczekuje` = trwały brak kursora/klików.
4. Brak `gui_ustaw_przejecie_myszy(true)` — mniej prawdopodobne (WM ustawia przy starcie), ale wtedy brak klików Ring 3 przy ewentualnym ruchu legacy.

## Checklista diagnostyczna QEMU (Windows, natywne `qemu-system-x86_64.exe`)

1. Serial / log: `[WM] Initial Desktop ready`, `[WM] gui_ustaw_przejecie_myszy(true)`, `[BOOT-TELEMETRY] … desktop_ready…`.
2. `[VIRTIO-INPUT]` / `[POINTER BOOT]` / `[INPUT OWNER]`.
3. Build z `BURSZTYNDIAGVIRTIOINPUTINIT=1` — śledź `interactive=0` vs `1`.
4. Po pulpitu: ruch myszy zmienia współrzędne; kursor widoczny; klik Start/ikona działa.
5. Porównaj profil VirtIO mouse vs `usb-tablet` tylko do izolacji transportu.

## Pliki kluczowe

| Warstwa | Ścieżki |
|---------|---------|
| Boot / handshake | `jadro/rdzen/kernel.cpp`, `jadro/bws/bws_nowoczesne.cpp` |
| VirtIO input | `sterowniki/virtio/virtio_input.cpp` |
| Present / kursor | `sterowniki/grafika/grafika.cpp`, `jadro/gui/kursor_prezentacja.h`, `jadro/gui/skladacz_obrazu.cpp` |
| Splash | `jadro/gui/ekran_powitalny.cpp` |
| WM / pulpit | `programy/menedzer_okien/menedzer_okien.cpp`, `jadro/gui/desktop_layout.h`, `jadro/gui/desktop_layout_settings.h` |
| BWS nowoczesne | `docs/11_audyt_bws_nowoczesne.md`, `jadro/bws/bws_nowoczesne.*` |

## Bramki weryfikacji po naprawach

- Host: `test-compositor-cursor`, suite desktop / persistence, pointer coexistence gdy dotyczy.
- QEMU (WSL build OK, okno gościa tylko Windows QEMU): `test-virtio-input-qemu`, `test-virtio-gpu-qemu` gdy dotyczy.
- Szybki gate: `make test-smoke`.

## Implementacja w tej sesji (po raporcie)

| Zmiana | Pliki |
|--------|-------|
| VirtIO admission bez bramki splash (po handshake) | `sterowniki/virtio/virtio_input.cpp` |
| WM zawsze woła `gui_potwierdz_gotowosc_pulpitu` po pierwszym paint | `programy/menedzer_okien/menedzer_okien.cpp` |
| Watchdog splash 10 s po handshake; kursor po `powitanie_pulpit_potwierdzony` | `ekran_powitalny.*`, `grafika.cpp` |
| Domyślny układ kolumnowy + „Ułóż ikony automatycznie” | `desktop_layout.h`, `plik_popup_ui.h`, WM |
| Zalążek protokołu usług UI (IPC dialogi/powiadomienia) | `bursztyn_ui/usluga_ui_protokol.h`, `test-usluga-ui-protokol-host` |
