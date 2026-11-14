#!/bin/bash
# usage: idle_cpu_bench.sh VARIANT_DIR PORT [covered] -> per-process guest CPU share over 30 quiet seconds
# VARIANT_DIR contains iso + system_operacyjny.bin + *.elf of a BURSZTYNDIAGGUIRUNTIME=1 build.
cd "$(dirname "$0")/.."
d=$1; port=$2; mode=$3
mkdir -p /tmp/idlebench
python3 narzedzia/guest_session.py --iso $d/iso --build $d --out $d/sess-$port --pages testy/husarz_load --port $port > /tmp/idlebench/s$port.log 2>&1 &
until grep -q READY /tmp/idlebench/s$port.log; do sleep 3; done
C="python3 narzedzia/guest_ctl.py --port $port"
$C wait 15 >/dev/null
if [ "$mode" = "covered" ]; then $C launch notatnik >/dev/null; $C wait 8 >/dev/null; fi
$C perf >/dev/null; $C wait 30 >/dev/null
$C perf | python3 -c "
import sys,json
d=json.load(sys.stdin)['perf']
out=[(v.get('path','').split('/')[-1] or 'kernel/idle', round(v.get('cpu_ns',0)/1e9/30*100,1), v.get('bws_gui',0), v.get('wake',0)) for v in d.values()]
print('$d $mode', sorted(out,key=lambda x:-x[1])[:4])"
$C quit >/dev/null
