#include <stdint.h>
#include <uart.h>
#include <printk.h>

#ifndef RUN_ID
#define RUN_ID -1
#endif

void
_trap (uint64_t cause, uint64_t pc, uint64_t value)
{
	/* initialize */
	uart_init ();

	printk ("TRAP run=%d cause=%x pc=%x value=%x\n", RUN_ID, cause, pc, value);
}

void
_main (uint64_t hartid, uintptr_t dtb)
{
	/* initialize */
	uart_init ();

	printk ("BOOT FROM PCIe\n");
	printk ("MAIN run=%d, hartid=%d, dtb=%x\n", RUN_ID, hartid, dtb);
}

