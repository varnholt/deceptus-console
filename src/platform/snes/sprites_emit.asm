.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; packed c arguments: x/y/tile words, then palette/flip/priority/size bytes.
; validate the complete scanline range before changing either counters or oam.
; a scanline's load word with bit 7 or 15 set holds too much: SNES_OBJ_LINE_FULL in sprites.c.
.define SNES_OBJ_LINE_FULL $8080

.section "snes_sprite_emit" semisuperfree banks 2-15
snesObjPiece:
    php
    rep #$30
    phb
    phd
    phx
    phy
    lda #0
    tcd
    lda 12,s
    sta.b tcc__r0
    lda 14,s
    sta.b tcc__r1
    lda 16,s
    sta.b tcc__r2
    lda 21,s
    and #255
    sta.b tcc__r4
    lsr a
    lsr a
    lsr a
    sta.b tcc__r9
    lda.b tcc__r0
    bmi sprite_x_negative
    cmp #256
    bcc sprite_x_negative
    jmp.w sprite_done
sprite_x_negative:
    clc
    adc.b tcc__r4
    beq sprite_clipped
    bmi sprite_clipped
    lda.b tcc__r1
    bmi sprite_y_negative
    cmp #224
    bcc sprite_y_negative
sprite_clipped:
    jmp.w sprite_done
sprite_y_negative:
    clc
    adc.b tcc__r4
    beq sprite_clipped
    bmi sprite_clipped
    cmp #224
    bcc sprite_bottom_ready
    lda #224
sprite_bottom_ready:
    sta.b tcc__r5h
    lda.b tcc__r1
    bpl sprite_top_ready
    lda #0
sprite_top_ready:
    sta.b tcc__r5
    lda.l snes_obj_count
    cmp #128
    bcc sprite_lines_start
    jmp.w sprite_overflow
sprite_lines_start:
    ; one add per line counts the piece and sets bit 7 or 15 of the line's load word when the line
    ; would hold too many objects or slivers (snes_obj_line_load in sprites.c). r10: the load of
    ; this piece, r3: the end offset, x: the offset of the current line.
    lda.b tcc__r9
    xba
    ora #1
    sta.b tcc__r10
    lda.b tcc__r5h
    asl a
    sta.b tcc__r3
    lda.b tcc__r5
    asl a
    tax
sprite_line_add:
    lda.l snes_obj_line_load,x
    clc
    adc.b tcc__r10
    bit #SNES_OBJ_LINE_FULL
    bne sprite_line_full
    sta.l snes_obj_line_load,x
    inx
    inx
    cpx.b tcc__r3
    bcc sprite_line_add
    bra sprite_lines_counted
sprite_line_full:
    ; the piece does not fit: the lines it counted already give their load back.
    lda.b tcc__r5
    asl a
    sta.b tcc__r3
sprite_line_undo:
    cpx.b tcc__r3
    beq sprite_line_dropped
    dex
    dex
    lda.l snes_obj_line_load,x
    sec
    sbc.b tcc__r10
    sta.l snes_obj_line_load,x
    bra sprite_line_undo
sprite_line_dropped:
    jmp.w sprite_overflow
sprite_lines_counted:
    rep #$20
    lda 18,s
    and #255
    asl a
    sta.b tcc__r3
    lda 20,s
    and #255
    asl a
    asl a
    asl a
    asl a
    ora.b tcc__r3
    sta.b tcc__r3
    lda.b tcc__r2
    xba
    and #255
    ora.b tcc__r3
    sta.b tcc__r3
    lda 19,s
    and #255
    beq sprite_attributes_ready
    lda.b tcc__r3
    ora #$40
    sta.b tcc__r3
sprite_attributes_ready:
    lda.l snes_obj_count
    asl a
    asl a
    tax
    sep #$20
    lda.b tcc__r0
    sta.l snes_obj_oam,x
    lda.b tcc__r1
    sta.l snes_obj_oam+1,x
    lda.b tcc__r2
    sta.l snes_obj_oam+2,x
    lda.b tcc__r3
    sta.l snes_obj_oam+3,x
    rep #$20
    lda.l snes_obj_count
    and #3
    asl a
    tay
    lda.l snes_obj_count
    lsr a
    lsr a
    tax
    lda.b tcc__r0
    xba
    and #1
    sta.b tcc__r3
    lda.b tcc__r4
    cmp #16
    bne sprite_size_ready
    lda.b tcc__r3
    ora #2
    sta.b tcc__r3
sprite_size_ready:
    lda.b tcc__r3
    cpy #0
    beq sprite_high_ready
sprite_high_shift:
    asl a
    dey
    bne sprite_high_shift
sprite_high_ready:
    sep #$20
    ora.l snes_obj_oam+512,x
    sta.l snes_obj_oam+512,x
    rep #$20
    lda.l snes_obj_count
    inc a
    sta.l snes_obj_count
    bra sprite_done
sprite_overflow:
    sep #$20
    lda #1
    sta.l snes_sprite_overflow
sprite_done:
    rep #$30
    ply
    plx
    pld
    plb
    plp
    rtl
.ends
