.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; zeroes the game's work RAM globals, which the build gathers in section game_bss (bank 7e). the
; sdk start-up clears only one section named ".bss", so main calls this before anything else.

.section "snes_game_ram" semisuperfree banks 2-15
snesClearGameRam:
    php
    phb
    sep #$20
    lda #:SECTIONSTART_game_bss
    pha
    plb
    rep #$10
    ldx #(SECTIONSTART_game_bss & $FFFF)
-   cpx #(SECTIONEND_game_bss & $FFFF)
    beq +
    stz.w $0000,x
    inx
    bra -
+   plb
    plp
    rtl
.ends
