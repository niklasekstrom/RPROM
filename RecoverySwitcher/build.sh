#!/bin/bash
set -x
vc +kick13 -nostdlib main.asm -o recovery_switcher
if [[ $(stat -c%s recovery_switcher) -gt 294 ]]; then
    echo "Error: Resulting recovery code is too big"
    exit 1
fi
tail -c +33 recovery_switcher | head -c -4 | xxd -i > recovery_switcher.h
rm recovery_switcher
