#!/usr/bin/env python3
"""Interaktywny test renderowania dialogów, menu i toastów Kontrolki 4 w QEMU."""
import json, socket, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHOTS = ROOT / ".kontrolki4-qemu"
ARTIFACTS_DIR = Path("/home/programista/.gemini/antigravity-cli/brain/874b5b92-0132-453d-9b9f-622d2bf615e0")
QMP_PATH = Path("/tmp/k4-qmp.sock")

class QMP:
    def __init__(self, path=QMP_PATH):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(str(path))
        self.f = self.s.makefile("rwb", buffering=0)
        self.f.readline()
        self.call("qmp_capabilities")

    def call(self, cmd, args=None):
        q = {"execute": cmd}
        if args is not None:
            q["arguments"] = args
        self.f.write((json.dumps(q) + "\n").encode())
        while True:
            r = json.loads(self.f.readline())
            if "return" in r:
                return r["return"]
            if "error" in r:
                raise RuntimeError(r)

    def hmp(self, cmd):
        return self.call("human-monitor-command", {"command-line": cmd})

    def key(self, key):
        self.hmp("sendkey " + key)
        time.sleep(.25)

    def click(self, x, y, double=False, settle=.7):
        for _ in range(20):
            self.hmp("mouse_move -100 -100")
            time.sleep(.03)
        rem_x, rem_y = x, y
        while rem_x or rem_y:
            dx = min(rem_x, 100)
            dy = min(rem_y, 100)
            self.hmp(f"mouse_move {dx} {dy}")
            time.sleep(.04)
            rem_x -= dx
            rem_y -= dy
        for _ in range(2 if double else 1):
            self.hmp("mouse_button 1")
            time.sleep(.1)
            self.hmp("mouse_button 0")
            time.sleep(.15)
        time.sleep(settle)

    def drag(self, start_x, start_y, end_x, end_y, settle=0.7):
        for _ in range(20):
            self.hmp("mouse_move -100 -100")
            time.sleep(.03)
        rem_x, rem_y = start_x, start_y
        while rem_x or rem_y:
            dx = min(rem_x, 100)
            dy = min(rem_y, 100)
            self.hmp(f"mouse_move {dx} {dy}")
            time.sleep(.03)
            rem_x -= dx
            rem_y -= dy
        time.sleep(0.2)
        self.hmp("mouse_button 1")
        time.sleep(0.2)
        delta_x = end_x - start_x
        delta_y = end_y - start_y
        steps = 20
        step_dx = int(delta_x / steps)
        step_dy = int(delta_y / steps)
        for _ in range(steps):
            self.hmp(f"mouse_move {step_dx} {step_dy}")
            time.sleep(0.04)
        rem_dx = delta_x - (step_dx * steps)
        rem_dy = delta_y - (step_dy * steps)
        if rem_dx or rem_dy:
            self.hmp(f"mouse_move {rem_dx} {rem_dy}")
            time.sleep(0.04)
        time.sleep(0.2)
        self.hmp("mouse_button 0")
        time.sleep(settle)

    def shot(self, name):
        SHOTS.mkdir(exist_ok=True)
        p = SHOTS / name
        self.call("screendump", {"filename": str(p), "format": "png"})
        print(f"[SHOT] {p}")
        if ARTIFACTS_DIR.exists():
            try:
                import shutil
                shutil.copy2(p, ARTIFACTS_DIR / name)
            except Exception:
                pass
        return p

def main():
    iso = ROOT / "BursztynOS.iso"
    if not iso.exists():
        print(f"[BLAD] Brak pliku ISO: {iso}", file=sys.stderr)
        sys.exit(1)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        print("[INFO] Tworzenie wirtualny_dysk.img...")
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    SHOTS.mkdir(exist_ok=True)
    boot_log_path = SHOTS / "boot.log"

    if QMP_PATH.exists():
        try:
            QMP_PATH.unlink()
        except OSError:
            pass

    qemu_cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-netdev", "user,id=n1",
        "-device", "e1000,netdev=n1",
        "-m", "2G",
        "-vga", "std",
        "-global", "VGA.vgamem_mb=64",
        "-display", "none",
        "-qmp", f"unix:{QMP_PATH},server=on,wait=off",
        "-cdrom", str(iso),
        "-boot", "d",
        "-serial", f"file:{boot_log_path}"
    ]

    print(f"[INFO] Uruchamianie QEMU: {' '.join(qemu_cmd)}")
    proc = subprocess.Popen(qemu_cmd)

    try:
        # Oczekiwanie na uruchomienie pulpitu z logu szeregowego (GRUB ładuje się i uruchamia jądro automatycznie)
        print("[INFO] Czekam na zaladowanie pulpitu Bursztyn OS...")
        deadline = time.time() + 120
        pulpit_gotowy = False
        while time.time() < deadline:
            if boot_log_path.exists():
                try:
                    txt = boot_log_path.read_text(errors="replace")
                    if "System operacyjny gotowy!" in txt:
                        pulpit_gotowy = True
                        break
                except Exception:
                    pass
            time.sleep(1)

        if not pulpit_gotowy:
            raise RuntimeError("Pulpit nie uruchomil sie w zadanym czasie (sprawdz boot.log).")

        print("[INFO] Pulpit gotowy! Czekam 2s na ustabilizowanie...")
        time.sleep(2)

        # Oczekiwanie na gniazdo QMP i nawiązanie połączenia
        t0 = time.time()
        while time.time() - t0 < 15:
            if QMP_PATH.exists():
                break
            time.sleep(0.2)
        if not QMP_PATH.exists():
            raise RuntimeError("Nie udalo sie utworzyc gniazda QMP.")

        q = QMP()
        print("[INFO] Polaczono z QMP.")

        # 2. Zrzut ekranu pulpitu
        q.shot("00-pulpit.png")

        # 3. Uruchomienie aplikacji Kontrolki 4 (dwuklik w ikone na pulpicie)
        print("[INFO] Otwieram Kontrolki 4 (klik w 320, 145)...")
        q.click(320, 145, double=True, settle=3.5)
        q.shot("01-kontrolki4.png")

        # 4. Weryfikacja kotwicy MenuGlowne ("Plik")
        print("[INFO] Test MenuGlowne (Plik)...")
        q.click(125, 129, settle=1.0)
        q.shot("02-menuglowne-anchor.png")
        q.key("esc")
        time.sleep(0.5)

        # 5. Karta 1: Polecenia i Menu -> PrzyciskPodzielony ("Zapisz" dropdown)
        print("[INFO] Przelaczam na karte Polecenia i Menu...")
        q.click(230, 158, settle=1.5)
        # Klikniecie w strzalke dropdown przycisku podzielonego Zapisz
        print("[INFO] Otwieram menu Zapisz...")
        q.click(474, 245, settle=1.0)
        q.shot("03-zapisz-dropdown.png")
        q.key("esc")
        time.sleep(0.5)

        # 6. Karta 2: Komunikaty i Dialogi
        print("[INFO] Przelaczam na karte Komunikaty...")
        q.click(340, 158, settle=1.5)
        q.shot("04-komunikaty.png")

        # 7. OknoDialogowe Tak/Nie
        print("[INFO] Otwieram dialog Tak / Nie...")
        q.click(375, 225, settle=1.0)
        q.shot("05-dialog-tak-nie.png")
        q.key("esc")
        time.sleep(0.5)

        # 8. OknoDialogowe Tak/Nie/Anuluj
        print("[INFO] Otwieram dialog Tak / Nie / Anuluj...")
        q.click(500, 225, settle=1.0)
        q.shot("06-dialog-tak-nie-anuluj.png")
        q.key("esc")
        time.sleep(0.5)

        # 9. OknoDialogowe Przerwij/Ponow/Ignoruj
        print("[INFO] Otwieram dialog Przerwij / Ponow / Ignoruj...")
        q.click(345, 269, settle=1.0)
        q.shot("07-dialog-przerwij-ponow-ignoruj.png")
        q.key("esc")
        time.sleep(0.5)

        # 10. Pojedynczy Toast (Sukces)
        print("[INFO] Pokazuje pojedynczy Toast Sukces...")
        q.click(175, 620, settle=0.1)
        time.sleep(0.4)
        q.shot("08-toast-single.png")

        # 11. Stos Toastow o roznych wysokosciach
        print("[INFO] Pokazuje stos Toastow...")
        q.click(320, 620, settle=0.1)
        time.sleep(0.15)
        q.click(455, 620, settle=0.1)
        time.sleep(0.15)
        q.click(175, 620, settle=0.1)
        time.sleep(0.4)
        q.shot("09-toast-stack.png")

        # 12. Oczekiwanie na wygasniecie i ponowne dodanie (zagęszczenie)
        print("[INFO] Czekam 5.5s na wygasniecie toastow...")
        time.sleep(5.5)
        q.click(175, 620, settle=0.5)
        q.shot("10-toast-after-expiry.png")

        # 13. Karta 3: Dialogi systemowe (DialogPliku & DialogKoloru)
        print("[INFO] Przelaczam na karte Dialogi systemowe...")
        q.click(460, 158, settle=1.5)
        q.shot("11-karta-dialogi-systemowe.png")

        # 14. DialogPliku - Tryb Otwórz (dialog-pliku-improved.png)
        print("[INFO] Otwieram DialogPliku (tryb Otwórz)...")
        q.click(198, 268, settle=1.5)
        q.shot("dialog-pliku-improved.png")

        # 15. DialogPliku - regresje wejscia: spacje w nazwie i klik belki tytulowej
        print("[INFO] Sprawdzam spacje w polu Nazwa pliku...")
        q.click(465, 482, settle=0.4)
        for key in ["m", "o", "j", "spc", "spc", "p", "l", "i", "k"]:
            q.key(key)
        q.shot("dialog-pliku-spaces-still-open.png")

        print("[INFO] Klikam belke tytulowa DialogPliku...")
        q.click(520, 105, settle=0.6)
        q.shot("dialog-pliku-title-click-still-open.png")

        # 16. DialogPliku - Lista filtrów (dialog-pliku-filters.png)
        print("[INFO] Rozwijam listę filtrów w DialogPliku...")
        q.click(866, 528, settle=1.0)
        q.shot("dialog-pliku-filters.png")
        q.key("esc")
        time.sleep(0.6)

        # 17. DialogPliku - Nowy katalog (dialog-pliku-new-folder.png)
        print("[INFO] Otwieram prompt Nowy katalog w DialogPliku...")
        q.click(510, 138, settle=1.0)
        q.shot("dialog-pliku-new-folder.png")
        q.key("esc")
        time.sleep(0.6)
        q.key("esc")
        time.sleep(0.8)

        # 18. DialogKoloru - Wizualizacja selektora 2D, RGB, HEX i podglądu (dialog-koloru-improved.png)
        print("[INFO] Otwieram DialogKoloru...")
        q.click(198, 405, settle=1.5)
        q.shot("dialog-koloru-improved.png")

        # 19. DialogKoloru - Przeciąganie okna za belkę tytułową (dialog-koloru-dragged.png)
        print("[INFO] Przeciągam DialogKoloru za belkę tytułową...")
        q.drag(500, 180, 660, 260, settle=1.0)
        q.shot("dialog-koloru-dragged.png")
        q.key("esc")
        time.sleep(0.8)

        # 20. Przełączenie motywu na Bursztyn Jasny i weryfikacja DialogKoloru (dialog-koloru-light-theme.png)
        print("[INFO] Przełączam motyw na Bursztyn Jasny przez Ustawienia...")
        q.click(34, 700, settle=0.8)
        for letter in 'ustawienia': q.key(letter)
        q.key('ret');time.sleep(2.5)
        q.click(190, 299, settle=0.8) # Motywy
        q.click(310, 290, settle=0.8) # Bursztyn Jasny
        q.click(488, 400, settle=2.0) # Zastosuj
        q.click(1033, 114, settle=1.0)

        print("[INFO] Aktywuję okno Kontrolki 4 i otwieram DialogKoloru w motywie jasnym...")
        q.click(250, 120, settle=0.6)
        q.click(460, 158, settle=0.8)
        q.click(198, 405, settle=1.5)
        q.shot("dialog-koloru-light-theme.png")
        q.key("esc")
        time.sleep(0.6)

        print("[OK] Test QEMU zakonczony sukcesem!")

    finally:
        try:
            q.call("quit")
        except Exception:
            pass
        try:
            proc.terminate()
            proc.wait(timeout=3)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        if QMP_PATH.exists():
            try:
                QMP_PATH.unlink()
            except OSError:
                pass

if __name__ == "__main__":
    main()
