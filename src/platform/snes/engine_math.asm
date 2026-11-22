.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; 65816 versions of shared engine arithmetic, like the 68000 paths in
; grabrope.c. compiled c pays a library call for every 16x16 product, 32-bit
; shift and 32-bit division; these routines return exactly what the c in
; grabrope.c returns. the cpu multiplier at $4202/$4203 is used only outside
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

; DeceptusU16 snesRopeDistance(DeceptusI32 x, DeceptusI32 y)
; floor(sqrt(x * x + y * y)) for |x|, |y| <= 65535, as deceptusGrabRopeDistance.
snesRopeDistance:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; x low 11,s, x high 13,s, y low 15,s, y high 17,s.
    lda 13,s
    bpl distance_x_positive
    lda 11,s
    eor #$ffff
    inc a
    bra distance_x_square
distance_x_positive:
    lda 11,s
distance_x_square:
    jsr square_magnitude
    lda.b tcc__r3
    sta.b tcc__r0
    lda.b tcc__r3h
    sta.b tcc__r0h
    lda 17,s
    bpl distance_y_positive
    lda 15,s
    eor #$ffff
    inc a
    bra distance_y_square
distance_y_positive:
    lda 15,s
distance_y_square:
    jsr square_magnitude
    lda.b tcc__r0
    clc
    adc.b tcc__r3
    sta.b tcc__r0
    lda.b tcc__r0h
    adc.b tcc__r3h
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

; DeceptusI32 snesRopeQuotient(DeceptusI32 numerator, DeceptusU16 denominator)
; numerator / denominator truncated toward zero, as c division; denominator > 0.
snesRopeQuotient:
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
