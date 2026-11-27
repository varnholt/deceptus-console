#ifndef DECEPTUS_SMD_STRING_H
#define DECEPTUS_SMD_STRING_H

#include <stdint.h>

/* match sgdk's freestanding memory abi; all runtime pools are below 64 kib. */
void memcpy(void* destination, const void* source, uint16_t bytes);
void memset(void* destination, uint8_t value, uint16_t bytes);

#endif
