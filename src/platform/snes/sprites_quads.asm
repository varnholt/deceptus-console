.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; expand one cached source piece. local words are x/y/tile, then packed
; palette/flip/priority/quads and a quadrant counter. emitter calls may clobber
; every imaginary register, so persistent values remain on the caller stack.
.section "snes_sprite_quads" semisuperfree banks 2-15
snes_obj_emit_quads:
    php
    rep #$30
    phb
    phd
    phx
    phy
    lda #0
    tcd
    tsa
    sec
    sbc #12
    tas
    lda 24,s
    sta 1,s
    lda 26,s
    sta 3,s
    lda 28,s
    and #120
    asl a
    asl a
    sta.b tcc__r0
    lda 28,s
    and #7
    asl a
    ora.b tcc__r0
    sta 5,s
    lda 30,s
    sta 7,s
    lda 32,s
    sta 9,s
    lda 10,s
    and #255
    cmp #15
    bne quads_partial_start
    lda #4
    sta 11,s
    stz.b tcc__r0
    stz.b tcc__r0h
    stz.b tcc__r1
    lda #16
    sta.b tcc__r2
    jmp.w quads_emit
quads_partial_start:
    lda #0
    sta 11,s
quads_next:
    lda 11,s
    cmp #4
    bcc quads_check
    jmp.w quads_done
quads_check:
    tax
    sep #$20
    lda.l quads_masks,x
    and 10,s
    rep #$20
    and #255
    bne quads_visible
    jmp.w quads_advance
quads_visible:
    lda 11,s
    and #1
    sta.b tcc__r0
    lda 8,s
    and #255
    beq quads_dx_ready
    lda #1
    sec
    sbc.b tcc__r0
    sta.b tcc__r0
quads_dx_ready:
    lda.b tcc__r0
    asl a
    asl a
    asl a
    sta.b tcc__r0
    lda 11,s
    and #2
    asl a
    asl a
    sta.b tcc__r0h
    asl a
    sta.b tcc__r1
    lda 11,s
    and #1
    ora.b tcc__r1
    sta.b tcc__r1
    lda #8
    sta.b tcc__r2
quads_emit:
    lda.b tcc__r2
    xba
    sta.b tcc__r2
    lda 9,s
    and #255
    ora.b tcc__r2
    pha
    lda 9,s
    pha
    lda 9,s
    clc
    adc.b tcc__r1
    pha
    lda 9,s
    clc
    adc.b tcc__r0h
    pha
    lda 9,s
    clc
    adc.b tcc__r0
    pha
    jsl snes_obj_piece
    tsa
    clc
    adc #10
    tas
    lda 11,s
    cmp #4
    beq quads_done
quads_advance:
    lda 11,s
    inc a
    sta 11,s
    jmp.w quads_next
quads_done:
    tsa
    clc
    adc #12
    tas
    ply
    plx
    pld
    plb
    plp
    rtl
quads_masks:
    .db 1,2,4,8
.ends
