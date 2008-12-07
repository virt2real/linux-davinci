/*
 * TI DaVinci EVM board support
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/hardware.h>
#include <mach/psc.h>
#include <mach/common.h>
#include <mach/board.h>
#include <mach/emac.h>
#include <mach/i2c.h>
#include <mach/serial.h>


static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
};

static struct i2c_board_info dm355evm_i2c_info[] = {
	{ I2C_BOARD_INFO("dm355evm_msp", 0x25), /* plus irq */ },
	/* { I2C_BOARD_INFO("tlv320aic3x", 0x1b), }, */
	/* { I2C_BOARD_INFO("tvp5146", 0x5d), }, */
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);

	gpio_request(5, "dm355evm_msp");
	gpio_direction_input(5);
	dm355evm_i2c_info[0].irq = gpio_to_irq(5);

	i2c_register_board_info(1, dm355evm_i2c_info,
			ARRAY_SIZE(dm355evm_i2c_info));
}

static struct resource dm355evm_dm9000_rsrc[] = {
	{
		/* addr */
		.start	= 0x04014000,
		.end	= 0x04014001,
		.flags	= IORESOURCE_MEM,
	}, {
		/* data */
		.start	= 0x04014002,
		.end	= 0x04014003,
		.flags	= IORESOURCE_MEM,
	}, {
		.flags	= IORESOURCE_IRQ
			| IORESOURCE_IRQ_HIGHEDGE /* rising (active high) */,
	},
};

static struct platform_device dm355evm_dm9000 = {
	.name		= "dm9000",
	.id		= -1,
	.resource	= dm355evm_dm9000_rsrc,
	.num_resources	= ARRAY_SIZE(dm355evm_dm9000_rsrc),
};

static struct platform_device *davinci_evm_devices[] __initdata = {
	&dm355evm_dm9000,
};

static struct davinci_uart_config davinci_evm_uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static struct davinci_board_config_kernel davinci_evm_config[] __initdata = {
	{ DAVINCI_TAG_UART,	&davinci_evm_uart_config },
};

static void __init dm355_evm_map_io(void)
{
	davinci_map_common_io();
}

static __init void dm355_evm_init(void)
{
	davinci_psc_init();

	gpio_request(1, "dm9000");
	gpio_direction_input(1);
	dm355evm_dm9000_rsrc[2].start = gpio_to_irq(1);

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	evm_init_i2c();
	davinci_board_config = davinci_evm_config;
	davinci_board_config_size = ARRAY_SIZE(davinci_evm_config);
	davinci_serial_init();

}

static __init void dm355_evm_irq_init(void)
{
	davinci_init_common_hw();
	davinci_irq_init();
}

MACHINE_START(DAVINCI_DM355_EVM, "DaVinci DM355 EVM")
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (0x80000100),
	.map_io	      = dm355_evm_map_io,
	.init_irq     = dm355_evm_irq_init,
	.timer	      = &davinci_timer,
	.init_machine = dm355_evm_init,
MACHINE_END
