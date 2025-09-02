;Based on coldreboot.asm from the Amiga Hardware Reference manual.
;Used to reboot after switching kickstart with the RPROM, as the
;exec.library/ColdReboot() vector does not match the ROM code and
;can't be called then.

MAGIC_ROMEND        EQU $01000000   ;End of Kickstart ROM
MAGIC_SIZEOFFSET    EQU -$14        ;Offset from end of ROM to Kickstart size

;;-------------- MagicResetCode ---------DO NOT CHANGE-----------------------
    CNOP    0,4                     ;IMPORTANT! Longword align!
_reboot::
    lea.l   MAGIC_ROMEND,a0         ;(end of ROM)
    sub.l   MAGIC_SIZEOFFSET(a0),a0 ;(end of ROM)-(ROM size)=PC
    move.l  4(a0),a0                ;Get Initial Program Counter
    subq.l  #2,a0                   ;now points to second RESET
    reset                           ;first RESET instruction
    jmp     (a0)                    ;CPU Prefetch executes this
    ;NOTE: the RESET and JMP instructions must share a longword!
;---------------------------------------DO NOT CHANGE-----------------------
    END
