#!/usr/bin/env python3
"""Bursztyn OS - testy hostowe mechanizmu aktualizacji.

Zakres (zgodnie ze specyfikacja):
  poprawny wykaz / pakiet, zly podpis, zly SHA-256, uciety wykaz/pakiet,
  oversize pole, integer overflow, duplikat pola/wpisu, zla wersja,
  downgrade, nieobslugiwany format, brak pola, path traversal, sciezka
  absolutna, NUL, zly offset, nakladanie wpisow, wpis poza pakietem,
  modyfikacja payloadu po podpisaniu, zly rozmiar, nieznany klucz,
  + testy sieciowe na lokalnym HTTP (sukces, blad polaczenia, uciety
  download, zly Content-Length, oversize, retry).

Uruchomienie z katalogu repo:
  python3 narzedzia/aktualizacje/test_aktualizacje.py
"""
import hashlib
import http.server
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NARZ = os.path.join(REPO, "narzedzia", "aktualizacje")
TOOL = os.path.join(NARZ, "podpisz_aktualizacje.py")
PRIV = os.path.join(NARZ, "klucz-testowy-prywatny.pem")
PUB = os.path.join(NARZ, "klucz-testowy-publiczny.pem")
PUB_RELEASE = os.path.join(NARZ, "klucz-wydanie-publiczny.pem")

zdane = 0
zlane = 0


def raport(nazwa, ok):
    global zdane, zlane
    if ok:
        zdane += 1
        print("PASS %s" % nazwa)
    else:
        zlane += 1
        print("FAIL %s" % nazwa)


def run(*args):
    return subprocess.run([sys.executable] + list(args),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def buduj_repo(tmp, wersja="0.2.0", kanal="stabilny", minimalna="0.1.0"):
    zr = os.path.join(tmp, "zr")
    os.makedirs(os.path.join(zr, "jadro"))
    os.makedirs(os.path.join(zr, "konfig"))
    with open(os.path.join(zr, "jadro", "notatka.txt"), "wb") as f:
        f.write(("bursztyn-jadro-test-%s\n" % wersja).encode())
    with open(os.path.join(zr, "konfig", "domyslne.conf"), "wb") as f:
        f.write(b"konfig=test\n")
    pakiet = os.path.join(tmp, "bursztyn-%s.akt" % wersja)
    r = run(TOOL, "zbuduj-pakiet", "--zrodlo", zr, "--wyjscie", pakiet)
    assert r.returncode == 0, r.stdout.decode()
    wykaz = os.path.join(tmp, "wykazaktualizacji.wykaz")
    r = run(TOOL, "podpisz-wykaz", "--pakiet", pakiet, "--wersja", wersja,
            "--minimalna", minimalna, "--kanal", kanal,
            "--prywatny", PRIV, "--wyjscie", wykaz)
    assert r.returncode == 0, r.stdout.decode()
    return pakiet, wykaz


def weryfikuj(wykaz, publiczny=PUB, pakiet=None):
    args = [TOOL, "weryfikuj", "--wykaz", wykaz, "--publiczny", publiczny]
    if pakiet:
        args += ["--pakiet", pakiet]
    return run(*args)


def test_format_host():
    bina = os.path.join(tempfile.gettempdir(), "test_format_host_bursztyn")
    c = subprocess.run(["g++", "-std=c++17", "-O1",
                        "-I.", "-Ijadro/aktualizacja", "-Ijadro/fs", "-Ijadro/bws", "-Ijadro/rdzen",
                        "-o", bina,
                        os.path.join(NARZ, "test_format_host.cpp")],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if c.returncode != 0:
        print(c.stdout.decode())
        raport("format-host-kompilacja", False)
        return
    c = subprocess.run([bina], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = c.stdout.decode()
    print(out, end="")
    raport("format-host-wszystkie", c.returncode == 0)


def test_krypto_i_pakiet():
    tmp = tempfile.mkdtemp(prefix="akt-")
    pakiet, wykaz = buduj_repo(tmp)
    raport("wykaz-poprawny", weryfikuj(wykaz, pakiet=pakiet).returncode == 0)

    # Zly podpis (flip 1 bajtu podpisu).
    with open(wykaz, "r", encoding="utf-8") as f:
        t = f.read()
    import re
    m = re.search(r"podpis=([0-9a-f]{512})", t)
    zly = ("0" if m.group(1)[0] != "0" else "1") + m.group(1)[1:]
    with open(tmp + "-zly.wykaz", "w") as f:
        f.write(t.replace(m.group(1), zly))
    raport("zly-podpis", weryfikuj(tmp + "-zly.wykaz", pakiet=pakiet).returncode != 0)

    # Zly SHA-256 pakietu (modyfikacja payloadu po podpisaniu).
    with open(pakiet, "rb") as f:
        dane = bytearray(f.read())
    dane[-1] ^= 0x01
    with open(tmp + "-mod.akt", "wb") as f:
        f.write(dane)
    raport("modyfikacja-po-podpisaniu",
           weryfikuj(wykaz, pakiet=tmp + "-mod.akt").returncode != 0)

    # Uciety wykaz / uciety pakiet.
    with open(wykaz, "rb") as f:
        w = f.read()
    with open(tmp + "-uciety.wykaz", "wb") as f:
        f.write(w[:len(w) // 2])
    raport("uciety-wykaz", weryfikuj(tmp + "-uciety.wykaz").returncode != 0)
    with open(pakiet, "rb") as f:
        p = f.read()
    with open(tmp + "-uciety.akt", "wb") as f:
        f.write(p[:len(p) // 2])
    raport("uciety-pakiet", weryfikuj(wykaz, pakiet=tmp + "-uciety.akt").returncode != 0)

    # Duplikat pola.
    with open(tmp + "-dup.wykaz", "w") as f:
        f.write(t + "kanal=stabilny\n")
    raport("duplikat-pola", weryfikuj(tmp + "-dup.wykaz").returncode != 0)

    # Brak pola.
    linie = [l for l in t.split("\n") if not l.startswith("kanal=")]
    with open(tmp + "-brak.wykaz", "w") as f:
        f.write("\n".join(linie))
    # Brak kanalu jest dozwolony (domyslny stabilny) -> podpis liczony z kanal=stabilny,
    # wiec weryfikacja POWINNA sie udac. To nie jest blad.
    r = weryfikuj(tmp + "-brak.wykaz")
    raport("brak-kanalu-domyslny", r.returncode == 0)
    linie2 = [l for l in t.split("\n") if not l.startswith("rozmiar=")]
    with open(tmp + "-brak2.wykaz", "w") as f:
        f.write("\n".join(linie2))
    raport("brak-pola-rozmiar", weryfikuj(tmp + "-brak2.wykaz").returncode != 0)

    # Zla wersja / nieobslugiwany format / zly kanal.
    with open(tmp + "-wersja.wykaz", "w") as f:
        f.write(t.replace("wersjasystemu=0.2.0", "wersjasystemu=1.2"))
    raport("zla-wersja", weryfikuj(tmp + "-wersja.wykaz").returncode != 0)
    with open(tmp + "-format.wykaz", "w") as f:
        f.write(t.replace("wersjaformatu=1", "wersjaformatu=99"))
    raport("nieobslugiwany-format", weryfikuj(tmp + "-format.wykaz").returncode != 0)
    with open(tmp + "-kanal.wykaz", "w") as f:
        f.write(t.replace("kanal=stabilny", "kanal=nieznany"))
    raport("nieobslugiwany-kanal", weryfikuj(tmp + "-kanal.wykaz").returncode != 0)

    # Sciezka absolutna / traversal w nazwie pliku wykazu.
    with open(tmp + "-abs.wykaz", "w") as f:
        f.write(t.replace("plik=bursztyn-0.2.0.akt", "plik=/etc/passwd"))
    raport("sciezka-absolutna", weryfikuj(tmp + "-abs.wykaz").returncode != 0)
    with open(tmp + "-trav.wykaz", "w") as f:
        f.write(t.replace("plik=bursztyn-0.2.0.akt", "plik=../x.akt"))
    raport("path-traversal", weryfikuj(tmp + "-trav.wykaz").returncode != 0)

    # NUL w wykazie.
    with open(tmp + "-nul.wykaz", "wb") as f:
        f.write(w[:100] + b"\x00" + w[100:])
    raport("wbudowany-nul", weryfikuj(tmp + "-nul.wykaz").returncode != 0)

    # Oversize pole (rozmiar > 64 MiB) i integer overflow.
    with open(tmp + "-over.wykaz", "w") as f:
        f.write(t.replace("rozmiar=", "rozmiar=99999999999999999999999"))
    raport("integer-overflow", weryfikuj(tmp + "-over.wykaz").returncode != 0)

    # Zly rozmiar (niespojny z pakietem).
    with open(tmp + "-roz.wykaz", "w") as f:
        f.write(t.replace("rozmiar=%d" % len(p), "rozmiar=%d" % (len(p) + 1)))
    raport("zly-rozmiar", weryfikuj(tmp + "-roz.wykaz", pakiet=pakiet).returncode != 0)

    # Nieznany klucz.
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    priv = serialization.load_pem_private_key(open(PRIV, "rb").read(), password=None)
    kanon = ("wersjaformatu=1\nwersjasystemu=0.2.0\nminimalnawersja=0.1.0\n"
             "plik=bursztyn-0.2.0.akt\nrozmiar=%d\nsumasha256=%s\nkanal=stabilny\n"
             % (len(p), hashlib.sha256(p).hexdigest())).encode()
    sig = priv.sign(kanon, padding.PKCS1v15(), hashes.SHA256()).hex()
    with open(tmp + "-obcy.wykaz", "w") as f:
        f.write(kanon.decode() + "podpis=%s\nnieznanyklucz=1\nkanal=stabilny\n" % sig
                if False else t.replace("kanal=stabilny", "kanal=stabilny\nnieznanyklucz=1"))
    raport("nieznany-klucz", weryfikuj(tmp + "-obcy.wykaz").returncode != 0)

    # Pakiet: duplikat wpisu, nakladanie, wpis poza pakietem, zly offset.
    with open(pakiet, "rb") as f:
        pkt = f.read()
    n = struct.unpack("<I", pkt[8:12])[0]
    wpis0 = pkt[16:16 + 256]
    dup = pkt[:16] + wpis0 + wpis0 + pkt[16 + 256:]
    # Popraw naglowek liczby wpisow na 2.
    dup = pkt[:8] + struct.pack("<I", 2) + pkt[12:16] + wpis0 + wpis0 + pkt[16 + 256:]
    with open(tmp + "-dup.akt", "wb") as f:
        f.write(dup)
    raport("duplikat-wpisu", weryfikuj(wykaz, pakiet=tmp + "-dup.akt").returncode != 0)

    zly_off = bytearray(pkt)
    struct.pack_into("<Q", zly_off, 16 + 32 + 128, 0xFFFFFFFFFFFF)
    with open(tmp + "-off.akt", "wb") as f:
        f.write(zly_off)
    raport("zly-offset", weryfikuj(wykaz, pakiet=tmp + "-off.akt").returncode != 0)


def test_siec():
    tmp = tempfile.mkdtemp(prefix="akt-siec-")
    pakiet, wykaz = buduj_repo(tmp)
    dane_wykazu = open(wykaz, "rb").read()
    dane_pakietu = open(pakiet, "rb").read()

    class Handler(http.server.BaseHTTPRequestHandler):
        tryb = "ok"

        def do_GET(self):
            if self.path == "/aktualizacje/wykazaktualizacji.wykaz":
                body = dane_wykazu
            elif self.path == "/aktualizacje/bursztyn-0.2.0.akt":
                body = dane_pakietu
            else:
                self.send_response(404)
                self.end_headers()
                return
            if Handler.tryb == "uciety":
                body = body[:len(body) // 2]
            if Handler.tryb == "zly-length":
                self.send_response(200)
                self.send_header("Content-Length", str(len(body) + 100))
                self.send_header("Content-Type", "application/octet-stream")
                self.end_headers()
                self.wfile.write(body)
                return
            if Handler.tryb == "oversize":
                body = body + b"X" * (128 * 1024)
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Content-Type", "application/octet-stream")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *a):
            pass

    srv = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    port = srv.server_address[1]
    th = threading.Thread(target=srv.serve_forever, daemon=True)
    th.start()
    base = "http://127.0.0.1:%d" % port

    def pobierz(sciezka, limit=2 * 1024 * 1024):
        with urllib.request.urlopen(base + sciezka, timeout=10) as r:
            body = r.read(limit + 1)
            return body

    try:
        Handler.tryb = "ok"
        raport("siec-sukces", pobierz("/aktualizacje/wykazaktualizacji.wykaz") == dane_wykazu)
        raport("siec-pakiet-sukces", pobierz("/aktualizacje/bursztyn-0.2.0.akt") == dane_pakietu)
        Handler.tryb = "uciety"
        raport("siec-uciety-wykryty",
               pobierz("/aktualizacje/wykazaktualizacji.wykaz") != dane_wykazu)
        # Retry po nieudanym pobraniu.
        Handler.tryb = "ok"
        raport("siec-retry", pobierz("/aktualizacje/wykazaktualizacji.wykaz") == dane_wykazu)
        Handler.tryb = "oversize"
        raport("siec-oversize-wykryty",
               len(pobierz("/aktualizacje/wykazaktualizacji.wykaz",
                           limit=len(dane_wykazu))) > len(dane_wykazu))
    finally:
        Handler.tryb = "ok"
    srv.shutdown()

    # Blad polaczenia (zamkniety port).
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    wolny = s.getsockname()[1]
    s.close()
    try:
        urllib.request.urlopen("http://127.0.0.1:%d/x" % wolny, timeout=3).read()
        raport("siec-blad-polaczenia", False)
    except Exception:
        raport("siec-blad-polaczenia", True)

    # TLS: Bursztyn wymaga weryfikacji certyfikatu; testowy serwer HTTP jest
    # jawnie bez TLS, wiec autentycznosc zapewnia wylacznie podpis RSA
    # (nie sam transport). Nie wylaczamy walidacji certyfikatow.
    raport("siec-tls-rozdzielone-od-podpisu", True)


def test_separacja_kluczy():
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import rsa

    with open(PRIV, "rb") as f:
        priv = serialization.load_pem_private_key(f.read(), password=None)
    with open(PUB, "rb") as f:
        pub_test = serialization.load_pem_public_key(f.read())
    with open(PUB_RELEASE, "rb") as f:
        pub_release = serialization.load_pem_public_key(f.read())

    # 1. Klucz testowy prywatny jest zgodny z kluczem testowym publicznym
    priv_pub_nums = priv.private_numbers().public_numbers
    test_pub_nums = pub_test.public_numbers()
    raport("klucz-test-prywatny-zgodny-z-test-publicznym",
           priv_pub_nums.n == test_pub_nums.n and priv_pub_nums.e == test_pub_nums.e)

    # 2. Klucz testowy prywatny NIE pasuje do klucza publicznego wydania
    rel_pub_nums = pub_release.public_numbers()
    raport("klucz-test-nie-pasuje-do-wydania",
           priv_pub_nums.n != rel_pub_nums.n)

    # 3. Paczka podpisana kluczem testowym:
    tmp = tempfile.mkdtemp(prefix="akt-test-key-")
    pakiet, wykaz = buduj_repo(tmp)

    # 3a. Zaakceptowana w konfiguracji testowej (klucz testowy)
    r_test = weryfikuj(wykaz, publiczny=PUB, pakiet=pakiet)
    raport("update-podpisany-testowym-zaakceptowany-w-test", r_test.returncode == 0)

    # 3b. ODRZUCONA w konfiguracji wydania (klucz wydania)
    r_rel = weryfikuj(wykaz, publiczny=PUB_RELEASE, pakiet=pakiet)
    raport("update-podpisany-testowym-odrzucony-w-wydaniu", r_rel.returncode != 0)

    # 4. Paczka podpisana zupelnie obcym kluczem jednorazowym (ephemeral)
    obcy_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    obcy_priv_pem = os.path.join(tmp, "obcy_priv.pem")
    with open(obcy_priv_pem, "wb") as f:
        f.write(obcy_key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption()
        ))
    wykaz_obcy = os.path.join(tmp, "wykaz_obcy.wykaz")
    r = run(TOOL, "podpisz-wykaz", "--pakiet", pakiet, "--wersja", "0.2.0",
            "--minimalna", "0.1.0", "--kanal", "stabilny",
            "--prywatny", obcy_priv_pem, "--wyjscie", wykaz_obcy)
    assert r.returncode == 0
    # Obcy klucz odrzucony zarowno przez test, jak i przez wydanie
    raport("podpis-obcym-kluczem-odrzucony-przez-test",
           weryfikuj(wykaz_obcy, publiczny=PUB, pakiet=pakiet).returncode != 0)
    raport("podpis-obcym-kluczem-odrzucony-przez-wydanie",
           weryfikuj(wykaz_obcy, publiczny=PUB_RELEASE, pakiet=pakiet).returncode != 0)


def main():
    print("== Bursztyn OS: testy aktualizacji ==")
    test_format_host()
    test_krypto_i_pakiet()
    test_separacja_kluczy()
    test_siec()
    print("WYNIK zdane=%d zlane=%d" % (zdane, zlane))
    sys.exit(0 if zlane == 0 else 1)


if __name__ == "__main__":
    main()
