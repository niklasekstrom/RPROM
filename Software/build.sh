#!/bin/bash
set -x
vc +kick13 -I${NDK32}/Include_H rprom.c reboot.asm -lamiga -o RPROM
