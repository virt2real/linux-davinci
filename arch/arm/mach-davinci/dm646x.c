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

#include <mach/dm646x.h>
#include <mach/clock.h>
#include <mach/psc.h>
#include <mach/mux.h>

#include "clock.h"
#include "mux.h"

/*
 * Device specific clocks
 */
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
};

static struct clk aux_clkin = {
	.name = "aux_clkin",
	.rate = DM646X_AUX_FREQ,
};

static struct clk pll1_clk = {
	.name = "pll1",
	.parent = &ref_clk,
	.pll_data = &pll1_data,
	.flags = CLK_PLL,
};

static struct clk pll1_sysclk1 = {
	.name = "pll1_sysclk1",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV1,
};

static struct clk pll1_sysclk2 = {
	.name = "pll1_sysclk2",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV2,
};

static struct clk pll1_sysclk3 = {
	.name = "pll1_sysclk3",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV3,
};

static struct clk pll1_sysclk4 = {
	.name = "pll1_sysclk4",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV4,
};

static struct clk pll1_sysclk5 = {
	.name = "pll1_sysclk5",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV5,
};

static struct clk pll1_sysclk6 = {
	.name = "pll1_sysclk6",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV6,
};

static struct clk pll1_sysclk8 = {
	.name = "pll1_sysclk8",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV8,
};

static struct clk pll1_sysclk9 = {
	.name = "pll1_sysclk9",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV9,
};

static struct clk pll1_sysclkbp = {
	.name = "pll1_sysclkbp",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
	.div_reg = BPDIV,
};

static struct clk pll1_aux_clk = {
	.name = "pll1_aux_clk",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
};

static struct clk pll2_clk = {
	.name = "pll2_clk",
	.parent = &ref_clk,
	.pll_data = &pll2_data,
	.flags = CLK_PLL,
};

static struct clk pll2_sysclk1 = {
	.name = "pll2_sysclk1",
	.parent = &pll2_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV1,
};

static struct clk dsp_clk = {
	.name = "dsp",
	.parent = &pll1_sysclk1,
	.lpsc = DM646X_LPSC_C64X_CPU,
	.flags = PSC_DSP,
	.usecount = 1,			/* REVISIT how to disable? */
};

static struct clk arm_clk = {
	.name = "arm",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_ARM,
	.flags = ALWAYS_ENABLED,
};

static struct clk uart0_clk = {
	.name = "uart0",
	.parent = &aux_clkin,
	.lpsc = DM646X_LPSC_UART0,
};

static struct clk uart1_clk = {
	.name = "uart1",
	.parent = &aux_clkin,
	.lpsc = DM646X_LPSC_UART1,
};

static struct clk uart2_clk = {
	.name = "uart2",
	.parent = &aux_clkin,
	.lpsc = DM646X_LPSC_UART2,
};

static struct clk i2c_clk = {
	.name = "I2CCLK",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_I2C,
};

static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_GPIO,
};

static struct clk aemif_clk = {
	.name = "aemif",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_AEMIF,
	.flags = ALWAYS_ENABLED,
};

static struct clk emac_clk = {
	.name = "emac",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_EMAC,
};

static struct clk pwm0_clk = {
	.name = "pwm0",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_PWM0,
	.usecount = 1,            /* REVIST: disabling hangs system */
};

static struct clk pwm1_clk = {
	.name = "pwm1",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_PWM1,
	.usecount = 1,            /* REVIST: disabling hangs system */
};

static struct clk timer0_clk = {
	.name = "timer0",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_TIMER0,
};

static struct clk timer1_clk = {
	.name = "timer1",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_TIMER1,
};

static struct clk timer2_clk = {
	.name = "timer2",
	.parent = &pll1_sysclk3,
	.flags = ALWAYS_ENABLED, /* no LPSC, always enabled; c.f. spruep9a */
};

struct davinci_clk dm646x_clks[] = {
	CLK(NULL, "ref", &ref_clk),
	CLK(NULL, "aux", &aux_clkin),
	CLK(NULL, "pll1", &pll1_clk),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk1),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk2),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk3),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk4),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk5),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk6),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk8),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk9),
	CLK(NULL, "pll1_sysclk", &pll1_sysclkbp),
	CLK(NULL, "pll1_aux", &pll1_aux_clk),
	CLK(NULL, "pll2", &pll2_clk),
	CLK(NULL, "pll2_sysclk1", &pll2_sysclk1),
	CLK(NULL, "dsp", &dsp_clk),
	CLK(NULL, "arm", &arm_clk),
	CLK(NULL, "uart0", &uart0_clk),
	CLK(NULL, "uart1", &uart1_clk),
	CLK(NULL, "uart2", &uart2_clk),
	CLK("i2c_davinci.1", NULL, &i2c_clk),
	CLK(NULL, "gpio", &gpio_clk),
	CLK(NULL, "aemif", &aemif_clk),
	CLK("davinci_emac.1", NULL, &emac_clk),
	CLK(NULL, "pwm0", &pwm0_clk),
	CLK(NULL, "pwm1", &pwm1_clk),
	CLK(NULL, "timer0", &timer0_clk),
	CLK(NULL, "timer1", &timer1_clk),
	CLK("watchdog", NULL, &timer2_clk),
	CLK(NULL, NULL, NULL),
};

/*
 * Device specific mux setup
 *
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config dm646x_pins[] = {
MUX_CFG(DM646X, ATAEN,		0,   0,     1,	  1,	 true)

MUX_CFG(DM646X, AUDCK1,		0,   29,    1,	  0,	 false)

MUX_CFG(DM646X, AUDCK0,		0,   28,    1,	  0,	 false)
};

void __init dm646x_init(void)
{
	davinci_clk_init(dm646x_clks);
	davinci_mux_register(dm646x_pins, ARRAY_SIZE(dm646x_pins));
}
