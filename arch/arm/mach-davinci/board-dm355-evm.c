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
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/onenand_regs.h>
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
#include <mach/emac.h>
#include <mach/i2c.h>
#include <mach/serial.h>
#include <mach/mmc.h>

#define DAVINCI_ASYNC_EMIF_CONTROL_BASE		0x01e10000
#define DAVINCI_ASYNC_EMIF_DATA_CE0_BASE	0x02000000

struct mtd_partition davinci_nand_partitions[] = {
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= 0x3c0000,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	}, {
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_256K,
		.mask_flags	= 0,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
		.mask_flags	= 0,
	}, {
		.name		= "filesystem1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_512M,
		.mask_flags	= 0,
	}, {
		.name		= "filesystem2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
};

static struct flash_platform_data davinci_nand_data = {
	.parts			= davinci_nand_partitions,
	.nr_parts		= ARRAY_SIZE(davinci_nand_partitions),
};

static struct resource davinci_nand_resources[] = {
	{
		.start		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE,
		.end		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE + SZ_32M - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= DAVINCI_ASYNC_EMIF_CONTROL_BASE,
		.end		= DAVINCI_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device davinci_nand_device = {
	.name			= "davinci_nand",
	.id			= -1,

	.num_resources		= ARRAY_SIZE(davinci_nand_resources),
	.resource		= davinci_nand_resources,

	.dev			= {
		.platform_data	= &davinci_nand_data,
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
};

static int dm355evm_mmc_gpios = -EINVAL;

static void dm355evm_mmcsd_gpios(unsigned gpio)
{
	gpio_request(gpio + 0, "mmc0_ro");
	gpio_request(gpio + 1, "mmc0_cd");
	gpio_request(gpio + 2, "mmc1_ro");
	gpio_request(gpio + 3, "mmc1_cd");

	/* we "know" these are input-only so we don't
	 * need to call gpio_direction_input()
	 */

	dm355evm_mmc_gpios = gpio;
}

static struct i2c_board_info dm355evm_i2c_info[] = {
	{ I2C_BOARD_INFO("dm355evm_msp", 0x25),
		.platform_data = dm355evm_mmcsd_gpios,
		/* plus irq */ },
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
	&davinci_nand_device,
};

static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static void __init dm355_evm_map_io(void)
{
	davinci_map_common_io();
}

static int dm355evm_mmc_get_cd(int module)
{
	if (!gpio_is_valid(dm355evm_mmc_gpios))
		return -ENXIO;
	/* low == card present */
	return !gpio_get_value_cansleep(dm355evm_mmc_gpios + 2 * module + 1);
}

static int dm355evm_mmc_get_ro(int module)
{
	if (!gpio_is_valid(dm355evm_mmc_gpios))
		return -ENXIO;
	/* high == card's write protect switch active */
	return gpio_get_value_cansleep(dm355evm_mmc_gpios + 2 * module + 0);
}

static struct davinci_mmc_config dm355evm_mmc_config = {
	.get_cd		= dm355evm_mmc_get_cd,
	.get_ro		= dm355evm_mmc_get_ro,
	.wires		= 4,
};

/* Don't connect anything to J10 unless you're only using USB host
 * mode *and* have to do so with some kind of gender-bender.  If
 * you have proper Mini-B or Mini-A cables (or Mini-A adapters)
 * the ID pin won't need any help.
 */
#ifdef CONFIG_USB_MUSB_PERIPHERAL
#define USB_ID_VALUE	0	/* ID pulled high; *should* float */
#else
#define USB_ID_VALUE	1	/* ID pulled low */
#endif

static __init void dm355_evm_init(void)
{
	davinci_psc_init();

	gpio_request(1, "dm9000");
	gpio_direction_input(1);
	dm355evm_dm9000_rsrc[2].start = gpio_to_irq(1);

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	evm_init_i2c();
	davinci_serial_init(&uart_config);

	gpio_request(2, "usb_id_toggle");
	gpio_direction_output(2, USB_ID_VALUE);
	/* irlml6401 switches over 1A in under 8 msec */
	setup_usb(500, 8);

	davinci_setup_mmc(0, &dm355evm_mmc_config);
	davinci_setup_mmc(1, &dm355evm_mmc_config);
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
