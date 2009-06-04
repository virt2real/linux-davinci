/*
 * DA830/OMAP L137 platform device data
 *
 * Copyright (c) 2007-2009, MontaVista Software, Inc. <source@mvista.com>
 * Derived from code that was:
 *	Copyright (C) 2006 Komal Shah <komal_shah802003@yahoo.com>
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
#include <linux/serial_8250.h>

#include <mach/cputype.h>
#include <mach/common.h>
#include <mach/time.h>
#include <mach/da830.h>

#include "clock.h"

#define DA830_TPCC_BASE			0x01c00000
#define DA830_TPTC0_BASE		0x01c08000
#define DA830_TPTC1_BASE		0x01c08400
#define DA830_WDOG_BASE			0x01c21000 /* DA830_TIMER64P1_BASE */
#define DA830_I2C0_BASE			0x01c22000
#define DA830_EMAC_CPPI_PORT_BASE	0x01e20000
#define DA830_EMAC_CPGMACSS_BASE	0x01e22000
#define DA830_EMAC_CPGMAC_BASE		0x01e23000
#define DA830_EMAC_MDIO_BASE		0x01e24000
#define DA830_GPIO_BASE			0x01e26000
#define DA830_I2C1_BASE			0x01e28000

#define DA830_EMAC_CTRL_REG_OFFSET	0x3000
#define DA830_EMAC_MOD_REG_OFFSET	0x2000
#define DA830_EMAC_RAM_OFFSET		0x0000
#define DA830_MDIO_REG_OFFSET		0x4000
#define DA830_EMAC_CTRL_RAM_SIZE	SZ_8K

static struct plat_serial8250_port da830_serial_pdata[] = {
	{
		.mapbase	= DA830_UART0_BASE,
		.irq		= IRQ_DA830_UARTINT0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
					UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.mapbase	= DA830_UART1_BASE,
		.irq		= IRQ_DA830_UARTINT1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
					UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.mapbase	= DA830_UART2_BASE,
		.irq		= IRQ_DA830_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
					UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	},
};

struct platform_device da830_serial_device = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM,
	.dev	= {
		.platform_data	= da830_serial_pdata,
	},
};

static const s8 da830_dma_chan_no_event[] = {
	20, 21,
	-1
};

static struct edma_soc_info da830_edma_info = {
	.n_channel	= 32,
	.n_region	= 4,
	.n_slot		= 128,
	.n_tc		= 2,
	.noevent	= da830_dma_chan_no_event,
};

static struct resource da830_edma_resources[] = {
	{
		.name	= "edma_cc",
		.start	= DA830_TPCC_BASE,
		.end	= DA830_TPCC_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc0",
		.start	= DA830_TPTC0_BASE,
		.end	= DA830_TPTC0_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc1",
		.start	= DA830_TPTC1_BASE,
		.end	= DA830_TPTC1_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA830_TCERRINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA830_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da830_edma_device = {
	.name		= "edma",
	.id		= -1,
	.dev = {
		.platform_data	= &da830_edma_info,
	},
	.num_resources	= ARRAY_SIZE(da830_edma_resources),
	.resource	= da830_edma_resources,
};

int __init da830_register_edma(void)
{
	return platform_device_register(&da830_edma_device);
}

static struct resource da830_i2c_resources0[] = {
	{
		.start	= DA830_I2C0_BASE,
		.end	= DA830_I2C0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA830_I2CINT0,
		.end	= IRQ_DA830_I2CINT0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da830_i2c_device0 = {
	.name		= "i2c_davinci",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(da830_i2c_resources0),
	.resource	= da830_i2c_resources0,
};

static struct resource da830_i2c_resources1[] = {
	{
		.start	= DA830_I2C1_BASE,
		.end	= DA830_I2C1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA830_I2CINT1,
		.end	= IRQ_DA830_I2CINT1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da830_i2c_device1 = {
	.name		= "i2c_davinci",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(da830_i2c_resources1),
	.resource	= da830_i2c_resources1,
};

int __init da830_register_i2c(int instance,
		struct davinci_i2c_platform_data *pdata)
{
	struct platform_device *pdev;

	if (instance == 0)
		pdev = &da830_i2c_device0;
	else if (instance == 1)
		pdev = &da830_i2c_device1;
	else
		return -EINVAL;

	pdev->dev.platform_data = pdata;
	return platform_device_register(pdev);
}

static struct resource da830_watchdog_resources[] = {
	{
		.start	= DA830_WDOG_BASE,
		.end	= DA830_WDOG_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device davinci_wdt_device = {
	.name		= "watchdog",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(da830_watchdog_resources),
	.resource	= da830_watchdog_resources,
};

int __init da830_register_watchdog(void)
{
	return platform_device_register(&davinci_wdt_device);
}

static struct resource da830_emac_resources[] = {
	{
		.start	= DA830_EMAC_CPPI_PORT_BASE,
		.end	= DA830_EMAC_CPPI_PORT_BASE + 0x5000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA830_C0_RX_THRESH_PULSE,
		.end	= IRQ_DA830_C0_RX_THRESH_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA830_C0_RX_PULSE,
		.end	= IRQ_DA830_C0_RX_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA830_C0_TX_PULSE,
		.end	= IRQ_DA830_C0_TX_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA830_C0_MISC_PULSE,
		.end	= IRQ_DA830_C0_MISC_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
};

struct emac_platform_data da830_emac_pdata = {
	.ctrl_reg_offset	= DA830_EMAC_CTRL_REG_OFFSET,
	.ctrl_mod_reg_offset	= DA830_EMAC_MOD_REG_OFFSET,
	.ctrl_ram_offset	= DA830_EMAC_RAM_OFFSET,
	.mdio_reg_offset	= DA830_MDIO_REG_OFFSET,
	.ctrl_ram_size		= DA830_EMAC_CTRL_RAM_SIZE,
	.version		= EMAC_VERSION_2,
};

static struct platform_device da830_emac_device = {
	.name		= "davinci_emac",
	.id		= 1,
	.dev = {
		.platform_data	= &da830_emac_pdata,
	},
	.num_resources	= ARRAY_SIZE(da830_emac_resources),
	.resource	= da830_emac_resources,
};

int __init da830_register_emac(void)
{
	return platform_device_register(&da830_emac_device);
}
