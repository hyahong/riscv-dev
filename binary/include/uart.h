#ifndef _UART_H_
#define _UART_H_

#include <stdint.h>

#define CPU_CLOCK_HZ 40000000
#define UART_BAUDRATE 115200

void uart_init (void);
void uart_putc (char c);

#endif
