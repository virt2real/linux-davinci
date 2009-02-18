/*
 * Serial port stubs for kernel decompress status messages
 *
 *  Author:     Anant Gole
 * (C) Copyright (C) 2006, Texas Instruments, Inc
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/serial_reg.h>
#include <mach/serial.h>

#include <asm/mach-types.h>

extern u32 davinci_machine_no;

static u8 first_time = 1;
static u32 *uart;

static u32 *get_uart_base(void)
{
	/* Add logic here for new platforms */
	return (u32 *)DAVINCI_UART0_BASE;
}

/* PORT_16C550A, in polled non-fifo mode */

static void putc(char c)
{
	if (first_time) {
		uart = get_uart_base();
		first_time = 0;
	}

	while (!(uart[UART_LSR] & UART_LSR_THRE))
		barrier();
	uart[UART_TX] = c;
}

static inline void flush(void)
{
	if (first_time) {
		uart = get_uart_base();
		first_time = 0;
	}

	while (!(uart[UART_LSR] & UART_LSR_THRE))
		barrier();
}

#define arch_decomp_setup()
#define arch_decomp_wdog()
