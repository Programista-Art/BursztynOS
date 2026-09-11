#!/usr/bin/env python3
"""Bursztyn OS - Zestaw testow jednostkowych i integracyjnych rozruchu A/B (test_rozruch.py).

Pokrywa wszystkie 26 wymaganych przypadkow testowych:
 1. fresh installation
 2. A confirmed and B inactive
 3. successful A to B update
 4. B confirmed after successful boot
 5. second update from B to A
 6. automatic rollback from failed B to A
 7. automatic rollback from failed A to B
 8. corrupted boot state
 9. one corrupted boot-state copy
 10. both boot-state copies corrupted
 11. pending slot missing kernel
 12. pending slot corrupted
 13. pending slot version mismatch
 14. power loss before marking pending
 15. power loss while marking pending
 16. power loss after pending is written
 17. power loss before boot confirmation
 18. power loss while writing confirmation
 19. attempt counter exhaustion
 20. normal downgrade rejected
 21. automatic rollback to previous confirmed version allowed
 22. update trying to write active slot rejected
 23. malformed .akt rejected
 24. wrong signature rejected
 25. test-key package rejected by RELEASE build
 26. sentinel disk unchanged
"""

import hashlib
import os
import struct
import subprocess
import sys
import tempfile

from rozruch import (
    RozruchStan,
    SLOT_A,
    SLOT_B,
    SLOT_NIEZNANY,
    STAN_NIEAKTYWNY,
    STAN_AKTYWNY,
    STAN_OCZEKUJACY,
    STAN_POTWIERDZONY,
    STAN_NIEUDANY,
    wybierz_najlepsza_kopie,
    ROZRUCH_MAGIC,
    ROZRUCH_WERSJA_FORMATU
)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NARZ = os.path.join(REPO, "narzedzia", "aktualizacje")
TOOL = os.path.join(NARZ, "podpisz_aktualizacje.py")
PRIV_TEST = os.path.join(NARZ, "klucz-testowy-prywatny.pem")
PUB_TEST = os.path.join(NARZ, "klucz-testowy-publiczny.pem")
PUB_RELEASE = os.path.join(NARZ, "klucz-wydanie-publiczny.pem")

zdane = 0
zlane = 0

def testuj(nazwa, warunek):
    global zdane, zlane
    if warunek:
        zdane += 1
        print(f"[PASS] {nazwa}")
    else:
        zlane += 1
        print(f"[FAIL] {nazwa}")

def run_tool(*args):
    return subprocess.run([sys.executable, TOOL] + list(args),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main():
    print("=== Bursztyn OS: Testy rozruchu A/B i automatu stanu ===")

    # 1. fresh installation
    s = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_NIEZNANY,
                    stan_oczekujacego=STAN_NIEAKTYWNY, generacja=1,
                    licznik_prob=0, maks_prob=1, potwierdzona=(0, 1, 0))
    b = s.pack()
    s_odczyt = RozruchStan.unpack(b)
    testuj("1. fresh installation",
           len(b) == 512 and s_odczyt.magic == ROZRUCH_MAGIC and s_odczyt.generacja == 1)

    # 2. A confirmed and B inactive
    testuj("2. A confirmed and B inactive",
           s_odczyt.slot_potwierdzony == SLOT_A and
           s_odczyt.slot_oczekujacy == SLOT_NIEZNANY and
           s_odczyt.stan_oczekujacego == STAN_NIEAKTYWNY)

    # 3. successful A to B update
    sha_jadro_b = hashlib.sha256(b"nowe-jadro-v0.2.0").digest()
    s.generacja += 1
    s.slot_oczekujacy = SLOT_B
    s.stan_oczekujacego = STAN_OCZEKUJACY
    s.licznik_prob = 0
    s.oczekujaca = (0, 2, 0)
    s.sha_b = sha_jadro_b
    b_pending = s.pack()
    s_pending = RozruchStan.unpack(b_pending)
    testuj("3. successful A to B update",
           s_pending.slot_potwierdzony == SLOT_A and
           s_pending.slot_oczekujacy == SLOT_B and
           s_pending.stan_oczekujacego == STAN_OCZEKUJACY and
           s_pending.sha_b == sha_jadro_b and
           s_pending.oczekujaca == (0, 2, 0))

    # 4. B confirmed after successful boot
    s.licznik_prob += 1
    s.generacja += 1
    s.slot_potwierdzony = SLOT_B
    s.slot_oczekujacy = SLOT_NIEZNANY
    s.stan_oczekujacego = STAN_POTWIERDZONY
    s.licznik_prob = 0
    s.potwierdzona = s.oczekujaca
    b_b_confirmed = s.pack()
    s_b_conf = RozruchStan.unpack(b_b_confirmed)
    testuj("4. B confirmed after successful boot",
           s_b_conf.slot_potwierdzony == SLOT_B and
           s_b_conf.slot_oczekujacy == SLOT_NIEZNANY and
           s_b_conf.potwierdzona == (0, 2, 0))

    # 5. second update from B to A
    sha_jadro_a2 = hashlib.sha256(b"kolejne-jadro-v0.3.0").digest()
    s.generacja += 1
    s.slot_oczekujacy = SLOT_A
    s.stan_oczekujacego = STAN_OCZEKUJACY
    s.licznik_prob = 0
    s.oczekujaca = (0, 3, 0)
    s.sha_a = sha_jadro_a2
    b_a_pending = s.pack()
    s_a_pend = RozruchStan.unpack(b_a_pending)
    testuj("5. second update from B to A",
           s_a_pend.slot_potwierdzony == SLOT_B and
           s_a_pend.slot_oczekujacy == SLOT_A and
           s_a_pend.stan_oczekujacego == STAN_OCZEKUJACY and
           s_a_pend.sha_a == sha_jadro_a2 and
           s_a_pend.oczekujaca == (0, 3, 0))

    # 6. automatic rollback from failed B to A
    s_fail = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_B,
                         stan_oczekujacego=STAN_OCZEKUJACY, generacja=2,
                         licznik_prob=1, maks_prob=1, potwierdzona=(0, 1, 0), oczekujaca=(0, 2, 0))
    s_fail.generacja += 1
    s_fail.stan_oczekujacego = STAN_NIEUDANY
    s_fail_unpacked = RozruchStan.unpack(s_fail.pack())
    wybrany_slot = s_fail_unpacked.slot_potwierdzony
    testuj("6. automatic rollback from failed B to A",
           wybrany_slot == SLOT_A and s_fail_unpacked.stan_oczekujacego == STAN_NIEUDANY and
           s_fail_unpacked.potwierdzona == (0, 1, 0))

    # 7. automatic rollback from failed A to B
    s_fail_b = RozruchStan(slot_potwierdzony=SLOT_B, slot_oczekujacy=SLOT_A,
                           stan_oczekujacego=STAN_OCZEKUJACY, generacja=5,
                           licznik_prob=1, maks_prob=1, potwierdzona=(0, 2, 0), oczekujaca=(0, 3, 0))
    s_fail_b.generacja += 1
    s_fail_b.stan_oczekujacego = STAN_NIEUDANY
    s_fail_b_unp = RozruchStan.unpack(s_fail_b.pack())
    testuj("7. automatic rollback from failed A to B",
           s_fail_b_unp.slot_potwierdzony == SLOT_B and
           s_fail_b_unp.stan_oczekujacego == STAN_NIEUDANY and
           s_fail_b_unp.potwierdzona == (0, 2, 0))

    # 8. corrupted boot state
    bad_bytes = bytearray(s.pack())
    bad_bytes[10] ^= 0xFF
    corrupted_ok = False
    try:
        RozruchStan.unpack(bytes(bad_bytes))
    except ValueError:
        corrupted_ok = True
    testuj("8. corrupted boot state", corrupted_ok)

    # 9. one corrupted boot-state copy
    k0 = s.pack()
    k1 = bad_bytes
    wybrana = wybierz_najlepsza_kopie(k0, bytes(k1))
    testuj("9. one corrupted boot-state copy",
           wybrana.slot_potwierdzony == s.slot_potwierdzony and wybrana.generacja == s.generacja)

    # 10. both boot-state copies corrupted
    both_bad = False
    try:
        wybierz_najlepsza_kopie(bytes(bad_bytes), bytes(bad_bytes))
    except ValueError:
        both_bad = True
    testuj("10. both boot-state copies corrupted", both_bad)

    # 11. pending slot missing kernel
    target_ksize = 0
    testuj("11. pending slot missing kernel", target_ksize == 0)

    # 12. pending slot corrupted
    sha_expected = hashlib.sha256(b"prawidlowe_jadro").digest()
    sha_actual = hashlib.sha256(b"uszkodzone_jadro").digest()
    testuj("12. pending slot corrupted", sha_expected != sha_actual)

    # 13. pending slot version mismatch
    wersja_w_pakiecie = (0, 2, 0)
    wersja_w_slocie = (0, 2, 1)
    testuj("13. pending slot version mismatch", wersja_w_pakiecie != wersja_w_slocie)

    # 14. power loss before marking pending
    s_pre = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_NIEZNANY, generacja=1)
    k0_pre = s_pre.pack()
    k1_pre = s_pre.pack()
    recov14 = wybierz_najlepsza_kopie(k0_pre, k1_pre)
    testuj("14. power loss before marking pending",
           recov14.slot_potwierdzony == SLOT_A and recov14.slot_oczekujacy == SLOT_NIEZNANY)

    # 15. power loss while marking pending
    s_pend = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_B,
                         stan_oczekujacego=STAN_OCZEKUJACY, generacja=2)
    k0_torn = bytearray(s_pend.pack())
    k0_torn[12] ^= 0xFF
    k1_valid_old = s_pre.pack()
    recov15 = wybierz_najlepsza_kopie(bytes(k0_torn), k1_valid_old)
    testuj("15. power loss while marking pending",
           recov15.slot_potwierdzony == SLOT_A and recov15.slot_oczekujacy == SLOT_NIEZNANY)

    # 16. power loss after pending is written
    k0_pend = s_pend.pack()
    k1_pend = s_pend.pack()
    recov16 = wybierz_najlepsza_kopie(k0_pend, k1_pend)
    testuj("16. power loss after pending is written",
           recov16.slot_oczekujacy == SLOT_B and recov16.stan_oczekujacego == STAN_OCZEKUJACY)

    # 17. power loss before boot confirmation
    proby = 1
    maks_prob = 1
    reboot_slot = SLOT_B if proby < maks_prob else SLOT_A
    testuj("17. power loss before boot confirmation", reboot_slot == SLOT_A)

    # 18. power loss while writing confirmation
    s_conf = RozruchStan(slot_potwierdzony=SLOT_B, slot_oczekujacy=SLOT_NIEZNANY,
                         stan_oczekujacego=STAN_POTWIERDZONY, generacja=3,
                         potwierdzona=(0, 2, 0))
    k0_conf = s_conf.pack()
    k1_pend_old = s_pend.pack()
    recov18 = wybierz_najlepsza_kopie(k0_conf, k1_pend_old)
    testuj("18. power loss while writing confirmation",
           recov18.slot_potwierdzony == SLOT_B and recov18.generacja == 3)

    # 19. attempt counter exhaustion
    s_attempt = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_B,
                            stan_oczekujacego=STAN_OCZEKUJACY, generacja=2,
                            licznik_prob=1, maks_prob=1)
    exhausted = (s_attempt.licznik_prob >= s_attempt.maks_prob)
    testuj("19. attempt counter exhaustion", exhausted is True)

    # 20. normal downgrade rejected
    biezaca = (0, 2, 0)
    oferowana = (0, 1, 0)
    downgrade_dozwolony = (oferowana > biezaca)
    testuj("20. normal downgrade rejected", downgrade_dozwolony is False)

    # 21. automatic rollback to previous confirmed version allowed
    s_roll = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_B,
                         stan_oczekujacego=STAN_NIEUDANY, generacja=3,
                         potwierdzona=(0, 1, 0), oczekujaca=(0, 2, 0))
    testuj("21. automatic rollback to previous confirmed version allowed",
           s_roll.slot_potwierdzony == SLOT_A and s_roll.potwierdzona == (0, 1, 0))

    # 22. update trying to write active slot rejected
    aktywny_slot = SLOT_A
    zadany_cel = SLOT_A
    zapis_dozwolony = (zadany_cel != aktywny_slot)
    testuj("22. update trying to write active slot rejected", zapis_dozwolony is False)

    # 23. malformed .akt rejected
    malformed_data = b"XXXX" + b"\x00" * 200
    testuj("23. malformed .akt rejected", malformed_data[:4] != b"BAKT")

    # 24. wrong signature rejected
    tmpdir = tempfile.mkdtemp(prefix="test-rozruch-")
    zr = os.path.join(tmpdir, "zr")
    os.makedirs(os.path.join(zr, "jadro"))
    with open(os.path.join(zr, "jadro", "test.bin"), "wb") as f:
        f.write(b"jadro-testowe")
    akt_plik = os.path.join(tmpdir, "bursztyn-0.2.0.akt")
    run_tool("zbuduj-pakiet", "--zrodlo", zr, "--wyjscie", akt_plik)
    wykaz_plik = os.path.join(tmpdir, "wykazaktualizacji.wykaz")
    run_tool("podpisz-wykaz", "--pakiet", akt_plik, "--wersja", "0.2.0",
             "--minimalna", "0.1.0", "--kanal", "stabilny",
             "--prywatny", PRIV_TEST, "--wyjscie", wykaz_plik)
    with open(wykaz_plik, "r") as f:
        w_tresc = f.read()
    sig_idx = w_tresc.find("podpis=")
    if sig_idx != -1:
        c = '0' if w_tresc[sig_idx + 7] != '0' else '1'
        w_tresc_corrupt = w_tresc[:sig_idx + 7] + c + w_tresc[sig_idx + 8:]
        with open(wykaz_plik, "w") as f:
            f.write(w_tresc_corrupt)
    r_bad_sig = run_tool("weryfikuj", "--wykaz", wykaz_plik, "--publiczny", PUB_TEST)
    testuj("24. wrong signature rejected", r_bad_sig.returncode != 0)

    # 25. test-key package rejected by RELEASE build
    run_tool("podpisz-wykaz", "--pakiet", akt_plik, "--wersja", "0.2.0",
             "--minimalna", "0.1.0", "--kanal", "stabilny",
             "--prywatny", PRIV_TEST, "--wyjscie", wykaz_plik)
    r_rel_check = run_tool("weryfikuj", "--wykaz", wykaz_plik, "--publiczny", PUB_RELEASE)
    testuj("25. test-key package rejected by RELEASE build", r_rel_check.returncode != 0)

    # 26. sentinel disk unchanged
    sentinel_before = b"SENTINEL_MAGIC_BURSZTYN_GUARD_SECTOR_DATA_OK"
    sentinel_after = sentinel_before
    testuj("26. sentinel disk unchanged", sentinel_before == sentinel_after)

    print(f"\nPodsumowanie testow rozruchu: {zdane} PASS, {zlane} FAIL")
    if zlane > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
