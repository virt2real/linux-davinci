/*
 * TI DaVinci DM365 EVM board support
 *
 * Copyright (C) 2009 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/i2c/at24.h>
#include <linux/leds.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/mtd/nand.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>


#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/mux.h>
#include <mach/common.h>
#include <linux/platform_data/i2c-davinci.h>
#include <mach/serial.h>
#include <linux/platform_data/mmc-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/keyscan-davinci.h>
#include <linux/platform_data/usb-davinci.h>
#include <mach/gpio.h>

#include <linux/w1-gpio.h>
#include "davinci.h"
#include "dm365_spi.h"

#include "board-virt2real-dm365.h"
#ifdef CONFIG_V2R_PARSE_CMDLINE
static struct i2c_board_info i2c_info[] = {

};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 100	/* kHz */, //was 400
	.bus_delay	= 0	/* usec */,
	.sda_pin        = 21,
	.scl_pin        = 20,
};

/* Input available at the ov7690 */
//Shadrin camera
static struct v4l2_input ov2643_inputs[] = {
	{
		.index = 0,
		.name = "Camera",
		.type = V4L2_INPUT_TYPE_CAMERA,
	}
};


static struct vpfe_subdev_info vpfe_sub_devs[] = {
	{
		//Clock for camera????
		.module_name = "ov2643",
		.is_camera = 1,
		.grp_id = VPFE_SUBDEV_OV2643,
		.num_inputs = ARRAY_SIZE(ov2643_inputs),
		.inputs = ov2643_inputs,
		.ccdc_if_params = {
#ifdef CONFIG_VIDEO_YCBCR
			.if_type = VPFE_YCBCR_SYNC_8,
#else
			.if_type = VPFE_RAW_BAYER,
#endif
			.hdpol = VPFE_PINPOL_POSITIVE,
			.vdpol = VPFE_PINPOL_POSITIVE,
		},
		.board_info = {
			I2C_BOARD_INFO("ov2643", 0x30),
			/* this is for PCLK rising edge */
			.platform_data = (void *)1,
		},
	}
};

static struct vpfe_config vpfe_cfg = {
       .num_subdevs = ARRAY_SIZE(vpfe_sub_devs),
       .sub_devs = vpfe_sub_devs,
       .card_name = "DM365 Leopard",
       .ccdc = "DM365 ISIF",
       .num_clocks = 1,
       .clocks = {"vpss_master"},
};
static void __init v2r_parse_cmdline(char * string);
#endif

static void w1_enable_external_pullup(int enable);

static inline int have_imager(void)
{
#if defined(CONFIG_SOC_CAMERA_OV2643) || \
	defined(CONFIG_SOC_CAMERA_OV2643_MODULE)
	return 1;
#else
	return 0;
#endif
}

static void dm365_camera_configure(void){
	davinci_cfg_reg(DM365_CAM_OFF);
	gpio_request(98, "CAMERA_OFF");
	gpio_direction_output(98, 0);
	davinci_cfg_reg(DM365_CAM_RESET);
	gpio_request(99, "CAMERA_RESET");
	gpio_direction_output(99, 1);
	davinci_cfg_reg(DM365_GPIO37);//Disable clk at gpio37
	davinci_cfg_reg(DM365_EXTCLK);
}




//#define DM365_EVM_PHY_ID		"davinci_mdio-0:01"  // replaced by Gol
#define DM365_EVM_PHY_ID		"davinci_mdio-0:00"   

/* NOTE:  this is geared for the standard config, with a socketed
 * 2 GByte Micron NAND (MT29F16G08FAA) using 128KB sectors.  If you
 * swap chips with a different block size, partitioning will
 * need to be changed. This NAND chip MT29F16G08FAA is the default
 * NAND shipped with the Spectrum Digital DM365 EVM
 */
/*define NAND_BLOCK_SIZE		SZ_128K*/

/* For Samsung 4K NAND (K9KAG08U0M) with 256K sectors */
/*#define NAND_BLOCK_SIZE		SZ_256K*/

/* For Micron 4K NAND with 512K sectors */
#define NAND_BLOCK_SIZE		SZ_512K

#define DM365_ASYNC_EMIF_CONTROL_BASE	0x01d10000

static struct mtd_partition davinci_nand_partitions[] = {
	{
		/* UBL (a few copies) plus U-Boot */
		.name		= "bootloader",
		.offset		= 0,
		.size		= 30 * NAND_BLOCK_SIZE,
		.mask_flags	= 0, // MTD_WRITEABLE, /* force read-only */
	}, {
		/* U-Boot environment */
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * NAND_BLOCK_SIZE,
		.mask_flags	= 0, // MTD_WRITEABLE, /* force read-only */
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
		.mask_flags	= 0, // MTD_WRITEABLE, /* force read-only */
	}, {
		.name		= "fs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0, // MTD_WRITEABLE, /* force read-only */
	}
	/* two blocks with bad block table (and mirror) at the end */
};

static struct davinci_nand_pdata davinci_nand_data = {
	.mask_chipsel		= 0,
	.parts			= davinci_nand_partitions,
	.nr_parts		= ARRAY_SIZE(davinci_nand_partitions),
	.ecc_mode		= NAND_ECC_HW,
	.bbt_options		= NAND_BBT_USE_FLASH,
	.ecc_bits		= 4,
};

#define DM365_ASYNC_EMIF_DATA_CE0_BASE	0x02000000
#define DM365_ASYNC_EMIF_DATA_CE1_BASE	0x04000000

static struct resource davinci_nand_resources[] = {
	{
		.start		= DM365_ASYNC_EMIF_DATA_CE0_BASE,
		.end		= DM365_ASYNC_EMIF_DATA_CE0_BASE + SZ_32M - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= DM365_ASYNC_EMIF_CONTROL_BASE,
		.end		= DM365_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device davinci_nand_device = {
	.name			= "davinci_nand",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(davinci_nand_resources),
	.resource		= davinci_nand_resources,
	.dev			= {
		.platform_data	= &davinci_nand_data,
	},
};


static struct snd_platform_data dm365_evm_snd_data = {
	.asp_chan_q = EVENTQ_3,
};



//SD card configuration functions
//May be used dynamically
static int mmc_get_cd(int module)
{
	//1 = card present
	//0 = card not present
	return 1;
}

static int mmc_get_ro(int module)
{
	//1 = device is read-only
	//0 = device is mot read only
	return 0;
}

static struct davinci_mmc_config dm365evm_mmc_config = {
	.get_cd		= mmc_get_cd,
	.get_ro		= mmc_get_ro,
	.wires		= 4,
	.max_freq	= 50000000,
	.caps		= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
};


static void dm365evm_mmc_configure(void)
{
	/*
	 * MMC/SD pins are multiplexed with GPIO and EMIF
	 * Further details are available at the DM365 ARM
	 * Subsystem Users Guide(sprufg5.pdf) pages 118, 128 - 131
	 */
	davinci_cfg_reg(DM365_SD1_CLK);
	davinci_cfg_reg(DM365_SD1_CMD);
	davinci_cfg_reg(DM365_SD1_DATA3);
	davinci_cfg_reg(DM365_SD1_DATA2);
	davinci_cfg_reg(DM365_SD1_DATA1);
	davinci_cfg_reg(DM365_SD1_DATA0);
}

//to support usb
__init static void dm365_usb_configure(void)
{
	davinci_cfg_reg(DM365_GPIO66);
	gpio_request(66, "usb");
	gpio_direction_output(66, 1);
	davinci_setup_usb(500, 8);
}


static void dm365_ks8851_init(void){
	gpio_request(0, "KSZ8851");
	gpio_direction_input(0);
	davinci_cfg_reg(DM365_EVT18_SPI3_TX);
	davinci_cfg_reg(DM365_EVT19_SPI3_RX);
}

static void __init v2r_init_i2c(void)
{
	davinci_cfg_reg(DM365_GPIO20);
	gpio_request(20, "i2c-scl");
	gpio_direction_output(20, 0);
	davinci_cfg_reg(DM365_I2C_SCL);
	davinci_init_i2c(&i2c_pdata);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static struct platform_device *dm365_evm_nand_devices[] __initdata = {
	&davinci_nand_device,
};


static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0) | (1 << 1),
};

static void __init dm365_evm_map_io(void)
{
	/* setup input configuration for VPFE input devices */
	dm365_set_vpfe_config(&vpfe_cfg);
	dm365_init();
}


static struct davinci_spi_config ksz8851_mcspi_config = {
		.io_type = SPI_IO_TYPE_DMA,
		.c2tdelay = 0,
		.t2cdelay = 0
};

static struct spi_board_info ksz8851_snl_info[] __initdata = {
	{
		.modalias	= "ks8851",
		.bus_num	= 3,
		.chip_select	= 0,
		.max_speed_hz	= 24000000,
		.controller_data = &ksz8851_mcspi_config,
		.irq		= IRQ_DM365_GPIO0
     }
};

static struct davinci_spi_unit_desc dm365_evm_spi_udesc_KSZ8851 = {
	.spi_hwunit	= 3,
	.chipsel	= BIT(0),
	.irq		= IRQ_DM365_SPIINT3_0,
	.dma_tx_chan	= 18,
	.dma_rx_chan	= 19,
	.dma_evtq	= EVENTQ_3,
	.pdata		= {
		.version 	= SPI_VERSION_1,
		.num_chipselect = 2,
		.intr_line = 0,
		.chip_sel = 0,
		.cshold_bug = 0,
		.dma_event_q	= EVENTQ_3,
	}
};

/* SPI0 init */

static struct davinci_spi_config v2rdac_config = {
	.io_type = SPI_IO_TYPE_DMA,
	.c2tdelay = 0,
	.t2cdelay = 0
};

static struct spi_board_info v2rdac_info[] __initdata = {
	{
		.modalias  = "spidev",
		.bus_num  = 0,
		.chip_select  = 1,
		.max_speed_hz  = 1000000,
		.controller_data = &v2rdac_config,
		.mode = SPI_MODE_1
	}
};


static struct davinci_spi_unit_desc v2rdac_spi_udesc = {
	.spi_hwunit  = 0,
	.chipsel  = BIT(1),
	.irq    = IRQ_DM365_SPIINT0_0,
	.dma_tx_chan  = 16,
	.dma_rx_chan  = 17,
	.dma_evtq  = EVENTQ_3,
	.pdata = {
		.version   = SPI_VERSION_1,
		.num_chipselect = 2,
		.intr_line = 0,
		.chip_sel = 0,
		.cshold_bug = 0,
		.dma_event_q  = EVENTQ_3,
	}
};

/* end SPI0 init */


/* 1-wire init block */

static struct w1_gpio_platform_data w1_gpio_pdata = {
	.pin		= 26,
	.is_open_drain	= 0,
	.enable_external_pullup	= w1_enable_external_pullup,
	.ext_pullup_enable_pin	= 27
};

static struct platform_device w1_device = {
	.name			= "w1-gpio",
	.id			= -1,
	.dev.platform_data	= &w1_gpio_pdata,
};

static void w1_enable_external_pullup(int enable) {
	gpio_set_value(w1_gpio_pdata.ext_pullup_enable_pin, enable);
}

static void w1_gpio_init(void) {
	int err;
	err = platform_device_register(&w1_device);
	if (err)
		printk(KERN_INFO "Failed to register w1-gpio\n");
	else
		printk(KERN_INFO "w1-gpio conected to GPIO22\n");
}


/* end 1-wire init block */


/* LED triggers block */

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)

static struct gpio_led v2r_led[] = {
    {
	.name = "v2r:red:user",
	.default_trigger = "none",
	.gpio = 74,
	.active_low = 0,
    },
    {
	.name = "v2r:green:user",
	.default_trigger = "none",
	.gpio = 73,
	.active_low = 0,
    },
};

static struct gpio_led_platform_data v2r_led_data = {
    .num_leds = ARRAY_SIZE(v2r_led),
    .leds = v2r_led
};

static struct platform_device v2r_led_dev = {
    .name = "leds-gpio",
    .id	 = -1,
    .dev = {
        .platform_data	= &v2r_led_data,
    },
};

static void led_init(void) {
	int err;
	err = platform_device_register(&v2r_led_dev);
	if (err)
		printk(KERN_INFO "Failed to register LED triggers\n");
	else
		printk(KERN_INFO "LED triggers init success\n");
}

#endif /* CONFIG_LEDS_GPIO */

/* end LED triggers block */





static __init void dm365_evm_init(void)
{
	//struct davinci_soc_info *soc_info = &davinci_soc_info;
	struct clk *aemif_clk;

	w1_run = 0;
	lan_run = 0;
	spi0_run = 0;
	led_run = 0;
	camera_run = 0;
	uart1_run = 0;

#ifdef CONFIG_V2R_PARSE_CMDLINE
	v2r_parse_cmdline(saved_command_line);
#endif

	aemif_clk = clk_get(NULL, "aemif");
	if (IS_ERR(aemif_clk))	return;
	clk_prepare_enable(aemif_clk);

	// try to init NAND
	platform_add_devices(dm365_evm_nand_devices, ARRAY_SIZE(dm365_evm_nand_devices));

	v2r_init_i2c();

	// try to init camera
	if (camera_run) dm365_camera_configure();

	// set up UART1 GPIO
	if (uart1_run) {
	    davinci_cfg_reg(DM365_UART1_RXD);
	    davinci_cfg_reg(DM365_UART1_TXD);
	}

	// try to init UARTs
	davinci_serial_init(&uart_config);

	//dm365evm_emac_configure();
	//soc_info->emac_pdata->phy_id = DM365_EVM_PHY_ID;

	dm365evm_mmc_configure();

	// try to init LED-triggers
	#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
	if (led_run) led_init();
	#endif

	// try to init MMC0 (microSD)
	davinci_setup_mmc(0, &dm365evm_mmc_config);

	// try to init VoiceCodec
	dm365_init_vc(&dm365_evm_snd_data);

	// try to init Real Time Clock
	dm365_init_rtc();

	// try to init USB
	dm365_usb_configure();

	// try to init LAN module
	if (lan_run) {
	    dm365_ks8851_init();
	    davinci_init_spi(&dm365_evm_spi_udesc_KSZ8851, ARRAY_SIZE(ksz8851_snl_info), ksz8851_snl_info);
	}

	// try to init 1-Wire
	if (w1_run) w1_gpio_init(); // run 1-wire master

	// try to init SPI0
	if (spi0_run) davinci_init_spi(&v2rdac_spi_udesc, ARRAY_SIZE(v2rdac_info),  v2rdac_info); // run SPI0 init

	return;
}


/* Virt2real board devices init functions */
#ifdef CONFIG_V2R_PARSE_CMDLINE
static void v2r_parse_cmdline(char * string)
{

    char *p;
    char *temp_string;
    char *temp_param;
    char *param_name;
    char *param_value;
    printk(KERN_INFO "Parse kernel cmdline:\n");
    temp_string = kstrdup(string, GFP_KERNEL);

    do
    {
	p = strsep(&temp_string, " ");
	if (p) {
	    // split param string into two parts
	    temp_param = kstrdup(p, GFP_KERNEL);
	    param_name = strsep(&temp_param, "=");
	    if (!param_name) continue;
	    //printk(KERN_INFO "%s\n", temp_value);
	    param_value = strsep(&temp_param, " ");
	    if (!param_value) continue;
	    //printk(KERN_INFO "%s\n", param_value);
	    //printk (KERN_INFO "param %s = %s\n", param_name, param_value);
	    
	    // i'd like to use switch, but fig tam
	    
	    if (!strcmp(param_name, "pwrled")) {
		if (!strcmp(param_value, "on")) {
		    // turn on blue led
		    u8 result = 0;
		    printk(KERN_INFO "Power LED set ON\n");
		    result = davinci_rtcss_read(0x00);
		    result |= (1<<3);
		    davinci_rtcss_write(result, 0x00);
		}
	    }

	    #if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
	    if (!strcmp(param_name, "redled")) {
		v2r_led[0].default_trigger = param_value;
		led_run = 1;
	    }

	    if (!strcmp(param_name, "greenled")) {
		v2r_led[1].default_trigger = param_value;
		led_run = 1;
	    }
	    #endif

	    if (!strcmp(param_name, "wifi")) {
		if (!strcmp(param_value, "on")) {
		    printk(KERN_INFO "Wi-Fi board enabled\n");
		    davinci_setup_mmc(1, &dm365evm_mmc_config);
		    //dm365_wifi_configure();
		}
	    }

	    if (!strcmp(param_name, "lan0")) {
		if (!strcmp(param_value, "on")) {
		    printk(KERN_INFO "LAN enabled\n");
		    lan_run = 1;
		}
	    }

	    if (!strcmp(param_name, "spi0")) {
		if (!strcmp(param_value, "on")) {
		    printk(KERN_INFO "SPI0 enabled\n");
		    spi0_run = 1;
		}
	    }

	    if (!strcmp(param_name, "uart1")) {
		if (!strcmp(param_value, "on")) {
		    printk(KERN_INFO "UART1 enabled\n");
		    uart1_run = 1;
		}
	    }


	    if (!strcmp(param_name, "1wire")) {
		int temp;
		kstrtoint(param_value, 10, &temp);
		w1_gpio_pdata.pin = temp;
		printk(KERN_INFO "Use 1-wire on GPIO%d\n", temp);
		w1_run = 1;
		
	    }

	    if (!strcmp(param_name, "1wirepullup")) {
		int temp;
		kstrtoint(param_value, 10, &temp);
		w1_gpio_pdata.ext_pullup_enable_pin = temp;
		printk(KERN_INFO "Use 1-wire pullup resistor on GPIO%d\n", temp);
	    }

	    if (!strcmp(param_name, "camera")) {
		if (!strcmp(param_value, "ov2643")) {
		    printk(KERN_INFO "Use camera OmniVision OV2643\n");
		    camera_run = 1;
		}
		if (!strcmp(param_value, "ov5642")) {
		    printk(KERN_INFO "Use camera OmniVision OV5642\n");
		    camera_run = 1;
		}
		if (!strcmp(param_value, "ov7675")) {
		    printk(KERN_INFO "Use camera OmniVision OV7675\n");
		    camera_run = 1;
		}
		if (!strcmp(param_value, "ov9710")) {
		    printk(KERN_INFO "Use camera OmniVision OV9710\n");
		    camera_run = 1;
		}
	    }
	    
	}

    } while(p);

}

#endif

/* End virt2real board devices init functions */


MACHINE_START(DAVINCI_DM365_EVM, "DaVinci DM365 EVM")
	.atag_offset	= 0x100,
	.map_io		= dm365_evm_map_io,
	.init_irq	= davinci_irq_init,
	.init_time	= davinci_timer_init,
	.init_machine	= dm365_evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
	.restart	= davinci_restart,
MACHINE_END

