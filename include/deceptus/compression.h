#ifndef DECEPTUS_COMPRESSION_H
#define DECEPTUS_COMPRESSION_H
#include "deceptus/types.h"

/* rejects truncated packets, excess input and output outside the exact requested size. */
DeceptusU8 deceptusRleDecode(
   const DeceptusU8* source,
   DeceptusU32 source_size,
   DeceptusU8* destination,
   DeceptusU32 destination_size,
   DeceptusU8 unit_bytes
);
/* decode two-byte big-endian resource units directly into native words. */
DeceptusU8 deceptusRleDecodeWords(const DeceptusU8* source, DeceptusU32 source_size, DeceptusU16* destination, DeceptusU16 word_count);
DeceptusU8 deceptusLz4Decode(const DeceptusU8* source, DeceptusU32 source_size, DeceptusU8* destination, DeceptusU32 destination_size);
#endif
