/*
 * TI DaVinci DM646X EVM board
 *
 * Derived from: arch/arm/mach-davinci/board-evm.c
 * Copyright (C) 2006 Texas Instruments.
 *
 * (C) 2007-2008, MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

/**************************************************************************
 * Included Files
 **************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/dma-mapping.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include <asm/setup.h>
#include <linux/io.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/psc.h>
#include <mach/serial.h>
#include <mach/i2c.h>

static struct davinci_uart_config davinci_evm_uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static struct davinci_board_config_kernel davinci_evm_config[] __initdata = {
	{ DAVINCI_TAG_UART,     &davinci_evm_uart_config },
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq       = 100 /* kHz */,
	.bus_delay      = 0 /* usec */,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
}

#define UART_DM646X_SCR         (DAVINCI_UART0_BASE + 0x40)
/*
 * Internal UARTs need to be initialized for the 8250 autoconfig to work
 * properly. Note that the TX watermark initialization may not be needed
 * once the 8250.c watermark handling code is merged.
 */
static int __init dm646x_serial_reset(void)
{
	davinci_writel(0x08, UART_DM646X_SCR);

	return 0;
}
late_initcall(dm646x_serial_reset);

static void board_init(void)
{
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DM646X_LPSC_AEMIF, 1);
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DM646X_LPSC_GPIO, 1);
}

static void __init davinci_map_io(void)
{
	davinci_map_common_io();

	/* Initialize the DaVinci EVM board settigs */
	board_init();
}

static __init void evm_init(void)
{
	evm_init_i2c();
	davinci_board_config = davinci_evm_config;
	davinci_board_config_size = ARRAY_SIZE(davinci_evm_config);
	davinci_serial_init();
}

static __init void davinci_dm646x_evm_irq_init(void)
{
	davinci_init_common_hw();
	davinci_irq_init();
}

MACHINE_START(DAVINCI_DM6467_EVM, "DaVinci DM646x EVM")
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (0x80000100),
	.map_io       = davinci_map_io,
	.init_irq     = davinci_dm646x_evm_irq_init,
	.timer        = &davinci_timer,
	.init_machine = evm_init,
MACHINE_END

