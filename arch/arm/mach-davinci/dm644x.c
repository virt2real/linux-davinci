/*
 * TI DaVinci DM644x chip specific setup
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>

#include <mach/dm644x.h>
#include <mach/clock.h>
#include <mach/psc.h>
#include <mach/mux.h>

#include "clock.h"

#define DM644X_REF_FREQ		27000000

static struct pll_data pll1_data = {
	.num       = 1,
	.phys_base = DAVINCI_PLL1_BASE,
};

static struct pll_data pll2_data = {
	.num       = 2,
	.phys_base = DAVINCI_PLL2_BASE,
};

static struct clk ref_clk = {
	.name = "ref_clk",
	.rate = DM644X_REF_FREQ,
	.flags = CLK_PLL,
};

static struct clk pll1_clk = {
	.name = "pll1",
	.parent = &ref_clk,
	.pll_data = &pll1_data,
	.flags = CLK_PLL,
};

static struct clk pll2_clk = {
	.name = "pll2",
	.parent = &ref_clk,
	.pll_data = &pll2_data,
	.flags = CLK_PLL,
};

static struct clk sysclk1_clk = {
	.name = "SYSCLK1",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV1,
};

static struct clk sysclk2_clk = {
	.name = "SYSCLK2",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV2,
};

static struct clk sysclk3_clk = {
	.name = "SYSCLK3",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV3,
};

static struct clk sysclk5_clk = {
	.name = "SYSCLK5",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV5,
};

static struct clk arm_clk = {
	.name = "ARMCLK",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_NONE,
	.flags = ALWAYS_ENABLED,
};

static struct clk uart0_clk = {
	.name = "uart0",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_UART0,
};

static struct clk uart1_clk = {
	.name = "uart1",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_UART1,
};

static struct clk uart2_clk = {
	.name = "uart2",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_UART2,
};

static struct clk emac_clk = {
	.name = "EMACCLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_EMAC_WRAPPER,
};

static struct clk i2c_clk = {
	.name = "I2CCLK",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_I2C,
};

static struct clk ide_clk = {
	.name = "IDECLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_ATA,
};

static struct clk asp_clk = {
	.name = "asp0_clk",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_McBSP,
};

static struct clk mmcsd_clk = {
	.name = "MMCSDCLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_MMC_SD,
};

static struct clk spi_clk = {
	.name = "SPICLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_SPI,
};

static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_GPIO,
};

static struct clk usb_clk = {
	.name = "USBCLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_USB,
};

static struct clk vlynq_clk = {
	.name = "VLYNQCLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_VLYNQ,
};

static struct clk aemif_clk = {
	.name = "AEMIFCLK",
	.parent = &sysclk5_clk,
	.lpsc = DAVINCI_LPSC_AEMIF,
	.flags = ALWAYS_ENABLED,
};

static struct clk timer0_clk = {
	.name = "timer0",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_TIMER0,
};

static struct clk timer1_clk = {
	.name = "timer1",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_TIMER1,
};

static struct clk timer2_clk = {
	.name = "timer2",
	.parent = &ref_clk,
	.lpsc = DAVINCI_LPSC_TIMER2,
};

static struct clk *dm644x_clks[] __initdata = {
	&ref_clk,
	&pll1_clk,
	&pll2_clk,
	&sysclk1_clk,
	&sysclk2_clk,
	&sysclk3_clk,
	&sysclk5_clk,
	&arm_clk,
	&uart0_clk,
	&uart1_clk,
	&uart2_clk,
	&emac_clk,
	&i2c_clk,
	&ide_clk,
	&asp_clk,
	&mmcsd_clk,
	&spi_clk,
	&gpio_clk,
	&usb_clk,
	&vlynq_clk,
	&aemif_clk,
	&timer0_clk,
	&timer1_clk,
	&timer2_clk,
	NULL,
};

void __init dm644x_init(void)
{
	davinci_clk_init(dm644x_clks);
	davinci_mux_init();
}
