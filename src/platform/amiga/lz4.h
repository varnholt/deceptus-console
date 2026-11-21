#ifndef DECEPTUS_AMIGA_LZ4_H
#define DECEPTUS_AMIGA_LZ4_H

#include "deceptus/types.h"

/* deceptusLz4Decode with the same result for every input; blocks of up to 64K decode in 68000 code. */
DeceptusU8 amigaLz4Decode(const DeceptusU8 *source, DeceptusU32 source_size, DeceptusU8 *destination, DeceptusU32 destination_size);

#endif
