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
#include <linux/leds.h>

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

/* other misc. init functions */
void __init davinci_psc_init(void);
void __init davinci_irq_init(void);
void __init davinci_map_common_io(void);
void __init davinci_init_common_hw(void);

/* NOR Flash base address set to CS0 by default */
#define NOR_FLASH_PHYS 0x02000000

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
	.start		= NOR_FLASH_PHYS,
	.end		= NOR_FLASH_PHYS + SZ_16M - 1,
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
	.end		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE + SZ_16K - 1,
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

/*
 * USB
 */
#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)

#include <linux/usb/musb.h>

static struct musb_hdrc_platform_data usb_data = {
#if     defined(CONFIG_USB_MUSB_OTG)
	/* OTG requires a Mini-AB connector */
	.mode           = MUSB_OTG,
#elif   defined(CONFIG_USB_MUSB_PERIPHERAL)
	.mode           = MUSB_PERIPHERAL,
#elif   defined(CONFIG_USB_MUSB_HOST)
	.mode           = MUSB_HOST,
#endif
	/* irlml6401 switches 5V */
	.power          = 250,          /* sustains 3.0+ Amps (!) */
	.potpgt         = 4,            /* ~8 msec */

	/* REVISIT multipoint is a _chip_ capability; not board specific */
	.multipoint     = 1,
};

static struct resource usb_resources [] = {
	{
		/* physical address */
		.start          = DAVINCI_USB_OTG_BASE,
		.end            = DAVINCI_USB_OTG_BASE + 0x5ff,
		.flags          = IORESOURCE_MEM,
	},
	{
		.start          = IRQ_USBINT,
		.flags          = IORESOURCE_IRQ,
	},
};

static u64 usb_dmamask = DMA_32BIT_MASK;

static struct platform_device usb_dev = {
	.name           = "musb_hdrc",
	.id             = -1,
	.dev = {
		.platform_data		= &usb_data,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask      = DMA_32BIT_MASK,
        },
	.resource       = usb_resources,
	.num_resources  = ARRAY_SIZE(usb_resources),
};

#define setup_usb(void)	do {} while(0)
#endif  /* CONFIG_USB_MUSB_HDRC */

static struct platform_device rtc_dev = {
	.name           = "rtc_davinci_evm",
	.id             = -1,
};

static struct resource ide_resources[] = {
	{
		.start          = IO_ADDRESS(DAVINCI_CFC_ATA_BASE),
		.end            = IO_ADDRESS(DAVINCI_CFC_ATA_BASE) + SZ_4K,
		.flags          = IORESOURCE_MEM,
	},
	{
		.start          = IRQ_IDE,
		.flags          = IORESOURCE_IRQ,
	},
};

static struct platform_device ide_dev = {
	.name           = "palm_bk3710",
	.id             = -1,
	.resource       = ide_resources,
	.num_resources  = ARRAY_SIZE(ide_resources),
};

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

#if 0
static struct pcf857x_platform_data pcf_data_u35 = {
	.gpio_base = PCF_Uxx_BASE(2),
};
#endif

/*----------------------------------------------------------------------*/

static struct i2c_board_info __initdata i2c_info[] =  {
	{
		I2C_BOARD_INFO("pcf857x", 0x38),
		.type		= "pcf8574",
		.platform_data	= &pcf_data_u2,
	},
	{
		I2C_BOARD_INFO("pcf857x", 0x39),
		.type		= "pcf8574",
		.platform_data	= &pcf_data_u18,
	},
#if 0
/* don't clash with mach-davinci/i2c-client.c
 * or drivers/i2c/chips/gpio_expander_davinci.c
 * ... eventually both should vanish
 */
	{
		I2C_BOARD_INFO("pcf857x", 0x3a),
		.type		= "pcf8574a",
		.platform_data	= &pcf_data_u35,
	},
#endif
	/* ALSO:
	 * - tvl320aic33 audio codec (0x1b)
	 * - msp430 microcontroller (0x23)
	 * - 24wc256 eeprom (0x50)
	 * - tvp5146 video decoder (0x5d)
	 */
};

static struct platform_device *davinci_evm_devices[] __initdata = {
	&davinci_evm_norflash_device,
#if defined(CONFIG_MTD_NAND_DAVINCI) || defined(CONFIG_MTD_NAND_DAVINCI_MODULE)
	&davinci_evm_nandflash_device,
#endif
#if defined(CONFIG_FB_DAVINCI) || defined(CONFIG_FB_DAVINCI_MODULE)
	&davinci_fb_device,
#endif
#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
	&usb_dev,
#endif
	&rtc_dev,
	&ide_dev,
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
	printk(KERN_WARNING "WARNING: both IDE and NOR flash are enabled, "
	       "but share pins.\n\t Disable IDE for NOR support.\n");
#endif

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
	davinci_board_config = davinci_evm_config;
	davinci_board_config_size = ARRAY_SIZE(davinci_evm_config);
	davinci_serial_init();
	setup_usb();
}

static __init void davinci_evm_irq_init(void)
{
	davinci_init_common_hw();
	davinci_irq_init();
}

MACHINE_START(DAVINCI_EVM, "DaVinci EVM")
	/* Maintainer: MontaVista Software <source@mvista.com> */
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (io_p2v(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (DAVINCI_DDR_BASE + 0x100),
	.map_io	      = davinci_evm_map_io,
	.init_irq     = davinci_evm_irq_init,
	.timer	      = &davinci_timer,
	.init_machine = davinci_evm_init,
MACHINE_END
