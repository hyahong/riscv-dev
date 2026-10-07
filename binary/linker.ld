OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;

  .text :
	{
		KEEP(*(.text.init))
		*(.text .text.*)
	}

  .rodata : ALIGN(8)
	{
		*(.rodata .rodata.*)
	}

  .data : ALIGN(8)
	{
    __global_pointer$ = . + 0x800;
    *(.sdata .sdata.*)
    *(.data .data.*)
  }

  .bss (NOLOAD) : ALIGN(8)
	{
    __bss_start = .;

    *(.sbss .sbss.*)
    *(.bss .bss.*)
		*(COMMON)

    . = ALIGN(8);
    __bss_end = .;
  }

  . = ALIGN(16);
  . += 0x4000;
  __stack_top = .;
}
