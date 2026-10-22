#ifndef _HC_PREFWAIT_H_
#define _HC_PREFWAIT_H_

#include "hc.h"
#include <stddef.h>
#include <stdbool.h>

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

static inline void
hc_prefwait (const void *memory, size_t bytes)
{
	uintptr_t address = (uintptr_t) memory;
	while (bytes > 256)
	{
		hc_prefetch (address, 256);
		hc_wait ();
		address += 256;
		bytes -= 256;
	}
	if (hc_prefetch_arm_range ((const void *) address, bytes))
		hc_wait ();
}

#define hc_counter(index) __extension__ ({ \
	uint64_t hc_counter_value_; \
	__asm__ volatile (".insn i 0x0b, 0, %0, x0, %1" \
		: "=r"(hc_counter_value_) : "i"(index) : "memory"); \
	hc_counter_value_; \
})

#endif
