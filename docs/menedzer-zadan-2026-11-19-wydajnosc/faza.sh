#!/bin/bash
# faza.sh NAZWA ruch|spoczynek  -> zapisuje zagregowane [GUI PERF] z ~15 s
G="python3 narzedzia/guest_ctl.py --port ${PORT:-5579}"
O=$(dirname "$0")
$G perf > /dev/null            # wyzerowanie okna agregacji
t0=$(date +%s.%N)
if [ "$2" = ruch ]; then
  i=0; koniec=$(( $(date +%s) + 15 ))
  while [ $(date +%s) -lt $koniec ]; do
    x=$(( 300 + (i % 20) * 30 )); y=$(( 250 + ((i / 20) % 3) * 120 + (i % 2) * 15 ))
    $G move $x $y > /dev/null; i=$((i+1))
  done
  echo "ruchow: $i"
else
  $G wait 15 > /dev/null
fi
t1=$(date +%s.%N)
$G perf > "$O/perf-$1.json"
echo "$1: czas fazy $(python3 -c "print(round($t1-$t0,1))") s"
