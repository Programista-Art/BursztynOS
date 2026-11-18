"""Autorytatywna numeracja BWS: parsuje enum z jadro/bws/bws_api.h zamiast twardo kodowac 'nastepny wolny numer'."""
import re
from pathlib import Path


def wczytaj_numeracje(root):
    """Zwraca {nazwa: numer} dla wszystkich BWS_* = <liczba> z enum w bws_api.h."""
    tekst = (Path(root) / "jadro/bws/bws_api.h").read_text(encoding="utf-8", errors="replace")
    tekst = re.sub(r"/\*.*?\*/", "", tekst, flags=re.S)      # komentarze nie definiuja numerow
    tekst = re.sub(r"//[^\n]*", "", tekst)
    wynik = {}
    for nazwa, num in re.findall(r"\b(BWS_[A-Z0-9_]+)\s*=\s*(\d+)\s*[,}\n]", tekst):
        wynik[nazwa] = int(num)
    return wynik


def zwaliduj_numeracje(num, wymagane):
    """Zwraca liste bledow. `wymagane` = {nazwa: numer}, ktore pakiet zaklada (np. BWS_HDA_DIAG: 89).
    Sprawdza: wymagane numery, unikalnosc, BWS_PIERWSZY_WOLNY wiekszy od kazdego numeru, 99 zarezerwowane jako
    nieprawidlowe (zadna nazwa nie moze go zajac)."""
    bledy = []
    if "BWS_PIERWSZY_WOLNY" not in num:
        return ["brak BWS_PIERWSZY_WOLNY"]
    for nazwa, oczekiwany in wymagane.items():
        if num.get(nazwa) != oczekiwany:
            bledy.append(f"{nazwa}: oczekiwano {oczekiwany}, jest {num.get(nazwa)}")
    uzywane = {n: v for n, v in num.items() if n != "BWS_PIERWSZY_WOLNY"}
    odwrotnie = {}
    for n, v in uzywane.items():
        odwrotnie.setdefault(v, []).append(n)
    for v, nazwy in odwrotnie.items():
        if len(nazwy) > 1:
            bledy.append(f"numer {v} uzyty wielokrotnie: {nazwy}")
    if 99 in odwrotnie:
        bledy.append(f"numer 99 jest zarezerwowany jako nieprawidlowy, a zajmuje go {odwrotnie[99]}")
    pierwszy = num["BWS_PIERWSZY_WOLNY"]
    if uzywane and pierwszy <= max(uzywane.values()):
        bledy.append(f"BWS_PIERWSZY_WOLNY={pierwszy} nie jest wiekszy od najwyzszego numeru {max(uzywane.values())}")
    return bledy
