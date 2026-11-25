.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; snesObjDrawResourceReference (sprites.c) as one routine: the request's pieces, their resident
; tile slots, the quadrants of partly opaque pieces and the objects with their scanline limits.
; split into routines with stacked c arguments, the calls cost about as much as the work, so the
; request's values stay in the direct page and the objects are written by local subroutines.
;
; a scanline's load word with bit 7 or 15 set holds too much: SNES_OBJ_LINE_FULL in sprites.c.
.define LINE_FULL $8080
; the load of one object on each of its lines: one object, plus its 8-pixel slivers.
.define LOAD_LARGE $0201
.define LOAD_SMALL $0101
; oam offset after the last of the 128 objects.
.define OAM_END 512
; the 224 visible lines, as offsets into snes_obj_line_load.
.define LINES_END 448

; direct page. the request's values, kept across the c allocator call (draw_allocate saves the
; first twelve words):
.define RES tcc__r0
.define OAMX tcc__r0h
.define QOFF tcc__r1
.define ATTR tcc__r1h
.define DX_EVEN tcc__r2
.define DX_ODD tcc__r2h
.define XSTEP tcc__r3
.define PY tcc__r3h
.define PX tcc__r4
.define PIECE tcc__r4h
.define COLUMNS_LEFT tcc__r5
.define OPAQUE_ROW tcc__r5h
; values of one piece or object:
.define OX tcc__r9
.define OY tcc__r9h
.define OTILE tcc__r10
.define SCRATCH tcc__r10h
.define TOP2 tcc__f2
.define QUADS tcc__f2h
.define TILE tcc__f3
; the request pointer, read before any of the above is set.
.define REQUEST tcc__r10

; stack frame below the saved registers.
.define FRAME 6
.define X_START 1
.define ROWS_LEFT 3
.define COLUMNS 5
; arguments: 3 bytes return address, 8 bytes saved registers, the frame.
.define ARG_REQUEST 12+FRAME
.define ARG_PALETTE 16+FRAME

; one scanline of an object, entered with carry clear and x = the offset of the object's end
; line: line k of a size-line block is the line k - size before the end. a full line keeps its
; load and leaves through the size's undo with y = 2 * (size - k). the adds never carry: a stored
; load has bits 7 and 15 clear, and an object adds at most 2 slivers.
.macro LINE_ADD ARGS k, size, load, full
    lda.l snes_obj_line_load+2*(k-size),x
    adc #load
    bit #LINE_FULL
    beq line_fits_\@
    ldy #2*(size-k)
    jmp full
line_fits_\@:
    sta.l snes_obj_line_load+2*(k-size),x
.endm

.section "snes_sprite_draw" semisuperfree banks 2-15

; void snesObjDrawResource(const SnesObjRequest *request, DeceptusU16 palette)
snesObjDrawResource:
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
    sbc #FRAME
    tas
    lda ARG_REQUEST,s
    sta.b REQUEST
    lda ARG_REQUEST+2,s
    sta.b REQUEST+2
    ; request: resource, x, y words, flip and priority bytes. SCRATCH overlaps the pointer's
    ; bank, so the flags come last.
    lda.b [REQUEST]
    sta.b RES
    ldy #2
    lda.b [REQUEST],y
    sta X_START,s
    ldy #4
    lda.b [REQUEST],y
    sta.b PY
    ldy #6
    lda.b [REQUEST],y
    sta.b SCRATCH
    ; attribute byte in the high byte: palette, priority, flip; the c byte keeps only bits 0-7.
    lda ARG_PALETTE,s
    asl a
    sta.b ATTR
    lda.b SCRATCH
    xba
    and #$00ff
    asl a
    asl a
    asl a
    asl a
    ora.b ATTR
    sta.b ATTR
    ; columns and rows of the resource (sixteen-byte entries).
    lda.b RES
    asl a
    asl a
    asl a
    sta.b OPAQUE_ROW
    asl a
    tax
    lda.l snes_sprite_resources+10,x
    tay
    and #$00ff
    sta COLUMNS,s
    tya
    xba
    and #$00ff
    sta ROWS_LEFT,s
    lda.b RES
    asl a
    tax
    lda.l snes_sprite_quad_offsets,x
    sta.b QOFF
    lda.b SCRATCH
    and #$00ff
    bne draw_flipped
    stz.b DX_EVEN
    lda #8
    sta.b DX_ODD
    lda #16
    sta.b XSTEP
    bra draw_attribute
draw_flipped:
    ; mirrored: columns from the right, each quadrant on the other side.
    lda #8
    sta.b DX_EVEN
    stz.b DX_ODD
    lda #-16
    sta.b XSTEP
    lda COLUMNS,s
    dec a
    asl a
    asl a
    asl a
    asl a
    clc
    adc X_START,s
    sta X_START,s
    lda.b ATTR
    ora #$40
    sta.b ATTR
draw_attribute:
    lda.b ATTR
    xba
    and #$ff00
    sta.b ATTR
    lda.l snes_obj_count
    asl a
    asl a
    sta.b OAMX
    stz.b PIECE

draw_row:
    lda ROWS_LEFT,s
    bne draw_row_clip
    jmp draw_done
draw_row_clip:
    ; the pieces of a row show when -16 < y < 224.
    lda.b PY
    bmi draw_row_above
    cmp #224
    bcc draw_row_visible
    bra draw_row_skip
draw_row_above:
    cmp #-15
    bcs draw_row_visible
draw_row_skip:
    lda.b PIECE
    clc
    adc COLUMNS,s
    sta.b PIECE
    jmp draw_next_row
draw_row_visible:
    lda X_START,s
    sta.b PX
    lda COLUMNS,s
    sta.b COLUMNS_LEFT

draw_column:
    ; a piece shows when -16 < x < 256.
    lda.b PX
    bmi draw_column_left
    cmp #256
    bcc draw_opaque
    jmp draw_next_column
draw_column_left:
    cmp #-15
    bcs draw_opaque
    jmp draw_next_column
draw_opaque:
    ; snes_sprite_opaque[resource][piece >> 3] bit piece & 7.
    lda.b PIECE
    lsr a
    lsr a
    lsr a
    clc
    adc.b OPAQUE_ROW
    tax
    lda.l snes_sprite_opaque,x
    sta.b SCRATCH
    lda.b PIECE
    and #7
    asl a
    tax
    lda.l draw_piece_bits,x
    and.b SCRATCH
    bne draw_slot
    jmp draw_next_column
draw_slot:
    ; a resident piece is on its resource's slot list (slot: resource word, then piece, valid,
    ; used, held, pending, next); only a missing one goes through the c allocator.
    ldx.b RES
    lda.l snes_obj_heads,x
    and #$00ff
draw_slot_walk:
    beq draw_slot_missing
    dec a
    sta.b TILE
    asl a
    asl a
    asl a
    tax
    lda.l snes_obj_slots+2,x
    and #$00ff
    cmp.b PIECE
    beq draw_slot_found
    lda.l snes_obj_slots+7,x
    and #$00ff
    bra draw_slot_walk
draw_slot_found:
    sep #$20
    lda #1
    sta.l snes_obj_slots+4,x
    rep #$20
    lda.b TILE
    bra draw_tile
draw_slot_missing:
    jsr draw_allocate
    cmp #$ffff
    bne draw_tile
    jmp draw_next_column
draw_tile:
    ; snesObjTile: slot / 64 * 256 + (slot & 63) / 8 * 32 + (slot & 7) * 2, for slots below 128.
    tay
    and #$0078
    asl a
    asl a
    sta.b TILE
    tya
    and #$0007
    asl a
    ora.b TILE
    sta.b TILE
    lda.b QOFF
    clc
    adc.b PIECE
    tax
    lda.l snes_sprite_quads,x
    and #$00ff
    cmp #15
    bne draw_quadrants
    lda.b PX
    sta.b OX
    lda.b PY
    sta.b OY
    lda.b TILE
    sta.b OTILE
    jsr emit_large
    jmp draw_next_column
draw_quadrants:
    ; the opaque 8x8 quadrants in order: top left, top right, bottom left, bottom right, each
    ; on the other side when mirrored.
    sta.b QUADS
    lsr.b QUADS
    bcc draw_quadrant_1
    lda.b PX
    clc
    adc.b DX_EVEN
    sta.b OX
    lda.b PY
    sta.b OY
    lda.b TILE
    sta.b OTILE
    jsr emit_small
draw_quadrant_1:
    lsr.b QUADS
    bcc draw_quadrant_2
    lda.b PX
    clc
    adc.b DX_ODD
    sta.b OX
    lda.b PY
    sta.b OY
    lda.b TILE
    inc a
    sta.b OTILE
    jsr emit_small
draw_quadrant_2:
    lsr.b QUADS
    bcc draw_quadrant_3
    lda.b PX
    clc
    adc.b DX_EVEN
    sta.b OX
    lda.b PY
    clc
    adc #8
    sta.b OY
    lda.b TILE
    clc
    adc #16
    sta.b OTILE
    jsr emit_small
draw_quadrant_3:
    lsr.b QUADS
    bcc draw_next_column
    lda.b PX
    clc
    adc.b DX_ODD
    sta.b OX
    lda.b PY
    clc
    adc #8
    sta.b OY
    lda.b TILE
    clc
    adc #17
    sta.b OTILE
    jsr emit_small

draw_next_column:
    inc.b PIECE
    lda.b PX
    clc
    adc.b XSTEP
    sta.b PX
    dec.b COLUMNS_LEFT
    beq draw_next_row
    jmp draw_column
draw_next_row:
    lda.b PY
    clc
    adc #16
    sta.b PY
    lda ROWS_LEFT,s
    dec a
    sta ROWS_LEFT,s
    jmp draw_row
draw_done:
    lda.b OAMX
    lsr a
    lsr a
    sta.l snes_obj_count
    tsa
    clc
    adc #FRAME
    tas
    ply
    plx
    pld
    plb
    plp
    rtl

; the slot of the missing piece from snesObjSlotFor in sprites.c, or 65535. the c call may
; change every direct page register, so the request's twelve words are saved around it.
draw_allocate:
    ldx #22
draw_allocate_save:
    lda.b RES,x
    pha
    dex
    dex
    bpl draw_allocate_save
    lda.b PIECE
    pha
    lda.b RES
    pha
    jsl snesObjSlotFor
    pla
    pla
    lda.b tcc__r0
    tay
    ldx #0
draw_allocate_restore:
    pla
    sta.b RES,x
    inx
    inx
    cpx #24
    bcc draw_allocate_restore
    tya
    rts

; a 16x16 object at OX, OY with OTILE; the piece clip has shown it already.
emit_large:
    lda.b OAMX
    cmp #OAM_END
    bcc emit_large_lines
    jmp emit_overflow
emit_large_lines:
    lda.b OY
    bmi emit_large_above
    asl a
    sta.b TOP2
    clc
    adc #32
    cmp #LINES_END
    bcc emit_large_end
    lda #LINES_END
emit_large_end:
    tay
    sec
    sbc.b TOP2
    tax
    bra emit_large_enter
emit_large_above:
    clc
    adc #16
    asl a
    tay
    tax
    stz.b TOP2
emit_large_enter:
    ; x = 2 * lines selects the block line to enter at; then x = 2 * end for the blocks.
    lda.l emit_large_entries,x
    pha
    tyx
    clc
    rts
emit_large_line_0:
    LINE_ADD 0, 16, LOAD_LARGE, emit_large_full
emit_large_line_1:
    LINE_ADD 1, 16, LOAD_LARGE, emit_large_full
emit_large_line_2:
    LINE_ADD 2, 16, LOAD_LARGE, emit_large_full
emit_large_line_3:
    LINE_ADD 3, 16, LOAD_LARGE, emit_large_full
emit_large_line_4:
    LINE_ADD 4, 16, LOAD_LARGE, emit_large_full
emit_large_line_5:
    LINE_ADD 5, 16, LOAD_LARGE, emit_large_full
emit_large_line_6:
    LINE_ADD 6, 16, LOAD_LARGE, emit_large_full
emit_large_line_7:
    LINE_ADD 7, 16, LOAD_LARGE, emit_large_full
emit_large_line_8:
    LINE_ADD 8, 16, LOAD_LARGE, emit_large_full
emit_large_line_9:
    LINE_ADD 9, 16, LOAD_LARGE, emit_large_full
emit_large_line_10:
    LINE_ADD 10, 16, LOAD_LARGE, emit_large_full
emit_large_line_11:
    LINE_ADD 11, 16, LOAD_LARGE, emit_large_full
emit_large_line_12:
    LINE_ADD 12, 16, LOAD_LARGE, emit_large_full
emit_large_line_13:
    LINE_ADD 13, 16, LOAD_LARGE, emit_large_full
emit_large_line_14:
    LINE_ADD 14, 16, LOAD_LARGE, emit_large_full
emit_large_line_15:
    LINE_ADD 15, 16, LOAD_LARGE, emit_large_full
    lda #2
    jmp emit_write
emit_large_full:
    ; the lines from the top to the full one give their load back.
    sty.b SCRATCH
    txa
    sec
    sbc.b SCRATCH
    tax
emit_large_undo:
    cpx.b TOP2
    beq emit_overflow
    dex
    dex
    lda.l snes_obj_line_load,x
    sec
    sbc #LOAD_LARGE
    sta.l snes_obj_line_load,x
    bra emit_large_undo

emit_overflow:
    sep #$20
    lda #1
    sta.l snes_sprite_overflow
    rep #$20
    rts

; an 8x8 object at OX, OY with OTILE, unless it is outside -8 < x < 256 and -8 < y < 224.
emit_small:
    lda.b OX
    bmi emit_small_left
    cmp #256
    bcc emit_small_vertical
    rts
emit_small_left:
    cmp #-7
    bcs emit_small_vertical
    rts
emit_small_vertical:
    lda.b OY
    bmi emit_small_above
    cmp #224
    bcc emit_small_count
    rts
emit_small_count:
    lda.b OAMX
    cmp #OAM_END
    bcs emit_overflow
    lda.b OY
    asl a
    sta.b TOP2
    clc
    adc #16
    cmp #LINES_END
    bcc emit_small_end
    lda #LINES_END
emit_small_end:
    tay
    sec
    sbc.b TOP2
    tax
    bra emit_small_enter
emit_small_above:
    cmp #-7
    bcs emit_small_above_count
    rts
emit_small_above_count:
    lda.b OAMX
    cmp #OAM_END
    bcs emit_overflow
    lda.b OY
    clc
    adc #8
    asl a
    tay
    tax
    stz.b TOP2
emit_small_enter:
    lda.l emit_small_entries,x
    pha
    tyx
    clc
    rts
emit_small_line_0:
    LINE_ADD 0, 8, LOAD_SMALL, emit_small_full
emit_small_line_1:
    LINE_ADD 1, 8, LOAD_SMALL, emit_small_full
emit_small_line_2:
    LINE_ADD 2, 8, LOAD_SMALL, emit_small_full
emit_small_line_3:
    LINE_ADD 3, 8, LOAD_SMALL, emit_small_full
emit_small_line_4:
    LINE_ADD 4, 8, LOAD_SMALL, emit_small_full
emit_small_line_5:
    LINE_ADD 5, 8, LOAD_SMALL, emit_small_full
emit_small_line_6:
    LINE_ADD 6, 8, LOAD_SMALL, emit_small_full
emit_small_line_7:
    LINE_ADD 7, 8, LOAD_SMALL, emit_small_full
    lda #0
    bra emit_write
emit_small_full:
    sty.b SCRATCH
    txa
    sec
    sbc.b SCRATCH
    tax
emit_small_undo:
    cpx.b TOP2
    bne emit_small_undo_line
    jmp emit_overflow
emit_small_undo_line:
    dex
    dex
    lda.l snes_obj_line_load,x
    sec
    sbc #LOAD_SMALL
    sta.l snes_obj_line_load,x
    bra emit_small_undo

; the object's oam entry and its two high-table bits; a = 2 for a 16x16 object, else 0.
emit_write:
    sta.b SCRATCH
    ldx.b OAMX
    sep #$20
    lda.b OX
    sta.l snes_obj_oam,x
    lda.b OY
    sta.l snes_obj_oam+1,x
    rep #$20
    lda.b OTILE
    ora.b ATTR
    sta.l snes_obj_oam+2,x
    ; emit_high_bits index: the object's place in its high-table byte, size, x bit 8.
    txa
    and #12
    ora.b SCRATCH
    sta.b SCRATCH
    lda.b OX+1
    and #1
    ora.b SCRATCH
    tax
    lda.l emit_high_bits,x
    sta.b SCRATCH
    lda.b OAMX
    lsr a
    lsr a
    lsr a
    lsr a
    tax
    sep #$20
    lda.b SCRATCH
    ora.l snes_obj_oam+512,x
    sta.l snes_obj_oam+512,x
    rep #$20
    lda.b OAMX
    clc
    adc #4
    sta.b OAMX
    rts

; per number of lines (index 2 * lines), the block line to enter at, minus one for rts.
emit_large_entries:
    .dw 0
    .dw emit_large_line_15-1
    .dw emit_large_line_14-1
    .dw emit_large_line_13-1
    .dw emit_large_line_12-1
    .dw emit_large_line_11-1
    .dw emit_large_line_10-1
    .dw emit_large_line_9-1
    .dw emit_large_line_8-1
    .dw emit_large_line_7-1
    .dw emit_large_line_6-1
    .dw emit_large_line_5-1
    .dw emit_large_line_4-1
    .dw emit_large_line_3-1
    .dw emit_large_line_2-1
    .dw emit_large_line_1-1
    .dw emit_large_line_0-1
emit_small_entries:
    .dw 0
    .dw emit_small_line_7-1
    .dw emit_small_line_6-1
    .dw emit_small_line_5-1
    .dw emit_small_line_4-1
    .dw emit_small_line_3-1
    .dw emit_small_line_2-1
    .dw emit_small_line_1-1
    .dw emit_small_line_0-1
; per object place in its byte (0-3, times 4) plus its two bits, those bits shifted into place.
emit_high_bits:
    .db 0, 1, 2, 3, 0, 4, 8, 12, 0, 16, 32, 48, 0, 64, 128, 192
draw_piece_bits:
    .dw 1, 2, 4, 8, 16, 32, 64, 128
.ends
