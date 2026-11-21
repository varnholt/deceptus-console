#include "lz4.h"
#include "deceptus/compression.h"

/* tile banks, sprite frames and rasters decode on every cache miss; the portable decoder spends most
   of its time on 32-bit bookkeeping per token. this one keeps the stream in address registers, checks
   each copy once against both ends and copies through an unrolled byte loop entered Duff-style.
   failure cases match the portable decoder: truncated lengths or offsets, a zero offset or one before
   the output start, a copy past either end, or a stream that does not end exactly after literals
   with a full destination. */
DeceptusU8 amigaLz4DecodeBlock(const DeceptusU8 *source, DeceptusU32 source_size, DeceptusU8 *destination, DeceptusU32 destination_size);

__asm__(
   "   .text\n"
   "   .even\n"
   "   .globl amigaLz4DecodeBlock\n"
   "amigaLz4DecodeBlock:\n"
   "   movem.l %d2-%d5/%a2-%a4,-(%sp)\n"
   "   move.l 32(%sp),%a0\n"            /* source */
   "   move.l 36(%sp),%d0\n"
   "   lea (%a0,%d0.l),%a1\n"           /* source end */
   "   move.l 40(%sp),%a2\n"            /* destination cursor */
   "   move.l %a2,%d5\n"                /* destination start */
   "   move.l 44(%sp),%d0\n"
   "   lea (%a2,%d0.l),%a3\n"           /* destination end */
   ".Llz4_token:\n"
   "   cmpa.l %a1,%a0\n"
   "   bcc.w .Llz4_fail\n"
   "   moveq #0,%d0\n"
   "   move.b (%a0)+,%d0\n"
   "   move.l %d0,%d1\n"                /* d0 is zero-extended; the length is used as a long */
   "   lsr.w #4,%d1\n"
   "   beq.s .Llz4_match\n"
   "   cmp.w #15,%d1\n"
   "   bne.s .Llz4_literals\n"
   ".Llz4_literal_length:\n"
   "   cmpa.l %a1,%a0\n"
   "   bcc.w .Llz4_fail\n"
   "   moveq #0,%d2\n"
   "   move.b (%a0)+,%d2\n"
   "   add.l %d2,%d1\n"
   "   cmp.b #-1,%d2\n"
   "   beq.s .Llz4_literal_length\n"
   ".Llz4_literals:\n"
   "   lea (%a0,%d1.l),%a4\n"
   "   cmpa.l %a1,%a4\n"
   "   bhi.w .Llz4_fail\n"
   "   lea (%a2,%d1.l),%a4\n"
   "   cmpa.l %a3,%a4\n"
   "   bhi.w .Llz4_fail\n"
   "   moveq #7,%d4\n"
   "   and.w %d1,%d4\n"
   "   lsr.w #3,%d1\n"
   "   add.w %d4,%d4\n"
   "   neg.w %d4\n"
   "   jmp .Llz4_literal_loop(%pc,%d4.w)\n"
   ".Llz4_literal_body:\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   "   move.b (%a0)+,(%a2)+\n"
   ".Llz4_literal_loop:\n"
   "   dbra %d1,.Llz4_literal_body\n"
   ".Llz4_match:\n"
   "   cmpa.l %a1,%a0\n"
   "   beq.s .Llz4_end\n"
   "   lea 2(%a0),%a4\n"
   "   cmpa.l %a1,%a4\n"
   "   bhi.s .Llz4_fail\n"
   "   moveq #0,%d2\n"
   "   move.b (%a0)+,%d2\n"
   "   moveq #0,%d3\n"
   "   move.b (%a0)+,%d3\n"
   "   lsl.w #8,%d3\n"
   "   or.w %d3,%d2\n"
   "   beq.s .Llz4_fail\n"
   "   and.w #15,%d0\n"
   "   cmp.w #15,%d0\n"
   "   bne.s .Llz4_match_copy\n"
   ".Llz4_match_length:\n"
   "   cmpa.l %a1,%a0\n"
   "   bcc.s .Llz4_fail\n"
   "   moveq #0,%d3\n"
   "   move.b (%a0)+,%d3\n"
   "   add.l %d3,%d0\n"
   "   cmp.b #-1,%d3\n"
   "   beq.s .Llz4_match_length\n"
   ".Llz4_match_copy:\n"
   "   addq.l #4,%d0\n"
   "   lea (%a2,%d0.l),%a4\n"
   "   cmpa.l %a3,%a4\n"
   "   bhi.s .Llz4_fail\n"
   "   move.l %a2,%a4\n"
   "   suba.l %d2,%a4\n"
   "   cmpa.l %d5,%a4\n"
   "   bcs.s .Llz4_fail\n"
   "   moveq #7,%d4\n"
   "   and.w %d0,%d4\n"
   "   lsr.w #3,%d0\n"
   "   add.w %d4,%d4\n"
   "   neg.w %d4\n"
   "   jmp .Llz4_match_loop(%pc,%d4.w)\n"
   ".Llz4_match_body:\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   "   move.b (%a4)+,(%a2)+\n"
   ".Llz4_match_loop:\n"
   "   dbra %d0,.Llz4_match_body\n"
   "   bra.w .Llz4_token\n"
   ".Llz4_end:\n"
   "   cmpa.l %a3,%a2\n"
   "   bne.s .Llz4_fail\n"
   "   moveq #1,%d0\n"
   "   bra.s .Llz4_return\n"
   ".Llz4_fail:\n"
   "   moveq #0,%d0\n"
   ".Llz4_return:\n"
   "   movem.l (%sp)+,%d2-%d5/%a2-%a4\n"
   "   rts\n");


DeceptusU8 amigaLz4Decode(const DeceptusU8 *source, DeceptusU32 source_size, DeceptusU8 *destination, DeceptusU32 destination_size)
{
   /* the unrolled copies count in 16 bits; anything unusual keeps the portable decoder. */
   if (!source || !source_size || !destination || !destination_size || destination_size > 65535UL)
   {
      return deceptusLz4Decode(source, source_size, destination, destination_size);
   }
   return amigaLz4DecodeBlock(source, source_size, destination, destination_size);
}
