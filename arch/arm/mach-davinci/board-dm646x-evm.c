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
#include <mach/dm646x.h>
#include <mach/common.h>
#include <mach/psc.h>
#include <mach/serial.h>
#include <mach/i2c.h>
#include <mach/mmc.h>

#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/etherdevice.h>
#include <mach/emac.h>

#define DM646X_EVM_PHY_MASK		(0x2)
#define DM646X_EVM_MDIO_FREQUENCY	(2200000) /* PHY bus frequency */

static struct emac_platform_data dm646x_evm_emac_pdata = {
	.phy_mask	= DM646X_EVM_PHY_MASK,
	.mdio_max_freq	= DM646X_EVM_MDIO_FREQUENCY,
};

static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

/* Most of this EEPROM is unused, but U-Boot uses some data:
 *  - 0x7f00, 6 bytes Ethernet Address
 *  - ... newer boards may have more
 */
static struct at24_iface *at24_if;

static int at24_setup(struct at24_iface *iface, void *context)
{
	DECLARE_MAC_BUF(mac_str);
	char mac_addr[6];

	at24_if = iface;

	/* Read MAC addr from EEPROM */
	if (at24_if->read(at24_if, mac_addr, 0x7f00, 6) == 6) {
		printk(KERN_INFO "Read MAC addr from EEPROM: %s\n",
		print_mac(mac_str, mac_addr));

		memcpy(dm646x_evm_emac_pdata.mac_addr, mac_addr, 6);
	}
	return 0;
}
static struct at24_platform_data eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = at24_setup,
};

int dm646xevm_eeprom_read(void *buf, off_t off, size_t count)
{
	if (at24_if)
		return at24_if->read(at24_if, buf, off, count);
	return -ENODEV;
}
EXPORT_SYMBOL(dm646xevm_eeprom_read);

int dm646xevm_eeprom_write(void *buf, off_t off, size_t count)
{
	if (at24_if)
		return at24_if->write(at24_if, buf, off, count);
	return -ENODEV;
}
EXPORT_SYMBOL(dm646xevm_eeprom_write);

static struct i2c_board_info __initdata i2c_info[] =  {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data  = &eeprom_info,
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq       = 100 /* kHz */,
	.bus_delay      = 0 /* usec */,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static void __init davinci_map_io(void)
{
	davinci_map_common_io();
	dm646x_init();
}

static __init void evm_init(void)
{
	evm_init_i2c();
	davinci_serial_init(&uart_config);
	davinci_init_emac(&dm646x_evm_emac_pdata);
}

static __init void davinci_dm646x_evm_irq_init(void)
{
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

