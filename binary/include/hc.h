#ifndef _HC_H_
#define _HC_H_

#include <stdint.h>

/* Experimental bare-metal ABI: custom-0, funct3=0, I-format operation. */
#define hc_cfg(index, value) do { \
	uint64_t hc_value_ = (uint64_t) (value); \
	__asm__ volatile (".insn i 0x0b, 0, x0, %0, %1" :: "r"(hc_value_), "i"(index) : "memory"); \
} while (0)

static inline void
hc_run (void)
{
	__asm__ volatile (".insn i 0x0b, 0, x0, x0, 0x100" ::: "memory");
}

static inline void
hc_yield (void)
{
	__asm__ volatile (".insn i 0x0b, 0, x0, x0, 0x101" ::: "memory");
}

static inline void
hc_wait (void)
{
	__asm__ volatile (".insn i 0x0b, 0, x0, x0, 0x105" ::: "memory");
}

/* bytes must be a compile-time constant in [1,256]. Completion only permits
 * resumption; eviction, dropped hints and timeout can still cause a load miss. */
#define hc_prefetch(address, bytes) do { \
	__asm__ volatile (".insn i 0x0b, 0, x0, %0, %1" :: \
		"r"((uintptr_t) (address)), "i"(0x200 + (bytes) - 1) : "memory"); \
} while (0)

static inline __attribute__((noreturn)) void
hc_exit (void)
{
	__asm__ volatile (".insn i 0x0b, 0, x0, x0, 0x102" ::: "memory");
	__builtin_unreachable ();
}

static inline int64_t
hc_next (void)
{
	int64_t value;
	__asm__ volatile (".insn i 0x0b, 0, %0, x0, 0x103" : "=r"(value) :: "memory");
	return value;
}

static inline uint64_t
hc_status (void)
{
	uint64_t value;
	__asm__ volatile (".insn i 0x0b, 0, %0, x0, 0x104" : "=r"(value) :: "memory");
	return value;
}

#endif
