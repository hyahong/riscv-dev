#ifndef HWCORO_PREFETCH_ARM_H
#define HWCORO_PREFETCH_ARM_H
#include "hc.h"
#include <stddef.h>
#include <stdbool.h>

// Arm exactly one descriptor, without consuming it. The caller must consume
// the previous descriptor with HC_WAIT before calling again, and must consume
// this descriptor before leaving the operation. A rejected range creates none.
// Constant supported sizes round upward as in hc_prefwait; readiness is advisory.
static inline bool
hc_prefetch_arm_range (const void *memory, size_t bytes)
{
	if (!bytes || bytes > 256)
		return false;
	const uintptr_t address = (uintptr_t) memory;
	if (bytes <= 4)
		hc_prefetch (address, 4);
	else if (bytes <= 8)
		hc_prefetch (address, 8);
	else if (bytes <= 64)
		hc_prefetch (address, 64);
	else if (bytes <= 68)
		hc_prefetch (address, 68);
	else if (bytes <= 128)
		hc_prefetch (address, 128);
	else if (bytes <= 132)
		hc_prefetch (address, 132);
	else
		hc_prefetch (address, 256);
	return true;
}
#endif
