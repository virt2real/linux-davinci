/*
 * DAVINCI pin multiplexing configurations
 *
 * Author: Vladimir Barinov, MontaVista Software, Inc. <source@mvista.com>
 *
 * Based on linux/arch/arm/mach-omap1/mux.c:
 * Copyright (C) 2003 - 2005 Nokia Corporation
 *
 * Written by Tony Lindgren
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright (C) 2008 Texas Instruments.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <mach/cpu.h>
#include <mach/mux.h>

#define MUX_CFG(soc, desc, muxreg, mode_offset, mode_mask, mux_mode, dbg)\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "PINMUX"#muxreg,		\
			.mux_reg = PINMUX##muxreg,			\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#define INT_CFG(soc, desc, mode_offset, mode_mask, mux_mode, dbg)	\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "INTMUX",			\
			.mux_reg = INTMUX,				\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#define EVT_CFG(soc, desc, mode_offset, mode_mask, mux_mode, dbg)	\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "EVTMUX",			\
			.mux_reg = EVTMUX,				\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

/*
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config davinci_dm644x_pins[] = {
MUX_CFG(DM644X, HDIREN,		0,   16,    1,	  1,	 true)
MUX_CFG(DM644X, ATAEN,		0,   17,    1,	  1,	 true)
MUX_CFG(DM644X, ATAEN_DISABLE,	0,   17,    1,	  0,	 true)

MUX_CFG(DM644X, HPIEN_DISABLE,	0,   29,    1,	  0,	 true)

MUX_CFG(DM644X, AEAW,		0,   0,     31,	  31,	 true)

MUX_CFG(DM644X, MSTK,		1,   9,     1,	  0,	 false)

MUX_CFG(DM644X, I2C,		1,   7,     1,	  1,	 false)

MUX_CFG(DM644X, MCBSP,		1,   10,    1,	  1,	 false)

MUX_CFG(DM644X, UART1,		1,   1,     1,	  1,	 true)
MUX_CFG(DM644X, UART2,		1,   2,     1,	  1,	 true)

MUX_CFG(DM644X, PWM0,		1,   4,     1,	  1,	 false)

MUX_CFG(DM644X, PWM1,		1,   5,     1,	  1,	 false)

MUX_CFG(DM644X, PWM2,		1,   6,     1,	  1,	 false)

MUX_CFG(DM644X, VLYNQEN,	0,   15,    1,	  1,	 false)
MUX_CFG(DM644X, VLSCREN,	0,   14,    1,	  1,	 false)
MUX_CFG(DM644X, VLYNQWD,	0,   12,    3,	  3,	 false)

MUX_CFG(DM644X, EMACEN,		0,   31,    1,	  1,	 true)

MUX_CFG(DM644X, GPIO3V,		0,   31,    1,	  0,	 true)

MUX_CFG(DM644X, GPIO0,		0,   24,    1,	  0,	 true)
MUX_CFG(DM644X, GPIO3,		0,   25,    1,	  0,	 false)
MUX_CFG(DM644X, GPIO43_44,	1,   7,     1,	  0,	 false)
MUX_CFG(DM644X, GPIO46_47,	0,   22,    1,	  0,	 true)

MUX_CFG(DM644X, RGB666,		0,   22,    1,	  1,	 true)

MUX_CFG(DM644X, LOEEN,		0,   24,    1,	  1,	 true)
MUX_CFG(DM644X, LFLDEN,		0,   25,    1,	  1,	 false)
};

/*
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config davinci_dm646x_pins[] = {
MUX_CFG(DM646X, ATAEN,		0,   0,     1,	  1,	 true)

MUX_CFG(DM646X, AUDCK1,		0,   29,    1,	  0,	 false)

MUX_CFG(DM646X, AUDCK0,		0,   28,    1,	  0,	 false)
};

/*
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config davinci_dm355_pins[] = {
MUX_CFG(DM355,	MMCSD0,		4,   2,     1,	  0,	 false)

MUX_CFG(DM355,	SD1_CLK,	3,   6,     1,	  1,	 false)
MUX_CFG(DM355,	SD1_CMD,	3,   7,     1,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA3,	3,   8,     3,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA2,	3,   10,    3,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA1,	3,   12,    3,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA0,	3,   14,    3,	  1,	 false)

MUX_CFG(DM355,	I2C_SDA,	3,   19,    1,	  1,	 false)
MUX_CFG(DM355,	I2C_SCL,	3,   20,    1,	  1,	 false)

MUX_CFG(DM355,	MCBSP0_BDX,	3,   0,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_X,	3,   1,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_BFSX,	3,   2,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_BDR,	3,   3,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_R,	3,   4,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_BFSR,	3,   5,     1,	  1,	 false)

MUX_CFG(DM355,	SPI0_SDI,	4,   1,     1,    0,	 false)
MUX_CFG(DM355,	SPI0_SDENA0,	4,   0,     1,    0,	 false)
MUX_CFG(DM355,	SPI0_SDENA1,	3,   28,    1,    1,	 false)

INT_CFG(DM355,  INT_EDMA_CC,	      2,    1,    1,     false)
INT_CFG(DM355,  INT_EDMA_TC0_ERR,     3,    1,    1,     false)
INT_CFG(DM355,  INT_EDMA_TC1_ERR,     4,    1,    1,     false)

EVT_CFG(DM355,  EVT8_ASP1_TX,	      0,    1,    0,     false)
EVT_CFG(DM355,  EVT9_ASP1_RX,	      1,    1,    0,     false)
EVT_CFG(DM355,  EVT26_MMC0_RX,	      2,    1,    0,     false)
};

void __init davinci_mux_init(void)
{
	if (cpu_is_davinci_dm644x())
		davinci_mux_register(davinci_dm644x_pins,
				     ARRAY_SIZE(davinci_dm644x_pins));
	else if (cpu_is_davinci_dm646x())
		davinci_mux_register(davinci_dm646x_pins,
					ARRAY_SIZE(davinci_dm646x_pins));
	else if (cpu_is_davinci_dm355())
		davinci_mux_register(davinci_dm355_pins,
					ARRAY_SIZE(davinci_dm355_pins));
	else
		printk(KERN_ERR "PSC: DaVinci variant not supported\n");
}

