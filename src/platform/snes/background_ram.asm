.include "hdr.asm"

; separate bank ownership keeps the background cache out of the shared game's
; 7e data bank. c and assembly access this allocation through its full far address.
; on the SA-1 cartridge the SA-1 prepares the background, so the area moves to the
; second BW-RAM bank, which the S-CPU's commit and DMA reach as well.
.base 0
.ifdef SNES_SA1
.ramsection "snes_background_storage" bank $41 slot 3 orga $0000 force
.else
.ramsection "snes_background_storage" bank $7f slot 3 orga $0000 force
.endif
snes_background_storage dsb 32768
.ends

; the presentation packets (SnesBackgroundPacket in background_work.h) above the work area in
; the same bank: the commit and its dma reach both through their far address.
.ifdef SNES_SA1
.ramsection "snes_background_packets" bank $41 slot 3 orga $8000 force
.else
.ramsection "snes_background_packets" bank $7f slot 3 orga $8000 force
.endif
snes_background_packets dsb 8192
.ends
