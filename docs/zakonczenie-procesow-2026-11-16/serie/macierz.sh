#!/bin/bash
# Macierz przyczynowosci: czy przerwy hosta zalezą od zachowania goscia (zalew syscalli / probkowanie monitora)?
cd /home/programista/Pulpit/Burstzyn/BursztynOS
S=.zakonczenie-test/serie
python3 testy/heartbeat_hosta.py $S/heartbeat_macierz.log 1.5 3000 &
HB=$!
export ZK_TYLKO_FLOOD=1
echo "== A: fix, bez regs" > $S/macierz.out
python3 testy/test_zakonczenie_qemu.py --runs 5 --timeout 200 --out-runs serie/m_A >> $S/macierz.out 2>&1
echo "== B: baseline, bez regs" >> $S/macierz.out
ZK_BASELINE=1 python3 testy/test_zakonczenie_qemu.py --runs 5 --timeout 200 --out-runs serie/m_B >> $S/macierz.out 2>&1
echo "== C: baseline, z regs" >> $S/macierz.out
ZK_BASELINE=1 python3 testy/test_zakonczenie_qemu.py --runs 5 --regs --timeout 200 --out-runs serie/m_C >> $S/macierz.out 2>&1
echo "== D: fix, z regs" >> $S/macierz.out
python3 testy/test_zakonczenie_qemu.py --runs 5 --regs --timeout 200 --out-runs serie/m_D >> $S/macierz.out 2>&1
echo done >> $S/macierz.out
kill $HB 2>/dev/null
