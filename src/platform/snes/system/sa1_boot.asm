.include "hdr.asm"
.accu 16
.index 16
.16bit

; SA-1 start-up. the SA-1 reset vector is a 16-bit address in bank 00, where the SA-1 sees the
; first 32 KiB of ROM block 0; the build fills it with bank zero's upper half, so this code lives
; in that upper half and runs at the same offsets in both views.

.DEFINE SA1_CCNT $002200
.DEFINE SA1_CRV $002203
.DEFINE SA1_SBWE $002226
.DEFINE SA1_CBWE $002227
.DEFINE SA1_BWPA $002228
.DEFINE SA1_SIWP $002229
.DEFINE SA1_CIWP $00222A
.DEFINE SA1_CCNT_RESET $20
.DEFINE SA1_WRITE_ALL $FF
.DEFINE SA1_BWRAM_WRITE $80
; SA-1 view of I-RAM: direct page $000-$0ff (compiler registers, memcpy stubs), the mailbox at
; $100-$13f, the stack below $7ff. the unused stack is filled with a pattern for the low-water mark.
.DEFINE SA1_STACK_TOP $07FF
.DEFINE SA1_STACK_FLOOR $0140
.DEFINE SA1_STACK_PATTERN $A5A5
; the compiler's globals of SA-1 objects: BW-RAM bank $40, link slot 2 ($2000-$ffff).
.DEFINE SA1_BSS_BANK $40
.DEFINE SA1_BSS_START $2000
; memcpy and memmove execute "mvn/mvp dst,src; rts" from direct page $28 and $2c.
.DEFINE OP_MVN $54
.DEFINE OP_MVP $44
.DEFINE OP_RTS $60

.BASE $C0
.BANK 0 SLOT 0
.SECTION "snes_sa1_boot" SEMIFREE ORG $8000

; s-cpu: point the SA-1 at its reset code, open I-RAM and BW-RAM to both cpus, release it.
snesSa1Start:
    php
    sep #$20
    lda #SA1_CCNT_RESET
    sta.l SA1_CCNT
    rep #$20
    lda.w #(snesSa1Reset & $FFFF)
    sta.l SA1_CRV
    sep #$20
    lda #SA1_WRITE_ALL
    sta.l SA1_SIWP
    lda #SA1_BWRAM_WRITE
    sta.l SA1_SBWE
    lda #0
    sta.l SA1_BWPA
    sta.l SA1_CCNT
    plp
    rtl

; SA-1.
snesSa1Reset:
    sei
    clc
    xce
    rep #$38
    lda #0
    tcd
    ldx #SA1_STACK_TOP
    txs
    sep #$20
    lda #SA1_WRITE_ALL
    sta.l SA1_CIWP
    lda #SA1_BWRAM_WRITE
    sta.l SA1_CBWE
    lda #OP_MVN
    sta.b $28
    lda #OP_MVP
    sta.b $2C
    lda #OP_RTS
    sta.b $2B
    sta.b $2F
    rep #$20
    lda #SA1_STACK_PATTERN
    ldx #SA1_STACK_FLOOR
-   sta.w $0000,x
    inx
    inx
    cpx #(SA1_STACK_TOP - 1)
    bcc -
    sep #$20
    lda #SA1_BSS_BANK
    pha
    plb
    rep #$20
    ldx #SA1_BSS_START
-   stz.w $0000,x
    inx
    inx
    bne -
    jml snesSa1Main
.ENDS
