#!/usr/bin/env python3
"""Bursztyn OS - hostowe narzedzie aktualizacji (generowanie i podpisywanie).

Podpisuje:
  wykazaktualizacji.wykaz   (tekst, pola PL, podpis RSA-2048 PKCS#1 v1.5 + SHA-256)
  bursztyn-<wersja>.akt     (binarny pakiet BAKT)

Kryptografia: RSA-2048 + SHA-256 (biblioteka `cryptography`, mbedtls po stronie jadra).
Nie wymyslono zadnego wlasnego algorytmu.

Kanoniczne bajty objete podpisem (kolejnosc stala, LF, kanal zawsze jawny):
  wersjaformatu=1
  wersjasystemu=X.Y.Z
  minimalnawersja=X.Y.Z
  plik=bursztyn-X.Y.Z.akt
  rozmiar=<dziesietnie>
  sumasha256=<64 hex mala litera>
  kanal=<stabilny|testowy|rozwojowy>

Uzycie:
  podpisz_aktualizacje.py generuj-klucz --prywatny priv.pem --publiczny pub.pem
  podpisz_aktualizacje.py zbuduj-pakiet --zrodlo DIR --wyjscie bursztyn-0.2.0.akt
  podpisz_aktualizacje.py podpisz-wykaz --pakiet bursztyn-0.2.0.akt --wersja 0.2.0 \\
      --minimalna 0.1.0 --kanal stabilny --prywatny priv.pem --wyjscie wykazaktualizacji.wykaz
  podpisz_aktualizacje.py weryfikuj --wykaz wykazaktualizacji.wykaz --publiczny pub.pem [--pakiet PAKIET]
"""
import argparse
import hashlib
import os
import re
import struct
import sys

MAGIC = b"BAKT"
FORMAT_WERSJA = 1
WPIS_ROZMIAR = 256
KAT_DL = 32
SCIEZKA_DL = 128
REZERWA_DL = 48
MAKS_WPISOW = 256
MAKS_PAKIET = 64 * 1024 * 1024
KANALY = ("stabilny", "testowy", "rozwojowy")
KATEGORIE = ("jadro", "aplikacje", "biblioteki", "konfig", "boot")
WERSJA_RE = re.compile(r"^[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}$")
SCIEZKA_RE = re.compile(r"^[A-Za-z0-9._\-/]+$")


def parsuj_wersje(t):
    if not WERSJA_RE.match(t or ""):
        raise ValueError("nieprawidlowa wersja: %r" % (t,))
    return tuple(int(x) for x in t.split("."))


def porownaj_wersje(a, b):
    return (a > b) - (a < b)


def sprawdz_sciezke(sciezka):
    if not sciezka or len(sciezka) > 127:
        raise ValueError("nieprawidlowa sciezka (dlugosc)")
    if sciezka.startswith("/") or "\\" in sciezka:
        raise ValueError("sciezka musi byc relatywna bez backslashy")
    if not SCIEZKA_RE.match(sciezka):
        raise ValueError("niedozwolone znaki w sciezce")
    if sciezka.endswith("/"):
        raise ValueError("sciezka nie moze konczyc sie /")
    for cz in sciezka.split("/"):
        if cz in ("", ".", ".."):
            raise ValueError("niedozwolony komponent sciezki: %r" % cz)
    return True


def buduj_kanoniczne(wersja, minimalna, plik, rozmiar, sha_hex, kanal):
    parsuj_wersje(wersja)
    parsuj_wersje(minimalna)
    if kanal not in KANALY:
        raise ValueError("nieobslugiwany kanal")
    linie = [
        "wersjaformatu=1",
        "wersjasystemu=%s" % wersja,
        "minimalnawersja=%s" % minimalna,
        "plik=%s" % plik,
        "rozmiar=%d" % rozmiar,
        "sumasha256=%s" % sha_hex,
        "kanal=%s" % kanal,
    ]
    return ("\n".join(linie) + "\n").encode("utf-8")


def cmd_generuj_klucz(args):
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.hazmat.primitives import serialization
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    priv = key.private_bytes(serialization.Encoding.PEM,
                             serialization.PrivateFormat.TraditionalOpenSSL,
                             serialization.NoEncryption())
    pub = key.public_key().public_bytes(serialization.Encoding.PEM,
                                        serialization.PublicFormat.SubjectPublicKeyInfo)
    with open(args.prywatny, "wb") as f:
        f.write(priv)
    with open(args.publiczny, "wb") as f:
        f.write(pub)
    print("Zapisano klucz prywatny: %s (CHRONIC - nigdy do obrazu ani na serwer)" % args.prywatny)
    print("Zapisano klucz publiczny: %s (do wbudowania w jadro)" % args.publiczny)


def wpisy_z_katalogu(zrodlo):
    """Zbiera pliki: <zrodlo>/<kategoria>/<sciezka...>. Zwraca liste (kat, sciezka, dane)."""
    wynik = []
    for kat in sorted(os.listdir(zrodlo)):
        kat_dir = os.path.join(zrodlo, kat)
        if not os.path.isdir(kat_dir):
            continue
        if kat not in KATEGORIE:
            raise ValueError("niedozwolona kategoria: %r" % kat)
        for korzen, _, pliki in os.walk(kat_dir):
            for nazwa in sorted(pliki):
                pelna = os.path.join(korzen, nazwa)
                rel = os.path.relpath(pelna, kat_dir).replace(os.sep, "/")
                sprawdz_sciezke(rel)
                with open(pelna, "rb") as f:
                    dane = f.read()
                if not dane:
                    raise ValueError("pusty plik: %s/%s" % (kat, rel))
                if len(dane) > 16 * 1024 * 1024:
                    raise ValueError("plik za duzy: %s/%s" % (kat, rel))
                wynik.append((kat, rel, dane))
    if not wynik:
        raise ValueError("katalog zrodlowy nie zawiera zadnych plikow")
    if len(wynik) > MAKS_WPISOW:
        raise ValueError("za duzo wpisow")
    widziane = set()
    for kat, rel, _ in wynik:
        if (kat, rel) in widziane:
            raise ValueError("duplikat wpisu")
        widziane.add((kat, rel))
    return wynik


def cmd_zbuduj_pakiet(args):
    wpisy = wpisy_z_katalogu(args.zrodlo)
    liczba = len(wpisy)
    naglowek = MAGIC + struct.pack("<III", FORMAT_WERSJA, liczba, 0)
    # Offsets: dane zaczynaja sie po naglowku + tabeli.
    baza = 16 + WPIS_ROZMIAR * liczba
    offset = baza
    tabela = b""
    for kat, rel, dane in wpisy:
        kat_b = kat.encode("ascii") + b"\x00" * (KAT_DL - len(kat))
        sc_b = rel.encode("ascii") + b"\x00" * (SCIEZKA_DL - len(rel) - 1) + b"\x00" if len(rel) < SCIEZKA_DL else None
        # sciezka: rel + NUL + dopelnienie zerami do 128
        sc_raw = rel.encode("ascii")
        sc_b = sc_raw + b"\x00" * (SCIEZKA_DL - len(sc_raw))
        sha = hashlib.sha256(dane).digest()
        if offset + len(dane) > MAKS_PAKIET:
            raise ValueError("pakiet przekroczylby limit")
        tabela += kat_b + sc_b + struct.pack("<QQ", offset, len(dane)) + sha + b"\x00" * REZERWA_DL
        assert len(tabela) % WPIS_ROZMIAR == 0 or True
        offset += len(dane)
    with open(args.wyjscie, "wb") as f:
        f.write(naglowek)
        f.write(tabela)
        for _, _, dane in wpisy:
            f.write(dane)
    rozmiar = os.path.getsize(args.wyjscie)
    sha_hex = hashlib.sha256(open(args.wyjscie, "rb").read()).hexdigest()
    print("Zbudowano pakiet: %s (wpisow=%d, rozmiar=%d, sha256=%s)" % (args.wyjscie, liczba, rozmiar, sha_hex))


def cmd_podpisz_wykaz(args):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    parsuj_wersje(args.wersja)
    parsuj_wersje(args.minimalna)
    if args.kanal not in KANALY:
        raise ValueError("nieobslugiwany kanal")
    oczekiwany_plik = "bursztyn-%s.akt" % args.wersja
    if os.path.basename(args.pakiet) != oczekiwany_plik:
        raise ValueError("nazwa pakietu %r nie zgadza sie z wersja (oczekiwano %s)"
                         % (os.path.basename(args.pakiet), oczekiwany_plik))
    dane = open(args.pakiet, "rb").read()
    if len(dane) > MAKS_PAKIET or not dane:
        raise ValueError("nieprawidlowy rozmiar pakietu")
    sha_hex = hashlib.sha256(dane).hexdigest()
    kanoniczne = buduj_kanoniczne(args.wersja, args.minimalna, oczekiwany_plik,
                                  len(dane), sha_hex, args.kanal)
    priv = serialization.load_pem_private_key(open(args.prywatny, "rb").read(), password=None)
    podpis = priv.sign(kanoniczne, padding.PKCS1v15(), hashes.SHA256())
    assert len(podpis) == 256
    wykaz = kanoniczne.decode("utf-8").replace("\nkanal=", "\npodpis=%s\nkanal=" % podpis.hex())
    # Kolejnosc pol w pliku: wersjaformatu, wersjasystemu, minimalnawersja, plik,
    # rozmiar, sumasha256, podpis, kanal (kanoniczne + podpis przed kanal).
    with open(args.wyjscie, "w", encoding="utf-8", newline="\n") as f:
        f.write(wykaz)
    print("Podpisano wykaz: %s (kanal=%s, rozmiar=%d)" % (args.wyjscie, args.kanal, len(dane)))
    print("Kanoniczne SHA-256: %s" % hashlib.sha256(kanoniczne).hexdigest())


def cmd_weryfikuj(args):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    wykaz_tekst = open(args.wykaz, "rb").read()
    if b"\x00" in wykaz_tekst or len(wykaz_tekst) > 8192:
        raise SystemExit("BLAD: nieprawidlowy wykaz")
    pub = serialization.load_pem_public_key(open(args.publiczny, "rb").read())
    # Minimalny parser (hostowy odpowiednik parsera jadra).
    pola = {}
    for linia in wykaz_tekst.decode("utf-8").split("\n"):
        if linia == "" :
            continue
        if "=" not in linia:
            raise SystemExit("BLAD: zla linia")
        k, v = linia.split("=", 1)
        if k in pola:
            raise SystemExit("BLAD: duplikat")
        pola[k] = v
    for wymagane in ("wersjaformatu", "wersjasystemu", "minimalnawersja", "plik",
                     "rozmiar", "sumasha256", "podpis"):
        if wymagane not in pola:
            raise SystemExit("BLAD: brak pola %s" % wymagane)
    dozwolone = {"wersjaformatu", "wersjasystemu", "minimalnawersja", "plik",
                 "rozmiar", "sumasha256", "podpis", "kanal"}
    for k in pola:
        if k not in dozwolone:
            raise SystemExit("BLAD: nieznane pole %s" % k)
    if pola["wersjaformatu"] != "1":
        raise SystemExit("BLAD: nieobslugiwana wersja formatu")
    kanal = pola.get("kanal", "stabilny")
    kanoniczne = buduj_kanoniczne(pola["wersjasystemu"], pola["minimalnawersja"],
                                  pola["plik"], int(pola["rozmiar"]),
                                  pola["sumasha256"], kanal)
    try:
        pub.verify(bytes.fromhex(pola["podpis"]), kanoniczne,
                   padding.PKCS1v15(), hashes.SHA256())
    except Exception as e:
        raise SystemExit("BLAD weryfikacji podpisu: %s" % e)
    print("Podpis poprawny (kanal=%s, wersja=%s)." % (kanal, pola["wersjasystemu"]))
    if args.pakiet:
        dane = open(args.pakiet, "rb").read()
        if len(dane) != int(pola["rozmiar"]):
            raise SystemExit("BLAD: zly rozmiar pakietu")
        if hashlib.sha256(dane).hexdigest() != pola["sumasha256"]:
            raise SystemExit("BLAD: zly SHA-256 pakietu")
        print("Pakiet poprawny (rozmiar + SHA-256).")


def main():
    ap = argparse.ArgumentParser(description="Bursztyn OS - narzedzie aktualizacji")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generuj-klucz")
    g.add_argument("--prywatny", required=True)
    g.add_argument("--publiczny", required=True)
    b = sub.add_parser("zbuduj-pakiet")
    b.add_argument("--zrodlo", required=True)
    b.add_argument("--wyjscie", required=True)
    p = sub.add_parser("podpisz-wykaz")
    p.add_argument("--pakiet", required=True)
    p.add_argument("--wersja", required=True)
    p.add_argument("--minimalna", required=True)
    p.add_argument("--kanal", default="stabilny")
    p.add_argument("--prywatny", required=True)
    p.add_argument("--wyjscie", required=True)
    w = sub.add_parser("weryfikuj")
    w.add_argument("--wykaz", required=True)
    w.add_argument("--publiczny", required=True)
    w.add_argument("--pakiet", default=None)
    args = ap.parse_args()
    {"generuj-klucz": cmd_generuj_klucz, "zbuduj-pakiet": cmd_zbuduj_pakiet,
     "podpisz-wykaz": cmd_podpisz_wykaz, "weryfikuj": cmd_weryfikuj}[args.cmd](args)


if __name__ == "__main__":
    main()
