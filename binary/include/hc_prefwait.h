#ifndef BOOM_HC_PREFWAIT_H
#define BOOM_HC_PREFWAIT_H
#include "hc_prefetch_arm.h"

/* Compatibility for the ordinary-function HNSW worker. Each accepted range
 * is consumed before another is armed. These are advisory hints: rounding
 * may touch an extra cacheable line, and WAIT does not promise a demand hit.
 * No descriptor is created for an empty range. */
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

/* index must be an immediate; unlike STATUS these are observational counters. */
#define hc_counter(index) __extension__ ({ \
	uint64_t hc_counter_value_; \
	__asm__ volatile (".insn i 0x0b, 0, %0, x0, %1" \
		: "=r"(hc_counter_value_) : "i"(index) : "memory"); \
	hc_counter_value_; \
})
#endif
