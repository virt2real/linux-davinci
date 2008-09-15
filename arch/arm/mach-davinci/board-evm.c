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
#include <linux/gpio.h>
#include <linux/i2c/pcf857x.h>
#include <linux/i2c/at24.h>
#include <linux/leds.h>
#include <linux/etherdevice.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/common.h>
#include <mach/board.h>
#include <mach/emac.h>
#include <mach/i2c.h>

/* other misc. init functions */
void __init davinci_psc_init(void);
void __init davinci_irq_init(void);
void __init davinci_map_common_io(void);
void __init davinci_init_common_hw(void);

#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)

static struct mtd_partition davinci_evm_norflash_partitions[] = {
	/* bootloader (U-Boot, etc) in first 4 sectors */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= 4 * SZ_64K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next 1 sectors */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_64K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data davinci_evm_norflash_data = {
	.width		= 2,
	.parts		= davinci_evm_norflash_partitions,
	.nr_parts	= ARRAY_SIZE(davinci_evm_norflash_partitions),
};

/* NOTE: CFI probe will correctly detect flash part as 32M, but EMIF
 * limits addresses to 16M, so using addresses past 16M will wrap */
static struct resource davinci_evm_norflash_resource = {
	.start		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE,
	.end		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE + SZ_16M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device davinci_evm_norflash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &davinci_evm_norflash_data,
	},
	.num_resources	= 1,
	.resource	= &davinci_evm_norflash_resource,
};

#endif

#if defined(CONFIG_MTD_NAND_DAVINCI) || defined(CONFIG_MTD_NAND_DAVINCI_MODULE)
struct mtd_partition davinci_evm_nandflash_partition[] = {
	/* 5 MB space at the beginning for bootloader and kernel */
	{
		.name		= "NAND filesystem",
		.offset		= 5 * SZ_1M,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
};

static struct flash_platform_data davinci_evm_nandflash_data = {
	.parts		= davinci_evm_nandflash_partition,
	.nr_parts	= ARRAY_SIZE(davinci_evm_nandflash_partition),
};

static struct resource davinci_evm_nandflash_resource = {
	.start		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE,
	.end		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE + SZ_16M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device davinci_evm_nandflash_device = {
	.name		= "davinci_nand",
	.id		= 0,
	.dev		= {
		.platform_data	= &davinci_evm_nandflash_data,
	},
	.num_resources	= 1,
	.resource	= &davinci_evm_nandflash_resource,
};
#endif

#if defined(CONFIG_FB_DAVINCI) || defined(CONFIG_FB_DAVINCI_MODULE)

static u64 davinci_fb_dma_mask = DMA_32BIT_MASK;

static struct platform_device davinci_fb_device = {
	.name		= "davincifb",
	.id		= -1,
	.dev = {
		.dma_mask		= &davinci_fb_dma_mask,
		.coherent_dma_mask      = DMA_32BIT_MASK,
	},
	.num_resources = 0,
};
#endif

static struct platform_device rtc_dev = {
	.name           = "rtc_davinci_evm",
	.id             = -1,
};

#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)

static struct resource ide_resources[] = {
	{
		.start          = DAVINCI_CFC_ATA_BASE,
		.end            = DAVINCI_CFC_ATA_BASE + 0x7ff,
		.flags          = IORESOURCE_MEM,
	},
	{
		.start          = IRQ_IDE,
		.end            = IRQ_IDE,
		.flags          = IORESOURCE_IRQ,
	},
};

static u64 ide_dma_mask = DMA_32BIT_MASK;

static struct platform_device ide_dev = {
	.name           = "palm_bk3710",
	.id             = -1,
	.resource       = ide_resources,
	.num_resources  = ARRAY_SIZE(ide_resources),
	.dev = {
		.dma_mask		= &ide_dma_mask,
		.coherent_dma_mask      = DMA_32BIT_MASK,
	},
};

#endif

/*----------------------------------------------------------------------*/

/*
 * I2C GPIO expanders
 */

#define PCF_Uxx_BASE(x)	(DAVINCI_N_GPIO + ((x) * 8))


/* U2 -- LEDs */

static struct gpio_led evm_leds[] = {
	{ .name = "DS8", .active_low = 1,
		.default_trigger = "heartbeat", },
	{ .name = "DS7", .active_low = 1, },
	{ .name = "DS6", .active_low = 1, },
	{ .name = "DS5", .active_low = 1, },
	{ .name = "DS4", .active_low = 1, },
	{ .name = "DS3", .active_low = 1, },
	{ .name = "DS2", .active_low = 1,
		.default_trigger = "mmc0", },
	{ .name = "DS1", .active_low = 1,
		.default_trigger = "ide-disk", },
};

static const struct gpio_led_platform_data evm_led_data = {
	.num_leds	= ARRAY_SIZE(evm_leds),
	.leds		= evm_leds,
};

static struct platform_device *evm_led_dev;

static int
evm_led_setup(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	struct gpio_led *leds = evm_leds;
	int status;

	while (ngpio--) {
		leds->gpio = gpio++;
		leds++;
	}

	/* what an extremely annoying way to be forced to handle
	 * device unregistration ...
	 */
	evm_led_dev = platform_device_alloc("leds-gpio", 0);
	platform_device_add_data(evm_led_dev,
			&evm_led_data, sizeof evm_led_data);

	evm_led_dev->dev.parent = &client->dev;
	status = platform_device_add(evm_led_dev);
	if (status < 0) {
		platform_device_put(evm_led_dev);
		evm_led_dev = NULL;
	}
	return status;
}

static int
evm_led_teardown(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	if (evm_led_dev) {
		platform_device_unregister(evm_led_dev);
		evm_led_dev = NULL;
	}
	return 0;
}

static struct pcf857x_platform_data pcf_data_u2 = {
	.gpio_base	= PCF_Uxx_BASE(0),
	.setup		= evm_led_setup,
	.teardown	= evm_led_teardown,
};


/* U18 - A/V clock generator and user switch */

static int sw_gpio;

static ssize_t
sw_show(struct device *d, struct device_attribute *a, char *buf)
{
	char *s = gpio_get_value_cansleep(sw_gpio) ? "on\n" : "off\n";

	strcpy(buf, s);
	return strlen(s);
}

static DEVICE_ATTR(user_sw, S_IRUGO, sw_show, NULL);

static int
evm_u18_setup(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	int	status;

	/* export dip switch option */
	sw_gpio = gpio + 7;
	status = gpio_request(sw_gpio, "user_sw");
	if (status == 0)
		status = gpio_direction_input(sw_gpio);
	if (status == 0)
		status = device_create_file(&client->dev, &dev_attr_user_sw);
	else
		gpio_free(sw_gpio);
	if (status != 0)
		sw_gpio = -EINVAL;

	/* audio PLL:  48 kHz (vs 44.1 or 32), single rate (vs double) */
	gpio_request(gpio + 3, "pll_fs2");
	gpio_direction_output(gpio + 3, 0);

	gpio_request(gpio + 2, "pll_fs1");
	gpio_direction_output(gpio + 2, 0);

	gpio_request(gpio + 1, "pll_sr");
	gpio_direction_output(gpio + 1, 0);

	return 0;
}

static int
evm_u18_teardown(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	gpio_free(gpio + 1);
	gpio_free(gpio + 2);
	gpio_free(gpio + 3);

	if (sw_gpio > 0) {
		device_remove_file(&client->dev, &dev_attr_user_sw);
		gpio_free(sw_gpio);
	}
	return 0;
}

static struct pcf857x_platform_data pcf_data_u18 = {
	.gpio_base	= PCF_Uxx_BASE(1),
	.n_latch	= (1 << 3) | (1 << 2) | (1 << 1),
	.setup		= evm_u18_setup,
	.teardown	= evm_u18_teardown,
};


/* U35 - various I/O signals used to manage USB, CF, ATA, etc */

static int
evm_u35_setup(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	/* REVISIT -- get polarity of these right; active low == 'n' prefix */

	/* p0 = nDRV_VBUS (initial:  don't supply it) */
	gpio_request(gpio + 0, "nDRV_VBUS");
	gpio_direction_output(gpio + 0, 1);

	/* p1 = VDDIMX_EN */
	gpio_request(gpio + 1, "VDDIMX_EN");
	gpio_direction_output(gpio + 1, 1);

	/* p2 = VLYNQ_EN */
	gpio_request(gpio + 2, "VLYNQ_EN");
	gpio_direction_output(gpio + 2, 1);

	/* p3 = n3V3_CF_RESET (initial: stay in reset) */
	gpio_request(gpio + 3, "nCF_RESET");
	gpio_direction_output(gpio + 3, 0);

	/* (p4 unused) */

	/* p5 = 1V8_WLAN_RESET (initial: stay in reset) */
	gpio_request(gpio + 5, "WLAN_RESET");
	gpio_direction_output(gpio + 5, 1);

	/* p6 = nATA_SEL (initial: select) */
	gpio_request(gpio + 6, "nATA_SEL");
	gpio_direction_output(gpio + 6, 0);

	/* p7 = nCF_SEL (initial: deselect) */
	gpio_request(gpio + 7, "nCF_SEL");
	gpio_direction_output(gpio + 7, 1);

	return 0;
}

static int
evm_u35_teardown(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	gpio_free(gpio + 7);
	gpio_free(gpio + 6);
	gpio_free(gpio + 5);
	gpio_free(gpio + 3);
	gpio_free(gpio + 2);
	gpio_free(gpio + 1);
	gpio_free(gpio + 0);
	return 0;
}

static struct pcf857x_platform_data pcf_data_u35 = {
	.gpio_base	= PCF_Uxx_BASE(2),
	.setup		= evm_u35_setup,
	.teardown	= evm_u35_teardown,
};

/*----------------------------------------------------------------------*/

/* Most of this EEPROM is unused, but U-Boot uses some data:
 *  - 0x7f00, 6 bytes Ethernet Address
 *  - 0x0039, 1 byte NTSC vs PAL (bit 0x80 == PAL)
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

		davinci_init_emac(mac_addr);
	}

	return 0;
}

static struct at24_platform_data eeprom_info = {
	.byte_len	= (256*1024) / 8,
	.page_size	= 64,
	.flags		= AT24_FLAG_ADDR16,
	.setup          = at24_setup,
};

int dm6446evm_eeprom_read(void *buf, off_t off, size_t count)
{
	if (at24_if)
		return at24_if->read(at24_if, buf, off, count);
	return -ENODEV;
}
EXPORT_SYMBOL(dm6446evm_eeprom_read);

int dm6446evm_eeprom_write(void *buf, off_t off, size_t count)
{
	if (at24_if)
		return at24_if->write(at24_if, buf, off, count);
	return -ENODEV;
}
EXPORT_SYMBOL(dm6446evm_eeprom_write);

static struct i2c_board_info __initdata i2c_info[] =  {
	{
		I2C_BOARD_INFO("pcf8574", 0x38),
		.platform_data	= &pcf_data_u2,
	},
	{
		I2C_BOARD_INFO("pcf8574", 0x39),
		.platform_data	= &pcf_data_u18,
	},
	{
		I2C_BOARD_INFO("pcf8574", 0x3a),
		.platform_data	= &pcf_data_u35,
	},
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data	= &eeprom_info,
	},
	/* ALSO:
	 * - tvl320aic33 audio codec (0x1b)
	 * - msp430 microcontroller (0x23)
	 * - tvp5146 video decoder (0x5d)
	 */
};

/* The msp430 uses a slow bitbanged I2C implementation (ergo 20 KHz),
 * which requires 100 usec of idle bus after i2c writes sent to it.
 */
static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 20 /* kHz */,
	.bus_delay	= 100 /* usec */,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static struct platform_device *davinci_evm_devices[] __initdata = {
#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)
	&davinci_evm_norflash_device,
#endif
#if defined(CONFIG_MTD_NAND_DAVINCI) || defined(CONFIG_MTD_NAND_DAVINCI_MODULE)
	&davinci_evm_nandflash_device,
#endif
#if defined(CONFIG_FB_DAVINCI) || defined(CONFIG_FB_DAVINCI_MODULE)
	&davinci_fb_device,
#endif
	&rtc_dev,
#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)
	&ide_dev,
#endif
};

static struct davinci_uart_config davinci_evm_uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static struct davinci_board_config_kernel davinci_evm_config[] __initdata = {
	{ DAVINCI_TAG_UART,	&davinci_evm_uart_config },
};

static void __init
davinci_evm_map_io(void)
{
	davinci_map_common_io();
}

static __init void davinci_evm_init(void)
{
	davinci_psc_init();

#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)
#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)
	printk(KERN_WARNING "WARNING: both IDE and NOR flash are enabled, "
	       "but share pins.\n\t Disable IDE for NOR support.\n");
#endif
#endif

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	evm_init_i2c();
	davinci_board_config = davinci_evm_config;
	davinci_board_config_size = ARRAY_SIZE(davinci_evm_config);
	davinci_serial_init();

	/* irlml6401 sustains over 3A, switches 5V in under 8 msec */
	setup_usb(500, 8);
}

static __init void davinci_evm_irq_init(void)
{
	davinci_init_common_hw();
	davinci_irq_init();
}

MACHINE_START(DAVINCI_EVM, "DaVinci EVM")
	/* Maintainer: MontaVista Software <source@mvista.com> */
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (DAVINCI_DDR_BASE + 0x100),
	.map_io	      = davinci_evm_map_io,
	.init_irq     = davinci_evm_irq_init,
	.timer	      = &davinci_timer,
	.init_machine = davinci_evm_init,
MACHINE_END
