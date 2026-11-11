#!/bin/bash
for i in {1..5}; do
    echo "Run $i"
    python3 narzedzia/test_reboot_gpu_qemu.py --test 2 > loop_log_$i.txt 2>&1
    if grep -q "FAIL" loop_log_$i.txt; then
        echo "Failed on run $i!"
        exit 1
    fi
done
echo "All passed"
