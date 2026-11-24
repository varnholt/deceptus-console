.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; arithmetic for code that runs on the SA-1. the library's tcc__mul, tcc__mull, tcc__udiv and
; tcc__div use the S-CPU multiplier/divider and a lock in S-CPU work RAM, which the SA-1 cannot
; reach; the build points SA-1 objects at these, which keep the library routines' contracts and
; results and use the SA-1 arithmetic unit: a signed 16x16 multiplier and a divider of a signed
; 16-bit dividend by an unsigned 16-bit divisor. results are ready five SA-1 cycles after the
; write to MB; the nop and the following long load take longer.

.DEFINE SA1_MCNT $002250
.DEFINE SA1_MA $002251
.DEFINE SA1_MB $002253
.DEFINE SA1_MR $002306
.DEFINE SA1_MCNT_MULTIPLY 0
.DEFINE SA1_MCNT_DIVIDE 1

.section "snes_sa1_runtime" semisuperfree banks 2-15

; tcc__mul: a = low word of tcc__r9 * tcc__r10, flags from a. the low word of a product does not
; depend on the operands' signedness.
snesSa1Mul:
    sep #$20
    lda #SA1_MCNT_MULTIPLY
    sta.l SA1_MCNT
    rep #$20
    lda.b tcc__r9
    sta.l SA1_MA
    lda.b tcc__r10
    sta.l SA1_MB
    nop
    lda.l SA1_MR
    rtl

; tcc__mull: tcc__r9 * tcc__r10 unsigned (16x16, high words ignored like the library's hardware
; version); y = low word, x = high word. from the signed product, an operand with bit 15 set adds
; the other operand times 65536.
snesSa1Mull:
    sep #$20
    lda #SA1_MCNT_MULTIPLY
    sta.l SA1_MCNT
    rep #$20
    lda.b tcc__r9
    sta.l SA1_MA
    lda.b tcc__r10
    sta.l SA1_MB
    nop
    lda.l SA1_MR + 2
    bit.b tcc__r9
    bpl +
    clc
    adc.b tcc__r10
+   bit.b tcc__r10
    bpl +
    clc
    adc.b tcc__r9
+   tax
    lda.l SA1_MR
    tay
    rtl

; tcc__udiv: x / a unsigned; tcc__r9 = quotient, x = remainder, destroys y. the divider takes a
; signed dividend, so dividends from $8000 and division by zero use the library's software routine.
snesSa1Udiv:
    cmp #0
    beq udiv_software
    cpx #$8000
    bcs udiv_software
    tay
    sep #$20
    lda #SA1_MCNT_DIVIDE
    sta.l SA1_MCNT
    rep #$20
    txa
    sta.l SA1_MA
    tya
    sta.l SA1_MB
    nop
    lda.l SA1_MR
    sta.b tcc__r9
    lda.l SA1_MR + 2
    tax
    rtl
udiv_software:
    jml tcc__udiv_sw

; tcc__div: x / a, signed, through snesSa1Udiv.
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
    jsr.l snesSa1Udiv
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

; DeceptusI32 snesDivS32U16(DeceptusI32 numerator, DeceptusU16 denominator) on the SA-1 divider
; when the numerator's magnitude is below $8000, the divider's signed 16-bit range; every other
; numerator, and a zero denominator, takes the software routine in engine_math.asm. the divider
; floors negative dividends, so it divides the magnitude and the sign follows, truncating toward
; zero as c does. numerator 5,s (low) and 7,s (high), denominator 9,s after php. result in
; tcc__r0 (low), tcc__r1 (high).
snesSa1DivS32U16:
    php
    rep #$30
    lda 9,s
    beq sdiv_software
    ldy #0
    lda 7,s
    beq sdiv_positive
    cmp #$ffff
    bne sdiv_software
    ; negative: magnitude 65536 - low, below $8000 from low $8001 up.
    lda 5,s
    cmp #$8001
    bcc sdiv_software
    eor #$ffff
    inc a
    ldy #1
    bra sdiv_hardware
sdiv_positive:
    lda 5,s
    bmi sdiv_software
sdiv_hardware:
    tax
    sep #$20
    lda #SA1_MCNT_DIVIDE
    sta.l SA1_MCNT
    rep #$20
    txa
    sta.l SA1_MA
    lda 9,s
    sta.l SA1_MB
    ; the quotient is ready five SA-1 cycles after the write to MB.
    nop
    lda.l SA1_MR
    stz.b tcc__r1
    cpy #0
    beq sdiv_store
    eor #$ffff
    inc a
    beq sdiv_store
    dec.b tcc__r1
sdiv_store:
    sta.b tcc__r0
    plp
    rtl
sdiv_software:
    plp
    jml snesDivS32U16

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
