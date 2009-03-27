/*
 * DaVinci (and relatives) specific tweaks.
 * This is merged into head.S by the linker.
 */

#include <linux/linkage.h>
#include <asm/mach-types.h>

		.section        ".start", "ax"

__davinci_start:
		/* Save machine number for later conditional code */
		adr	r0, davinci_machine_no
		str	r7, [r0]

		.globl	davinci_machine_no
davinci_machine_no:
		.word	0x00000000
