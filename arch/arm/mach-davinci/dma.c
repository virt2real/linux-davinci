/*
 * arch/arm/mach-davinci/dma.c - EDMA3 support for DaVinci
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/io.h>

#include <mach/cpu.h>
#include <mach/memory.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/edma.h>
#include <mach/mux.h>


/* Offsets matching "struct edmacc_param" */
#define PARM_OPT		0x00
#define PARM_SRC		0x04
#define PARM_A_B_CNT		0x08
#define PARM_DST		0x0c
#define PARM_SRC_DST_BIDX	0x10
#define PARM_LINK_BCNTRLD	0x14
#define PARM_SRC_DST_CIDX	0x18
#define PARM_CCNT		0x1c

#define PARM_SIZE		0x20

/* Offsets for EDMA CC global channel registers and their shadows */
#define SH_ER		0x00	/* 64 bits */
#define SH_ECR		0x08	/* 64 bits */
#define SH_ESR		0x10	/* 64 bits */
#define SH_CER		0x18	/* 64 bits */
#define SH_EER		0x20	/* 64 bits */
#define SH_EECR		0x28	/* 64 bits */
#define SH_EESR		0x30	/* 64 bits */
#define SH_SER		0x38	/* 64 bits */
#define SH_SECR		0x40	/* 64 bits */
#define SH_IER		0x50	/* 64 bits */
#define SH_IECR		0x58	/* 64 bits */
#define SH_IESR		0x60	/* 64 bits */
#define SH_IPR		0x68	/* 64 bits */
#define SH_ICR		0x70	/* 64 bits */
#define SH_IEVAL	0x78
#define SH_QER		0x80
#define SH_QEER		0x84
#define SH_QEECR	0x88
#define SH_QEESR	0x8c
#define SH_QSER		0x90
#define SH_QSECR	0x94
#define SH_SIZE		0x200

/* Offsets for EDMA CC global registers */
#define EDMA_REV	0x0000
#define EDMA_CCCFG	0x0004
#define EDMA_QCHMAP	0x0200	/* 8 registers */
#define EDMA_DMAQNUM	0x0240	/* 8 registers (4 on OMAP-L1xx) */
#define EDMA_QDMAQNUM	0x0260
#define EDMA_QUETCMAP	0x0280
#define EDMA_QUEPRI	0x0284
#define EDMA_EMR	0x0300	/* 64 bits */
#define EDMA_EMCR	0x0308	/* 64 bits */
#define EDMA_QEMR	0x0310
#define EDMA_QEMCR	0x0314
#define EDMA_CCERR	0x0318
#define EDMA_CCERRCLR	0x031c
#define EDMA_EEVAL	0x0320
#define EDMA_DRAE	0x0340	/* 4 x 64 bits*/
#define EDMA_QRAE	0x0380	/* 4 registers */
#define EDMA_QUEEVTENTRY	0x0400	/* 2 x 16 registers */
#define EDMA_QSTAT	0x0600	/* 2 registers */
#define EDMA_QWMTHRA	0x0620
#define EDMA_QWMTHRB	0x0624
#define EDMA_CCSTAT	0x0640

#define EDMA_M		0x1000	/* global channel registers */
#define EDMA_SHADOW0	0x2000	/* 4 regions shadowing global channels */
#define EDMA_PARM	0x4000	/* 128 param entries */

#define DAVINCI_DMA_3PCC_BASE	0x01C00000

#define PARM_OFFSET(param_no)	(EDMA_PARM + ((param_no) << 5))

static const void __iomem *edmacc_regs_base = IO_ADDRESS(DAVINCI_DMA_3PCC_BASE);

/*****************************************************************************/

static inline unsigned int edma_read(int offset)
{
	return (unsigned int)__raw_readl(edmacc_regs_base + offset);
}

static inline void edma_write(int offset, int val)
{
	__raw_writel(val, edmacc_regs_base + offset);
}
static inline void edma_modify(int offset, unsigned and, unsigned or)
{
	unsigned val = edma_read(offset);
	val &= and;
	val |= or;
	edma_write(offset, val);
}
static inline void edma_and(int offset, unsigned and)
{
	unsigned val = edma_read(offset);
	val &= and;
	edma_write(offset, val);
}
static inline void edma_or(int offset, unsigned or)
{
	unsigned val = edma_read(offset);
	val |= or;
	edma_write(offset, val);
}
static inline unsigned int edma_read_array(int offset, int i)
{
	return edma_read(offset + (i << 2));
}
static inline void edma_write_array(int offset, int i, unsigned val)
{
	edma_write(offset + (i << 2), val);
}
static inline void edma_modify_array(int offset, int i,
		unsigned and, unsigned or)
{
	edma_modify(offset + (i << 2), and, or);
}
static inline void edma_or_array(int offset, int i, unsigned or)
{
	edma_or(offset + (i << 2), or);
}
static inline void edma_or_array2(int offset, int i, int j, unsigned or)
{
	edma_or(offset + ((i*2 + j) << 2), or);
}
static inline void edma_write_array2(int offset, int i, int j, unsigned val)
{
	edma_write(offset + ((i*2 + j) << 2), val);
}
static inline unsigned int edma_shadow0_read(int offset)
{
	return edma_read(EDMA_SHADOW0 + offset);
}
static inline unsigned int edma_shadow0_read_array(int offset, int i)
{
	return edma_read(EDMA_SHADOW0 + offset + (i << 2));
}
static inline void edma_shadow0_write(int offset, unsigned val)
{
	edma_write(EDMA_SHADOW0 + offset, val);
}
static inline void edma_shadow0_write_array(int offset, int i, unsigned val)
{
	edma_write(EDMA_SHADOW0 + offset + (i << 2), val);
}
static inline unsigned int edma_parm_read(int offset, int param_no)
{
	return edma_read(EDMA_PARM + offset + (param_no << 5));
}
static inline void edma_parm_write(int offset, int param_no, unsigned val)
{
	edma_write(EDMA_PARM + offset + (param_no << 5), val);
}
static inline void edma_parm_modify(int offset, int param_no,
		unsigned and, unsigned or)
{
	edma_modify(EDMA_PARM + offset + (param_no << 5), and, or);
}
static inline void edma_parm_and(int offset, int param_no, unsigned and)
{
	edma_and(EDMA_PARM + offset + (param_no << 5), and);
}
static inline void edma_parm_or(int offset, int param_no, unsigned or)
{
	edma_or(EDMA_PARM + offset + (param_no << 5), or);
}

/*****************************************************************************/

static struct platform_driver edma_driver = {
	.driver.name	= "edma",
};

static struct resource edma_resources[] = {
	{
		.start	= DAVINCI_DMA_3PCC_BASE,
		.end	= DAVINCI_DMA_3PCC_BASE + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc0",
		.start	= 0x01c10000,
		.end	= 0x01c10000 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc1",
		.start	= 0x01c10400,
		.end	= 0x01c10400 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device edma_dev = {
	.name		= "edma",
	.id		= -1,
	.dev.driver	= &edma_driver.driver,
	.num_resources	= ARRAY_SIZE(edma_resources),
	.resource	= edma_resources,
};

/*****************************************************************************/

static struct dma_interrupt_data {
	void (*callback) (int lch, unsigned short ch_status, void *data);
	void *data;
} intr_data[DAVINCI_EDMA_NUM_DMACH];

/* The edma_inuse bit for each PaRAM slot is clear unless the
 * channel is in use ... by ARM or DSP, for QDMA, or whatever.
 */
static DECLARE_BITMAP(edma_inuse, DAVINCI_EDMA_NUM_PARAMENTRY);

/* The edma_noevent bit for each master channel is clear unless
 * it doesn't trigger DMA events on this platform.  It uses a
 * bit of SOC-specific initialization code.
 */
static DECLARE_BITMAP(edma_noevent, DAVINCI_EDMA_NUM_DMACH);

static const s8 __initconst dma_chan_dm644x_no_event[] = {
	0, 1, 12, 13, 14, 15, 25, 30, 31, 45, 46, 47, 55, 56, 57, 58, 59, 60,
	61, 62, 63, -1
};
static const s8 __initconst dma_chan_dm355_no_event[] = {
	12, 13, 24, 56, 57, 58, 59, 60, 61, 62, 63, -1
};

static const int __initconst
queue_tc_mapping[DAVINCI_EDMA_NUM_EVQUE + 1][2] = {
/* {event queue no, TC no} */
	{0, 0},
	{1, 1},
	{-1, -1}
};

static const int __initconst
queue_priority_mapping[DAVINCI_EDMA_NUM_EVQUE + 1][2] = {
	/* {event queue no, Priority} */
	{0, 3},
	{1, 7},
	{-1, -1}
};

/*****************************************************************************/

static void map_dmach_queue(int ch_no, int queue_no)
{
	queue_no &= 7;
	if (ch_no < DAVINCI_EDMA_NUM_DMACH) {
		int bit = (ch_no & 0x7) * 4;
		edma_modify_array(EDMA_DMAQNUM, (ch_no >> 3),
				~(0x7 << bit), queue_no << bit);
	}
}

static void __init map_queue_tc(int queue_no, int tc_no)
{
	int bit = queue_no * 4;
	edma_modify(EDMA_QUETCMAP, ~(0x7 << bit), ((tc_no & 0x7) << bit));
}

static void __init assign_priority_to_queue(int queue_no, int priority)
{
	int bit = queue_no * 4;
	edma_modify(EDMA_QUEPRI, ~(0x7 << bit), ((priority & 0x7) << bit));
}

static inline void
setup_dma_interrupt(unsigned lch,
	void (*callback)(int lch, unsigned short ch_status, void *data),
	void *data)
{
	if (!callback) {
		edma_shadow0_write_array(SH_IECR, lch >> 5,
				(1 << (lch & 0x1f)));
	}

	intr_data[lch].callback = callback;
	intr_data[lch].data = data;

	if (callback) {
		edma_shadow0_write_array(SH_ICR, lch >> 5,
				(1 << (lch & 0x1f)));
		edma_shadow0_write_array(SH_IESR, lch >> 5,
				(1 << (lch & 0x1f)));
	}
}

/**
 * davinci_dma_getposition - returns the current transfer points
 * @lch: logical channel number
 * @src: pointer to source port position
 * @dst: pointer to destination port position
 *
 * Returns current source and destination address of a particular
 * DMA channel.  The channel should not be active when this is called.
 */
void davinci_dma_getposition(int lch, dma_addr_t *src, dma_addr_t *dst)
{
	struct edmacc_param temp;

	davinci_get_dma_params(lch, &temp);
	if (src != NULL)
		*src = temp.src;
	if (dst != NULL)
		*dst = temp.dst;
}
EXPORT_SYMBOL(davinci_dma_getposition);

/******************************************************************************
 *
 * DMA interrupt handler
 *
 *****************************************************************************/
static irqreturn_t dma_irq_handler(int irq, void *data)
{
	int i;
	unsigned int cnt = 0;

	dev_dbg(&edma_dev.dev, "dma_irq_handler\n");

	if ((edma_shadow0_read_array(SH_IPR, 0) == 0)
	    && (edma_shadow0_read_array(SH_IPR, 1) == 0))
		return IRQ_NONE;

	while (1) {
		int j;
		if (edma_shadow0_read_array(SH_IPR, 0))
			j = 0;
		else if (edma_shadow0_read_array(SH_IPR, 1))
			j = 1;
		else
			break;
		dev_dbg(&edma_dev.dev, "IPR%d %08x\n", j,
				edma_shadow0_read_array(SH_IPR, j));
		for (i = 0; i < 32; i++) {
			int k = (j << 5) + i;
			if (edma_shadow0_read_array(SH_IPR, j) & (1 << i)) {
				/* Clear the corresponding IPR bits */
				edma_shadow0_write_array(SH_ICR, j, (1 << i));
				if (intr_data[k].callback) {
					intr_data[k].callback(k, DMA_COMPLETE,
						intr_data[k].data);
				}
			}
		}
		cnt++;
		if (cnt > 10)
			break;
	}
	edma_shadow0_write(SH_IEVAL, 1);
	return IRQ_HANDLED;
}

/******************************************************************************
 *
 * DMA error interrupt handler
 *
 *****************************************************************************/
static irqreturn_t dma_ccerr_handler(int irq, void *data)
{
	int i;
	unsigned int cnt = 0;

	dev_dbg(&edma_dev.dev, "dma_ccerr_handler\n");

	if ((edma_read_array(EDMA_EMR, 0) == 0) &&
	    (edma_read_array(EDMA_EMR, 1) == 0) &&
	    (edma_read(EDMA_QEMR) == 0) && (edma_read(EDMA_CCERR) == 0))
		return IRQ_NONE;

	while (1) {
		int j = -1;
		if (edma_read_array(EDMA_EMR, 0))
			j = 0;
		else if (edma_read_array(EDMA_EMR, 1))
			j = 1;
		if (j >= 0) {
			dev_dbg(&edma_dev.dev, "EMR%d %08x\n", j,
					edma_read_array(EDMA_EMR, j));
			for (i = 0; i < 32; i++) {
				int k = (j << 5) + i;
				if (edma_read_array(EDMA_EMR, j) & (1 << i)) {
					/* Clear the corresponding EMR bits */
					edma_write_array(EDMA_EMCR, j, 1 << i);
					/* Clear any SER */
					edma_shadow0_write_array(SH_SECR, j,
							(1 << i));
					if (intr_data[k].callback) {
						intr_data[k].callback(k,
								DMA_CC_ERROR,
								intr_data
								[k].data);
					}
				}
			}
		} else if (edma_read(EDMA_QEMR)) {
			dev_dbg(&edma_dev.dev, "QEMR %02x\n",
				edma_read(EDMA_QEMR));
			for (i = 0; i < 8; i++) {
				if (edma_read(EDMA_QEMR) & (1 << i)) {
					/* Clear the corresponding IPR bits */
					edma_write(EDMA_QEMCR, 1 << i);
					edma_shadow0_write(SH_QSECR, (1 << i));

					/* NOTE:  not reported!! */
				}
			}
		} else if (edma_read(EDMA_CCERR)) {
			dev_dbg(&edma_dev.dev, "CCERR %08x\n",
				edma_read(EDMA_CCERR));
			/* FIXME:  CCERR.BIT(16) ignored!  much better
			 * to just write CCERRCLR with CCERR value...
			 */
			for (i = 0; i < 8; i++) {
				if (edma_read(EDMA_CCERR) & (1 << i)) {
					/* Clear the corresponding IPR bits */
					edma_write(EDMA_CCERRCLR, 1 << i);

					/* NOTE:  not reported!! */
				}
			}
		}
		if ((edma_read_array(EDMA_EMR, 0) == 0)
		    && (edma_read_array(EDMA_EMR, 1) == 0)
		    && (edma_read(EDMA_QEMR) == 0)
		    && (edma_read(EDMA_CCERR) == 0)) {
			break;
		}
		cnt++;
		if (cnt > 10)
			break;
	}
	edma_write(EDMA_EEVAL, 1);
	return IRQ_HANDLED;
}

/******************************************************************************
 *
 * Transfer controller error interrupt handlers
 *
 *****************************************************************************/

#define tc_errs_handled	false	/* disabled as long as they're NOPs */

static irqreturn_t dma_tc0err_handler(int irq, void *data)
{
	dev_dbg(&edma_dev.dev, "dma_tc0err_handler\n");
	return IRQ_HANDLED;
}

static irqreturn_t dma_tc1err_handler(int irq, void *data)
{
	dev_dbg(&edma_dev.dev, "dma_tc1err_handler\n");
	return IRQ_HANDLED;
}

/******************************************************************************
 *
 * DMA initialisation on davinci
 *
 *****************************************************************************/
static int __init davinci_dma_init(void)
{
	int i;
	int status;
	const s8 *noevent;

	platform_driver_register(&edma_driver);
	platform_device_register(&edma_dev);

	dev_dbg(&edma_dev.dev, "DMA REG BASE ADDR=%p\n", edmacc_regs_base);

	for (i = 0; i < DAVINCI_EDMA_NUM_PARAMENTRY * PARM_SIZE; i += 4)
		edma_write(EDMA_PARM + i, 0);

	if (cpu_is_davinci_dm355()) {
		/* NOTE conflicts with SPI1_INT{0,1} and SPI2_INT0 */
		davinci_cfg_reg(DM355_INT_EDMA_CC);
		if (tc_errs_handled) {
			davinci_cfg_reg(DM355_INT_EDMA_TC0_ERR);
			davinci_cfg_reg(DM355_INT_EDMA_TC1_ERR);
		}
		noevent = dma_chan_dm355_no_event;
	} else if (cpu_is_davinci_dm644x()) {
		noevent = dma_chan_dm644x_no_event;
	} else {
		/* request_dma(DAVINCI_DMA_CHANNEL_ANY) fails */
		noevent = NULL;
	}

	if (noevent) {
		while (*noevent != -1)
			set_bit(*noevent++, edma_noevent);
	}

	status = request_irq(IRQ_CCINT0, dma_irq_handler, 0, "edma", NULL);
	if (status < 0) {
		dev_dbg(&edma_dev.dev, "request_irq %d failed --> %d\n",
			IRQ_CCINT0, status);
		return status;
	}
	status = request_irq(IRQ_CCERRINT, dma_ccerr_handler, 0,
				"edma_error", NULL);
	if (status < 0) {
		dev_dbg(&edma_dev.dev, "request_irq %d failed --> %d\n",
			IRQ_CCERRINT, status);
		return status;
	}

	if (tc_errs_handled) {
		status = request_irq(IRQ_TCERRINT0, dma_tc0err_handler, 0,
					"edma_tc0", NULL);
		if (status < 0) {
			dev_dbg(&edma_dev.dev, "request_irq %d failed --> %d\n",
				IRQ_TCERRINT0, status);
			return status;
		}
		status = request_irq(IRQ_TCERRINT, dma_tc1err_handler, 0,
					"edma_tc1", NULL);
		if (status < 0) {
			dev_dbg(&edma_dev.dev, "request_irq %d --> %d\n",
				IRQ_TCERRINT, status);
			return status;
		}
	}

	/* Everything lives on transfer controller 1 until otherwise specified.
	 * This way, long transfers on the low priority queue
	 * started by the codec engine will not cause audio defects.
	 */
	for (i = 0; i < DAVINCI_EDMA_NUM_DMACH; i++)
		map_dmach_queue(i, 1);

	i = 0;
	/* Event queue to TC mapping */
	while (queue_tc_mapping[i][0] != -1) {
		map_queue_tc(queue_tc_mapping[i][0], queue_tc_mapping[i][1]);
		i++;
	}
	i = 0;
	/* Event queue priority mapping */
	while (queue_priority_mapping[i][0] != -1) {
		assign_priority_to_queue(queue_priority_mapping[i][0],
					 queue_priority_mapping[i][1]);
		i++;
	}
	for (i = 0; i < DAVINCI_EDMA_NUM_REGIONS; i++) {
		edma_write_array2(EDMA_DRAE, i, 0, 0x0);
		edma_write_array2(EDMA_DRAE, i, 1, 0x0);
		edma_write_array(EDMA_QRAE, i, 0x0);
	}
	return 0;
}
arch_initcall(davinci_dma_init);

/**
 * davinci_request_dma - allocate a DMA channel
 * @dev_id: specific DMA channel; else DAVINCI_DMA_CHANNEL_ANY to
 *	allocate some master channel without a hardware event, or
 *	DAVINCI_EDMA_PARAM_ANY to allocate some slave channel.
 * @name: name associated with @dev_id
 * @callback: to be issued on DMA completion or errors (master only)
 * @data: passed to callback (master only)
 * @lch: used to return the number of the allocated event channel; pass
 *	this later to davinci_free_dma()
 * @tcc: may be NULL; else an input for masters, an output for slaves.
 * @eventq_no: an EVENTQ_* constant, used to choose which Transfer
 *	Controller (TC) executes requests on this channel (master only)
 *
 * Returns zero on success, else negative errno.
 *
 * The @tcc parameter may be null, indicating default behavior:  no
 * transfer completion callbacks are issued, but masters use @callback
 * and @data (if provided) to report transfer errors.  Else masters use
 * it as an output, returning either what @lch returns (and enabling
 * transfer completion interrupts), or TCC_ANY if there is no callback.
 * Slaves use @tcc as an input:  TCC_ANY gives the default behavior,
 * else it specifies a transfer completion @callback to be used.
 *
 * These TCC settings are stored in PaRAM slots, so they may be updated
 * later.  In particular, reloading a master PaRAM entry from a slave
 * (via linking) overwrites everything, including those TCC settings.
 *
 * DMA transfers start from a master channel using davinci_start_dma()
 * or by chaining.  When the transfer described in that master's PaRAM
 * slot completes, its PaRAM data may be reloaded from a linked slave.
 *
 * DMA errors are only reported to the @callback associated with that
 * master channel, but transfer completion callbacks can be sent to
 * another master channel.  Drivers must not use DMA transfer completion
 * callbacks (@tcc) for master channels they did not allocate.  (The
 * same applies to transfer chaining, since the same @tcc codes are
 * used both to trigger completion interrupts and to chain transfers.)
 */
int davinci_request_dma(int dev_id, const char *name,
			void (*callback) (int lch, unsigned short ch_status,
					  void *data),
			void *data, int *lch,
			int *tcc, enum dma_event_q eventq_no)
{
	int tcc_val = tcc ? *tcc : TCC_ANY;

	/* REVISIT:  tcc would be better as a non-pointer parameter */
	switch (tcc_val) {
	case TCC_ANY:
	case 0 ... DAVINCI_EDMA_NUM_DMACH - 1:
		break;
	default:
		return -EINVAL;
	}

	switch (dev_id) {

	/* Allocate a specific master channel, e.g. for MMC1 RX or ASP0 TX */
	case 0 ... DAVINCI_EDMA_NUM_DMACH - 1:
		if (test_and_set_bit(dev_id, edma_inuse))
			return -EBUSY;

alloc_master:
		tcc_val = (tcc && callback) ? dev_id : TCC_ANY;

		/* ensure access through shadow region 0 */
		edma_or_array2(EDMA_DRAE, 0, dev_id >> 5,
				1 << (dev_id & 0x1f));

		if (callback)
			setup_dma_interrupt(dev_id, callback, data);

		map_dmach_queue(dev_id, eventq_no);

		/* ensure no events are pending */
		davinci_stop_dma(dev_id);
		break;

	/* Allocate a specific slave channel, mostly to reserve it
	 * as part of a set of resources allocated to a DSP.
	 */
	case DAVINCI_EDMA_NUM_DMACH ... DAVINCI_EDMA_NUM_PARAMENTRY - 1:
		if (test_and_set_bit(dev_id, edma_inuse))
			return -EBUSY;
		break;

	/* return some master channel with no event association */
	case DAVINCI_DMA_CHANNEL_ANY:
		dev_id = 0;
		for (;;) {
			dev_id = find_next_bit(edma_noevent,
					DAVINCI_EDMA_NUM_DMACH, dev_id);
			if (dev_id == DAVINCI_EDMA_NUM_DMACH)
				return -ENOMEM;
			if (!test_and_set_bit(dev_id, edma_inuse))
				goto alloc_master;
		}
		break;

	/* return some slave channel */
	case DAVINCI_EDMA_PARAM_ANY:
		dev_id = DAVINCI_EDMA_NUM_DMACH;
		for (;;) {
			dev_id = find_next_zero_bit(edma_inuse,
					DAVINCI_EDMA_NUM_PARAMENTRY, dev_id);
			if (dev_id == DAVINCI_EDMA_NUM_PARAMENTRY)
				return -ENOMEM;
			if (!test_and_set_bit(dev_id, edma_inuse))
				break;
		}
		break;

	default:
		return -EINVAL;
	}

	/* Optionally fire Transfer Complete interrupts.
	 *
	 * REVISIT: probably worth zeroing the whole PaRAM
	 * structure, so other flag values (like ITCINTEN
	 * and chaining options) are defined...
	 */
	if (tcc_val != TCC_ANY)
		edma_parm_modify(PARM_OPT, dev_id, ~TCC,
			((0x3f & tcc_val) << 12) | TCINTEN);
	else
		edma_parm_and(PARM_OPT, dev_id, ~TCINTEN);

	/* init the link field to no link. i.e 0xffff */
	edma_parm_or(PARM_LINK_BCNTRLD, dev_id, 0xffff);

	/* non-status return values */
	*lch = dev_id;
	if (tcc)
		*tcc = tcc_val;

	dev_dbg(&edma_dev.dev, "alloc lch %d, tcc %d\n", dev_id, tcc_val);
	return 0;
}
EXPORT_SYMBOL(davinci_request_dma);

/**
 * davinci_free_dma - deallocate a DMA channel
 * @lch: dma channel returned from davinci_request_dma()
 *
 * This deallocates the resources allocated by davinci_request_dma().
 * Callers are responsible for ensuring the channel is inactive, and
 * will not be reactivated by linking, chaining, or software calls to
 * davinci_start_dma().
 */
void davinci_free_dma(int lch)
{
	if (lch < 0 || lch >= DAVINCI_EDMA_NUM_PARAMENTRY)
		return;

	if (lch < DAVINCI_EDMA_NUM_DMACH) {
		setup_dma_interrupt(lch, NULL, NULL);
		/* REVISIT should probably take out shadow region 0 */
	}

	clear_bit(lch, edma_inuse);
}
EXPORT_SYMBOL(davinci_free_dma);

/**
 * davinci_set_dma_src_params - set initial DMA source address in PaRAM
 * @lch: logical channel being configured
 * @src_port: physical address of source (memory, controller FIFO, etc)
 * @addressMode: INCR, except in very rare cases
 * @fifoWidth: ignored unless @addressMode is FIFO, else specifies the
 *	width to use when addressing the fifo (e.g. W8BIT, W32BIT)
 *
 * Note that the source address is modified during the DMA transfer
 * according to davinci_set_dma_src_index().
 */
void davinci_set_dma_src_params(int lch, dma_addr_t src_port,
				enum address_mode mode, enum fifo_width width)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		int j = lch;
		unsigned int i = edma_parm_read(PARM_OPT, j);

		if (mode) {
			/* set SAM and program FWID */
			i = (i & ~(EDMA_FWID)) | (SAM | ((width & 0x7) << 8));
		} else {
			/* clear SAM */
			i &= ~SAM;
		}
		edma_parm_write(PARM_OPT, j, i);

		/* set the source port address
		   in source register of param structure */
		edma_parm_write(PARM_SRC, j, src_port);
	}
}
EXPORT_SYMBOL(davinci_set_dma_src_params);

/**
 * davinci_set_dma_dest_params - set initial DMA destination address in PaRAM
 * @lch: logical channel being configured
 * @dest_port: physical address of destination (memory, controller FIFO, etc)
 * @addressMode: INCR, except in very rare cases
 * @fifoWidth: ignored unless @addressMode is FIFO, else specifies the
 *	width to use when addressing the fifo (e.g. W8BIT, W32BIT)
 *
 * Note that the destination address is modified during the DMA transfer
 * according to davinci_set_dma_dest_index().
 */
void davinci_set_dma_dest_params(int lch, dma_addr_t dest_port,
				 enum address_mode mode, enum fifo_width width)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		int j = lch;
		unsigned int i = edma_parm_read(PARM_OPT, j);

		if (mode) {
			/* set DAM and program FWID */
			i = (i & ~(EDMA_FWID)) | (DAM | ((width & 0x7) << 8));
		} else {
			/* clear DAM */
			i &= ~DAM;
		}
		edma_parm_write(PARM_OPT, j, i);
		/* set the destination port address
		   in dest register of param structure */
		edma_parm_write(PARM_DST, j, dest_port);
	}
}
EXPORT_SYMBOL(davinci_set_dma_dest_params);

/**
 * davinci_set_dma_src_index - configure DMA source address indexing
 * @lch: logical channel being configured
 * @src_bidx: byte offset between source arrays in a frame
 * @src_cidx: byte offset between source frames in a block
 *
 * Offsets are specified to support either contiguous or discontiguous
 * memory transfers, or repeated access to a hardware register, as needed.
 * When accessing hardware registers, both offsets are normally zero.
 */
void davinci_set_dma_src_index(int lch, s16 src_bidx, s16 src_cidx)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		edma_parm_modify(PARM_SRC_DST_BIDX, lch,
				0xffff0000, src_bidx);
		edma_parm_modify(PARM_SRC_DST_CIDX, lch,
				0xffff0000, src_cidx);
	}
}
EXPORT_SYMBOL(davinci_set_dma_src_index);

/**
 * davinci_set_dma_dest_index - configure DMA destination address indexing
 * @lch: logical channel being configured
 * @dest_bidx: byte offset between destination arrays in a frame
 * @dest_cidx: byte offset between destination frames in a block
 *
 * Offsets are specified to support either contiguous or discontiguous
 * memory transfers, or repeated access to a hardware register, as needed.
 * When accessing hardware registers, both offsets are normally zero.
 */
void davinci_set_dma_dest_index(int lch, s16 dest_bidx, s16 dest_cidx)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		edma_parm_modify(PARM_SRC_DST_BIDX, lch,
				0x0000ffff, dest_bidx << 16);
		edma_parm_modify(PARM_SRC_DST_CIDX, lch,
				0x0000ffff, dest_cidx << 16);
	}
}
EXPORT_SYMBOL(davinci_set_dma_dest_index);

/**
 * davinci_set_dma_transfer_params - configure DMA transfer parameters
 * @lch: logical channel being configured
 * @acnt: how many bytes per array (at least one)
 * @bcnt: how many arrays per frame (at least one)
 * @ccnt: how many frames per block (at least one)
 * @bcnt_rld: used only for A-Synchronized transfers; this specifies
 *	the value to reload into bcnt when it decrements to zero
 * @sync_mode: ASYNC or ABSYNC
 *
 * See the EDMA3 documentation to understand how to configure and link
 * transfers using the fields in PaRAM slots.  If you are not doing it
 * all at once with davinci_set_dma_params() you will use this routine
 * plus two calls each for source and destination, setting the initial
 * address and saying how to index that address.
 *
 * An example of an A-Synchronized transfer is a serial link using a
 * single word shift register.  In that case, @acnt would be equal to
 * that word size; the serial controller issues a DMA synchronization
 * event to transfer each word, and memory access by the DMA transfer
 * controller will be word-at-a-time.
 *
 * An example of an AB-Synchronized transfer is a device using a FIFO.
 * In that case, @acnt equals the FIFO width and @bcnt equals its depth.
 * The controller with the FIFO issues DMA synchronization events when
 * the FIFO threshold is reached, and the DMA transfer controller will
 * transfer one frame to (or from) the FIFO.  It will probably use
 * efficient burst modes to access memory.
 */
void davinci_set_dma_transfer_params(int lch,
		u16 acnt, u16 bcnt, u16 ccnt,
		u16 bcnt_rld, enum sync_dimension sync_mode)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		int j = lch;

		edma_parm_modify(PARM_LINK_BCNTRLD, j,
				0x0000ffff, bcnt_rld << 16);
		if (sync_mode == ASYNC)
			edma_parm_and(PARM_OPT, j, ~SYNCDIM);
		else
			edma_parm_or(PARM_OPT, j, SYNCDIM);
		/* Set the acount, bcount, ccount registers */
		edma_parm_write(PARM_A_B_CNT, j, (bcnt << 16) | acnt);
		edma_parm_write(PARM_CCNT, j, ccnt);
	}
}
EXPORT_SYMBOL(davinci_set_dma_transfer_params);

/**
 * davinci_set_dma_params - write PaRAM data for channel
 * @lch: logical channel being configured
 * @temp: channel configuration to be used
 *
 * Use this to assign all parameters of a transfer at once.  This
 * allows more efficient setup of transfers than issuing multiple
 * calls to set up those parameters in small pieces, and provides
 * complete control over all transfer options.
 */
void davinci_set_dma_params(int lch, struct edmacc_param *temp)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		int j = lch;

		edma_parm_write(PARM_OPT, j, temp->opt);
		edma_parm_write(PARM_SRC, j, temp->src);
		edma_parm_write(PARM_A_B_CNT, j, temp->a_b_cnt);
		edma_parm_write(PARM_DST, j, temp->dst);
		edma_parm_write(PARM_SRC_DST_BIDX, j, temp->src_dst_bidx);
		edma_parm_write(PARM_LINK_BCNTRLD, j, temp->link_bcntrld);
		edma_parm_write(PARM_SRC_DST_CIDX, j, temp->src_dst_cidx);
		edma_parm_write(PARM_CCNT, j, temp->ccnt);
	}
}
EXPORT_SYMBOL(davinci_set_dma_params);

/**
 * davinci_get_dma_params - read PaRAM data for channel
 * @lch: logical channel being queried
 * @temp: where to store current channel configuration
 *
 * Use this to read the Parameter RAM for a channel, perhaps to
 * save them as a template for later reuse.
 */
void davinci_get_dma_params(int lch, struct edmacc_param *temp)
{
	if (lch >= 0 && lch < DAVINCI_EDMA_NUM_PARAMENTRY) {
		int j = lch;

		temp->opt = edma_parm_read(PARM_OPT, j);
		temp->src = edma_parm_read(PARM_SRC, j);
		temp->a_b_cnt = edma_parm_read(PARM_A_B_CNT, j);
		temp->dst = edma_parm_read(PARM_DST, j);
		temp->src_dst_bidx = edma_parm_read(PARM_SRC_DST_BIDX, j);
		temp->link_bcntrld = edma_parm_read(PARM_LINK_BCNTRLD, j);
		temp->src_dst_cidx = edma_parm_read(PARM_SRC_DST_CIDX, j);
		temp->ccnt = edma_parm_read(PARM_CCNT, j);
	}
}
EXPORT_SYMBOL(davinci_get_dma_params);

/*
 * DMA pause - pauses the dma on the channel passed
 */
void davinci_pause_dma(int lch)
{
	if ((lch >= 0) && (lch < DAVINCI_EDMA_NUM_DMACH)) {
		unsigned int mask = (1 << (lch & 0x1f));

		edma_shadow0_write_array(SH_EECR, lch >> 5, mask);
	}
}
EXPORT_SYMBOL(davinci_pause_dma);
/*
 * DMA resume - resumes the dma on the channel passed
 */
void davinci_resume_dma(int lch)
{
	if ((lch >= 0) && (lch < DAVINCI_EDMA_NUM_DMACH)) {
		unsigned int mask = (1 << (lch & 0x1f));

		edma_shadow0_write_array(SH_EESR, lch >> 5, mask);
	}
}
EXPORT_SYMBOL(davinci_resume_dma);

/**
 * davinci_start_dma - start dma on a master channel
 * @lch: logical master channel being activated
 *
 * Channels with event associations will be triggered by their hardware
 * events, and channels without such associations will be triggered by
 * software.  (At this writing there is no interface for using software
 * triggers except with channels that don't support hardware triggers.)
 *
 * Returns zero on success, else negative errno.
 */
int davinci_start_dma(int lch)
{
	int ret_val = 0;

	if ((lch >= 0) && (lch < DAVINCI_EDMA_NUM_DMACH)) {
		int j = lch >> 5;
		unsigned int mask = (1 << (lch & 0x1f));

		/* EDMA channels without event association */
		if (test_bit(lch, edma_noevent)) {
			dev_dbg(&edma_dev.dev, "ESR%d %08x\n", j,
				edma_shadow0_read_array(SH_ESR, j));
			edma_shadow0_write_array(SH_ESR, j, mask);
			return ret_val;
		}

		/* EDMA channel with event association */
		dev_dbg(&edma_dev.dev, "ER%d %08x\n", j,
			edma_shadow0_read_array(SH_ER, j));
		/* Clear any pending error */
		edma_write_array(EDMA_EMCR, j, mask);
		/* Clear any SER */
		edma_shadow0_write_array(SH_SECR, j, mask);
		edma_shadow0_write_array(SH_EESR, j, mask);
		dev_dbg(&edma_dev.dev, "EER%d %08x\n", j,
			edma_shadow0_read_array(SH_EER, j));
	} else {		/* for slaveChannels */
		ret_val = -EINVAL;
	}
	return ret_val;
}
EXPORT_SYMBOL(davinci_start_dma);

/**
 * davinci_stop_dma - stops dma on the channel passed
 * @lch: logical channel being deactivated
 *
 * When @lch is a master channel, any active transfer is paused and
 * all pending hardware events are cleared.  The current transfer
 * may not be resumed, and the channel's Parameter RAM should be
 * reinitialized before being reused.
 */
void davinci_stop_dma(int lch)
{
	if (lch < 0 || lch >= DAVINCI_EDMA_NUM_PARAMENTRY)
		return;

	if (lch < DAVINCI_EDMA_NUM_DMACH) {
		int j = lch >> 5;
		unsigned int mask = (1 << (lch & 0x1f));

		edma_shadow0_write_array(SH_EECR, j, mask);
		if (edma_shadow0_read_array(SH_ER, j) & mask) {
			dev_dbg(&edma_dev.dev, "ER%d %08x\n", j,
				edma_shadow0_read_array(SH_ER, j));
			edma_shadow0_write_array(SH_ECR, j, mask);
		}
		if (edma_shadow0_read_array(SH_SER, j) & mask) {
			dev_dbg(&edma_dev.dev, "SER%d %08x\n", j,
				edma_shadow0_read_array(SH_SER, j));
			edma_shadow0_write_array(SH_SECR, j, mask);
		}
		if (edma_read_array(EDMA_EMR, j) & mask) {
			dev_dbg(&edma_dev.dev, "EMR%d %08x\n", j,
				edma_read_array(EDMA_EMR, j));
			edma_write_array(EDMA_EMCR, j, mask);
		}
		dev_dbg(&edma_dev.dev, "EER%d %08x\n", j,
				edma_shadow0_read_array(SH_EER, j));
		/*
		 * if the requested channel is one of the event channels
		 * then just set the link field of the corresponding
		 * param entry to 0xffff
		 */
		/* don't clear link until audio driver fixed
		 * edma_parm_or(PARM_LINK_BCNTRLD, lch, 0xffff);
		 */
	} else {
		/* for slaveChannels */
		edma_parm_or(PARM_LINK_BCNTRLD, lch, 0xffff);
	}
}
EXPORT_SYMBOL(davinci_stop_dma);

/******************************************************************************
 *
 * DMA channel link - link the two logical channels passed through by linking
 *		the link field of head to the param pointed by the lch_que.
 * ARGUMENTS:
 * lch  - logical channel number, in which the link field is linked
 *                  to the param pointed to by lch_que
 * lch_que - logical channel number or the param entry number, which is to be
 *                  linked to the lch
 *
 *****************************************************************************/
void davinci_dma_link_lch(int lch, int lch_que)
{
	if ((lch >= 0) && (lch < DAVINCI_EDMA_NUM_PARAMENTRY) &&
	    (lch_que >= 0) && (lch_que < DAVINCI_EDMA_NUM_PARAMENTRY)) {
		/* program LINK */
		edma_parm_modify(PARM_LINK_BCNTRLD, lch,
				0xffff0000,
				PARM_OFFSET(lch_que));
	}
}
EXPORT_SYMBOL(davinci_dma_link_lch);

/******************************************************************************
 *
 * DMA channel unlink - unlink the two logical channels passed through by
 *                   setting the link field of head to 0xffff.
 * ARGUMENTS:
 * lch - logical channel number, from which the link field is to be removed
 * lch_que - logical channel number or the param entry number, which is to be
 *             unlinked from lch
 *
 *****************************************************************************/
void davinci_dma_unlink_lch(int lch, int lch_que)
{
	if ((lch >= 0) && (lch < DAVINCI_EDMA_NUM_PARAMENTRY) &&
	    (lch_que >= 0) && (lch_que < DAVINCI_EDMA_NUM_PARAMENTRY)) {
		edma_parm_or(PARM_LINK_BCNTRLD, lch,
				0xffff);
	}
}
EXPORT_SYMBOL(davinci_dma_unlink_lch);

/******************************************************************************
 *
 * It cleans ParamEntry qand bring back EDMA to initial state if media has
 * been removed before EDMA has finished.It is usedful for removable media.
 * Arguments:
 *      ch_no     - channel no
 *
 * Return: zero on success, or corresponding error no on failure
 *
 *****************************************************************************/

void davinci_clean_channel(int ch_no)
{
	if ((ch_no >= 0) && (ch_no < DAVINCI_EDMA_NUM_DMACH)) {
		int j = (ch_no >> 5);
		unsigned int mask = 1 << (ch_no & 0x1f);
		dev_dbg(&edma_dev.dev, "EMR%d %08x\n", j,
				edma_read_array(EDMA_EMR, j));
		edma_shadow0_write_array(SH_ECR, j, mask);
		/* Clear the corresponding EMR bits */
		edma_write_array(EDMA_EMCR, j, mask);
		/* Clear any SER */
		edma_shadow0_write_array(SH_SECR, j, mask);
		edma_write(EDMA_CCERRCLR, (1 << 16) | 0x3);
	}
}
EXPORT_SYMBOL(davinci_clean_channel);
