.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; request abi: resource/x/y words and flip/priority bytes. source descriptors
; have sixteen-byte stride; slot misses retain the bounded c allocation path.
.section "SnesSpriteResource" semisuperfree banks 2-15
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
    sbc #32
    tas
    lda 44,s
    sta.b tcc__r0
    lda 46,s
    sta.b tcc__r0h
    ldy #0
    lda.b [tcc__r0],y
    sta 1,s
    iny
    iny
    lda.b [tcc__r0],y
    sta 3,s
    iny
    iny
    lda.b [tcc__r0],y
    sta 5,s
    iny
    iny
    lda.b [tcc__r0],y
    and #255
    sta 11,s
    iny
    lda.b [tcc__r0],y
    and #255
    sta 13,s
    lda 48,s
    sta 15,s
    lda 1,s
    asl a
    tax
    lda.l snes_sprite_quad_offsets,x
    sta 27,s
    lda 1,s
    asl a
    asl a
    asl a
    asl a
    tax
    lda.l snes_sprite_resources+10,x
    sta.b tcc__r0
    and #255
    sta 7,s
    lda.b tcc__r0
    xba
    and #255
    sta 9,s
    lda #0
    sta 17,s
    sta 21,s
    lda 5,s
    sta 25,s
resource_row:
    lda 17,s
    cmp 9,s
    bcc resource_row_visible
    jmp.w resource_done
resource_row_visible:
    lda 3,s
    sta 23,s
    lda 11,s
    beq resource_row_x_ready
    lda 7,s
    dec a
    asl a
    asl a
    asl a
    asl a
    clc
    adc 3,s
    sta 23,s
resource_row_x_ready:
    lda #0
    sta 19,s
resource_column:
    lda 19,s
    cmp 7,s
    bcc resource_clip
    jmp.w resource_next_row
resource_clip:
    lda 23,s
    bmi resource_clip_left
    cmp #256
    bcc resource_clip_left
    jmp.w resource_next_column
resource_clip_left:
    clc
    adc #16
    beq resource_skip
    bmi resource_skip
    lda 25,s
    bmi resource_clip_top
    cmp #224
    bcc resource_clip_top
resource_skip:
    jmp.w resource_next_column
resource_clip_top:
    clc
    adc #16
    beq resource_skip
    bmi resource_skip
    lda 1,s
    asl a
    asl a
    asl a
    sta.b tcc__r0
    lda 21,s
    lsr a
    lsr a
    lsr a
    clc
    adc.b tcc__r0
    tax
    sep #$20
    lda.l snes_sprite_opaque,x
    sta.b tcc__r0
    rep #$20
    lda 21,s
    and #7
    tax
    sep #$20
    lda.l resource_masks,x
    and.b tcc__r0
    rep #$20
    and #255
    bne resource_allocate
    jmp.w resource_next_column
resource_allocate:
    lda 21,s
    pha
    lda 3,s
    pha
    jsl snesObjSlotFor
    tsa
    clc
    adc #4
    tas
    lda.b tcc__r0
    cmp #65535
    bne resource_emit
    jmp.w resource_next_column
resource_emit:
    sta 29,s
    lda 27,s
    clc
    adc 21,s
    tax
    sep #$20
    lda.l snes_sprite_quads,x
    rep #$20
    and #255
    xba
    ora 13,s
    pha
    lda 13,s
    xba
    ora 17,s
    pha
    lda 33,s
    pha
    lda 31,s
    pha
    lda 31,s
    pha
    jsl snesObjEmitQuads
    tsa
    clc
    adc #10
    tas
resource_next_column:
    lda 21,s
    inc a
    sta 21,s
    lda 19,s
    inc a
    sta 19,s
    lda 11,s
    bne resource_step_left
    lda 23,s
    clc
    adc #16
    sta 23,s
    jmp.w resource_column
resource_step_left:
    lda 23,s
    sec
    sbc #16
    sta 23,s
    jmp.w resource_column
resource_next_row:
    lda 17,s
    inc a
    sta 17,s
    lda 25,s
    clc
    adc #16
    sta 25,s
    jmp.w resource_row
resource_done:
    tsa
    clc
    adc #32
    tas
    ply
    plx
    pld
    plb
    plp
    rtl
resource_masks:
    .db 1,2,4,8,16,32,64,128
.ends

; resident pieces are found by walking the resource's slot list, as the c
; allocator does first. only a miss pays for the compiled allocation path.
; slot layout: resource word, then piece, valid, used, held, pending, next.
.section "snes_sprite_slot_for" semisuperfree banks 2-15
; DeceptusU16 snesObjSlotFor(DeceptusU16 resource, DeceptusU16 piece)
snesObjSlotFor:
    php
    rep #$30
    phd
    phx
    lda #0
    tcd
    ; return address 3 bytes, saved state 5 bytes: resource 9,s, piece 11,s.
    lda 11,s
    cmp #256
    bcs slot_for_miss
    lda 9,s
    tax
    lda.l snes_obj_heads,x
    and #255
slot_for_walk:
    beq slot_for_miss
    dec a
    sta.b tcc__r0
    asl a
    asl a
    asl a
    tax
    lda.l snes_obj_slots+2,x
    and #255
    cmp 11,s
    beq slot_for_hit
    lda.l snes_obj_slots+7,x
    and #255
    bra slot_for_walk
slot_for_hit:
    sep #$20
    lda #1
    sta.l snes_obj_slots+4,x
    rep #$20
    plx
    pld
    plp
    rtl
slot_for_miss:
    lda 11,s
    pha
    lda 11,s
    pha
    jsl snesObjSlotAllocate
    pla
    pla
    plx
    pld
    plp
    rtl
.ends
