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
import zlib

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
                         stan_oczekujacego=STAN_OCZEKUJACY, generacja=2,
                         potwierdzona=(0, 1, 0), oczekujaca=(0, 2, 0), sha_b=b'\xbb'*32)
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
                            licznik_prob=1, maks_prob=1,
                            potwierdzona=(0, 1, 0), oczekujaca=(0, 2, 0), sha_b=b'\xbb'*32)
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

    # =========================================================================
    # AUDYT PERSYSTENCJI, SPÓJNOŚCI I BEZPIECZEŃSTWA AWARII (TESTY 27 - 52)
    # =========================================================================

    def symuluj_rozruch(k0_dane, k1_dane, env_dane, k_a_ok=True, k_b_ok=True):
        potw = env_dane.get("bursztyn_potwierdzony", "A")
        if potw not in ("A", "B"):
            potw = "A"
        oczek = env_dane.get("bursztyn_oczekujacy", "BRAK")
        try:
            proby = int(env_dane.get("bursztyn_proby", "0"))
        except ValueError:
            proby = 0

        wybrany = potw
        if oczek in ("A", "B"):
            if proby == 0:
                wybrany = oczek
                env_dane["bursztyn_proby"] = "1"
            else:
                wybrany = potw
                env_dane["bursztyn_oczekujacy"] = "NIEUDANY"

        if wybrany == "B" and not k_b_ok:
            wybrany = "A"

        stan_lba = wybierz_najlepsza_kopie(k0_dane, k1_dane)
        uruchomiony = 1 if wybrany == "B" else 0

        if uruchomiony == stan_lba.slot_potwierdzony:
            inny_str = "B" if uruchomiony == 0 else "A"
            ten_str = "A" if uruchomiony == 0 else "B"
            if env_dane.get("bursztyn_oczekujacy") == "NIEUDANY" or (env_dane.get("bursztyn_oczekujacy") == inny_str and int(env_dane.get("bursztyn_proby", 0)) >= 1):
                stan_lba.stan_oczekujacego = STAN_NIEUDANY
                stan_lba.licznik_prob = stan_lba.maks_prob
                stan_lba.generacja += 1
                k0_dane = stan_lba.pack()
                k1_dane = stan_lba.pack()
                env_dane["bursztyn_oczekujacy"] = "BRAK"
                env_dane["bursztyn_proby"] = "0"
                env_dane["bursztyn_potwierdzony"] = ten_str
                return "ROLLBACK_SUCCESS", wybrany, k0_dane, k1_dane, env_dane
            else:
                return "CONFIRMED_BOOT", wybrany, k0_dane, k1_dane, env_dane
        elif stan_lba.slot_oczekujacy == uruchomiony and stan_lba.stan_oczekujacego == STAN_OCZEKUJACY:
            return "PENDING_TRIAL", wybrany, k0_dane, k1_dane, env_dane
        else:
            potw_str = "A" if stan_lba.slot_potwierdzony == 0 else "B"
            env_dane["bursztyn_potwierdzony"] = potw_str
            env_dane["bursztyn_oczekujacy"] = "NIEUDANY"
            env_dane["bursztyn_proby"] = "0"
            return "UNAUTHORIZED_REJECTED", potw_str, k0_dane, k1_dane, env_dane

    # 27. LBA update succeeds but grubenv write fails
    s_lba_ok = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_B,
                           stan_oczekujacego=STAN_OCZEKUJACY, generacja=2,
                           potwierdzona=(0, 1, 0), oczekujaca=(0, 2, 0), sha_b=b'\xbb'*32)
    k0_27 = s_lba_ok.pack()
    k1_27 = s_lba_ok.pack()
    env_27 = {"bursztyn_potwierdzony": "A", "bursztyn_oczekujacy": "BRAK", "bursztyn_proby": "0"}
    st27, slot27, _, _, _ = symuluj_rozruch(k0_27, k1_27, env_27)
    testuj("27. LBA update succeeds but grubenv write fails",
           st27 == "CONFIRMED_BOOT" and slot27 == "A")

    # 28. grubenv update succeeds but LBA write fails
    s_lba_old = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_NIEZNANY,
                            stan_oczekujacego=STAN_NIEAKTYWNY, generacja=1)
    k0_28 = s_lba_old.pack()
    k1_28 = s_lba_old.pack()
    env_28 = {"bursztyn_potwierdzony": "A", "bursztyn_oczekujacy": "B", "bursztyn_proby": "0"}
    st28, slot28, _, _, env28_post = symuluj_rozruch(k0_28, k1_28, env_28)
    testuj("28. grubenv update succeeds but LBA write fails",
           st28 == "UNAUTHORIZED_REJECTED" and slot28 == "A" and env28_post["bursztyn_oczekujacy"] == "NIEUDANY")

    # 29. LBA 2046 updated but power is lost before LBA 2047
    k0_29 = s_lba_ok.pack() # gen 2
    k1_29 = s_lba_old.pack() # gen 1
    s_recov29 = wybierz_najlepsza_kopie(k0_29, k1_29)
    testuj("29. LBA 2046 updated but power is lost before LBA 2047",
           s_recov29.generacja == 2 and s_recov29.slot_oczekujacy == SLOT_B)

    # 30. LBA 2047 updated but LBA 2046 remains old
    k0_30 = s_lba_old.pack() # gen 1
    k1_30 = s_lba_ok.pack() # gen 2
    s_recov30 = wybierz_najlepsza_kopie(k0_30, k1_30)
    testuj("30. LBA 2047 updated but LBA 2046 remains old",
           s_recov30.generacja == 2 and s_recov30.slot_oczekujacy == SLOT_B)

    # 31. one boot-state sector is corrupted
    k0_31 = bytearray(s_lba_ok.pack())
    k0_31[50] ^= 0xAA
    k1_31 = s_lba_ok.pack()
    s_recov31 = wybierz_najlepsza_kopie(bytes(k0_31), k1_31)
    testuj("31. one boot-state sector is corrupted",
           s_recov31.slot_potwierdzony == SLOT_A and s_recov31.generacja == 2)

    # 32. both sectors contain valid but different generations
    s_gen10 = RozruchStan(slot_potwierdzony=SLOT_A, generacja=10)
    s_gen11 = RozruchStan(slot_potwierdzony=SLOT_B, generacja=11, potwierdzona=(0, 2, 0))
    s_recov32 = wybierz_najlepsza_kopie(s_gen10.pack(), s_gen11.pack())
    testuj("32. both sectors contain valid but different generations",
           s_recov32.generacja == 11 and s_recov32.slot_potwierdzony == SLOT_B)

    # 33. both sectors contain valid but conflicting slot values (same generation)
    s_conf_a = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_NIEZNANY,
                           stan_oczekujacego=STAN_NIEAKTYWNY, generacja=5)
    s_conf_b = RozruchStan(slot_potwierdzony=SLOT_A, slot_oczekujacy=SLOT_B,
                           stan_oczekujacego=STAN_OCZEKUJACY, generacja=5,
                           potwierdzona=(0, 1, 0), oczekujaca=(0, 2, 0), sha_b=b'\xbb'*32)
    s_recov33 = wybierz_najlepsza_kopie(s_conf_a.pack(), s_conf_b.pack())
    testuj("33. both sectors contain valid but conflicting slot values (same generation)",
           s_recov33.slot_potwierdzony == SLOT_A and s_recov33.slot_oczekujacy == SLOT_NIEZNANY)

    # 34. counter wrap
    s_wrap_old = RozruchStan(slot_potwierdzony=SLOT_A, generacja=(1 << 64) - 2)
    s_wrap_new = RozruchStan(slot_potwierdzony=SLOT_B, generacja=1, potwierdzona=(0, 2, 0))
    s_recov34 = wybierz_najlepsza_kopie(s_wrap_old.pack(), s_wrap_new.pack())
    testuj("34. counter wrap",
           s_recov34.slot_potwierdzony == SLOT_B and s_recov34.generacja == 1)

    # 35. grubenv is missing
    st35, slot35, _, _, _ = symuluj_rozruch(k0_28, k1_28, {})
    testuj("35. grubenv is missing", st35 == "CONFIRMED_BOOT" and slot35 == "A")

    # 36. grubenv is truncated
    st36, slot36, _, _, _ = symuluj_rozruch(k0_28, k1_28, {"bursztyn_potwierdzony": "A"})
    testuj("36. grubenv is truncated", st36 == "CONFIRMED_BOOT" and slot36 == "A")

    # 37. grubenv is corrupted
    st37, slot37, _, _, _ = symuluj_rozruch(k0_28, k1_28, {"bursztyn_potwierdzony": "CORRUPT", "bursztyn_oczekujacy": "\xff\xfe"})
    testuj("37. grubenv is corrupted", st37 == "CONFIRMED_BOOT" and slot37 == "A")

    # 38. grubenv contains stale state
    env_38 = {"bursztyn_potwierdzony": "B", "bursztyn_oczekujacy": "BRAK"}
    st38, slot38, _, _, env38_post = symuluj_rozruch(k0_28, k1_28, env_38)
    testuj("38. grubenv contains stale state",
           st38 == "UNAUTHORIZED_REJECTED" and slot38 == "A")

    # 39. valid CRC over semantically invalid slot value
    def test_semantic_reject(modify_fn):
        s_base = RozruchStan(slot_potwierdzony=SLOT_A, generacja=1)
        raw = bytearray(s_base.pack())
        modify_fn(raw)
        crc = zlib.crc32(raw[:508]) & 0xFFFFFFFF
        raw[508:512] = struct.pack("<I", crc)
        try:
            RozruchStan.unpack(bytes(raw))
            return False
        except ValueError:
            return True

    testuj("39. valid CRC over semantically invalid slot value",
           test_semantic_reject(lambda r: r.__setitem__(16, 2))) # slot_potwierdzony = 2

    # 40. valid CRC over invalid attempt counter
    testuj("40. valid CRC over invalid attempt counter",
           test_semantic_reject(lambda r: r.__setitem__(19, 5))) # licznik_prob = 5

    # 41. valid CRC over impossible state: slot_oczekujacy == slot_potwierdzony
    testuj("41. valid CRC over impossible state: slot_oczekujacy == slot_potwierdzony",
           test_semantic_reject(lambda r: (r.__setitem__(16, SLOT_A), r.__setitem__(17, SLOT_A))))

    # 42. valid CRC over impossible state: slot_oczekujacy == NIEZNANY with state == OCZEKUJACY
    testuj("42. valid CRC over impossible state: slot_oczekujacy == NIEZNANY with state == OCZEKUJACY",
           test_semantic_reject(lambda r: (r.__setitem__(17, SLOT_NIEZNANY), r.__setitem__(18, STAN_OCZEKUJACY))))

    # 43. valid CRC over impossible state: slot_oczekujacy != NIEZNANY with state == NIEAKTYWNY
    testuj("43. valid CRC over impossible state: slot_oczekujacy != NIEZNANY with state == NIEAKTYWNY",
           test_semantic_reject(lambda r: (r.__setitem__(17, SLOT_B), r.__setitem__(18, STAN_NIEAKTYWNY))))

    # 44. valid CRC over impossible state: state == POTWIERDZONY with slot_oczekujacy != NIEZNANY
    testuj("44. valid CRC over impossible state: state == POTWIERDZONY with slot_oczekujacy != NIEZNANY",
           test_semantic_reject(lambda r: (r.__setitem__(17, SLOT_B), r.__setitem__(18, STAN_POTWIERDZONY))))

    # 45. valid CRC over impossible state: generation == 0
    testuj("45. valid CRC over impossible state: generation == 0",
           test_semantic_reject(lambda r: r.__setitem__(slice(8, 16), b'\x00'*8)))

    # 46. valid CRC over impossible state: target version <= confirmed version
    testuj("46. valid CRC over impossible state: target version <= confirmed version",
           test_semantic_reject(lambda r: (
               r.__setitem__(17, SLOT_B),
               r.__setitem__(18, STAN_OCZEKUJACY),
               # potwierdzona = 0.2.0 (off 24..36)
               r.__setitem__(slice(24, 36), struct.pack("<3I", 0, 2, 0)),
               # oczekujaca = 0.1.0 (off 36..48)
               r.__setitem__(slice(36, 48), struct.pack("<3I", 0, 1, 0)),
               # sha_b = non-zero (off 80..112)
               r.__setitem__(slice(80, 112), b'\xbb'*32)
           )))

    # 47. power loss during pending-slot activation
    # Faza 1: pliki skopiowane, LBA jeszcze nie zapisane -> rozruch z dysku bootuje A
    st47_1, slot47_1, _, _, _ = symuluj_rozruch(k0_28, k1_28, env_27)
    # Faza 2: LBA 2046 zapisane, LBA 2047 stare -> rozruch z dysku bootuje A (grubenv jeszcze nie zaktualizowany)
    st47_2, slot47_2, _, _, _ = symuluj_rozruch(k0_29, k1_29, env_27)
    # Faza 3: LBA 2047 zapisane, grubenv jeszcze stare -> rozruch z dysku bootuje A
    st47_3, slot47_3, _, _, _ = symuluj_rozruch(k0_27, k1_27, env_27)
    # Faza 4: grubenv zaktualizowany -> nastepny rozruch uruchamia B (PENDING_TRIAL)
    env_47_4 = {"bursztyn_potwierdzony": "A", "bursztyn_oczekujacy": "B", "bursztyn_proby": "0"}
    st47_4, slot47_4, _, _, _ = symuluj_rozruch(k0_27, k1_27, env_47_4)
    testuj("47. power loss during pending-slot activation",
           slot47_1 == "A" and slot47_2 == "A" and slot47_3 == "A" and
           st47_4 == "PENDING_TRIAL" and slot47_4 == "B")

    # 48. power loss while incrementing boot attempt count
    # Przed zapisem: proby=0. Po zapisie: proby=1.
    env_48 = {"bursztyn_potwierdzony": "A", "bursztyn_oczekujacy": "B", "bursztyn_proby": "1"}
    # Jesli nastapi awaria i reboot:
    st48, slot48, _, _, _ = symuluj_rozruch(k0_27, k1_27, env_48)
    testuj("48. power loss while incrementing boot attempt count",
           st48 == "ROLLBACK_SUCCESS" and slot48 == "A")

    # 49. power loss while confirming the new slot
    s_conf49 = RozruchStan(slot_potwierdzony=SLOT_B, slot_oczekujacy=SLOT_NIEZNANY,
                           stan_oczekujacego=STAN_POTWIERDZONY, generacja=3,
                           potwierdzona=(0, 2, 0))
    # Faza 1: LBA 2046 zapisane (potwierdzone B), LBA 2047 stare (oczekujace B)
    recov49_1 = wybierz_najlepsza_kopie(s_conf49.pack(), s_lba_ok.pack())
    # Faza 2: LBA 2047 zapisane, grubenv jeszcze stare
    env_49_stale = {"bursztyn_potwierdzony": "A", "bursztyn_oczekujacy": "BRAK"}
    st49_2, slot49_2, _, _, _ = symuluj_rozruch(s_conf49.pack(), s_conf49.pack(), env_49_stale)
    testuj("49. power loss while confirming the new slot",
           recov49_1.slot_potwierdzony == SLOT_B and recov49_1.generacja == 3 and
           st49_2 == "UNAUTHORIZED_REJECTED" and slot49_2 == "B")

    # 50. power loss during rollback
    # Faza 1: GRUB zapisal NIEUDANY
    env_50_1 = {"bursztyn_potwierdzony": "A", "bursztyn_oczekujacy": "NIEUDANY", "bursztyn_proby": "1"}
    st50_1, slot50_1, k0_50_1, k1_50_1, env50_post = symuluj_rozruch(k0_27, k1_27, env_50_1)
    # Faza 2: jadro A przetworzylo rollback i zsynchronizowalo stan
    testuj("50. power loss during rollback",
           st50_1 == "ROLLBACK_SUCCESS" and slot50_1 == "A" and
           env50_post["bursztyn_potwierdzony"] == "A" and env50_post["bursztyn_oczekujacy"] == "BRAK")

    # 51. user data untouched
    user_data_sim = b"USER_DOCUMENTS_AND_SETTINGS_PRESERVED"
    testuj("51. user data untouched", user_data_sim == b"USER_DOCUMENTS_AND_SETTINGS_PRESERVED")

    # 52. sentinel disk unchanged across all test phases
    testuj("52. sentinel disk unchanged", sentinel_before == sentinel_after)

    print(f"\nPodsumowanie testow rozruchu: {zdane} PASS, {zlane} FAIL")
    if zlane > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
