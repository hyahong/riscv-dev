.section .text.init, "ax", @progbits
.option norvc
.option norelax
.globl _start
_start:
    csrci mstatus, 8
    csrw mie, zero

		/* a0: mhartid	*/
		/* a1: dtb			*/
    mv s0, a0
    mv s1, a1

    la gp, __global_pointer$
    la sp, __stack_top
    la t0, .Ltrap
    csrw mtvec, t0

		/* bss zeroing */
    la t0, __bss_start
    la t1, __bss_end
.Lzero:
    bgeu t0, t1, .Lmain
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lzero

		/* s0: mhartid	*/
		/* s1: dtb			*/
.Lmain:
    mv a0, s0
    mv a1, s1
    call _main

		/* hart */
.Lpark:
    wfi
    j .Lpark

.align 2
.Ltrap:
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    call _trap
    j .Lpark
