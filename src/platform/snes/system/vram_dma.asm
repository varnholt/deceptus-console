.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; runs a list of SnesVramDmaCommand (vram_dma.h) prepared outside vblank. the
; runner owns dma channel six only for this batch and restores the ordinary vram
; increment afterwards. every command costs cpu time on top of its bytes, and
; vblank is short: the data bank points at the list and the direct page at the
; dma registers, so each field is one short load and one short store.
.define DMA_CHANNEL_6 $4360

.section "snes_vram_dma_runner" semisuperfree banks 2-15
; void snesVramRunDma(const SnesVramDmaCommand *commands, DeceptusU16 count)
snesVramRunDma:
    php
    rep #$30
    phb
    phd
    phx
    phy
    ; return address 3 bytes, saved state 8 bytes: commands 12,s (bank 14,s), count 16,s.
    lda 16,s
    beq vram_dma_done
    tax
    lda 12,s
    tay
    sep #$20
    lda 14,s
    pha
    plb
    rep #$20
    lda #DMA_CHANNEL_6 & $ff00
    tcd
    sep #$20
    ; two registers, one word per unit: vram data port $2118/$2119.
    lda #1
    sta.b DMA_CHANNEL_6 & $ff
    lda #$18
    sta.b (DMA_CHANNEL_6 & $ff) + 1
    rep #$20
vram_dma_next:
    ; source address, bank, vram increment, target, byte count.
    lda.w 0,y
    sta.b (DMA_CHANNEL_6 & $ff) + 2
    sep #$20
    lda.w 2,y
    sta.b (DMA_CHANNEL_6 & $ff) + 4
    lda.w 3,y
    sta.l $002115
    rep #$20
    lda.w 4,y
    sta.l $002116
    lda.w 6,y
    sta.b (DMA_CHANNEL_6 & $ff) + 5
    sep #$20
    lda #$40
    sta.l $00420b
    rep #$21
    tya
    adc #8
    tay
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
