#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <reent.h>

extern unsigned char __heap_start[];
extern unsigned char __heap_end[];

static uintptr_t heap_break;

void *
_sbrk_r (struct _reent *r, ptrdiff_t incr)
{
	const uintptr_t start = (uintptr_t) __heap_start;
	const uintptr_t end = (uintptr_t) __heap_end;

	if (heap_break == 0)
			heap_break = start;

	if (end < start || heap_break < start || heap_break > end)
			goto fail;

	const uintptr_t old_break = heap_break;

	if (incr >= 0)
	{
		const uintptr_t size = (uintptr_t) incr;

		if (size > end - heap_break)
				goto fail;

		heap_break += size;
	}
	else
	{
			const uintptr_t size = (uintptr_t) (-(incr + 1)) + 1;

			if (size > heap_break - start)
					goto fail;

			heap_break -= size;
	}

	return (void *) old_break;

fail:
	r->_errno = ENOMEM;
	return (void *) -1;
}
