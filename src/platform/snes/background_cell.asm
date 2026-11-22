.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; streamed background cells. scrolling loads and releases a column or row of
; cells per plane each presentation, and the compiled c spent most of every cell
; on far-pointer arithmetic into the bank-7f work area. these entry points
; handle the common cases and call the c reference functions in background.c,
; before changing any state, for everything else:
; - load: cell inside the room and outside the union of the plane's enabled
;   clip rectangles, row in the row table, pattern zero or already resident.
; - release: blank or unreferenced cell, or a reference count that stays above
;   zero.
; the offsets are those of SnesBackgroundWork and SnesBackgroundCellView;
; background.c fails to compile if the layouts move them.
.define SNES_BG_REFERENCES 2144
.define SNES_BG_HASH_KEYS 6432
.define SNES_BG_HASH_SLOTS 10528
.define SNES_BG_MAPS 14624
.define SNES_BG_DIRTY_COLUMN 28882
.define SNES_BG_DIRTY_ROWS 29842
.define SNES_BG_DMA_BYTES 29850
.define SNES_BG_ROOM_CELLS 29858
.define SNES_BG_ROW_OFFSETS 29868
.define VIEW_COLUMNS 0
.define VIEW_ROWS 2
.define VIEW_PATTERNS 4
.define VIEW_CHUNKS 8
.define VIEW_CLIP_LEFT 16
.define VIEW_CLIP_TOP 20
.define VIEW_CLIP_RIGHT 24
.define VIEW_CLIP_BOTTOM 28

.section "snes_background_cell" semisuperfree banks 2-15
; byte offset of the cell in the plane's map, mapAddress(x, y) * 2 plus 4096
; per plane, into a. expects plane, x, y at 13,s, 15,s, 17,s (one jsr deeper).
cell_map_offset:
    lda 17,s
    and #31
    asl a
    asl a
    asl a
    asl a
    asl a
    sta.b tcc__r1
    lda 15,s
    and #63
    cmp #32
    bcc cell_map_low
    ; carry set: (x - 32) + 1024
    adc #1024-32-1
cell_map_low:
    ora.b tcc__r1
    asl a
    sta.b tcc__r1
    lda 13,s
    xba
    asl a
    asl a
    asl a
    asl a
    ora.b tcc__r1
    rts

cell_bits:
    .dw 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768

; early bail-outs of the load path branch here; load_slow is out of 8-bit reach.
load_slow_near:
    jmp load_slow

; void snesBackgroundLoadCellFast(DeceptusU16 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU16 dirty)
snesBackgroundLoadCellFast:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; return address 3 bytes, saved state 7 bytes: plane 11,s, x 13,s, y 15,s, dirty 17,s.
    ; unsigned compares also send negative coordinates to the c path.
    lda 13,s
    cmp.l snes_background_cell_view+VIEW_COLUMNS
    bcs load_slow_near
    lda 15,s
    cmp.l snes_background_cell_view+VIEW_ROWS
    bcs load_slow_near
    cmp #1024
    bcs load_slow_near
    lda 11,s
    asl a
    tax
    lda.l snes_background_cell_view+VIEW_CLIP_RIGHT,x
    beq load_outside_clips
    lda 13,s
    cmp.l snes_background_cell_view+VIEW_CLIP_LEFT,x
    bcc load_outside_clips
    cmp.l snes_background_cell_view+VIEW_CLIP_RIGHT,x
    bcs load_outside_clips
    lda 15,s
    cmp.l snes_background_cell_view+VIEW_CLIP_TOP,x
    bcc load_outside_clips
    cmp.l snes_background_cell_view+VIEW_CLIP_BOTTOM,x
    bcc load_slow_near
load_outside_clips:
    lda 15,s
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_ROW_OFFSETS,x
    clc
    adc 13,s
    cmp.l snes_background_storage+SNES_BG_ROOM_CELLS
    bcs load_slow_near
    sta.b tcc__r2
    ; chunk table entry offset >> 13, four bytes per far pointer.
    xba
    and #$00e0
    lsr a
    lsr a
    lsr a
    tay
    lda 11,s
    asl a
    asl a
    tax
    lda.l snes_background_cell_view+VIEW_CHUNKS,x
    sta.b tcc__r0
    lda.l snes_background_cell_view+VIEW_CHUNKS+2,x
    sta.b tcc__r0h
    lda [tcc__r0],y
    tax
    iny
    iny
    lda [tcc__r0],y
    sta.b tcc__r1h
    stx.b tcc__r1
    ; two words per cell: pattern, attributes.
    lda.b tcc__r2
    and #8191
    asl a
    asl a
    tay
    iny
    iny
    lda [tcc__r1],y
    and #$dc00
    sta.b tcc__r3
    dey
    dey
    lda [tcc__r1],y
    beq load_tile
    cmp.l snes_background_cell_view+VIEW_PATTERNS
    bcs load_slow
    sta.b tcc__r2
    lda 11,s
    beq load_domain
    lda.b tcc__r2
    ora #$4000
    sta.b tcc__r2
load_domain:
    pea 1
    lda.b tcc__r2
    pha
    jsl snesBackgroundHashFind
    pla
    pla
    lda.b tcc__r0
    cmp #$ffff
    beq load_slow
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_HASH_KEYS,x
    cmp.b tcc__r2
    bne load_slow
    lda.l snes_background_storage+SNES_BG_HASH_SLOTS,x
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_REFERENCES,x
    inc a
    sta.l snes_background_storage+SNES_BG_REFERENCES,x
    txa
    lsr a
    bra load_tile

load_slow:
    ; each push moves the next argument to 17,s.
    lda 17,s
    pha
    lda 17,s
    pha
    lda 17,s
    pha
    lda 17,s
    pha
    jsl snesBackgroundLoadCell
    pla
    pla
    pla
    pla
    jmp load_return

load_tile:
    ; a = slot. slotTile(plane, slot) with the plane's priority bit.
    tay
    lda 11,s
    beq load_plane_zero
    tya
    cmp #768
    bcs load_high_slot
    adc #256
    bra load_plane_one
load_high_slot:
    sbc #816
load_plane_one:
    ora #$2000
    bra load_word
load_plane_zero:
    tya
load_word:
    ora.b tcc__r3
    sta.b tcc__r2
    jsr cell_map_offset
    tax
    lda.b tcc__r2
    sta.l snes_background_storage+SNES_BG_MAPS,x
    lda 17,s
    cmp #1
    beq load_dirty_column
    cmp #2
    bne load_return
    ; dirty row: dirty_rows[plane][(y & 31) >> 4] bit (y & 15).
    lda 15,s
    and #15
    asl a
    tax
    lda.l cell_bits,x
    sta.b tcc__r1
    lda 15,s
    and #16
    lsr a
    lsr a
    lsr a
    sta.b tcc__r2
    lda 11,s
    asl a
    asl a
    ora.b tcc__r2
    tax
    lda.l snes_background_storage+SNES_BG_DIRTY_ROWS,x
    bit.b tcc__r1
    bne load_return
    ora.b tcc__r1
    sta.l snes_background_storage+SNES_BG_DIRTY_ROWS,x
    lda.l snes_background_storage+SNES_BG_DMA_BYTES
    clc
    adc #128
    sta.l snes_background_storage+SNES_BG_DMA_BYTES
    bra load_return
load_dirty_column:
    lda 11,s
    tax
    lda 13,s
    and #63
    sta.b tcc__r1
    sep #$20
    lda.l snes_background_storage+SNES_BG_DIRTY_COLUMN,x
    cmp #255
    beq load_column_first
    cmp.b tcc__r1
    beq load_column_done
    lda #1
    sta.l snes_background_error
    bra load_column_done
load_column_first:
    lda.b tcc__r1
    sta.l snes_background_storage+SNES_BG_DIRTY_COLUMN,x
    rep #$20
    lda.l snes_background_storage+SNES_BG_DMA_BYTES
    clc
    adc #64
    sta.l snes_background_storage+SNES_BG_DMA_BYTES
load_column_done:
    rep #$20
load_return:
    ply
    plx
    pld
    plp
    rtl

; void snesBackgroundReleaseCellFast(DeceptusU16 plane, DeceptusI16 x, DeceptusI16 y)
snesBackgroundReleaseCellFast:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; plane 11,s, x 13,s, y 15,s.
    jsr cell_map_offset
    sta.b tcc__r2
    tax
    lda.l snes_background_storage+SNES_BG_MAPS,x
    and #1023
    tay
    lda 11,s
    beq release_plane_zero
    tya
    cmp #256
    bcc release_low_tile
    sbc #256
    bra release_slot
release_low_tile:
    adc #816
    bra release_slot
release_plane_zero:
    tya
release_slot:
    beq release_clear
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_REFERENCES,x
    beq release_clear
    cmp #1
    beq release_slow
    dec a
    sta.l snes_background_storage+SNES_BG_REFERENCES,x
release_clear:
    ldx.b tcc__r2
    lda 11,s
    beq release_blank
    lda #256
release_blank:
    sta.l snes_background_storage+SNES_BG_MAPS,x
release_return:
    ply
    plx
    pld
    plp
    rtl
release_slow:
    lda 15,s
    pha
    lda 15,s
    pha
    lda 15,s
    pha
    jsl snesBackgroundReleaseCell
    pla
    pla
    pla
    bra release_return
.ends
