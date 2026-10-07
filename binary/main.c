#include <stdint.h>

void
_trap (uint64_t cause, uint64_t pc, uint64_t value)
{
	(void) cause;
	(void) pc;
	(void) value;
}

void
_main (uint64_t hartid, uintptr_t dtb)
{
	(void) hartid;
	(void) dtb;
}
