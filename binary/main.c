#include <stdint.h>
#include <uart.h>
#include <printk.h>
#include <tool.h>

#ifndef RUN_ID
#define RUN_ID -1
#endif

/* HNSW */
void hnsw_demo (void);
void corohnsw_demo (void);
void sdmhnsw_demo (void);

/* STL */
/* map */
void map_demo (void);
void sdmmap_demo (void);
void mapscan_demo (void);
void sdmmapscan_demo (void);

/* set */
void set_demo (void);
void sdmset_demo (void);
void setscan_demo (void);
void sdmsetscan_demo (void);

/* list */
void list_demo (void);
void sdmlist_demo (void);

/* vector */
void vector_demo (void);
void sdmvector_demo (void);
void vectorscan_demo (void);
void sdmvectorscan_demo (void);

/* priority_queue */
void priority_queue_demo (void);
void sdmpriority_queue_demo (void);

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

	//hnsw_demo ();
	//corohnsw_demo ();
	//sdmhnsw_demo ();
	//map_demo ();
	sdmmap_demo ();
	//mapscan_demo ();
	//sdmmapscan_demo ();
	//set_demo ();
	//sdmset_demo ();
	//setscan_demo ();
	//sdmsetscan_demo ();
	//list_demo ();
	//sdmlist_demo ();
	//vector_demo ();
	//sdmvector_demo ();
	//vectorscan_demo ();
	//sdmvectorscan_demo ();
	//priority_queue_demo ();
	//sdmpriority_queue_demo ();

	printk ("[%s:MAIN] WFI\n", RUN_ID);
}

