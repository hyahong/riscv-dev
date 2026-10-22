#include <stdint.h>
#include <uart.h>
#include <printk.h>
#include <tool.h>

#ifndef RUN_ID
#define RUN_ID -1
#endif

void hnsw_demo (void);
void corohnsw_demo (void);
void hchnsw_demo (unsigned active);

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

	hnsw_demo ();
	//corohnsw_demo ();
	//hchnsw_demo (2);

	printk ("[%s:MAIN] WFI\n", RUN_ID);
}

