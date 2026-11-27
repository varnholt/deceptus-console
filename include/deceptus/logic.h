#ifndef DECEPTUS_LOGIC_H
#define DECEPTUS_LOGIC_H

#include "deceptus/mechanisms.h"

#define DECEPTUS_LOGIC_OK 0
#define DECEPTUS_LOGIC_BAD_CODE 1
#define DECEPTUS_LOGIC_MISSING_TARGET 2
#define DECEPTUS_LOGIC_BUDGET 3

/* executes bounded boolean callbacks compiled from source Lua. writes commit
 * only after END, so an unsupported target or malformed program changes nothing. */
DeceptusU8 deceptusLogicExecute(DeceptusMechanismSet* set, const DeceptusU8* code, DeceptusU16 length, DeceptusU8 arguments);

#endif
