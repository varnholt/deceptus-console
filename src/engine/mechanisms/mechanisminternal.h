#ifndef DECEPTUS_MECHANISM_INTERNAL_H
#define DECEPTUS_MECHANISM_INTERNAL_H

/* private cross-family helpers; these modules share one compilation unit. */
static DeceptusU8 onOffBlockSolid(const DeceptusMechanismSet* set, DeceptusU16 index, const DeceptusRect* bounds);
static DeceptusU8 collapsingPlatformSolid(const DeceptusMechanismSet* set, DeceptusU16 index, const DeceptusRect* bounds);
static void damageRectContact(DeceptusMechanismSet* set, DeceptusU16 index);

#endif
