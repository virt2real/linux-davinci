/*
 * TI DaVinci clock config file
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include <mach/psc.h>
#include <mach/cpu.h>
#include "clock.h"

#define DAVINCI_PLL_CNTRL0_BASE 0x01C40800

/* PLL/Reset register offsets */
#define PLLM		0x110

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static unsigned int commonrate;
static unsigned int div_by_four;
static unsigned int div_by_six;
static unsigned int div_by_eight;
static unsigned int armrate;
static unsigned int fixedrate = 27000000;	/* 27 MHZ */

extern void davinci_psc_config(unsigned int domain, unsigned int id, char enable);

/*
 * Register a mapping { dev, logical_clockname } --> clock
 *
 * Device drivers should always use logical clocknames, so they
 * don't need to change the physical name when new silicon grows
 * another instance of that module or changes the clock tree.
 */

struct clk_mapping {
	struct device		*dev;
	const char		*name;
	struct clk		*clock;
	struct clk_mapping	*next;
};

static struct clk_mapping *maplist;

int __init davinci_clk_associate(struct device *dev,
		const char *logical_clockname,
		const char *physical_clockname)
{
	int			status = -EINVAL;
	struct clk		*clock;
	struct clk_mapping	*mapping;

	if (!dev)
		goto done;

	clock = clk_get(dev, physical_clockname);
	if (IS_ERR(clock) || !try_module_get(clock->owner))
		goto done;

	mutex_lock(&clocks_mutex);
	for (mapping = maplist; mapping; mapping = mapping->next) {
		if (dev != mapping->dev)
			continue;
		if (strcmp(logical_clockname, mapping->name) != 0)
			continue;
		goto fail;
	}

	mapping = kzalloc(sizeof *mapping, GFP_KERNEL);
	mapping->dev = dev;
	mapping->name = logical_clockname;
	mapping->clock = clock;

	mapping->next = maplist;
	maplist = mapping;

	status = 0;
fail:
	mutex_unlock(&clocks_mutex);
done:
	WARN_ON(status < 0);
	return status;
}

/*
 * Returns a clock. Note that we first try to use device id on the bus
 * and clock name. If this fails, we try to use clock name only.
 */
struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);
	int idno;
	struct clk_mapping *mapping;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	mutex_lock(&clocks_mutex);

	/* always prefer logical clock names */
	if (dev) {
		for (mapping = maplist; mapping; mapping = mapping->next) {
			if (dev != mapping->dev)
				continue;
			if (strcmp(id, mapping->name) != 0)
				continue;
			clk = mapping->clock;
			goto found;
		}
	}

	list_for_each_entry(p, &clocks, node) {
		if (p->id == idno &&
		    strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			goto found;
		}
	}

	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}

found:
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	if (clk && !IS_ERR(clk))
		module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);

static int __clk_enable(struct clk *clk)
{
	if (clk->flags & ALWAYS_ENABLED)
		return 0;

	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, clk->lpsc, 1);
	return 0;
}

static void __clk_disable(struct clk *clk)
{
	if (clk->usecount)
		return;

	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, clk->lpsc, 0);
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;
	
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (clk->usecount++ == 0) {
		spin_lock_irqsave(&clockfw_lock, flags);
		ret = __clk_enable(clk);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	if (clk->usecount > 0 && !(--clk->usecount)) {
		spin_lock_irqsave(&clockfw_lock, flags);
		__clk_disable(clk);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return *(clk->rate);
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return *(clk->rate);
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/* changing the clk rate is not supported */
	return -EINVAL;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);

	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

static struct clk davinci_clks[] = {
	{
		.name = "ARMCLK",
		.rate = &armrate,
		.lpsc = -1,
		.flags = ALWAYS_ENABLED,
	},
	{
		.name = "UART0",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART0,
	},
	{
		.name = "UART1",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART1,
	},
	{
		.name = "UART2",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART2,
	},
	{
		.name = "EMACCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_EMAC_WRAPPER,
	},
	{
		.name = "I2CCLK",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_I2C,
	},
	{
		.name = "IDECLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_ATA,
	},
	{
		.name = "McBSPCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_McBSP,
	},
	{
		.name = "MMCSDCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_MMC_SD,
	},
	{
		.name = "SPICLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_SPI,
	},
	{
		.name = "gpio",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_GPIO,
	},
	{
		.name = "USBCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_USB,
	},
	{
		.name = "VLYNQCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_VLYNQ,
	},
	{
		.name = "AEMIFCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_AEMIF,
		.usecount = 1,
	}
};
static struct clk davinci_dm646x_clks[] = {
	{
		.name = "ARMCLK",
		.rate = &armrate,
		.lpsc = -1,
		.flags = ALWAYS_ENABLED,
	},
	{
		.name = "UART0",
		.rate = &fixedrate,
		.lpsc = DM646X_LPSC_UART0,
	},
	{
		.name = "UART1",
		.rate = &fixedrate,
		.lpsc = DM646X_LPSC_UART1,
	},
	{
		.name = "UART2",
		.rate = &fixedrate,
		.lpsc = DM646X_LPSC_UART2,
	},
	{
		.name = "I2CCLK",
		.rate = &div_by_four,
		.lpsc = DM646X_LPSC_I2C,
	},
	{
		.name = "gpio",
		.rate = &commonrate,
		.lpsc = DM646X_LPSC_GPIO,
	},
	{
		.name = "AEMIFCLK",
		.rate = &div_by_four,
		.lpsc = DM646X_LPSC_AEMIF,
		.usecount = 1,
	},
	{
		.name = "EMACCLK",
		.rate = &div_by_four,
		.lpsc = DM646X_LPSC_EMAC,
	},
};
static struct clk davinci_dm355_clks[] = {
	{
		.name = "ARMCLK",
		.rate = &armrate,
		.lpsc = -1,
		.flags = ALWAYS_ENABLED,
	},
	{
		.name = "UART0",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART0,
		.usecount = 1,
	},
	{
		.name = "UART1",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART1,
		.usecount = 1,
	},
	{
		.name = "UART2",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART2,
		.usecount = 1,
	},
	{
		.name = "I2CCLK",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_I2C,
	},
	{
		.name = "McBSPCLK0",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_McBSP,
	},
	{
		.name = "McBSPCLK1",
		.rate = &commonrate,
		.lpsc = DM355_LPSC_McBSP1,
	},
	{
		.name = "MMCSDCLK0",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_MMC_SD,
	},
	{
		.name = "MMCSDCLK1",
		.rate = &commonrate,
		.lpsc = DM355_LPSC_MMC_SD1,
	},
	{
		.name = "SPICLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_SPI,
	},
	{
		.name = "SPICLK1",
		.rate = &commonrate,
		.lpsc = DM355_LPSC_SPI1,
	},
	{
		.name = "SPICLK2",
		.rate = &commonrate,
		.lpsc = DM355_LPSC_SPI2,
	},
	{
		.name = "gpio",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_GPIO,
	},
	{
		.name = "AEMIFCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_AEMIF,
		.usecount = 1,
	},
	{
		.name = "PWM0_CLK",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_PWM0,
	},
	{
		.name = "PWM1_CLK",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_PWM1,
	},
	{
		.name = "PWM2_CLK",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_PWM2,
	},
	{
		.name = "PWM3_CLK",
		.rate = &fixedrate,
		.lpsc = DM355_LPSC_PWM3,
	},
	{
		.name = "USBCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_USB,
	},
};

#ifdef CONFIG_DAVINCI_RESET_CLOCKS
/*
 * Disable any unused clocks left on by the bootloader
 */
static int __init clk_disable_unused(void)
{
	struct clk *ck;
	unsigned long flags;

	list_for_each_entry(ck, &clocks, node) {
		if (ck->usecount > 0 || (ck->flags & ALWAYS_ENABLED))
			continue;

		printk(KERN_INFO "Clocks: disable unused %s\n", ck->name);
		spin_lock_irqsave(&clockfw_lock, flags);
		__clk_disable(ck);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	return 0;
}
late_initcall(clk_disable_unused);
#endif

int __init davinci_clk_init(void)
{
	struct clk *clkp;
	static struct clk *board_clks;
	int count = 0, num_clks;
	u32 pll_mult;

	pll_mult = davinci_readl(DAVINCI_PLL_CNTRL0_BASE + PLLM);
	commonrate = ((pll_mult + 1) * DM646X_OSC_FREQ) / 6;
	armrate = ((pll_mult + 1) * DM646X_OSC_FREQ) / 2;

	if (cpu_is_davinci_dm646x()) {
		fixedrate = 24000000;
		div_by_four = ((pll_mult + 1) * DM646X_OSC_FREQ) / 4;
		div_by_six = ((pll_mult + 1) * DM646X_OSC_FREQ) / 6;
		div_by_eight = ((pll_mult + 1) * DM646X_OSC_FREQ) / 8;
		armrate = ((pll_mult + 1) * DM646X_OSC_FREQ) / 2;

		board_clks = davinci_dm646x_clks;
		num_clks = ARRAY_SIZE(davinci_dm646x_clks);
	} else if (cpu_is_davinci_dm355()) {
		unsigned long postdiv;

		postdiv = (davinci_readl(DAVINCI_PLL_CNTRL0_BASE + 0x128)
			   & 0x1f) + 1;

		fixedrate = 24000000;
		armrate = (pll_mult + 1) * (fixedrate / (16 * postdiv));
		commonrate = armrate / 2;
		board_clks = davinci_dm355_clks;
		num_clks = ARRAY_SIZE(davinci_dm355_clks);
	} else {
		fixedrate = DM646X_OSC_FREQ;
		armrate = (pll_mult + 1) * (fixedrate / 2);
		commonrate = armrate / 3;

		board_clks = davinci_clks;
		num_clks = ARRAY_SIZE(davinci_clks);
	}

	for (clkp = board_clks; count < num_clks; count++, clkp++) {
		clk_register(clkp);

		/* Turn on clocks that have been enabled in the
		 * table above */
		if (clkp->usecount)
			clk_enable(clkp);
	}

	return 0;
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void *davinci_ck_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *davinci_ck_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void davinci_ck_stop(struct seq_file *m, void *v)
{
}

static int davinci_ck_show(struct seq_file *m, void *v)
{
	struct clk *cp;

	list_for_each_entry(cp, &clocks, node)
		seq_printf(m,"%s %d %d\n", cp->name, *(cp->rate), cp->usecount);

	return 0;
}

static const struct seq_operations davinci_ck_op = {
	.start	= davinci_ck_start,
	.next	= davinci_ck_next,
	.stop	= davinci_ck_stop,
	.show	= davinci_ck_show
};

static int davinci_ck_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &davinci_ck_op);
}

static const struct file_operations proc_davinci_ck_operations = {
	.open		= davinci_ck_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init davinci_ck_proc_init(void)
{
	proc_create("davinci_clocks", 0, NULL, &proc_davinci_ck_operations);
	return 0;

}
__initcall(davinci_ck_proc_init);
#endif /* CONFIG_DEBUG_PROC_FS */
