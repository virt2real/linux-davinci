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

/* various clock frequencies */
#define DM646X_REF_FREQ		27000000
#define DM646X_AUX_FREQ		24000000

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
	.rate = DM646X_REF_FREQ,
	.flags = CLK_PLL,
};

static struct clk aux_clk = {
	.name = "aux_clk",
	.rate = DM646X_AUX_FREQ,
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

static struct clk sysclk4_clk = {
	.name = "SYSCLK4",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV4,
};

static struct clk sysclk5_clk = {
	.name = "SYSCLK5",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV5,
};

static struct clk sysclk6_clk = {
	.name = "SYSCLK6",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV6,
};

static struct clk sysclk8_clk = {
	.name = "SYSCLK8",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV8,
};

static struct clk sysclk9_clk = {
	.name = "SYSCLK9",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV9,
};

static struct clk sysclkbp_clk = {
	.name = "SYSCLKBP",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = BPDIV,
};

static struct clk arm_clk = {
	.name = "ARMCLK",
	.parent = &sysclk2_clk,
	.lpsc = DAVINCI_LPSC_NONE,
	.flags = ALWAYS_ENABLED,
};

static struct clk uart0_clk = {
	.name = "uart0",
	.parent = &aux_clk,
	.lpsc = DM646X_LPSC_UART0,
};

static struct clk uart1_clk = {
	.name = "uart1",
	.parent = &aux_clk,
	.lpsc = DM646X_LPSC_UART1,
};

static struct clk uart2_clk = {
	.name = "uart2",
	.parent = &aux_clk,
	.lpsc = DM646X_LPSC_UART2,
};

static struct clk i2c_clk = {
	.name = "I2CCLK",
	.parent = &sysclk3_clk,
	.lpsc = DM646X_LPSC_I2C,
};

static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &sysclk3_clk,
	.lpsc = DM646X_LPSC_GPIO,
};

static struct clk aemif_clk = {
	.name = "AEMIFCLK",
	.parent = &sysclk3_clk,
	.lpsc = DM646X_LPSC_AEMIF,
	.flags = ALWAYS_ENABLED,
};

static struct clk emac_clk = {
	.name = "EMACCLK",
	.parent = &sysclk3_clk,
	.lpsc = DM646X_LPSC_EMAC,
};

static struct clk timer0_clk = {
	.name = "timer0",
	.parent = &sysclk3_clk,
	.lpsc = DM646X_LPSC_TIMER0,
};

static struct clk timer1_clk = {
	.name = "timer1",
	.parent = &sysclk3_clk,
	.lpsc = DM646X_LPSC_TIMER1,
};

static struct clk *dm646x_clks[] __initdata = {
	&ref_clk,
	&pll1_clk,
	&sysclk1_clk,
	&sysclk2_clk,
	&sysclk3_clk,
	&sysclk4_clk,
	&sysclk5_clk,
	&sysclk6_clk,
	&sysclk8_clk,
	&sysclk9_clk,
	&sysclkbp_clk,
	&pll2_clk,
	&arm_clk,
	&uart0_clk,
	&uart1_clk,
	&uart2_clk,
	&i2c_clk,
	&gpio_clk,
	&aemif_clk,
	&emac_clk,
	&timer0_clk,
	&timer1_clk,
	NULL,
};

void __init dm646x_init(void)
{
	davinci_clk_init(dm646x_clks);
	davinci_mux_init();
}
