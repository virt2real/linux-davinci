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
#include <media/tvp514x.h>

#include "davinci.h"

#include "board-virt2real-dm365.h"

#ifdef CONFIG_V2R_PARSE_CMDLINE
static void v2r_parse_cmdline(char * string);
#endif

static inline int have_imager(void)
{
	/* REVISIT when it's supported, trigger via Kconfig */
	return 0;
}


//#define DM365_EVM_PHY_ID		"davinci_mdio-0:01"  // replaced by Gol
#define DM365_EVM_PHY_ID		"davinci_mdio-0:00"   

/* NOTE:  this is geared for the standard config, with a socketed
 * 2 GByte Micron NAND (MT29F16G08FAA) using 128KB sectors.  If you
 * swap chips with a different block size, partitioning will
 * need to be changed. This NAND chip MT29F16G08FAA is the default
 * NAND shipped with the Spectrum Digital DM365 EVM
 */
#define NAND_BLOCK_SIZE		SZ_128K
#define DM365_ASYNC_EMIF_CONTROL_BASE	0x01d10000

static struct mtd_partition davinci_nand_partitions[] = {
	{
		/* UBL (a few copies) plus U-Boot */
		.name		= "bootloader",
		.offset		= 0,
		.size		= 30 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	}, {
		/* U-Boot environment */
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * NAND_BLOCK_SIZE,
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
	/* two blocks with bad block table (and mirror) at the end */
};

static struct davinci_nand_pdata davinci_nand_data = {
	.mask_chipsel		= BIT(14),
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
//Need to be deleted
static struct at24_platform_data eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = davinci_get_mac_addr,
	.context	= (void *)0x7f00,
};

static struct snd_platform_data dm365_evm_snd_data = {
	.asp_chan_q = EVENTQ_3,
};

//Here owr cameras support shold be added
static struct i2c_board_info i2c_info[] = {
	{
		I2C_BOARD_INFO("dm365evm_keys", 0x25),
	},
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data	= &eeprom_info,
	},
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
	.sda_pin        = 21,
	.scl_pin        = 20,
};

/* Input available at the ov7690 */
//Shadrin camera
static struct v4l2_input ov7690_inputs[] = {
	{
		.index = 0,
		.name = "Camera",
		.type = V4L2_INPUT_TYPE_CAMERA,
	}
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

static void dm365evm_emac_configure(void)
{
	/*
	 * EMAC pins are multiplexed with GPIO and UART
	 * Further details are available at the DM365 ARM
	 * Subsystem Users Guide(sprufg5.pdf) pages 125 - 127
	 */
	davinci_cfg_reg(DM365_EMAC_TX_EN);
	davinci_cfg_reg(DM365_EMAC_TX_CLK);
	davinci_cfg_reg(DM365_EMAC_COL);
	davinci_cfg_reg(DM365_EMAC_TXD3);
	davinci_cfg_reg(DM365_EMAC_TXD2);
	davinci_cfg_reg(DM365_EMAC_TXD1);
	davinci_cfg_reg(DM365_EMAC_TXD0);
	davinci_cfg_reg(DM365_EMAC_RXD3);
	davinci_cfg_reg(DM365_EMAC_RXD2);
	davinci_cfg_reg(DM365_EMAC_RXD1);
	davinci_cfg_reg(DM365_EMAC_RXD0);
	davinci_cfg_reg(DM365_EMAC_RX_CLK);
	davinci_cfg_reg(DM365_EMAC_RX_DV);
	davinci_cfg_reg(DM365_EMAC_RX_ER);
	davinci_cfg_reg(DM365_EMAC_CRS);
	davinci_cfg_reg(DM365_EMAC_MDIO);
	davinci_cfg_reg(DM365_EMAC_MDCLK);

	/*
	 * EMAC interrupts are multiplexed with GPIO interrupts
	 * Details are available at the DM365 ARM
	 * Subsystem Users Guide(sprufg5.pdf) pages 133 - 134
	 */
	davinci_cfg_reg(DM365_INT_EMAC_RXTHRESH);
	davinci_cfg_reg(DM365_INT_EMAC_RXPULSE);
	davinci_cfg_reg(DM365_INT_EMAC_TXPULSE);
	davinci_cfg_reg(DM365_INT_EMAC_MISCPULSE);
}

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

//Will be removed
static struct tvp514x_platform_data tvp5146_pdata = {
	.clk_polarity = 0,
	.hs_polarity = 1,
	.vs_polarity = 1
};

#define TVP514X_STD_ALL        (V4L2_STD_NTSC | V4L2_STD_PAL)
/* Inputs available at the TVP5146 */
static struct v4l2_input tvp5146_inputs[] = {
	{
		.index = 0,
		.name = "Composite",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = TVP514X_STD_ALL,
	},
	{
		.index = 1,
		.name = "S-Video",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = TVP514X_STD_ALL,
	},
};

//to support usb
__init static void dm365_usb_configure(void)
{
	davinci_cfg_reg(DM365_GPIO66);
	gpio_request(66, "usb");
	gpio_direction_output(66, 1);
	davinci_setup_usb(500, 8);
}
//For future use
static void dm365_wifi_configure(void)
{
	/*
	* CLKOUT1 pin is multiplexed with GPIO35 and SPI4
	* Further details are available at the DM365 ARM
	* Subsystem Users Guide(sprufg5.pdf) pages 118, 127 - 129
	*/
	/* Setup PWCTRO2 to generate 32kHz
	 * Setup GPIO36 as output 1 (WLAN RESET)
	 * Setup GPIO69 as output 1 (WLAN_SHDN)
	 * Setup sd2/mmc2 interface
	 * The HDG104 driver will be loaded later
	 */
	//Setup PWCTRO2 to generate 32kHz
	printk("Before start WiFi 32k\r\n");
	//configure_prtcss_32k();
	printk("After start WiFi 32k\r\n");
	//SET WLAN RESET
	{
		volatile int i = 0;
		for (i = 0; i < 65536; i++){
			if ((i%4096) == 0) printk("*");

		}
	}
	gpio_request(36, "wifi_reset");
	gpio_direction_output(36, 1);
	//SET WLAN_SHDN
	gpio_request(69, "wifi_shdn");
	gpio_direction_output(69, 1);
	//LETS CONSIDER SD2 AS INITIALIZED

#if 0
	struct clk *clkout1_clk;

	davinci_cfg_reg(DM365_CLKOUT1);

	clkout1_clk = clk_get(NULL, "clkout1");
	if (IS_ERR(clkout1_clk))
		return;
	clk_enable(clkout1_clk);

	/*
	* Configure CLKOUT1 OBSCLK registers
	*/

	/* (reg OCSEL) Setting OBSCLK source with Oscillator divider output enable */
	__raw_writel(0x0,IO_ADDRESS(0x01C40C00 + 0x104));

	/* (reg OSCDIV1) Setting the Oscillator divider enable with a divider ratio of 1 */
	__raw_writel(0x8000,IO_ADDRESS(0x01C40C00 + 0x124));

	/* (reg CKEN) Setting the OBSCLK clock enable */
	__raw_writel(0x02,IO_ADDRESS(0x01C40C00 + 0x148));
#endif
}
static void dm365leopard_camera_configure(void){
	gpio_request(70, "Camera_on_off");
	gpio_direction_output(70, 1);
	davinci_cfg_reg(DM365_SPI4_SDENA0);
	davinci_cfg_reg(DM365_EXTCLK);

}

static void dm365_gpio_configure(void){
	return;//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! \<<< Here is return
//
	gpio_request(22, "GPIO22");
	gpio_direction_input(22);
	davinci_cfg_reg(DM365_GPIO22);
//
	gpio_request(23, "GPIO23");
	gpio_direction_input(23);
	davinci_cfg_reg(DM365_GPIO23);
//
	gpio_request(24, "GPIO24");
	gpio_direction_input(24);
	davinci_cfg_reg(DM365_GPIO24);
//
	gpio_request(25, "GPIO25");
	gpio_direction_input(25);
	davinci_cfg_reg(DM365_GPIO25);
//
	gpio_request(26, "GPIO26");
	gpio_direction_input(26);
	davinci_cfg_reg(DM365_GPIO26);
//
	gpio_request(27, "GPIO27");
	gpio_direction_input(27);
	davinci_cfg_reg(DM365_GPIO27);
//
	gpio_request(28, "GPIO28");
	gpio_direction_input(28);
	davinci_cfg_reg(DM365_GPIO28);
//
	gpio_request(29, "GPIO29");
	gpio_direction_input(29);
	davinci_cfg_reg(DM365_GPIO29);
//
	gpio_request(30, "GPIO30");
	gpio_direction_input(30);
	davinci_cfg_reg(DM365_GPIO30);
//
	gpio_request(31, "GPIO31");
	gpio_direction_input(31);
	davinci_cfg_reg(DM365_GPIO31);
//
	gpio_request(32, "GPIO32");
	gpio_direction_input(32);
	davinci_cfg_reg(DM365_GPIO32);
//
	gpio_request(33, "GPIO33");
	gpio_direction_input(33);
	davinci_cfg_reg(DM365_GPIO33);
//
	gpio_request(34, "GPIO34");
	gpio_direction_input(34);
	davinci_cfg_reg(DM365_GPIO34);
//
	gpio_request(35, "GPIO35");
	gpio_direction_input(35);
	davinci_cfg_reg(DM365_GPIO35);
//
	gpio_request(44, "GPIO44");
	gpio_direction_input(44);
	davinci_cfg_reg(DM365_GPIO44);
//
	gpio_request(45, "GPIO45");
	gpio_direction_input(45);
	davinci_cfg_reg(DM365_GPIO45);
//
	gpio_request(46, "GPIO46");
	gpio_direction_input(46);
	davinci_cfg_reg(DM365_GPIO46);
//
	gpio_request(47, "GPIO47");
	gpio_direction_input(47);
	davinci_cfg_reg(DM365_GPIO47);
//
	gpio_request(48, "GPIO48");
	gpio_direction_input(48);
	davinci_cfg_reg(DM365_GPIO48);
//
	gpio_request(49, "GPIO49");
	gpio_direction_input(49);
	davinci_cfg_reg(DM365_GPIO49);
//
	gpio_request(50, "GPIO50");
	gpio_direction_input(50);
	davinci_cfg_reg(DM365_GPIO50);
//
	gpio_request(51, "GPIO51");
	gpio_direction_input(51);
	davinci_cfg_reg(DM365_GPIO51);
//
	gpio_request(67, "GPIO67");
	gpio_direction_input(67);
	davinci_cfg_reg(DM365_GPIO67);
//
	gpio_request(79, "GPIO79");
	gpio_direction_input(79);
	davinci_cfg_reg(DM365_GPIO79);
//
	gpio_request(80, "GPIO80");
	gpio_direction_input(80);
	davinci_cfg_reg(DM365_GPIO80);
//
	gpio_request(81, "GPIO81");
	gpio_direction_input(81);
	davinci_cfg_reg(DM365_GPIO81);
//
	gpio_request(82, "GPIO82");
	gpio_direction_input(82);
	davinci_cfg_reg(DM365_GPIO82);
//
	gpio_request(83, "GPIO83");
	gpio_direction_input(83);
	davinci_cfg_reg(DM365_GPIO83);
//
	gpio_request(84, "GPIO84");
	gpio_direction_input(84);
	davinci_cfg_reg(DM365_GPIO84);
//
	gpio_request(85, "GPIO85");
	gpio_direction_input(85);
	davinci_cfg_reg(DM365_GPIO85);
//
	gpio_request(86, "GPIO86");
	gpio_direction_input(86);
	davinci_cfg_reg(DM365_GPIO86);
//
	gpio_request(87, "GPIO87");
	gpio_direction_input(87);
	davinci_cfg_reg(DM365_GPIO87);
//
	gpio_request(88, "GPIO88");
	gpio_direction_input(88);
	davinci_cfg_reg(DM365_GPIO88);
//
	gpio_request(89, "GPIO89");
	gpio_direction_input(89);
	davinci_cfg_reg(DM365_GPIO89);
//
	gpio_request(90, "GPIO90");
	gpio_direction_input(90);
	davinci_cfg_reg(DM365_GPIO90);
//
	gpio_request(91, "GPIO91");
	gpio_direction_input(91);
	davinci_cfg_reg(DM365_GPIO91);
//
	gpio_request(92, "GPIO92");
	gpio_direction_input(92);
	davinci_cfg_reg(DM365_GPIO92);
//
	gpio_request(100, "GPIO100");
	gpio_direction_input(100);
	davinci_cfg_reg(DM365_GPIO100);
//
	gpio_request(101, "GPIO101");
	gpio_direction_input(101);
	davinci_cfg_reg(DM365_GPIO101);
//
	gpio_request(102, "GPIO102");
	gpio_direction_input(102);
	davinci_cfg_reg(DM365_GPIO102);
//
	gpio_request(103, "GPIO103");
	gpio_direction_input(103);
	davinci_cfg_reg(DM365_GPIO103);
}

/*
 * this is the route info for connecting each input to decoder
 * ouput that goes to vpfe. There is a one to one correspondence
 * with tvp5146_inputs
 */
static struct vpfe_route tvp5146_routes[] = {
	{
		.input = INPUT_CVBS_VI2B,
		.output = OUTPUT_10BIT_422_EMBEDDED_SYNC,
	},
{
		.input = INPUT_SVIDEO_VI2C_VI1C,
		.output = OUTPUT_10BIT_422_EMBEDDED_SYNC,
	},
};

static struct vpfe_subdev_info vpfe_sub_devs[] = {
	{
		.name = "tvp5146",
		.grp_id = 0,
		.num_inputs = ARRAY_SIZE(tvp5146_inputs),
		.inputs = tvp5146_inputs,
		.routes = tvp5146_routes,
		.can_route = 1,
		.ccdc_if_params = {
			.if_type = VPFE_BT656,
			.hdpol = VPFE_PINPOL_POSITIVE,
			.vdpol = VPFE_PINPOL_POSITIVE,
		},
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5d),
			.platform_data = &tvp5146_pdata,
		},
	},
};

static struct vpfe_config vpfe_cfg = {
	.num_subdevs = ARRAY_SIZE(vpfe_sub_devs),
	.sub_devs = vpfe_sub_devs,
	.i2c_adapter_id = 1,
	.card_name = "DM365 EVM",
	.ccdc = "ISIF",
};

static void __init evm_init_i2c(void)
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
	.enabled_uarts = (1 << 0),
};

static void __init dm365_evm_map_io(void)
{
	/* setup input configuration for VPFE input devices */
	dm365_set_vpfe_config(&vpfe_cfg);
	dm365_init();
}



static __init void dm365_evm_init(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	struct clk *aemif_clk;

	aemif_clk = clk_get(NULL, "aemif");
	if (IS_ERR(aemif_clk))	return;
	clk_prepare_enable(aemif_clk);
	platform_add_devices(dm365_evm_nand_devices, ARRAY_SIZE(dm365_evm_nand_devices));
	evm_init_i2c();
	davinci_serial_init(&uart_config);

	dm365evm_emac_configure();
	soc_info->emac_pdata->phy_id = DM365_EVM_PHY_ID;

	dm365evm_mmc_configure();

#ifdef CONFIG_V2R_PARSE_CMDLINE
	//printk (KERN_INFO "Parse cmdline: %s\n", saved_command_line);
	v2r_parse_cmdline(saved_command_line);
#endif

	davinci_setup_mmc(0, &dm365evm_mmc_config);
	davinci_setup_mmc(1, &dm365evm_mmc_config);
	/* maybe setup mmc1/etc ... _after_ mmc0 */
	dm365_wifi_configure();

#ifdef CONFIG_SND_DM365_AIC3X_CODEC
	dm365_init_asp(&dm365_evm_snd_data);
#elif defined(CONFIG_SND_DM365_VOICE_CODEC)
	dm365_init_vc(&dm365_evm_snd_data);
#endif
	dm365_init_rtc();
	dm365_usb_configure();
}


/* Virt2real board devices init functions */

#ifdef CONFIG_V2R_PARSE_CMDLINE
static void v2r_parse_cmdline(char * string)
{

    printk(KERN_INFO "Parse kernel cmdline:\n");

    char *p;
    char *temp_string;
    char *temp_param;
    char *param_name;
    char *param_value;
    
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
	    
	    if (!strcmp(param_name, "camera")) {
		if (!strcmp(param_value, "ov5642")) {
		    printk(KERN_INFO "Use camera OmniVision OV5642\n");
		}
		if (!strcmp(param_value, "ov7675")) {
		    printk(KERN_INFO "Use camera OmniVision OV7675\n");
		}
		if (!strcmp(param_value, "ov9710")) {
		    printk(KERN_INFO "Use camera OmniVision OV9710\n");
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

