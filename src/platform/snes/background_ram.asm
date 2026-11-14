.include "hdr.asm"

; separate bank ownership keeps the background cache out of the shared game's
; 7e data bank. c accesses this allocation through its full far pointer.
.base 0
.ramsection "snes_background_storage" bank $7f slot 3 orga $0000 force
snes_background_storage dsb 32768
.ends
