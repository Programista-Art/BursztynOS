#!/bin/bash
# usage: app_launch_bench.sh PORT OUTFILE   (session already running, diag ISO)
# Launch each app twice via the Start menu: cold (first start) and warm (after closing it).
C="python3 $(dirname "$0")/guest_ctl.py --port $1"
OUT=$2; : > "$OUT"
$C closeapp diagnostyka.elf Diagnostyka systemu > /dev/null 2>&1; $C wait 3 > /dev/null
for spec in "kalkulator.elf|Kalkulator|liczenie" "notatnik.elf|Notatnik|tekst" "eksplorator.elf|Eksplorator|pliki" \
            "shell.elf|Powłoka Bursztynowa|terminal" "ustawienia.elf|Ustawienia|ustawienia" "przegladarka.elf|Husarz|internet"; do
  IFS='|' read elf title query <<< "$spec"
  for pass in cold warm; do
    r=$($C bench "$elf" "${title// /_}" "$query")
    echo "$title $pass $r" >> "$OUT"
    $C wait 4 > /dev/null
    $C closeapp "$elf" "$title" > /dev/null 2>&1
    $C wait 3 > /dev/null
  done
done
