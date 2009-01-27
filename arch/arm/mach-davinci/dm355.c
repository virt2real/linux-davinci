/*
 * TI DaVinci DM355 chip specific setup
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

#include <mach/dm355.h>
#include <mach/clock.h>
#include <mach/psc.h>
#include <mach/mux.h>

#include "clock.h"

#define DM355_REF_FREQ		24000000	/* 24 or 36 MHz */

static struct pll_data pll1_data = {
	.num       = 1,
	.phys_base = DAVINCI_PLL1_BASE,
	.flags     = PLL_HAS_PREDIV | PLL_HAS_POSTDIV,
};

static struct pll_data pll2_data = {
	.num       = 2,
	.phys_base = DAVINCI_PLL2_BASE,
	.flags     = PLL_HAS_PREDIV,
};

static struct clk ref_clk = {
	.name = "ref_clk",
	/* FIXME -- crystal rate is board-specific */
	.rate = DM355_REF_FREQ,
	.flags = CLK_PLL,
};

static struct clk pll1_clk = {
	.name = "pll1",
	.parent = &ref_clk,
	.flags = CLK_PLL,
	.pll_data = &pll1_data,
};

static struct clk pll2_clk = {
	.name = "pll2",
	.parent = &ref_clk,
	.flags = CLK_PLL,
	.pll_data = &pll2_data,
};

static struct clk aux_clk = {
	.name = "aux_clk",
	.parent = &ref_clk,
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

static struct clk vpbe_clk = { /* a.k.a. PLL1.SYSCLK3 */
	.name = "vpbe",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV3,
};

static struct clk vpss_clk = {  /* a.k.a. PLL1.SYCLK4 */
	.name = "vpss",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV4,
};

static struct clk clkout1_clk = {
	.name = "clkout1",
	.parent = &aux_clk,
	.flags = CLK_PLL,
	/* NOTE:  clkout1 can be externally gated by muxing GPIO-18 */
};

static struct clk clkout2_clk = { /* a.k.a. PLL1.SYSCLKBP */
	.name = "clkout2",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = BPDIV,
};

static struct clk clkout3_clk = {
	.name = "clkout3",
	.parent = &pll2_clk,
	.flags = CLK_PLL,
	.div_reg = BPDIV,
	/* NOTE:  clkout3 can be externally gated by muxing GPIO-16 */
};

static struct clk arm_clk = {
	.name = "ARMCLK",
	.parent = &sysclk1_clk,
	.flags = ALWAYS_ENABLED | CLK_PLL,
};

/*
 * NOT LISTED below, but turned on by PSC init:
 *   - in SyncReset state by default
 *	.lpsc = DAVINCI_LPSC_VPSSMSTR, .parent = &vpss_clk,
 *	.lpsc = DAVINCI_LPSC_VPSSSLV, .parent = &vpss_clk,
 *	.lpsc = DAVINCI_LPSC_TPCC,
 *	.lpsc = DAVINCI_LPSC_TPTC0,
 *	.lpsc = DAVINCI_LPSC_TPTC1,
 *
 * NOT LISTED below, and not touched by Linux
 *   - in SyncReset state by default
 *	.lpsc = DAVINCI_LPSC_DDR_EMIF, .parent = &sysclk2_clk,
 *	.lpsc = DM355_LPSC_RT0, .parent = &aux_clk,
 *	.lpsc = DAVINCI_LPSC_MEMSTICK,
 *	.lpsc = 41, .parent = &vpss_clk, // VPSS DAC
 *   - in Enabled state by default
 *	.lpsc = DAVINCI_LPSC_SYSTEM_SUBSYS,
 *	.lpsc = DAVINCI_LPSC_SCR2,	// "bus"
 *	.lpsc = DAVINCI_LPSC_SCR3,	// "bus"
 *	.lpsc = DAVINCI_LPSC_SCR4,	// "bus"
 *	.lpsc = DAVINCI_LPSC_CROSSBAR,	// "emulation"
 *	.lpsc = DAVINCI_LPSC_CFG27,	// "test"
 *	.lpsc = DAVINCI_LPSC_CFG3,	// "test"
 *	.lpsc = DAVINCI_LPSC_CFG5,	// "test"
 */

static struct clk mjcp_clk = {
	.name = "mjcp",
	.parent = &sysclk1_clk,
	.lpsc = DAVINCI_LPSC_IMCOP,
};

static struct clk uart0_clk = {
	.name = "uart0",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_UART0,
};

static struct clk uart1_clk = {
	.name = "uart1",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_UART1,
};

static struct clk uart2_clk = {
	.name = "uart2",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_UART2,
};

static struct clk i2c_clk = {
	.name = "I2CCLK",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_I2C,
};

static struct clk asp0_clk = {
	.name = "asp0_clk",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_McBSP,
};

static struct clk asp1_clk = {
	.name = "asp1_clk",
	.parent = &sysclk2_clk,
	.lpsc = DM355_LPSC_McBSP1,
};

static struct clk mmcsd0_clk = {
	.name = "MMCSDCLK0",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_MMC_SD,
};

static struct clk mmcsd1_clk = {
	.name = "MMCSDCLK1",
	.parent = &sysclk2_clk,
	.lpsc = DM355_LPSC_MMC_SD1,
};

static struct clk spi0_clk = {
	.name = "SPICLK",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_SPI,
};

static struct clk spi1_clk = {
	.name = "SPICLK1",
	.parent = &sysclk2_clk,
	.lpsc = DM355_LPSC_SPI1,
};

static struct clk spi2_clk = {
	.name = "SPICLK2",
	.parent = &sysclk2_clk,
	.lpsc = DM355_LPSC_SPI2,
};
static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_GPIO,
};

static struct clk aemif_clk = {
	.name = "AEMIFCLK",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_AEMIF,
	.usecount = 1,
};

static struct clk pwm0_clk = {
	.name = "PWM0_CLK",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_PWM0,
};

static struct clk pwm1_clk = {
	.name = "PWM1_CLK",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_PWM1,
};

static struct clk pwm2_clk = {
	.name = "PWM2_CLK",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_PWM2,
};

static struct clk pwm3_clk = {
	.name = "PWM3_CLK",
	.parent = &aux_clk,
	.lpsc = DM355_LPSC_PWM3,
};

static struct clk timer0_clk = {
	.name = "timer0",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_TIMER0,
};

static struct clk timer1_clk = {
	.name = "timer1",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_TIMER1,
};

static struct clk timer2_clk = {
	.name = "timer2",
	.parent = &aux_clk,
	.lpsc = DAVINCI_LPSC_TIMER2,
};

static struct clk timer3_clk = {
	.name = "timer3",
	.parent = &aux_clk,
	.lpsc = DM355_LPSC_TIMER3,
};

static struct clk usb_clk = {
	.name = "USBCLK",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_USB,
};

static struct clk *dm355_clks[] __initdata = {
	&ref_clk,
	&pll1_clk,
	&aux_clk,
	&sysclk1_clk,
	&sysclk2_clk,
	&vpbe_clk,
	&vpss_clk,
	&clkout1_clk,
	&clkout2_clk,
	&pll2_clk,
	&clkout3_clk,
	&arm_clk,
	&mjcp_clk,
	&uart0_clk,
	&uart1_clk,
	&uart2_clk,
	&i2c_clk,
	&asp0_clk,
	&asp1_clk,
	&mmcsd0_clk,
	&mmcsd1_clk,
	&spi0_clk,
	&spi1_clk,
	&spi2_clk,
	&gpio_clk,
	&aemif_clk,
	&pwm0_clk,
	&pwm1_clk,
	&pwm2_clk,
	&pwm3_clk,
	&timer0_clk,
	&timer1_clk,
	&timer2_clk,
	&timer3_clk,
	&usb_clk,
	NULL,
};

void __init dm355_init(void)
{
	davinci_clk_init(dm355_clks);
	davinci_mux_init();
}
