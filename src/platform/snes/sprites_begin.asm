.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; clear presentation counters and hide unused objects. cached graphics identity,
; display protection and pending uploads remain intact across presentations.
.section "snes_sprite_begin" semisuperfree banks 2-15
snesObjBegin:
    php
    rep #$30
    phx
    ldx #0
sprite_begin_oam:
    lda #$f000
    sta.l snes_obj_oam,x
    lda #0
    sta.l snes_obj_oam+2,x
    inx
    inx
    inx
    inx
    cpx #512
    bne sprite_begin_oam
sprite_begin_high:
    sta.l snes_obj_oam,x
    inx
    inx
    cpx #544
    bne sprite_begin_high
    ldx #0
sprite_begin_lines:
    sta.l snes_obj_lines,x
    sta.l snes_obj_slivers,x
    inx
    inx
    cpx #224
    bne sprite_begin_lines
    sep #$20
    ldx #0
sprite_begin_used:
    sta.l snes_obj_slots+4,x
    inx
    inx
    inx
    inx
    inx
    inx
    inx
    inx
    cpx #1024
    bne sprite_begin_used
    rep #$20
    plx
    plp
    rtl
.ends

; publish a prepared presentation: copy the OAM shadow for the SDK VBlank and
; protect every slot displayed from now on. the C loop cost about a fifth of
; the sprite commit's VBlank time.
.section "snes_sprite_publish" semisuperfree banks 2-15
snesObjPublish:
    php
    rep #$30
    phx
    ldx #0
sprite_publish_oam:
    lda.l snes_obj_oam,x
    sta.l oamMemory,x
    inx
    inx
    cpx #544
    bne sprite_publish_oam
    sep #$20
    ldx #0
sprite_publish_held:
    lda.l snes_obj_slots+4,x
    sta.l snes_obj_slots+5,x
    rep #$21
    txa
    adc #8
    tax
    sep #$20
    cpx #1024
    bne sprite_publish_held
    rep #$20
    plx
    plp
    rtl
.ends
