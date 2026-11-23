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
;   clip rectangles, row in the row table, pattern zero, resident, or placed in
;   a free slot the plane can address (acquirePattern).
; - release: every case (releaseCell), with the backward-shift hash deletion.
; - place: a load whose pattern and attributes the caller already read.
; - direct clip refresh: both passes of the animated refresh in
;   snesBackgroundPrepare for a clip that is the topmost source of its cells.
; the offsets are those of SnesBackgroundWork and SnesBackgroundCellView;
; background.c fails to compile if the layouts move them.
.define SNES_BG_RESIDENT 0
.define SNES_BG_REFERENCES 2288
.define SNES_BG_FREE_SLOTS 4576
.define SNES_BG_HASH_KEYS 6864
.define SNES_BG_HASH_SLOTS 10960
.define SNES_BG_MAPS 15056
.define SNES_BG_UPLOADS 23248
.define SNES_BG_PENDING 27314
.define SNES_BG_DOMAINS 28458
.define SNES_BG_DIRTY_COLUMN 29602
.define SNES_BG_REFRESH_CHANGED 29604
.define SNES_BG_DIRTY_ROWS 30562
.define SNES_BG_DMA_BYTES 30570
.define SNES_BG_FREE_COUNT 30572
.define SNES_BG_UPLOAD_COUNT 30574
.define SNES_BG_ROOM_CELLS 30578
.define SNES_BG_HASH_TOMBSTONES 30580
.define SNES_BG_ROW_OFFSETS 30588
.define SNES_BG_STATUS_ERROR 32644
.define VIEW_COLUMNS 0
.define VIEW_ROWS 2
.define VIEW_PATTERNS 4
.define VIEW_CHUNKS 8
.define VIEW_CLIP_LEFT 16
.define VIEW_CLIP_TOP 20
.define VIEW_CLIP_RIGHT 24
.define VIEW_CLIP_BOTTOM 28
.define REFRESH_CHUNKS 0
.define REFRESH_PLANE 4
.define REFRESH_LEFT 6
.define REFRESH_TOP 8
.define REFRESH_RIGHT 10
.define REFRESH_BOTTOM 12
.define REFRESH_OFFSET 14
.define REFRESH_COLUMNS 16

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

; a = slot, kept. carry clear when the plane at 15,s (one jsr deeper than the
; entry points) can address the slot: slotAddressable in background.c.
slot_addressable:
    pha
    lda 15,s
    beq slot_addressable_zero
    pla
    cmp #768
    bcc slot_addressable_done
    cmp #1024
    bcs slot_addressable_yes
    sec
    rts
slot_addressable_yes:
    clc
slot_addressable_done:
    rts
slot_addressable_zero:
    pla
    cmp #1024
    rts

; a = domain key. removes its hash entry, if any: hashRemove in background.c.
release_domain:
    pea 0
    pha
    jsl snesBackgroundHashFind
    pla
    pla
    lda.b tcc__r0
    cmp #$ffff
    beq release_domain_done
    ; tcc__r0 = hole, x = next entry, both as byte offsets.
    asl a
    sta.b tcc__r0
    tax
release_shift_next:
    txa
    inc a
    inc a
    and #4095
    tax
    lda.l snes_background_storage+SNES_BG_HASH_KEYS,x
    cmp #$ffff
    beq release_shift_end
    cmp #$fffe
    beq release_shift_next
    sta.b tcc__r1
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    eor.b tcc__r1
    and #2047
    asl a
    sta.b tcc__r3
    ; an entry may fill the hole when the hole lies on its probe path:
    ; (next - home) >= (next - hole), all modulo the table.
    txa
    sec
    sbc.b tcc__r3
    and #4095
    sta.b tcc__r3
    txa
    sec
    sbc.b tcc__r0
    and #4095
    cmp.b tcc__r3
    beq release_shift_move
    bcs release_shift_next
release_shift_move:
    lda.l snes_background_storage+SNES_BG_HASH_SLOTS,x
    phx
    ldx.b tcc__r0
    sta.l snes_background_storage+SNES_BG_HASH_SLOTS,x
    lda.b tcc__r1
    sta.l snes_background_storage+SNES_BG_HASH_KEYS,x
    plx
    stx.b tcc__r0
    bra release_shift_next
release_shift_end:
    ldx.b tcc__r0
    lda #$ffff
    sta.l snes_background_storage+SNES_BG_HASH_KEYS,x
release_domain_done:
    rts

; early bail-outs of the load path branch here; load_slow is out of 8-bit reach.
load_slow_near:
    jmp load_slow

; void snesBackgroundPlaceCellFast(DeceptusU16 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU16 dirty,
;                                  DeceptusU16 pattern, DeceptusU16 attributes)
; loadCell for a cell whose source words the caller read; the load path's
; fallback reads the same words again through cellSource.
snesBackgroundPlaceCellFast:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    lda 21,s
    and #$dc00
    sta.b tcc__r3
    lda 19,s
    beq place_blank
    jmp load_pattern
place_blank:
    jmp load_tile

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
    bne load_pattern
    jmp load_tile
load_pattern:
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
    bne load_miss
    lda.l snes_background_storage+SNES_BG_HASH_SLOTS,x
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_REFERENCES,x
    inc a
    sta.l snes_background_storage+SNES_BG_REFERENCES,x
    txa
    lsr a
    jmp load_tile

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

load_miss:
    ; x = key * 2 of the insert position, tcc__r2 = domain key. the other
    ; plane's entry for the pattern may share its slot.
    stx.b tcc__r4
    pea 0
    lda.b tcc__r2
    eor #$4000
    pha
    jsl snesBackgroundHashFind
    pla
    pla
    lda.b tcc__r0
    cmp #$ffff
    beq load_free
    asl a
    tax
    lda.b tcc__r2
    eor #$4000
    cmp.l snes_background_storage+SNES_BG_HASH_KEYS,x
    bne load_free
    lda.l snes_background_storage+SNES_BG_HASH_SLOTS,x
    jsr slot_addressable
    bcc load_insert
load_free:
    ; the last free slot this plane can address, searched from the top.
    lda.l snes_background_storage+SNES_BG_FREE_COUNT
    asl a
    tax
load_free_next:
    cpx #0
    bne load_free_test
    ; no slot: the c path reports the full cache.
    jmp load_slow
load_free_test:
    dex
    dex
    lda.l snes_background_storage+SNES_BG_FREE_SLOTS,x
    jsr slot_addressable
    bcs load_free_next
    sta.b tcc__r5
    lda.l snes_background_storage+SNES_BG_FREE_COUNT
    dec a
    sta.l snes_background_storage+SNES_BG_FREE_COUNT
    phx
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_FREE_SLOTS,x
    plx
    sta.l snes_background_storage+SNES_BG_FREE_SLOTS,x
    lda.b tcc__r5
    asl a
    tax
    lda.b tcc__r2
    and #$3fff
    sta.l snes_background_storage+SNES_BG_RESIDENT,x
    ldx.b tcc__r5
    sep #$20
    lda.l snes_background_storage+SNES_BG_PENDING,x
    bne load_pending
    lda #1
    sta.l snes_background_storage+SNES_BG_PENDING,x
    rep #$20
    lda.l snes_background_storage+SNES_BG_UPLOAD_COUNT
    asl a
    tax
    lda.b tcc__r5
    sta.l snes_background_storage+SNES_BG_UPLOADS,x
    lda.l snes_background_storage+SNES_BG_UPLOAD_COUNT
    inc a
    sta.l snes_background_storage+SNES_BG_UPLOAD_COUNT
    lda.l snes_background_storage+SNES_BG_DMA_BYTES
    clc
    adc #32
    sta.l snes_background_storage+SNES_BG_DMA_BYTES
load_pending:
    rep #$20
    lda.b tcc__r5
load_insert:
    ; a = slot. claim the hash entry and count the reference.
    sta.b tcc__r5
    ldx.b tcc__r4
    lda.l snes_background_storage+SNES_BG_HASH_KEYS,x
    cmp #$fffe
    bne load_insert_key
    lda.l snes_background_storage+SNES_BG_HASH_TOMBSTONES
    dec a
    sta.l snes_background_storage+SNES_BG_HASH_TOMBSTONES
load_insert_key:
    lda.b tcc__r2
    sta.l snes_background_storage+SNES_BG_HASH_KEYS,x
    lda.b tcc__r5
    sta.l snes_background_storage+SNES_BG_HASH_SLOTS,x
    tax
    lda 11,s
    inc a
    sep #$20
    ora.l snes_background_storage+SNES_BG_DOMAINS,x
    sta.l snes_background_storage+SNES_BG_DOMAINS,x
    rep #$20
    lda.b tcc__r5
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_REFERENCES,x
    inc a
    sta.l snes_background_storage+SNES_BG_REFERENCES,x
    lda.b tcc__r5

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
    sta.l snes_background_storage+SNES_BG_STATUS_ERROR
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
    dec a
    sta.l snes_background_storage+SNES_BG_REFERENCES,x
    bne release_clear
    ; last reference: drop the slot's hash entries and free it.
    txa
    lsr a
    sta.b tcc__r5
    tax
    sep #$20
    lda.l snes_background_storage+SNES_BG_DOMAINS,x
    sta.b tcc__r4
    lda #0
    sta.l snes_background_storage+SNES_BG_DOMAINS,x
    rep #$20
    lda.b tcc__r4
    and #1
    beq release_domain_one
    lda.b tcc__r5
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_RESIDENT,x
    jsr release_domain
release_domain_one:
    lda.b tcc__r4
    and #2
    beq release_free
    lda.b tcc__r5
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_RESIDENT,x
    ora #$4000
    jsr release_domain
release_free:
    lda.b tcc__r5
    asl a
    tax
    lda #$ffff
    sta.l snes_background_storage+SNES_BG_RESIDENT,x
    lda.l snes_background_storage+SNES_BG_FREE_COUNT
    asl a
    tax
    lda.b tcc__r5
    sta.l snes_background_storage+SNES_BG_FREE_SLOTS,x
    lda.l snes_background_storage+SNES_BG_FREE_COUNT
    inc a
    sta.l snes_background_storage+SNES_BG_FREE_COUNT
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
; source words of the clip cell at offset a: pattern into a, attributes
; masked to $dc00 into tcc__r3.
clip_source:
    pha
    xba
    and #$00e0
    lsr a
    lsr a
    lsr a
    tay
    lda.l snes_background_clip_refresh+REFRESH_CHUNKS
    sta.b tcc__r0
    lda.l snes_background_clip_refresh+REFRESH_CHUNKS+2
    sta.b tcc__r0h
    lda [tcc__r0],y
    tax
    iny
    iny
    lda [tcc__r0],y
    sta.b tcc__r1h
    stx.b tcc__r1
    pla
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
    rts

; void snesBackgroundRefreshClipFast(void)
; both passes over snes_background_clip_refresh: mark the cells whose resident
; pattern or attributes differ from the clip frame and release them, then load
; the marked cells in row order.
; locals: x 1,s, y 3,s, cell offset 5,s, row offset 7,s, mark index 9,s,
; pattern 11,s, attributes 13,s.
snesBackgroundRefreshClipFast:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    tsc
    sec
    sbc #14
    tcs
    lda #0
    sta 9,s
    lda.l snes_background_clip_refresh+REFRESH_OFFSET
    sta 7,s
    lda.l snes_background_clip_refresh+REFRESH_TOP
    sta 3,s
refresh_mark_row:
    lda 3,s
    cmp.l snes_background_clip_refresh+REFRESH_BOTTOM
    bcc refresh_mark_columns
    jmp refresh_load_start
refresh_mark_columns:
    lda 7,s
    sta 5,s
    lda.l snes_background_clip_refresh+REFRESH_LEFT
    sta 1,s
refresh_mark_cell:
    lda 1,s
    cmp.l snes_background_clip_refresh+REFRESH_RIGHT
    bcc refresh_mark_test
    jmp refresh_mark_next_row
refresh_mark_test:
    lda 5,s
    jsr clip_source
    sta 11,s
    lda.l snes_background_clip_refresh+REFRESH_PLANE
    beq refresh_mark_expected
    lda #$2000
    tsb.b tcc__r3
refresh_mark_expected:
    ; map offset of (x, y) in the plane's map, as cell_map_offset.
    lda 3,s
    and #31
    asl a
    asl a
    asl a
    asl a
    asl a
    sta.b tcc__r1
    lda 1,s
    and #63
    cmp #32
    bcc refresh_mark_low
    adc #1024-32-1
refresh_mark_low:
    ora.b tcc__r1
    asl a
    sta.b tcc__r1
    lda.l snes_background_clip_refresh+REFRESH_PLANE
    xba
    asl a
    asl a
    asl a
    asl a
    ora.b tcc__r1
    tax
    lda.l snes_background_storage+SNES_BG_MAPS,x
    sta.b tcc__r1
    ; the attribute bits must match.
    and #$fc00
    cmp.b tcc__r3
    bne refresh_mark_changed
    ; tileSlot, then the resident pattern must match.
    lda.b tcc__r1
    and #1023
    tay
    lda.l snes_background_clip_refresh+REFRESH_PLANE
    beq refresh_mark_slot_zero
    tya
    cmp #256
    bcc refresh_mark_low_tile
    sbc #256
    bra refresh_mark_slot
refresh_mark_low_tile:
    adc #816
    bra refresh_mark_slot
refresh_mark_slot_zero:
    tya
refresh_mark_slot:
    asl a
    tax
    lda.l snes_background_storage+SNES_BG_RESIDENT,x
    cmp 11,s
    bne refresh_mark_changed
    lda 9,s
    tax
    sep #$20
    lda #0
    sta.l snes_background_storage+SNES_BG_REFRESH_CHANGED,x
    rep #$20
    bra refresh_mark_advance
refresh_mark_changed:
    lda 9,s
    tax
    sep #$20
    lda #1
    sta.l snes_background_storage+SNES_BG_REFRESH_CHANGED,x
    rep #$20
    ; releaseCell(plane, x, y): y pushed first.
    lda 3,s
    pha
    lda 3,s
    pha
    lda.l snes_background_clip_refresh+REFRESH_PLANE
    pha
    jsl snesBackgroundReleaseCellFast
    pla
    pla
    pla
refresh_mark_advance:
    lda 1,s
    inc a
    sta 1,s
    lda 5,s
    inc a
    sta 5,s
    lda 9,s
    inc a
    sta 9,s
    jmp refresh_mark_cell
refresh_mark_next_row:
    lda 7,s
    clc
    adc.l snes_background_clip_refresh+REFRESH_COLUMNS
    sta 7,s
    lda 3,s
    inc a
    sta 3,s
    jmp refresh_mark_row

refresh_load_start:
    lda #0
    sta 9,s
    lda.l snes_background_clip_refresh+REFRESH_OFFSET
    sta 7,s
    lda.l snes_background_clip_refresh+REFRESH_TOP
    sta 3,s
refresh_load_row:
    lda 3,s
    cmp.l snes_background_clip_refresh+REFRESH_BOTTOM
    bcc refresh_load_columns
    jmp refresh_done
refresh_load_columns:
    lda 7,s
    sta 5,s
    lda.l snes_background_clip_refresh+REFRESH_LEFT
    sta 1,s
refresh_load_cell:
    lda 1,s
    cmp.l snes_background_clip_refresh+REFRESH_RIGHT
    bcs refresh_load_next_row
    lda 9,s
    tax
    lda.l snes_background_storage+SNES_BG_REFRESH_CHANGED,x
    and #$00ff
    beq refresh_load_advance
    lda 5,s
    jsr clip_source
    sta 11,s
    ; the place entry masks the attributes itself.
    lda.b tcc__r3
    sta 13,s
    ; snesBackgroundPlaceCellFast(plane, x, y, 2, pattern, attributes).
    lda 13,s
    pha
    lda 13,s
    pha
    pea 2
    lda 9,s
    pha
    lda 9,s
    pha
    lda.l snes_background_clip_refresh+REFRESH_PLANE
    pha
    jsl snesBackgroundPlaceCellFast
    tsc
    clc
    adc #12
    tcs
refresh_load_advance:
    lda 1,s
    inc a
    sta 1,s
    lda 5,s
    inc a
    sta 5,s
    lda 9,s
    inc a
    sta 9,s
    bra refresh_load_cell
refresh_load_next_row:
    lda 7,s
    clc
    adc.l snes_background_clip_refresh+REFRESH_COLUMNS
    sta 7,s
    lda 3,s
    inc a
    sta 3,s
    jmp refresh_load_row
refresh_done:
    tsc
    clc
    adc #14
    tcs
    ply
    plx
    pld
    plp
    rtl
.ends
