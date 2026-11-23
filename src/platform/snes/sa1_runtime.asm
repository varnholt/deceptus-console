.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; arithmetic for code that runs on the SA-1. the library's tcc__mul, tcc__mull, tcc__udiv and
; tcc__div use the S-CPU multiplier/divider and a lock in S-CPU work RAM, which the SA-1 cannot
; reach; the build points SA-1 objects at tcc__mul_sw, tcc__mull_sw, tcc__udiv_sw and these.

.DEFINE SA1_MCNT $002250
.DEFINE SA1_MA $002251
.DEFINE SA1_MB $002253
.DEFINE SA1_MR $002306
.DEFINE SA1_MCNT_MULTIPLY 0

.section "snes_sa1_runtime" semisuperfree banks 2-15

; tcc__div with the software unsigned division: x / a, signed.
; out: tcc__r9 = quotient, x = remainder with the dividend's sign. destroys y, tcc__r10.
snesSa1Div:
    pha
    ldy #0
    stz.b tcc__r10
    txa
    bpl +
    eor #$ffff
    ina
    iny
    inc.b tcc__r10
    tax
+   pla
    bpl +
    eor #$ffff
    ina
    iny
+   phy
    jsr.l tcc__udiv_sw
    lda.b tcc__r9
    ply
    cpy #1
    bne +
    eor #$ffff
    ina
    sta.b tcc__r9
+   lda.b tcc__r10
    beq +
    txa
    eor #$ffff
    ina
    tax
+   rtl

; DeceptusI32 snesMulS16(DeceptusI16 left, DeceptusI16 right) on the SA-1 signed multiplier.
; left 5,s, right 7,s after php. result in tcc__r0 (low), tcc__r1 (high).
snesSa1MulS16:
    php
    rep #$30
    sep #$20
    lda #SA1_MCNT_MULTIPLY
    sta.l SA1_MCNT
    rep #$20
    lda 5,s
    sta.l SA1_MA
    lda 7,s
    sta.l SA1_MB
    ; the product is ready five SA-1 cycles after the write to MB.
    nop
    lda.l SA1_MR
    sta.b tcc__r0
    lda.l SA1_MR + 2
    sta.b tcc__r1
    plp
    rtl

; DeceptusU32 snesSquareU16(DeceptusU16 magnitude) on the SA-1 signed multiplier. the multiplier
; squares m as a signed s = m - 65536 when m >= $8000; then m*m = s*s + (m << 17) modulo 2^32.
snesSa1SquareU16:
    php
    rep #$30
    sep #$20
    lda #SA1_MCNT_MULTIPLY
    sta.l SA1_MCNT
    rep #$20
    lda 5,s
    sta.l SA1_MA
    sta.l SA1_MB
    nop
    lda.l SA1_MR
    sta.b tcc__r0
    lda.l SA1_MR + 2
    sta.b tcc__r1
    lda 5,s
    bpl +
    asl a
    clc
    adc.b tcc__r1
    sta.b tcc__r1
+   plp
    rtl
.ends
