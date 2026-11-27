.include "hdr.asm"
.accu 16
.index 16
.16bit
.base $c0

; linear-probe lookup in the background pattern hash. it runs for every cell
; streamed in or released while scrolling, and the compiled c loop spent most
; of its time on far-pointer arithmetic per probe. behaviour matches the c
; reference hashFind in background.c exactly, including the probe statistic.
; the offsets are those of hash_keys and hash_slots in SnesBackgroundWork;
; background.c fails to compile if the structure layout moves them.
.define SNES_BG_HASH_KEYS 6864

.section "snes_background_hash_find" semisuperfree banks 2-15
; DeceptusU16 snesBackgroundHashFind(DeceptusU16 pattern, DeceptusU16 insert)
snesBackgroundHashFind:
    php
    rep #$30
    phd
    phx
    phy
    lda #0
    tcd
    ; return address 3 bytes, saved state 7 bytes: pattern 11,s, insert 13,s.
    lda 11,s
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    eor 11,s
    and #2047
    asl a
    tax
    ldy #0
    ; tcc__r1 holds the first deleted entry's byte offset, or $ffff for none.
    lda #$ffff
    sta.b tcc__r1
hash_find_probe:
    lda.l snes_background_storage+SNES_BG_HASH_KEYS,x
    cmp 11,s
    beq hash_find_hit
    cmp #$ffff
    beq hash_find_empty
    cmp #$fffe
    beq hash_find_deleted
hash_find_next:
    txa
    inc a
    inc a
    and #4095
    tax
    iny
    cpy #2048
    bne hash_find_probe
    ; a full table without a match yields the remembered deleted entry.
    lda #2048
    sta.l snes_background_hash_probes
    lda.b tcc__r1
    cmp #$ffff
    beq hash_find_return
    lsr a
    bra hash_find_return
hash_find_deleted:
    lda 13,s
    beq hash_find_next
    lda.b tcc__r1
    cmp #$ffff
    bne hash_find_next
    stx.b tcc__r1
    bra hash_find_next
hash_find_empty:
    jsr hash_find_count
    ; an insert reuses the first deleted entry on the probe path.
    lda 13,s
    beq hash_find_key
    lda.b tcc__r1
    cmp #$ffff
    beq hash_find_key
    lsr a
    bra hash_find_return
hash_find_hit:
    jsr hash_find_count
hash_find_key:
    txa
    lsr a
hash_find_return:
    sta.b tcc__r0
    ply
    plx
    pld
    plp
    rtl

; raise the longest-probe statistic to this lookup's probe count, y plus one.
hash_find_count:
    tya
    inc a
    cmp.l snes_background_hash_probes
    bcc hash_find_count_done
    sta.l snes_background_hash_probes
hash_find_count_done:
    rts
.ends
