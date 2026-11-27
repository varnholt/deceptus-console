.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; 65816 versions of the arithmetic primitives in include/arith.h, mapped by
; platform_arith.h. compiled c pays a library call for every 16x16 product,
; 32-bit shift and 32-bit division; these return exactly what the reference
; implementations in src/engine/arith.c return. the cpu multiplier at $4202/$4203 is used only outside
; interrupts here and in the c library.

.section "snes_engine_math" semisuperfree banks 2-15

; a = magnitude (0..65535). its square into tcc__r3 (low) and tcc__r3h (high)
; from four 8x8 hardware products: l*l + (l*h << 9) + (h*h << 16).
square_magnitude:
    sta.b tcc__r4
    sep #$20
    ; l * l
    lda.b tcc__r4
    sta.l $004202
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    sta.b tcc__r3
    sep #$20
    ; h * h
    lda.b tcc__r4+1
    sta.l $004202
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    sta.b tcc__r3h
    sep #$20
    ; l * h
    lda.b tcc__r4
    sta.l $004202
    lda.b tcc__r4+1
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    ; add (l*h) << 9: (l*h) << 9 truncated to the low word, (l*h) >> 7 to the high.
    sta.b tcc__r4h
    xba
    and #$ff00
    asl a
    clc
    adc.b tcc__r3
    sta.b tcc__r3
    php
    lda.b tcc__r4h
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    plp
    adc.b tcc__r3h
    sta.b tcc__r3h
    rts

; tcc__r4 = a, tcc__r5 = b (0..65535 each). a * b into tcc__r3 (low) and
; tcc__r3h (high) from four 8x8 hardware products.
multiply_magnitudes:
    sep #$20
    ; al * bl
    lda.b tcc__r4
    sta.l $004202
    lda.b tcc__r5
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    sta.b tcc__r3
    sep #$20
    ; ah * bh
    lda.b tcc__r4+1
    sta.l $004202
    lda.b tcc__r5+1
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    sta.b tcc__r3h
    sep #$20
    ; al * bh
    lda.b tcc__r4
    sta.l $004202
    lda.b tcc__r5+1
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    jsr add_shifted_8
    sep #$20
    ; ah * bl
    lda.b tcc__r4+1
    sta.l $004202
    lda.b tcc__r5
    sta.l $004203
    nop
    nop
    nop
    rep #$20
    lda.l $004216
    jsr add_shifted_8
    rts

; tcc__r3/r3h += a << 8.
add_shifted_8:
    sta.b tcc__r4h
    xba
    and #$ff00
    clc
    adc.b tcc__r3
    sta.b tcc__r3
    lda.b tcc__r4h
    xba
    and #$00ff
    adc.b tcc__r3h
    sta.b tcc__r3h
    rts

; DeceptusI32 snesMulS16(DeceptusI16 left, DeceptusI16 right): arithMulS16.
snesMulS16:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; left 11,s, right 13,s.
    lda 11,s
    eor 13,s
    sta.b tcc__r2h
    lda 11,s
    bpl product_left
    eor #$ffff
    inc a
product_left:
    sta.b tcc__r4
    lda 13,s
    bpl product_right
    eor #$ffff
    inc a
product_right:
    sta.b tcc__r5
    jsr multiply_magnitudes
    lda.b tcc__r3
    sta.b tcc__r0
    lda.b tcc__r3h
    sta.b tcc__r1
    lda.b tcc__r2h
    bpl product_done
    lda.b tcc__r0
    eor #$ffff
    clc
    adc #1
    sta.b tcc__r0
    lda.b tcc__r1
    eor #$ffff
    adc #0
    sta.b tcc__r1
product_done:
    ply
    plx
    pld
    plp
    rtl

; DeceptusU32 snesSquareU16(DeceptusU16 magnitude): arithSquareU16.
snesSquareU16:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    lda 11,s
    jsr square_magnitude
    lda.b tcc__r3
    sta.b tcc__r0
    lda.b tcc__r3h
    sta.b tcc__r1
    ply
    plx
    pld
    plp
    rtl

; DeceptusU16 snesRootU32(DeceptusU32 value): arithRootU32, floor(sqrt(value)).
snesRootU32:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; value low 11,s, high 13,s.
    lda 11,s
    sta.b tcc__r0
    lda 13,s
    sta.b tcc__r0h
    ; digit-by-digit root: value tcc__r0/r0h, result tcc__r1/r1h, bit tcc__r2/r2h.
    stz.b tcc__r1
    stz.b tcc__r1h
    stz.b tcc__r2
    lda #$4000
    sta.b tcc__r2h
distance_bit_down:
    ; while bit > value: bit >>= 2.
    lda.b tcc__r2h
    cmp.b tcc__r0h
    bcc distance_loop
    bne distance_bit_shift
    lda.b tcc__r2
    cmp.b tcc__r0
    bcc distance_loop
    beq distance_loop
distance_bit_shift:
    lsr.b tcc__r2h
    ror.b tcc__r2
    lsr.b tcc__r2h
    ror.b tcc__r2
    lda.b tcc__r2
    ora.b tcc__r2h
    bne distance_bit_down
    bra distance_done
distance_loop:
    ; t = result + bit
    lda.b tcc__r1
    clc
    adc.b tcc__r2
    sta.b tcc__r3
    lda.b tcc__r1h
    adc.b tcc__r2h
    sta.b tcc__r3h
    ; value >= t ?
    lda.b tcc__r0h
    cmp.b tcc__r3h
    bcc distance_less
    bne distance_take
    lda.b tcc__r0
    cmp.b tcc__r3
    bcc distance_less
distance_take:
    lda.b tcc__r0
    sec
    sbc.b tcc__r3
    sta.b tcc__r0
    lda.b tcc__r0h
    sbc.b tcc__r3h
    sta.b tcc__r0h
    lsr.b tcc__r1h
    ror.b tcc__r1
    lda.b tcc__r1
    clc
    adc.b tcc__r2
    sta.b tcc__r1
    lda.b tcc__r1h
    adc.b tcc__r2h
    sta.b tcc__r1h
    bra distance_next
distance_less:
    lsr.b tcc__r1h
    ror.b tcc__r1
distance_next:
    lsr.b tcc__r2h
    ror.b tcc__r2
    lsr.b tcc__r2h
    ror.b tcc__r2
    lda.b tcc__r2
    ora.b tcc__r2h
    bne distance_loop
distance_done:
    lda.b tcc__r1
    sta.b tcc__r0
    ply
    plx
    pld
    plp
    rtl

; DeceptusI32 snesDivS32U16(DeceptusI32 numerator, DeceptusU16 denominator): arithDivS32U16,
; truncated toward zero as c division; denominator > 0.
snesDivS32U16:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; numerator low 11,s, high 13,s, denominator 15,s.
    lda 11,s
    sta.b tcc__r0
    lda 13,s
    sta.b tcc__r1
    ; tcc__r2h = sign
    sta.b tcc__r2h
    bpl quotient_magnitude
    lda.b tcc__r0
    eor #$ffff
    clc
    adc #1
    sta.b tcc__r0
    lda.b tcc__r1
    eor #$ffff
    adc #0
    sta.b tcc__r1
quotient_magnitude:
    lda 15,s
    sta.b tcc__r3
    ; shift-subtract over 32 bits: the dividend r0/r1 turns into the quotient,
    ; tcc__r2 holds the remainder (below the denominator before each step).
    stz.b tcc__r2
    ; a quotient below 65536: the high word is the first remainder and only the
    ; low word's 16 steps remain.
    lda.b tcc__r1
    cmp.b tcc__r3
    bcs quotient_full
    sta.b tcc__r2
    stz.b tcc__r1
    ldx #16
quotient_short_step:
    asl.b tcc__r0
    rol.b tcc__r2
    bcs quotient_short_subtract
    lda.b tcc__r2
    cmp.b tcc__r3
    bcc quotient_short_skip
quotient_short_subtract:
    lda.b tcc__r2
    sec
    sbc.b tcc__r3
    sta.b tcc__r2
    inc.b tcc__r0
quotient_short_skip:
    dex
    bne quotient_short_step
    bra quotient_sign
quotient_full:
    ldx #32
quotient_step:
    asl.b tcc__r0
    rol.b tcc__r1
    rol.b tcc__r2
    bcs quotient_subtract
    lda.b tcc__r2
    cmp.b tcc__r3
    bcc quotient_skip
quotient_subtract:
    lda.b tcc__r2
    sec
    sbc.b tcc__r3
    sta.b tcc__r2
    inc.b tcc__r0
quotient_skip:
    dex
    bne quotient_step
quotient_sign:
    lda.b tcc__r2h
    bpl quotient_done
    lda.b tcc__r0
    eor #$ffff
    clc
    adc #1
    sta.b tcc__r0
    lda.b tcc__r1
    eor #$ffff
    adc #0
    sta.b tcc__r1
quotient_done:
    ply
    plx
    pld
    plp
    rtl
.ends
