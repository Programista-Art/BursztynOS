#!/usr/bin/env python3
"""Obsluga formatu rekordu rozruchu Bursztyn OS (A/B) w Pythonie."""

import struct
import zlib

ROZRUCH_MAGIC = 0x5354414E # "STAN"
ROZRUCH_WERSJA_FORMATU = 1

SLOT_A = 0
SLOT_B = 1
SLOT_NIEZNANY = 0xFF

STAN_NIEAKTYWNY = 0
STAN_AKTYWNY = 1
STAN_OCZEKUJACY = 2
STAN_POTWIERDZONY = 3
STAN_NIEUDANY = 4

# Naglowek przed paddingiem:
# <I (magic) I (wersja) Q (generacja) B (potwierdzony) B (oczekujacy) B (stan) B (proby) B (maks_prob) 3s (rezerwa)
# 3I (potwierdzona: maj, min, pat) 3I (oczekujaca: maj, min, pat) 32s (sha_a) 32s (sha_b)
FORMAT_BEZ_PADDINGU = "<IIQBBBBB3s3I3I32s32s"
ROZMIAR_NAGLOWKA = struct.calcsize(FORMAT_BEZ_PADDINGU) # 112 bajtow
PADDING_DLUGOSC = 508 - ROZMIAR_NAGLOWKA # 396 bajtow
CALY_FORMAT = f"{FORMAT_BEZ_PADDINGU}{PADDING_DLUGOSC}sI"
assert struct.calcsize(CALY_FORMAT) == 512, f"Format rozruchu musi miec 512B, ma {struct.calcsize(CALY_FORMAT)}"

class RozruchStan:
    def __init__(self,
                 slot_potwierdzony=SLOT_A,
                 slot_oczekujacy=SLOT_NIEZNANY,
                 stan_oczekujacego=STAN_NIEAKTYWNY,
                 generacja=1,
                 licznik_prob=0,
                 maks_prob=1,
                 potwierdzona=(0, 1, 0),
                 oczekujaca=(0, 0, 0),
                 sha_a=b'\x00'*32,
                 sha_b=b'\x00'*32):
        self.magic = ROZRUCH_MAGIC
        self.wersja_formatu = ROZRUCH_WERSJA_FORMATU
        self.generacja = generacja
        self.slot_potwierdzony = slot_potwierdzony
        self.slot_oczekujacy = slot_oczekujacy
        self.stan_oczekujacego = stan_oczekujacego
        self.licznik_prob = licznik_prob
        self.maks_prob = maks_prob
        self.potwierdzona = potwierdzona
        self.oczekujaca = oczekujaca
        self.sha_a = sha_a if isinstance(sha_a, bytes) else bytes(sha_a)
        self.sha_b = sha_b if isinstance(sha_b, bytes) else bytes(sha_b)

    def pack(self) -> bytes:
        czesc_danych = struct.pack(
            FORMAT_BEZ_PADDINGU,
            self.magic,
            self.wersja_formatu,
            self.generacja,
            self.slot_potwierdzony,
            self.slot_oczekujacy,
            self.stan_oczekujacego,
            self.licznik_prob,
            self.maks_prob,
            b'\x00\x00\x00',
            self.potwierdzona[0], self.potwierdzona[1], self.potwierdzona[2],
            self.oczekujaca[0], self.oczekujaca[1], self.oczekujaca[2],
            self.sha_a.ljust(32, b'\x00')[:32],
            self.sha_b.ljust(32, b'\x00')[:32]
        ) + (b'\x00' * PADDING_DLUGOSC)
        assert len(czesc_danych) == 508
        crc = zlib.crc32(czesc_danych) & 0xFFFFFFFF
        return czesc_danych + struct.pack("<I", crc)

    @classmethod
    def unpack(cls, data: bytes):
        if len(data) != 512:
            raise ValueError(f"Nieprawidlowa dlugosc rekordu rozruchu: {len(data)} (wymagane 512)")
        oczekiwany_crc = zlib.crc32(data[:508]) & 0xFFFFFFFF
        dane_pol = struct.unpack(CALY_FORMAT, data)
        zapisany_crc = dane_pol[-1]
        if oczekiwany_crc != zapisany_crc:
            raise ValueError(f"Blad CRC rekordu rozruchu: {hex(zapisany_crc)} != {hex(oczekiwany_crc)}")
        magic, wersja, gen, slot_potw, slot_oczek, stan_oczek, proby, maks_p, _rez = dane_pol[:9]
        if magic != ROZRUCH_MAGIC:
            raise ValueError(f"Zly magic rekordu rozruchu: {hex(magic)}")
        if wersja != ROZRUCH_WERSJA_FORMATU:
            raise ValueError(f"Zla wersja formatu rozruchu: {wersja}")
        if gen == 0:
            raise ValueError("Nieprawidlowa generacja zerowa")
        if slot_potw not in (SLOT_A, SLOT_B):
            raise ValueError(f"Nieprawidlowy slot potwierdzony: {slot_potw}")
        if slot_oczek not in (SLOT_A, SLOT_B, SLOT_NIEZNANY):
            raise ValueError(f"Nieprawidlowy slot oczekujacy: {slot_oczek}")
        if slot_oczek == slot_potw:
            raise ValueError(f"Konflikt: slot oczekujacy jest identyczny z potwierdzonym ({slot_potw})")
        if stan_oczek > STAN_NIEUDANY:
            raise ValueError(f"Nieprawidlowy stan logiczny: {stan_oczek}")

        if slot_oczek == SLOT_NIEZNANY:
            if stan_oczek not in (STAN_NIEAKTYWNY, STAN_POTWIERDZONY):
                raise ValueError(f"Niemozliwy stan dla slotu nieznanego: {stan_oczek}")
        else:
            if stan_oczek not in (STAN_OCZEKUJACY, STAN_NIEUDANY):
                raise ValueError(f"Niemozliwy stan dla slotu docelowego: {stan_oczek}")

        if maks_p != 1:
            raise ValueError(f"Nieprawidlowy limit prob: {maks_p}")
        if proby > maks_p:
            raise ValueError(f"Licznik prob ({proby}) przekracza limit ({maks_p})")

        p_maj, p_min, p_pat = dane_pol[9:12]
        o_maj, o_min, o_pat = dane_pol[12:15]
        sha_a, sha_b = dane_pol[15], dane_pol[16]

        if stan_oczek == STAN_OCZEKUJACY:
            if (o_maj, o_min, o_pat) == (0, 0, 0):
                raise ValueError("Pusta wersja oczekujaca")
            if (o_maj, o_min, o_pat) <= (p_maj, p_min, p_pat):
                raise ValueError(f"Downgrade w rekordzie stanu: {o_maj}.{o_min}.{o_pat} <= {p_maj}.{p_min}.{p_pat}")
            target_sha = sha_a if slot_oczek == SLOT_A else sha_b
            if target_sha == b'\x00' * 32:
                raise ValueError("Wyzerowany hash oczekiwanego jadra")

        inst = cls(
            slot_potwierdzony=slot_potw,
            slot_oczekujacy=slot_oczek,
            stan_oczekujacego=stan_oczek,
            generacja=gen,
            licznik_prob=proby,
            maks_prob=maks_p,
            potwierdzona=(p_maj, p_min, p_pat),
            oczekujaca=(o_maj, o_min, o_pat),
            sha_a=sha_a,
            sha_b=sha_b
        )
        return inst

def signed_diff_64(a: int, b: int) -> int:
    diff = (a - b) & 0xFFFFFFFFFFFFFFFF
    if diff >= (1 << 63):
        diff -= (1 << 64)
    return diff

def wybierz_najlepsza_kopie(k0_bytes: bytes, k1_bytes: bytes) -> RozruchStan:
    s0, s1 = None, None
    try:
        s0 = RozruchStan.unpack(k0_bytes)
    except Exception:
        pass
    try:
        s1 = RozruchStan.unpack(k1_bytes)
    except Exception:
        pass

    if s0 is None and s1 is None:
        raise ValueError("Obie kopie stanu rozruchu sa uszkodzone")
    if s0 is not None and s1 is None:
        return s0
    if s0 is None and s1 is not None:
        return s1

    diff = signed_diff_64(s1.generacja, s0.generacja)
    if diff > 0:
        return s1
    if diff < 0:
        return s0

    # Identyczna generacja
    if k0_bytes == k1_bytes:
        return s0

    # Konflikt generacji (rowna generacja, rozna tresc)
    # Wybor deterministyczny na korzysc stanu bezpiecznego:
    if s0.stan_oczekujacego == STAN_NIEUDANY:
        return s0
    if s1.stan_oczekujacego == STAN_NIEUDANY:
        return s1
    if s0.slot_oczekujacy == SLOT_NIEZNANY and s1.slot_oczekujacy != SLOT_NIEZNANY:
        return s0
    if s1.slot_oczekujacy == SLOT_NIEZNANY and s0.slot_oczekujacy != SLOT_NIEZNANY:
        return s1
    if s0.slot_potwierdzony == SLOT_A:
        return s0
    return s1
