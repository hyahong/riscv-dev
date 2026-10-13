#include <stdint.h>
#include <printk.h>

#define COLD_LOADS 16
#define COLD_STRIDE 64

__attribute__((aligned(4096)))
static uint64_t cold_data[COLD_LOADS][COLD_STRIDE / sizeof(uint64_t)] = {
	[0 ... COLD_LOADS - 1] = { 0x1234 }
};

__attribute__((noinline))
static void
prefetch_all (void)
{
	uintptr_t addr = (uintptr_t) &cold_data[0][0];

	__asm__ volatile ("csrr zero,mcycle" ::: "memory");

	for (int i = 0; i < COLD_LOADS; i++)
	{
		__builtin_prefetch ((void *) addr, 0, 3);
		// wait until the pipeline is empty to avoid droping prefetch request
		__asm__ volatile ("csrr zero,mcycle" ::: "memory");
		addr += COLD_STRIDE;
	}
}

__attribute__((noinline))
static uint64_t
measure_loads (uint64_t *loaded_value)
{
	uint64_t start, end, value;
	uintptr_t addr = (uintptr_t) &cold_data[0][0];

	__asm__ volatile (
		"csrr %0,mcycle\n\t"
		".rept %c4\n\t"
		"ld %1,0(%3)\n\t"
		"addi %3,%3,%c5\n\t"
		".endr\n\t"
		"csrr %2,mcycle"
		: "=&r"(start), "=&r"(value), "=&r"(end), "+&r"(addr)
		: "i"(COLD_LOADS), "i"(COLD_STRIDE) : "memory"
	);

	*loaded_value = value;
	return end - start;
}

void
measure_latency (void)
{
	uint64_t cold_value, warm_value;
	uint64_t cold_cycles = measure_loads (&cold_value);
	uint64_t warm_cycles = measure_loads (&warm_value);

	printk ("[COLD] loads=%u, stride=%u, cycles=%lu, cycles/load=%lu.%03lu, value=%lx\n",
	        (unsigned) COLD_LOADS, (unsigned) COLD_STRIDE,
	        (unsigned long) cold_cycles, (unsigned long) (cold_cycles / COLD_LOADS),
	        (unsigned long) ((cold_cycles % COLD_LOADS) * 1000 / COLD_LOADS),
	        (unsigned long) cold_value);
	printk ("[WARM] loads=%u, stride=%u, cycles=%lu, cycles/load=%lu.%03lu, value=%lx\n",
	        (unsigned) COLD_LOADS, (unsigned) COLD_STRIDE,
	        (unsigned long) warm_cycles, (unsigned long) (warm_cycles / COLD_LOADS),
	        (unsigned long) ((warm_cycles % COLD_LOADS) * 1000 / COLD_LOADS),
	        (unsigned long) warm_value);
}

void
measure_latency_pf (void)
{
	prefetch_all ();

	uint64_t pre_value;
	uint64_t pre_cycles = measure_loads (&pre_value);

	printk ("[PRE] loads=%u, stride=%u, cycles=%lu, cycles/load=%lu.%03lu, value=%lx\n",
	        (unsigned) COLD_LOADS, (unsigned) COLD_STRIDE,
	        (unsigned long) pre_cycles, (unsigned long) (pre_cycles / COLD_LOADS),
	        (unsigned long) ((pre_cycles % COLD_LOADS) * 1000 / COLD_LOADS),
	        (unsigned long) pre_value);
}
