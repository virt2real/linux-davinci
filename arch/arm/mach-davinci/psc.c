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
	switch (id) {
	case DAVINCI_LPSC_ATA:
		davinci_mux_peripheral(DAVINCI_MUX_HDIREN, 1);
		davinci_mux_peripheral(DAVINCI_MUX_ATAEN, 1);
		break;
	case DAVINCI_LPSC_MMC_SD:
		/* VDD power manupulations are done in U-Boot for CPMAC
		 * so applies to MMC as well
		 */
		/*Set up the pull regiter for MMC */
		davinci_writel(0, DAVINCI_SYSTEM_MODULE_BASE + VDD3P3V_PWDN);
		davinci_mux_peripheral(DAVINCI_MUX_MSTK, 0);
		break;
	case DAVINCI_LPSC_I2C:
		davinci_mux_peripheral(DAVINCI_MUX_I2C, 1);
		break;
	case DAVINCI_LPSC_McBSP:
		davinci_mux_peripheral(DAVINCI_MUX_ASP, 1);
		break;
	default:
		break;
	}
}

#define DM355_ARM_PINMUX3	(DAVINCI_SYSTEM_MODULE_BASE + 0x0c)
#define DM355_ARM_PINMUX4	(DAVINCI_SYSTEM_MODULE_BASE + 0x10)
#define DM355_ARM_INTMUX	(DAVINCI_SYSTEM_MODULE_BASE + 0x18)
#define DM355_EDMA_EVTMUX	(DAVINCI_SYSTEM_MODULE_BASE + 0x1c)

static void dm355_psc_mux(unsigned int id)
{
	u32	tmp;

	/* REVISIT mixing pinmux with PSC setup seems pretty dubious,
	 * especially in cases like ASP0 where there are valid partial
	 * functionality use cases ... like half duplex links.  Best
	 * probably to do all this as part of platform_device setup,
	 * while declaring what pins/irqs/edmas/... we care about.
	 */
	switch (id) {
	case DM355_LPSC_MMC_SD1:	/* MMC1 */
		/* expose DATA[0..3], CMD, CLK */
		tmp = davinci_readl(DM355_ARM_PINMUX3);
		tmp &= ~((3 << 14) | (3 << 12) | (3 << 10) | (3 << 8));
		tmp |=   (1 << 14) | (1 << 12) | (1 << 10) | (1 << 8)
				| BIT(7) | BIT(6);
		davinci_writel(tmp, DM355_ARM_PINMUX3);
		break;
	case DM355_LPSC_McBSP1:		/* ASP1 */
		/* our ASoC code currently doesn't use these IRQs */
#if 0
		/* deliver ASP1_XINT and ASP1_RINT */
		tmp = davinci_readl(DM355_ARM_INTMUX);
		tmp |= BIT(6) | BIT(5);
		davinci_writel(tmp, DM355_ARM_INTMUX);
#endif

		/* support EDMA for ASP1_RX and ASP1_TX */
		tmp = davinci_readl(DM355_EDMA_EVTMUX);
		tmp &= ~(BIT(1) | BIT(0));
		davinci_writel(tmp, DM355_EDMA_EVTMUX);
		break;
	case DAVINCI_LPSC_MMC_SD:	/* MMC0 */
		/* expose all 6 MMC0 signals:  CLK, CMD, DATA[0..3] */
		tmp = davinci_readl(DM355_ARM_PINMUX4);
		tmp &= ~BIT(2);
		davinci_writel(tmp, DM355_ARM_PINMUX4);

		/* support EMDA for MMC0 RX */
		tmp = davinci_readl(DM355_EDMA_EVTMUX);
		tmp &= ~BIT(2);
		davinci_writel(tmp, DM355_EDMA_EVTMUX);
		break;
	case DAVINCI_LPSC_SPI:			/* SPI0 */
		/* expose SPI0_SDI
		 * NOTE: SPIO_SDENA0 and/or SPIO_SDENA1
		 * will need to be set too.
		 */
		tmp = davinci_readl(DM355_ARM_PINMUX4);
		tmp &= ~BIT(1);
		davinci_writel(tmp, DM355_ARM_PINMUX4);
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

	mdctl = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE + MDCTL + 4 * id);
	if (enable)
		mdctl |= 0x00000003;	/* Enable Module */
	else
		mdctl &= 0xFFFFFFF2;	/* Disable Module */
	davinci_writel(mdctl, DAVINCI_PWR_SLEEP_CNTRL_BASE + MDCTL + 4 * id);

	pdstat = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE + PDSTAT);
	if ((pdstat & 0x00000001) == 0) {
		pdctl1 = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE + PDCTL1);
		pdctl1 |= 0x1;
		davinci_writel(pdctl1, DAVINCI_PWR_SLEEP_CNTRL_BASE + PDCTL1);

		ptcmd = 1 << domain;
		davinci_writel(ptcmd, DAVINCI_PWR_SLEEP_CNTRL_BASE + PTCMD);

		do {
			epcpr = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE +
					      EPCPR);
		} while ((((epcpr >> domain) & 1) == 0));

		pdctl1 = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE + PDCTL1);
		pdctl1 |= 0x100;
		davinci_writel(pdctl1, DAVINCI_PWR_SLEEP_CNTRL_BASE + PDCTL1);

		do {
			ptstat = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE +
					       PTSTAT);
		} while (!(((ptstat >> domain) & 1) == 0));
	} else {
		ptcmd = 1 << domain;
		davinci_writel(ptcmd, DAVINCI_PWR_SLEEP_CNTRL_BASE + PTCMD);

		do {
			ptstat = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE +
					       PTSTAT);
		} while (!(((ptstat >> domain) & 1) == 0));
	}

	if (enable)
		mdstat_mask = 0x3;
	else
		mdstat_mask = 0x2;

	do {
		mdstat = davinci_readl(DAVINCI_PWR_SLEEP_CNTRL_BASE +
				       MDSTAT + 4 * id);
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

	if (cpu_is_davinci_dm644x() || cpu_is_davinci_dm355()) {
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_VPSSMSTR, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_VPSSSLV, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_TPCC, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_TPTC0, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_TPTC1, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_GPIO, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_USB, 1);

		/* Turn on WatchDog timer LPSC.	 Needed for RESET to work */
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DAVINCI_LPSC_TIMER2, 1);
	} else if (cpu_is_davinci_dm646x()) {
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DM646X_LPSC_AEMIF, 1);
		davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN,
					DM646X_LPSC_GPIO, 1);
	}
}
