MAGIC_ADDR_0    equ     $82*2
MAGIC_ADDR_1    equ     $44a*2
MAGIC_ADDR_2    equ     $3fa7f*2

CMD_UPDATE_ACTIVE_ROM_SLOT      equ     0
CMD_RESTORE_PAGE_TO_SRAM        equ     2

                code

ResetSP:        dc.l    128*1024
ResetPC:        dc.l    $f80000+(Start-ResetSP)

Start:          lea.l   ResetSP(pc),a0  ; $f80000
                lea.l   $bfe001,a1

                ; Set LED and OVL as outputs
                move.b  #3,$200(a1)

                ; Disable overlay (OVL)
                move.b  #2,(a1)

                ; Start CIA-A TOD
                move.b  d0,$800(a1)

                ; Copy code from ROM to Chip RAM
                move.l  a0,a2
                lea.l   0,a3
                move    #((End-ResetSP)/4)-1,d0
.CopyCodeLoop:  move.l  (a2)+,(a3)+
                dbf     d0,.CopyCodeLoop

                ; Start running from RAM
                jmp     (RunInChipRAM-ResetSP)

RunInChipRAM:
                moveq   #50,d0
                bsr.b   WaitVSyncTicks

                ; Check if LMB is pressed
                btst.b  #6,(a1)
                beq.b   ReadKeyboard

BootActive:     move.l  #(CMD_RESTORE_PAGE_TO_SRAM<<14),d0
                bsr.b   ExecCmd

                move    #1000-1,d0
.Loop:          move.b  (a1),d1
                dbf     d0,.Loop

Reboot:         move.l  4(a0),a0
                cnop    0,4
                reset
                jmp     (a0)

ReadKeyboard:
                move.b  #0,(a1) ; Enable LED

                bsr.b   HandshakeKbd

WaitKeyUp:      btst.b  #3,$d00(a1)
                beq.b   WaitKeyUp

                bsr.b   HandshakeKbd

                clr.l   d0
                move.b  $c00(a1),d0
                btst    #0,d0
                beq.b   WaitKeyUp

                not.b   d0
                lsr.b   #1,d0

                tst.b   d0
                beq.b   BootActive
                cmp.b   #7,d0
                bhi.b   BootActive

                ; CMD_UPDATE_ACTIVE_ROM_SLOT=0 => next instruction is a NOP
                ;or.l   #(CMD_UPDATE_ACTIVE_ROM_SLOT<<14),d0

                bsr.b   ExecCmd

                moveq   #25,d0          ; 25 * 20ms = 500 ms
                bsr.b   WaitVSyncTicks
                bra.b   Reboot

HandshakeKbd:   bset.b  #6,$e00(a1)
                moveq   #54-1,d0
.Loop:          move.b  (a1),d1
                dbf     d0,.Loop
                bclr.b  #6,$e00(a1)
                rts

WaitVSyncTicks: ; d0 = number of vsync ticks to wait
                move.l  d0,d1
                bsr.b   GetVSyncTicks
                add.l   d0,d1           ; d1 = deadline
.Loop:          bsr.b   GetVSyncTicks
                sub.l   d1,d0           ; d0 = now - deadline
                btst    #23,d0
                bne.b   .Loop
                rts

ExecCmd:        ; cmd + arg in d0.l
                lsl.l   #1,d0           ; turn byte address into word address
                move    MAGIC_ADDR_0(a0),d1
                move    MAGIC_ADDR_1(a0),d1
                move.l  #MAGIC_ADDR_2,d1
                move    (a0,d1.l),d1
                move    (a0,d0.l),d1
                rts

GetVSyncTicks:  clr.l   d0
                move.b  $a00(a1),d0
                lsl     #8,d0
                move.b  $900(a1),d0
                lsl.l   #8,d0
                move.b  $800(a1),d0
                rts

                cnop    0,4
End:
