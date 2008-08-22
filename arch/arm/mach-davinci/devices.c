/*
 * linux/arch/arm/mach-davinci/devices.c
 *
 * DaVinci platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>

#if 	defined(CONFIG_I2C_DAVINCI) || defined(CONFIG_I2C_DAVINCI_MODULE)

static struct resource i2c_resources[] = {
	{
		.start		= DAVINCI_I2C_BASE,
		.end		= DAVINCI_I2C_BASE + 0x40,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_I2C,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device davinci_i2c_device = {
	.name           = "i2c_davinci",
	.id             = 1,
	.num_resources	= ARRAY_SIZE(i2c_resources),
	.resource	= i2c_resources,
};

static void davinci_init_i2c(void)
{
	(void) platform_device_register(&davinci_i2c_device);
}

#else

static void davinci_init_i2c(void) {}

#endif


#if 	defined(CONFIG_MMC_DAVINCI) || defined(CONFIG_MMC_DAVINCI_MODULE)

static u64 mmc_dma_mask = DMA_32BIT_MASK;

static struct resource mmc_resources[] = {
	{
		.start = DAVINCI_MMC_SD_BASE,
		.end   = DAVINCI_MMC_SD_BASE + 0x73,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_MMCINT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device davinci_mmcsd_device = {
	.name = "davinci_mmc",
	.id = 1,
	.dev = {
		.dma_mask = &mmc_dma_mask,
		.coherent_dma_mask = DMA_32BIT_MASK,
	},
	.num_resources = ARRAY_SIZE(mmc_resources),
	.resource = mmc_resources,
};


static void davinci_init_mmcsd(void)
{
	(void) platform_device_register(&davinci_mmcsd_device);
}

#else

static void davinci_init_mmcsd(void) {}

#endif

#if defined(CONFIG_TI_DAVINCI_EMAC) || defined(CONFIG_TI_DAVINCI_EMAC_MODULE)

static struct resource emac_resources[] = {
       {
       .start = DAVINCI_EMAC_CNTRL_REGS_BASE,
       .end   = DAVINCI_EMAC_CNTRL_REGS_BASE + 0x4800, /* 4K */
       .flags = IORESOURCE_MEM,
       },
       {
       .start = IRQ_EMACINT,
       .flags = IORESOURCE_IRQ,
       },
};


static struct platform_device davinci_emac_device = {
       .name = "davinci_emac",
       .id = 1,
       .num_resources = ARRAY_SIZE(emac_resources),
       .resource = emac_resources,
};


static void davinci_init_emac(void)
{
       (void) platform_device_register(&davinci_emac_device);
}

#else

static void davinci_init_emac(void) {}

#endif

/*-------------------------------------------------------------------------*/

static int __init davinci_init_devices(void)
{
	/* please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */
	davinci_init_i2c();
	davinci_init_mmcsd();
	davinci_init_emac();

	return 0;
}
arch_initcall(davinci_init_devices);

