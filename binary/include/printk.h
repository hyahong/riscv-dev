#ifndef _PRINTK_H_
#define _PRINTK_H_

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define PRINTK_BUFFER_SIZE 512
#define PRINTK_MAX_WIDTH 32

#define PRINTK_FLAG_WIDTH 0x01

struct printk_info
{
	const char *format;
	char buffer[PRINTK_BUFFER_SIZE + 1];
	uint32_t size;
	uint32_t offset;

	/* flags */
	uint32_t flags;
	struct
	{
		int width;
		char fill;
		uint8_t length; /* 0: int, 1: long, 2: long long */
	} flag;
};

/*
 * Supported:
 *   %c, %s, %d, %u, %x, %X, %p, %%, numeric l/ll
 *   optional zero fill / decimal width (capped at 32)
 */
int printk (const char *format, ...);
int vprintk (const char *format, va_list args);

#endif
