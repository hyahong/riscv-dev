#include <uart.h>

/*
 * UART OFFSET TABLE
 *   txfifo = 0x00
 *   rxfifo = 0x04
 *   txctrl = 0x08
 *   txmark = 0x0a
 *   rxctrl = 0x0c
 *   rxmark = 0x0e
 *
 *   ie     = 0x10
 *   ip     = 0x14
 *   div    = 0x18
 *   parity = 0x1c
 *   wire4  = 0x20
 *   either8or9 = 0x24
*/

#define UART ((volatile uint32_t *) (uintptr_t) 0x64000000)

void
uart_init (void)
{
	/* 115200 baud rate */
	UART[6] = 50000000 / 115200 - 1;
	/* activate UART-tx */
	UART[2] = 1;
}

void
uart_putc (char c)
{
	/* wait until write-possible */
	while ((int32_t) UART[0] < 0);

	UART[0] = (uint8_t) c;
}

