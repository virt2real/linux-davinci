/*
 * TI DaVinci Power and Sleep Controller (PSC)
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/cpu.h>
#include <mach/hardware.h>
#include <mach/psc.h>
#include <mach/mux.h>

#define DAVINCI_PWR_SLEEP_CNTRL_BASE 0x01C41000

/* PSC register offsets */
#define EPCPR		0x070
#define PTCMD		0x120
#define PTSTAT		0x128
#define PDSTAT		0x200
#define PDCTL1		0x304
#define MDSTAT		0x800
#define MDCTL		0xA00

/* System control register offsets */
#define VDD3P3V_PWDN	0x48

static void (*davinci_psc_mux)(unsigned int id);

static void dm6446_psc_mux(unsigned int id)
{
	void __iomem *base = IO_ADDRESS(DAVINCI_SYSTEM_MODULE_BASE);

	switch (id) {
	case DAVINCI_LPSC_MMC_SD:
		/* VDD power manupulations are done in U-Boot for CPMAC
		 * so applies to MMC as well
		 */
		/*Set up the pull regiter for MMC */
		__raw_writel(0, base + VDD3P3V_PWDN);
		davinci_cfg_reg(DM644X_MSTK);
		break;
	case DAVINCI_LPSC_I2C:
		davinci_cfg_reg(DM644X_I2C);
		break;
	case DAVINCI_LPSC_McBSP:
		davinci_cfg_reg(DM644X_MCBSP);
		break;
	case DAVINCI_LPSC_VLYNQ:
		davinci_cfg_reg(DM644X_VLYNQEN);
		davinci_cfg_reg(DM644X_VLYNQWD);
		break;
	default:
		break;
	}
}

#define DM355_ARM_PINMUX3	0x0c
#define DM355_ARM_PINMUX4	0x10
#define DM355_ARM_INTMUX	0x18
#define DM355_EDMA_EVTMUX	0x1c

static void dm355_psc_mux(unsigned int id)
{
	u32	tmp;
	void __iomem *base = IO_ADDRESS(DAVINCI_SYSTEM_MODULE_BASE);

	/* REVISIT mixing pinmux with PSC setup seems pretty dubious,
	 * especially in cases like ASP0 where there are valid partial
	 * functionality use cases ... like half duplex links.  Best
	 * probably to do all this as part of platform_device setup,
	 * while declaring what pins/irqs/edmas/... we care about.
	 */
	switch (id) {
	case DM355_LPSC_McBSP1:		/* ASP1 */
		/* our ASoC code currently doesn't use these IRQs */
#if 0
		/* deliver ASP1_XINT and ASP1_RINT */
		tmp = __raw_readl(base + DM355_ARM_INTMUX);
		tmp |= BIT(6) | BIT(5);
		__raw_writel(tmp, base + DM355_ARM_INTMUX);
#endif

		/* support EDMA for ASP1_RX and ASP1_TX */
		tmp = __raw_readl(base + DM355_EDMA_EVTMUX);
		tmp &= ~(BIT(1) | BIT(0));
		__raw_writel(tmp, base + DM355_EDMA_EVTMUX);
		break;
	}
}

static void nop_psc_mux(unsigned int id)
{
	/* nothing */
}

/* Enable or disable a PSC domain */
void davinci_psc_config(unsigned int domain, unsigned int id, char enable)
{
	u32 epcpr, ptcmd, ptstat, pdstat, pdctl1, mdstat, mdctl, mdstat_mask;
	void __iomem *psc_base = IO_ADDRESS(DAVINCI_PWR_SLEEP_CNTRL_BASE);

	mdctl = __raw_readl(psc_base + MDCTL + 4 * id);
	if (enable)
		mdctl |= 0x00000003;	/* Enable Module */
	else
		mdctl &= 0xFFFFFFF2;	/* Disable Module */
	__raw_writel(mdctl, psc_base + MDCTL + 4 * id);

	pdstat = __raw_readl(psc_base + PDSTAT);
	if ((pdstat & 0x00000001) == 0) {
		pdctl1 = __raw_readl(psc_base + PDCTL1);
		pdctl1 |= 0x1;
		__raw_writel(pdctl1, psc_base + PDCTL1);

		ptcmd = 1 << domain;
		__raw_writel(ptcmd, psc_base + PTCMD);

		do {
			epcpr = __raw_readl(psc_base + EPCPR);
		} while ((((epcpr >> domain) & 1) == 0));

		pdctl1 = __raw_readl(psc_base + PDCTL1);
		pdctl1 |= 0x100;
		__raw_writel(pdctl1, psc_base + PDCTL1);

		do {
			ptstat = __raw_readl(psc_base +
					       PTSTAT);
		} while (!(((ptstat >> domain) & 1) == 0));
	} else {
		ptcmd = 1 << domain;
		__raw_writel(ptcmd, psc_base + PTCMD);

		do {
			ptstat = __raw_readl(psc_base + PTSTAT);
		} while (!(((ptstat >> domain) & 1) == 0));
	}

	if (enable)
		mdstat_mask = 0x3;
	else
		mdstat_mask = 0x2;

	do {
		mdstat = __raw_readl(psc_base + MDSTAT + 4 * id);
	} while (!((mdstat & 0x0000001F) == mdstat_mask));

	if (enable)
		davinci_psc_mux(id);
}

void __init davinci_psc_init(void)
{
	if (cpu_is_davinci_dm644x() || cpu_is_davinci_dm646x()) {
		davinci_psc_mux = dm6446_psc_mux;
	} else if (cpu_is_davinci_dm355()) {
		davinci_psc_mux = dm355_psc_mux;
	} else {
		pr_err("PSC: no PSC mux hooks for this CPU\n");
		davinci_psc_mux = nop_psc_mux;
	}
}
