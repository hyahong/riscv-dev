#include <stdint.h>
#include <uart.h>
#include <printk.h>
#include <stdlib.h>

#include <tool.h>

#ifndef RUN_ID
#define RUN_ID -1
#endif

void coro_demo (void);
void hnsw_demo (void);

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

	char *ptr1 = (char *) malloc (32);
	printk ("malloc(32) = %p\n", ptr1);
	ptr1 = (char *) malloc (4096);
	printk ("malloc(4K) = %p\n", ptr1);
	ptr1 = (char *) malloc (32);
	printk ("malloc(32) = %p\n", ptr1);

	
	coro_demo ();
	hnsw_demo ();
}

