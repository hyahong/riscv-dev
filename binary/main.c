#include <stdint.h>
#include <uart.h>
#include <printk.h>
#include <tool.h>

#ifndef RUN_ID
#define RUN_ID -1
#endif

// HNSW
void hnsw_demo (void);
void corohnsw_demo (void);
void sdmhnsw_demo (void);

// STL
struct Bench;
void bench_sequential (const struct Bench *bench);
void bench_sdm (const struct Bench *bench);

extern const struct Bench map_lookup;
extern const struct Bench map_scan;
extern const struct Bench set_lookup;
extern const struct Bench set_scan;
extern const struct Bench list_traverse;
extern const struct Bench vector_lookup;
extern const struct Bench vector_scan;
extern const struct Bench priority_queue_replace;
extern const struct Bench deque_lookup;
extern const struct Bench deque_scan;

void
_trap (uint64_t cause, uint64_t pc, uint64_t value)
{
	/* initialize */
	uart_init ();

	printk ("[%s:TRAP] cause=%x pc=%x value=%x\n", RUN_ID, cause, pc, value);
}

void
_main (uint64_t hartid, uintptr_t dtb)
{
	/* initialize */
	uart_init ();

	printk ("[%s:MAIN] BOOT FROM PCIe\n", RUN_ID);
	printk ("[%s:MAIN] hartid=%d, dtb=%x\n", RUN_ID, hartid, dtb);

	// HNSW
	hnsw_demo ();
	//corohnsw_demo ();
	//sdmhnsw_demo ();

	// STL
	//bench_sequential (&map_lookup);
	//bench_sdm (&map_lookup);
	//bench_sequential (&map_scan);
	//bench_sdm (&map_scan);

	//bench_sequential (&set_lookup);
	//bench_sdm (&set_lookup);
	//bench_sequential (&set_scan);
	//bench_sdm (&set_scan);

	//bench_sequential (&list_traverse);
	//bench_sdm (&list_traverse);

	//bench_sequential (&vector_lookup);
	//bench_sdm (&vector_lookup);
	//bench_sequential (&vector_scan);
	//bench_sdm (&vector_scan);

	//bench_sequential (&priority_queue_replace);
	//bench_sdm (&priority_queue_replace);

	//bench_sequential (&deque_lookup);
	//bench_sdm (&deque_lookup);
	//bench_sequential (&deque_scan);
	//bench_sdm (&deque_scan);

	printk ("[%s:MAIN] WFI\n", RUN_ID);
}

