.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; runs a list of SnesVramDmaCommand (vram_dma.h) prepared outside vblank. the
; runner owns dma channel six only for this batch and restores the ordinary vram
; increment afterwards.
.section "snes_vram_dma_runner" semisuperfree banks 2-15
snesVramRunDma:
    php
    rep #$30
    phb
    phd
    phx
    phy
    lda #0
    tcd
    lda 12,s
    sta.b tcc__r9
    lda 14,s
    sta.b tcc__r9h
    lda 16,s
    tax
    beq vram_dma_done
    ldy #0
    sep #$20
    lda #1
    sta.l $004360
    lda #$18
    sta.l $004361
    rep #$20
vram_dma_next:
    lda.b [tcc__r9],y
    sta.l $004362
    iny
    iny
    sep #$20
    lda.b [tcc__r9],y
    sta.l $004364
    iny
    lda.b [tcc__r9],y
    sta.l $002115
    iny
    rep #$20
    lda.b [tcc__r9],y
    sta.l $002116
    iny
    iny
    lda.b [tcc__r9],y
    sta.l $004365
    iny
    iny
    sep #$20
    lda #$40
    sta.l $00420b
    rep #$20
    dex
    bne vram_dma_next
vram_dma_done:
    sep #$20
    lda #$80
    sta.l $002115
    rep #$20
    ply
    plx
    pld
    plb
    plp
    rtl
.ends
