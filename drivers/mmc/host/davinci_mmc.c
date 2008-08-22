/*
 * linux/drivers/mmc/davinci.c
 *
 * TI DaVinci MMC controller file
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * ----------------------------------------------------------------------------
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
 * ----------------------------------------------------------------------------
 Modifications:
 ver. 1.0: Oct 2005, Purushotam Kumar   Initial version
 ver 1.1:  Nov  2005, Purushotam Kumar  Solved bugs
 ver 1.2:  Jan  2006, Purushotam Kumar   Added card remove insert support
 -
 *
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/hardware.h>

#include "davinci_mmc.h"
#include <mach/edma.h>

extern void davinci_clean_channel(int ch_no);

/* MMCSD Init clock in Hz in opendain mode */
#define MMCSD_INIT_CLOCK 		200000

#define DRIVER_NAME 			"davinci_mmc"
#define TCINTEN 			(0x1<<20)

/* This macro could not be defined to 0 (ZERO) or -ve value.
 * This value is multiplied to "HZ"
 * while requesting for timer interrupt every time for probing card.
 */
#define MULTIPILER_TO_HZ 1

static struct mmcsd_config_def mmcsd_cfg = {
/* read write thresholds (in bytes) can be 16/32 */
	32,
/* To use the DMA or not-- 1- Use DMA, 0-Interrupt mode */
	1,
/* flag Indicates 1bit/4bit mode */
	1
};

#define RSP_TYPE(x)	((x) & ~(MMC_RSP_BUSY|MMC_RSP_OPCODE))

static void mmc_davinci_start_command(struct mmc_davinci_host *host,
		struct mmc_command *cmd)
{
	u32 cmd_reg = 0;
	u32 resp_type = 0;
	u32 cmd_type = 0;
	u32 im_val;
	unsigned long flags;

#ifdef CONFIG_MMC_DEBUG
	dev_dbg(mmc_dev(host->mmc), "\nMMCSD : CMD%d, argument 0x%08x",
		cmd->opcode, cmd->arg);
	switch (RSP_TYPE(mmc_resp_type(cmd))) {
	case RSP_TYPE(MMC_RSP_R1):
		dev_dbg(mmc_dev(host->mmc), ", R1/R1b response");
		break;
	case RSP_TYPE(MMC_RSP_R2):
		dev_dbg(mmc_dev(host->mmc), ", R2 response");
		break;
	case RSP_TYPE(MMC_RSP_R3):
		dev_dbg(mmc_dev(host->mmc), ", R3 response");
		break;
	default:
		break;
	}
	dev_dbg(mmc_dev(host->mmc), "\n");
#endif
	host->cmd = cmd;

	/* Protocol layer does not provide response type,
	 * but our hardware needs to know exact type, not just size!
	 */
	switch (RSP_TYPE(mmc_resp_type(cmd))) {
	case MMC_RSP_NONE:
		/* resp 0 */
		break;
	case RSP_TYPE(MMC_RSP_R1):
		resp_type = 1;
		break;
	case RSP_TYPE(MMC_RSP_R2):
		resp_type = 2;
		break;
	case RSP_TYPE(MMC_RSP_R3):
		resp_type = 3;
		break;
	default:
		break;
	}

	/* Protocol layer does not provide command type, but our hardware
	 * needs it!
	 * any data transfer means adtc type (but that information is not
	 * in command structure, so we flagged it into host struct.)
	 * However, telling bc, bcr and ac apart based on response is
	 * not foolproof:
	 * CMD0  = bc  = resp0  CMD15 = ac  = resp0
	 * CMD2  = bcr = resp2  CMD10 = ac  = resp2
	 *
	 * Resolve to best guess with some exception testing:
	 * resp0 -> bc, except CMD15 = ac
	 * rest are ac, except if opendrain
	 */

	if (mmc_cmd_type(cmd) == MMC_CMD_ADTC)
		cmd_type = DAVINCI_MMC_CMDTYPE_ADTC;
	else if (mmc_cmd_type(cmd) == MMC_CMD_BC)
		cmd_type = DAVINCI_MMC_CMDTYPE_BC;
	else if (mmc_cmd_type(cmd) == MMC_CMD_BCR)
		cmd_type = DAVINCI_MMC_CMDTYPE_BCR;
	else
		cmd_type = DAVINCI_MMC_CMDTYPE_AC;

	/* Set command Busy or not */
	if (cmd->flags & MMC_RSP_BUSY) {
		/*
		 * Linux core sending BUSY which is not defined for cmd 24
		 * as per mmc standard
		 */
		if (cmd->opcode != 24)
			cmd_reg = cmd_reg | (1 << 8);
	}

	/* Set command index */
	cmd_reg |= cmd->opcode;

	/* Setting initialize clock */
	if (cmd->opcode == 0)
		cmd_reg = cmd_reg | (1 << 14);

	/* Set for generating DMA Xfer event */
	if ((host->do_dma == 1) && (host->data != NULL)
	    && ((cmd->opcode == 18) || (cmd->opcode == 25)
		|| (cmd->opcode == 24) || (cmd->opcode == 17)))
		cmd_reg = cmd_reg | (1 << 16);

	/* Setting whether command involves data transfer or not */
	if (cmd_type == DAVINCI_MMC_CMDTYPE_ADTC)
		cmd_reg = cmd_reg | (1 << 13);

	/* Setting whether stream or block transfer */
	if (cmd->flags & MMC_DATA_STREAM)
		cmd_reg = cmd_reg | (1 << 12);

	/* Setting whether data read or write */
	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE)
		cmd_reg = cmd_reg | (1 << 11);

	/* Setting response type */
	cmd_reg = cmd_reg | (resp_type << 9);

	if (host->bus_mode == MMC_BUSMODE_PUSHPULL)
		cmd_reg = cmd_reg | (1 << 7);

	/* set Command timeout */
	writel(0xFFFF, host->base + DAVINCI_MMCTOR);

	/* Enable interrupt (calculate here, defer until FIFO is stuffed). */
	im_val =  MMCSD_EVENT_EOFCMD
		| MMCSD_EVENT_ERROR_CMDCRC
		| MMCSD_EVENT_ERROR_DATACRC
		| MMCSD_EVENT_ERROR_CMDTIMEOUT
		| MMCSD_EVENT_ERROR_DATATIMEOUT;
	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE) {
		im_val |= MMCSD_EVENT_BLOCK_XFERRED;

		if (!host->do_dma)
			im_val |= MMCSD_EVENT_WRITE;
	} else if (host->data_dir == DAVINCI_MMC_DATADIR_READ) {
		im_val |= MMCSD_EVENT_BLOCK_XFERRED;

		if (!host->do_dma)
			im_val |= MMCSD_EVENT_READ;
	}

	/*
	 * It is required by controoler b4 WRITE command that
	 * FIFO should be populated with 32 bytes
	 */
	if ((host->data_dir == DAVINCI_MMC_DATADIR_WRITE)
			&& (cmd_type == DAVINCI_MMC_CMDTYPE_ADTC)
			&& (host->do_dma != 1))
		/* Fill the FIFO for Tx */
		davinci_fifo_data_trans(host, 32);

	if (cmd->opcode == 7) {
		spin_lock_irqsave(&host->lock, flags);
		host->is_card_removed = 0;
		host->new_card_state = 1;
		host->is_card_initialized = 1;
		host->old_card_state = host->new_card_state;
		host->is_init_progress = 0;
		spin_unlock_irqrestore(&host->lock, flags);
	}
	if (cmd->opcode == 1 || cmd->opcode == 41) {
		spin_lock_irqsave(&host->lock, flags);
		host->is_card_initialized = 0;
		host->is_init_progress = 1;
		spin_unlock_irqrestore(&host->lock, flags);
	}

	host->is_core_command = 1;
	writel(cmd->arg, host->base + DAVINCI_MMCARGHL);
	writel(cmd_reg,  host->base + DAVINCI_MMCCMD);
	writel(im_val, host->base + DAVINCI_MMCIM);
}

static void mmc_davinci_dma_cb(int lch, u16 ch_status, void *data)
{
	if (DMA_COMPLETE != ch_status) {
		struct mmc_davinci_host *host = (struct mmc_davinci_host *)data;
		dev_warn(mmc_dev(host->mmc), "[DMA FAILED]");
		davinci_abort_dma(host);
	}
}


#define DAVINCI_MMCSD_READ_FIFO(pDst, pRegs, cnt) asm( \
	"	cmp	%3,#16\n" \
	"1:	ldrhs	r0,[%1,%2]\n" \
	"	ldrhs	r1,[%1,%2]\n" \
	"	ldrhs	r2,[%1,%2]\n" \
	"	ldrhs	r3,[%1,%2]\n" \
	"	stmhsia	%0!,{r0,r1,r2,r3}\n" \
	"	beq	3f\n" \
	"	subhs	%3,%3,#16\n" \
	"	cmp	%3,#16\n" \
	"	bhs	1b\n" \
	"	tst	%3,#0x0c\n" \
	"2:	ldrne	r0,[%1,%2]\n" \
	"	strne	r0,[%0],#4\n" \
	"	subne	%3,%3,#4\n" \
	"	tst	%3,#0x0c\n" \
	"	bne	2b\n" \
	"	tst	%3,#2\n" \
	"	ldrneh	r0,[%1,%2]\n" \
	"	strneh	r0,[%0],#2\n" \
	"	tst	%3,#1\n" \
	"	ldrneb	r0,[%1,%2]\n" \
	"	strneb	r0,[%0],#1\n" \
	"3:\n" \
	 : "+r"(pDst) : "r"(pRegs), "i"(DAVINCI_MMCDRR), \
	 "r"(cnt) : "r0", "r1", "r2", "r3");

#define DAVINCI_MMCSD_WRITE_FIFO(pDst, pRegs, cnt) asm( \
	"	cmp	%3,#16\n" \
	"1:	ldmhsia	%0!,{r0,r1,r2,r3}\n" \
	"	strhs	r0,[%1,%2]\n" \
	"	strhs	r1,[%1,%2]\n" \
	"	strhs	r2,[%1,%2]\n" \
	"	strhs	r3,[%1,%2]\n" \
	"	beq	3f\n" \
	"	subhs	%3,%3,#16\n" \
	"	cmp	%3,#16\n" \
	"	bhs	1b\n" \
	"	tst	%3,#0x0c\n" \
	"2:	ldrne	r0,[%0],#4\n" \
	"	strne	r0,[%1,%2]\n" \
	"	subne	%3,%3,#4\n" \
	"	tst	%3,#0x0c\n" \
	"	bne	2b\n" \
	"	tst	%3,#2\n" \
	"	ldrneh	r0,[%0],#2\n" \
	"	strneh	r0,[%1,%2]\n" \
	"	tst	%3,#1\n" \
	"	ldrneb	r0,[%0],#1\n" \
	"	strneb	r0,[%1,%2]\n" \
	"3:\n" \
	 : "+r"(pDst) : "r"(pRegs), "i"(DAVINCI_MMCDXR), \
	 "r"(cnt) : "r0", "r1", "r2", "r3");

static void davinci_fifo_data_trans(struct mmc_davinci_host *host, int n)
{
	u8 *p;

	if (host->buffer_bytes_left == 0) {
		host->sg_idx++;
		BUG_ON(host->sg_idx == host->sg_len);
		mmc_davinci_sg_to_buf(host);
	}

	p = host->buffer;
	if (n > host->buffer_bytes_left)
		n = host->buffer_bytes_left;
	host->buffer_bytes_left -= n;
	host->bytes_left -= n;

	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE) {
		DAVINCI_MMCSD_WRITE_FIFO(p, host->base, n);
	} else {
		DAVINCI_MMCSD_READ_FIFO(p, host->base, n);
	}
	host->buffer = p;
}

static void davinci_reinit_chan(void)
{
	int sync_dev;

	sync_dev = DAVINCI_DMA_MMCTXEVT;
	davinci_stop_dma(sync_dev);
	davinci_clean_channel(sync_dev);

	sync_dev = DAVINCI_DMA_MMCRXEVT;
	davinci_stop_dma(sync_dev);
	davinci_clean_channel(sync_dev);
}

static void davinci_abort_dma(struct mmc_davinci_host *host)
{
	int sync_dev = 0;

	if (host->data_dir == DAVINCI_MMC_DATADIR_READ)
		sync_dev = DAVINCI_DMA_MMCTXEVT;
	else
		sync_dev = DAVINCI_DMA_MMCRXEVT;

	davinci_stop_dma(sync_dev);
	davinci_clean_channel(sync_dev);

}

static int mmc_davinci_start_dma_transfer(struct mmc_davinci_host *host,
		struct mmc_request *req)
{
	int use_dma = 1, i;
	struct mmc_data *data = host->data;
	int mask = mmcsd_cfg.rw_threshold-1;

	host->sg_len = dma_map_sg(mmc_dev(host->mmc), data->sg, host->sg_len,
				((data->flags & MMC_DATA_WRITE)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE));

	/* Decide if we can use DMA */
	for (i = 0; i < host->sg_len; i++) {
		if (data->sg[i].length & mask) {
			use_dma = 0;
			break;
		}
	}

	if (!use_dma) {
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->sg_len,
				  (data->flags & MMC_DATA_WRITE)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
		return -1;
	}

	host->do_dma = 1;

	mmc_davinci_send_dma_request(host, req);

	return 0;
}

static int davinci_release_dma_channels(struct mmc_davinci_host *host)
{
	davinci_free_dma(DAVINCI_DMA_MMCTXEVT);
	davinci_free_dma(DAVINCI_DMA_MMCRXEVT);

	if (host->edma_ch_details.cnt_chanel) {
		davinci_free_dma(host->edma_ch_details.chanel_num[0]);
		host->edma_ch_details.cnt_chanel = 0;
	}

	return 0;
}

static int davinci_acquire_dma_channels(struct mmc_davinci_host *host)
{
	int edma_chan_num, tcc = 0, r, sync_dev;
	enum dma_event_q queue_no = EVENTQ_0;

	/* Acquire master DMA write channel */
	r = davinci_request_dma(DAVINCI_DMA_MMCTXEVT, "MMC_WRITE",
		mmc_davinci_dma_cb, host, &edma_chan_num, &tcc, queue_no);
	if (r != 0) {
		dev_warn(mmc_dev(host->mmc),
				"MMC: davinci_request_dma() failed with %d\n",
				r);
		return r;
	}

	/* Acquire master DMA read channel */
	r = davinci_request_dma(DAVINCI_DMA_MMCRXEVT, "MMC_READ",
		mmc_davinci_dma_cb, host, &edma_chan_num, &tcc, queue_no);
	if (r != 0) {
		dev_warn(mmc_dev(host->mmc),
				"MMC: davinci_request_dma() failed with %d\n",
				r);
		goto free_master_write;
	}

	host->edma_ch_details.cnt_chanel = 0;

	/* currently data Writes are done using single block mode,
	 * so no DMA slave write channel is required for now */

	/* Create a DMA slave read channel
	 * (assuming max segments handled is 2) */
	sync_dev = DAVINCI_DMA_MMCRXEVT;
	r = davinci_request_dma(DAVINCI_EDMA_PARAM_ANY, "LINK", NULL, NULL,
		&edma_chan_num, &sync_dev, queue_no);
	if (r != 0) {
		dev_warn(mmc_dev(host->mmc),
			"MMC: davinci_request_dma() failed with %d\n", r);
		goto free_master_read;
	}

	host->edma_ch_details.cnt_chanel++;
	host->edma_ch_details.chanel_num[0] = edma_chan_num;

	return 0;

free_master_read:
	davinci_free_dma(DAVINCI_DMA_MMCRXEVT);
free_master_write:
	davinci_free_dma(DAVINCI_DMA_MMCTXEVT);

	return r;
}

static int mmc_davinci_send_dma_request(struct mmc_davinci_host *host,
					struct mmc_request *req)
{
	int sync_dev;
	unsigned char i, j;
	unsigned short acnt, bcnt, ccnt;
	unsigned int src_port, dst_port, temp_ccnt;
	enum address_mode mode_src, mode_dst;
	enum fifo_width fifo_width_src, fifo_width_dst;
	unsigned short src_bidx, dst_bidx;
	unsigned short src_cidx, dst_cidx;
	unsigned short bcntrld;
	enum sync_dimension sync_mode;
	edmacc_paramentry_regs temp;
	int edma_chan_num;
	struct mmc_data *data = host->data;
	struct scatterlist *sg = &data->sg[0];
	unsigned int count;
	int num_frames, frame;

#define MAX_C_CNT		64000

	frame = data->blksz;
	count = sg_dma_len(sg);

	if ((data->blocks == 1) && (count > data->blksz))
		count = frame;

	if ((count & (mmcsd_cfg.rw_threshold-1)) == 0) {
		/* This should always be true due to an earlier check */
		acnt = 4;
		bcnt = mmcsd_cfg.rw_threshold>>2;
		num_frames = count >> ((mmcsd_cfg.rw_threshold == 32)? 5 : 4);
	} else if (count < mmcsd_cfg.rw_threshold) {
		if ((count&3) == 0) {
			acnt = 4;
			bcnt = count>>2;
		} else if ((count&1) == 0) {
			acnt = 2;
			bcnt = count>>1;
		} else {
			acnt = 1;
			bcnt = count;
		}
		num_frames = 1;
	} else {
		acnt = 4;
		bcnt = mmcsd_cfg.rw_threshold>>2;
		num_frames = count >> ((mmcsd_cfg.rw_threshold == 32)? 5 : 4);
		dev_warn(mmc_dev(host->mmc),
			"MMC: count of 0x%x unsupported, truncating transfer\n",
			count);
	}

	if (num_frames > MAX_C_CNT) {
		temp_ccnt = MAX_C_CNT;
		ccnt = temp_ccnt;
	} else {
		ccnt = num_frames;
		temp_ccnt = ccnt;
	}

	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE) {
		/*AB Sync Transfer */
		sync_dev = DAVINCI_DMA_MMCTXEVT;

		src_port = (unsigned int)sg_dma_address(sg);
		mode_src = INCR;
		fifo_width_src = W8BIT;	/* It's not cared as modeDsr is INCR */
		src_bidx = acnt;
		src_cidx = acnt * bcnt;
		dst_port = (unsigned int)(host->mem_res->start +
				DAVINCI_MMCDXR);
		/* cannot be FIFO, address not aligned on 32 byte boundary */
		mode_dst = INCR;
		fifo_width_dst = W8BIT;	/* It's not cared as modeDsr is INCR */
		dst_bidx = 0;
		dst_cidx = 0;
		bcntrld = 8;
		sync_mode = ABSYNC;

	} else {
		sync_dev = DAVINCI_DMA_MMCRXEVT;

		src_port = (unsigned int)(host->mem_res->start +
				DAVINCI_MMCDRR);
		/* cannot be FIFO, address not aligned on 32 byte boundary */
		mode_src = INCR;
		fifo_width_src = W8BIT;
		src_bidx = 0;
		src_cidx = 0;
		dst_port = (unsigned int)sg_dma_address(sg);
		mode_dst = INCR;
		fifo_width_dst = W8BIT;	/* It's not cared as modeDsr is INCR */
		dst_bidx = acnt;
		dst_cidx = acnt * bcnt;
		bcntrld = 8;
		sync_mode = ABSYNC;
	}

	davinci_set_dma_src_params(sync_dev, src_port, mode_src,
			fifo_width_src);
	davinci_set_dma_dest_params(sync_dev, dst_port, mode_dst,
			fifo_width_dst);
	davinci_set_dma_src_index(sync_dev, src_bidx, src_cidx);
	davinci_set_dma_dest_index(sync_dev, dst_bidx, dst_cidx);
	davinci_set_dma_transfer_params(sync_dev, acnt, bcnt, ccnt, bcntrld,
					sync_mode);

	davinci_get_dma_params(sync_dev, &temp);
	if (sync_dev == DAVINCI_DMA_MMCTXEVT) {
		if (host->option_write == 0) {
			host->option_write = temp.opt;
		} else {
			temp.opt = host->option_write;
			davinci_set_dma_params(sync_dev, &temp);
		}
	}
	if (sync_dev == DAVINCI_DMA_MMCRXEVT) {
		if (host->option_read == 0) {
			host->option_read = temp.opt;
		} else {
			temp.opt = host->option_read;
			davinci_set_dma_params(sync_dev, &temp);
		}
	}

	if (host->sg_len > 1) {
		davinci_get_dma_params(sync_dev, &temp);
		temp.opt &= ~TCINTEN;
		davinci_set_dma_params(sync_dev, &temp);

		for (i = 0; i < host->sg_len - 1; i++) {
			sg = &data->sg[i + 1];

			if (i != 0) {
				j = i - 1;
				davinci_get_dma_params(
					host->edma_ch_details.chanel_num[j],
					&temp);
				temp.opt &= ~TCINTEN;
				davinci_set_dma_params(
					host->edma_ch_details.chanel_num[j],
					&temp);
			}

			edma_chan_num = host->edma_ch_details.chanel_num[0];

			frame = data->blksz;
			count = sg_dma_len(sg);

			if ((data->blocks == 1) && (count > data->blksz))
				count = frame;

			ccnt = count >> ((mmcsd_cfg.rw_threshold == 32)? 5 : 4);

			if (sync_dev == DAVINCI_DMA_MMCTXEVT)
				temp.src = (unsigned int)sg_dma_address(sg);
			else
				temp.dst = (unsigned int)sg_dma_address(sg);
			temp.opt |= TCINTEN;

			temp.ccnt = (temp.ccnt & 0xFFFF0000) | (ccnt);

			davinci_set_dma_params(edma_chan_num, &temp);
			if (i != 0) {
				j = i - 1;
				davinci_dma_link_lch(host->edma_ch_details.
						chanel_num[j],
						edma_chan_num);
			}
		}
		davinci_dma_link_lch(sync_dev,
				host->edma_ch_details.chanel_num[0]);
	}

	davinci_start_dma(sync_dev);
	return 0;
}

static void
mmc_davinci_prepare_data(struct mmc_davinci_host *host, struct mmc_request *req)
{
	int fifo_lev = (mmcsd_cfg.rw_threshold == 32)? MMCFIFOCTL_FIFOLEV : 0;
	int timeout, sg_len;

	host->data = req->data;
	if (req->data == NULL) {
		host->data_dir = DAVINCI_MMC_DATADIR_NONE;
		writel(0, host->base + DAVINCI_MMCBLEN);
		writel(0, host->base + DAVINCI_MMCNBLK);
		return;
	}

	/* Init idx */
	host->sg_idx = 0;

	dev_dbg(mmc_dev(host->mmc),
		"MMCSD : Data xfer (%s %s), "
		"DTO %d cycles + %d ns, %d blocks of %d bytes\r\n",
		(req->data->flags & MMC_DATA_STREAM) ? "stream" : "block",
		(req->data->flags & MMC_DATA_WRITE) ? "write" : "read",
		req->data->timeout_clks, req->data->timeout_ns,
		req->data->blocks, req->data->blksz);

	/* Convert ns to clock cycles by assuming 20MHz frequency
	 * 1 cycle at 20MHz = 500 ns
	 */
	timeout = req->data->timeout_clks + req->data->timeout_ns / 500;
	if (timeout > 0xffff)
		timeout = 0xffff;

	writel(timeout, host->base + DAVINCI_MMCTOD);
	writel(req->data->blocks, host->base + DAVINCI_MMCNBLK);
	writel(req->data->blksz, host->base + DAVINCI_MMCBLEN);
	host->data_dir = (req->data->flags & MMC_DATA_WRITE)
			? DAVINCI_MMC_DATADIR_WRITE
			: DAVINCI_MMC_DATADIR_READ;

	/* Configure the FIFO */
	switch (host->data_dir) {
	case DAVINCI_MMC_DATADIR_WRITE:
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_WR | MMCFIFOCTL_FIFORST,
			host->base + DAVINCI_MMCFIFOCTL);
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_WR,
			host->base + DAVINCI_MMCFIFOCTL);
		break;

	case DAVINCI_MMC_DATADIR_READ:
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_RD | MMCFIFOCTL_FIFORST,
			host->base + DAVINCI_MMCFIFOCTL);
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_RD,
			host->base + DAVINCI_MMCFIFOCTL);
		break;
	default:
		break;
	}

	sg_len = (req->data->blocks == 1) ? 1 : req->data->sg_len;
	host->sg_len = sg_len;

	host->bytes_left = req->data->blocks * req->data->blksz;

	if ((host->use_dma == 1) &&
		  ((host->bytes_left & (mmcsd_cfg.rw_threshold-1)) == 0) &&
	      (mmc_davinci_start_dma_transfer(host, req) == 0)) {
		host->buffer = NULL;
		host->bytes_left = 0;
	} else {
		/* Revert to CPU Copy */

		host->do_dma = 0;
		mmc_davinci_sg_to_buf(host);
	}
}

/* PIO only */
static void mmc_davinci_sg_to_buf(struct mmc_davinci_host *host)
{
	struct scatterlist *sg;

	sg = host->data->sg + host->sg_idx;
	host->buffer_bytes_left = sg->length;
	host->buffer = sg_virt(sg);
	if (host->buffer_bytes_left > host->bytes_left)
		host->buffer_bytes_left = host->bytes_left;
}

static inline void wait_on_data(struct mmc_davinci_host *host)
{
	unsigned long timeout = jiffies + usecs_to_jiffies(900000);

	while (time_before(jiffies, timeout)) {
		if (!(readl(host->base + DAVINCI_MMCST1) & MMCST1_BUSY))
			return;

		cpu_relax();
	}

	dev_warn(mmc_dev(host->mmc), "ERROR: TOUT waiting for BUSY\n");
}

static void mmc_davinci_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mmc_davinci_host *host = mmc_priv(mmc);
	unsigned long flags;

	if (host->is_card_removed) {
		if (req->cmd) {
			req->cmd->error = -ETIMEDOUT;
			mmc_request_done(mmc, req);
		}
		dev_dbg(mmc_dev(host->mmc),
			"From code segment excuted when card removed\n");
		return;
	}

	wait_on_data(host);

	if (!host->is_card_detect_progress) {
		spin_lock_irqsave(&host->lock, flags);
		host->is_card_busy = 1;
		spin_unlock_irqrestore(&host->lock, flags);
		host->do_dma = 0;
		mmc_davinci_prepare_data(host, req);
		mmc_davinci_start_command(host, req->cmd);
	} else {
		/* Queue up the request as card dectection is being excuted */
		host->que_mmc_request = req;
		spin_lock_irqsave(&host->lock, flags);
		host->is_req_queued_up = 1;
		spin_unlock_irqrestore(&host->lock, flags);
	}
}

static unsigned int calculate_freq_for_card(struct mmc_davinci_host *host,
	unsigned int mmc_req_freq)
{
	unsigned int mmc_freq = 0, cpu_arm_clk = 0, mmc_push_pull = 0;

	cpu_arm_clk = host->mmc_input_clk;
	if (cpu_arm_clk > (2 * mmc_req_freq))
		mmc_push_pull = ((unsigned int)cpu_arm_clk
				/ (2 * mmc_req_freq)) - 1;
	else
		mmc_push_pull = 0;

	mmc_freq = (unsigned int)cpu_arm_clk / (2 * (mmc_push_pull + 1));

	if (mmc_freq > mmc_req_freq)
		mmc_push_pull = mmc_push_pull + 1;

	return mmc_push_pull;
}

static void mmc_davinci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	unsigned int open_drain_freq = 0, cpu_arm_clk = 0;
	unsigned int mmc_push_pull_freq = 0;
	struct mmc_davinci_host *host = mmc_priv(mmc);

	cpu_arm_clk = host->mmc_input_clk;
	dev_dbg(mmc_dev(host->mmc),
		"clock %dHz busmode %d powermode %d Vdd %d.%02d\r\n",
		ios->clock, ios->bus_mode, ios->power_mode,
		ios->vdd / 100, ios->vdd % 100);
	if (ios->bus_width == MMC_BUS_WIDTH_4) {
		dev_dbg(mmc_dev(host->mmc), "\nEnabling 4 bit mode\n");
		writel(readl(host->base + DAVINCI_MMCCTL) | MMCCTL_WIDTH_4_BIT,
			host->base + DAVINCI_MMCCTL);
	} else {
		dev_dbg(mmc_dev(host->mmc), "Disabling 4 bit mode\n");
		writel(readl(host->base + DAVINCI_MMCCTL) & ~MMCCTL_WIDTH_4_BIT,
			host->base + DAVINCI_MMCCTL);
	}

	if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN) {
		u32 temp;
		open_drain_freq = ((unsigned int)cpu_arm_clk
				/ (2 * MMCSD_INIT_CLOCK)) - 1;
		temp = readl(host->base + DAVINCI_MMCCLK) & ~0xFF;
		temp |= open_drain_freq;
		writel(temp, host->base + DAVINCI_MMCCLK);
	} else {
		u32 temp;
		mmc_push_pull_freq = calculate_freq_for_card(host, ios->clock);

		temp = readl(host->base + DAVINCI_MMCCLK) & ~MMCCLK_CLKEN;
		writel(temp, host->base + DAVINCI_MMCCLK);

		udelay(10);

		temp = readl(host->base + DAVINCI_MMCCLK) & ~MMCCLK_CLKRT_MASK;
		temp |= mmc_push_pull_freq;
		writel(temp, host->base + DAVINCI_MMCCLK);

		writel(temp | MMCCLK_CLKEN, host->base + DAVINCI_MMCCLK);

		udelay(10);
	}

	host->bus_mode = ios->bus_mode;
	if (ios->power_mode == MMC_POWER_UP) {
		/* Send clock cycles, poll completion */
		writel(0, host->base + DAVINCI_MMCARGHL);
		writel(MMCCMD_INITCK, host->base + DAVINCI_MMCCMD);
		while (!(readl(host->base + DAVINCI_MMCST0) &
				MMCSD_EVENT_EOFCMD))
			cpu_relax();
	}
}

static void
mmc_davinci_xfer_done(struct mmc_davinci_host *host, struct mmc_data *data)
{
	unsigned long flags;

	host->data = NULL;
	host->data_dir = DAVINCI_MMC_DATADIR_NONE;
	if (data->error == 0)
		data->bytes_xfered += data->blocks * data->blksz;

	if (host->do_dma) {
		davinci_abort_dma(host);

		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->sg_len,
			     (data->flags & MMC_DATA_WRITE)
			     ? DMA_TO_DEVICE
			     : DMA_FROM_DEVICE);
	}

	if (data->error == -ETIMEDOUT) {
		spin_lock_irqsave(&host->lock, flags);
		host->is_card_busy = 0;
		spin_unlock_irqrestore(&host->lock, flags);
		mmc_request_done(host->mmc, data->mrq);
		return;
	}

	if (!data->stop) {
		spin_lock_irqsave(&host->lock, flags);
		host->is_card_busy = 0;
		spin_unlock_irqrestore(&host->lock, flags);
		mmc_request_done(host->mmc, data->mrq);
		return;
	}
	mmc_davinci_start_command(host, data->stop);
}

static void mmc_davinci_cmd_done(struct mmc_davinci_host *host,
				 struct mmc_command *cmd)
{
	unsigned long flags;
	host->cmd = NULL;

	if (!cmd) {
		dev_warn(mmc_dev(host->mmc),
			"%s(): No cmd ptr\n", __FUNCTION__);
		return;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* response type 2 */
			cmd->resp[3] = readl(host->base + DAVINCI_MMCRSP01);
			cmd->resp[2] = readl(host->base + DAVINCI_MMCRSP23);
			cmd->resp[1] = readl(host->base + DAVINCI_MMCRSP45);
			cmd->resp[0] = readl(host->base + DAVINCI_MMCRSP67);
		} else {
			/* response types 1, 1b, 3, 4, 5, 6 */
			cmd->resp[0] = readl(host->base + DAVINCI_MMCRSP67);
		}
	}

	if (host->data == NULL || cmd->error) {
		if (cmd->error == -ETIMEDOUT)
			cmd->mrq->cmd->retries = 0;
		spin_lock_irqsave(&host->lock, flags);
		host->is_card_busy = 0;
		spin_unlock_irqrestore(&host->lock, flags);
		mmc_request_done(host->mmc, cmd->mrq);
	}
}

static inline int handle_core_command(
		struct mmc_davinci_host *host, unsigned int status)
{
	int end_command = 0;
	int end_transfer = 0;
	unsigned int qstatus;
	unsigned long flags;

	if ((host->is_card_initialized) && (host->new_card_state == 0)) {
		if (host->cmd) {
			host->cmd->error = -ETIMEDOUT;
			mmc_davinci_cmd_done(host, host->cmd);
		}
		dev_dbg(mmc_dev(host->mmc), "From code segment "
			"excuted when card removed\n");
		return -1;
	}

	qstatus = status;
	while (1) {
		if ((status & MMCSD_EVENT_WRITE) &&
				(host->data_dir == DAVINCI_MMC_DATADIR_WRITE)
				&& (host->bytes_left > 0)) {
			/* Buffer almost empty */
			davinci_fifo_data_trans(host, mmcsd_cfg.rw_threshold);
		}

		if ((status & MMCSD_EVENT_READ) &&
				(host->data_dir == DAVINCI_MMC_DATADIR_READ)
				&& (host->bytes_left > 0)) {
			/* Buffer almost empty */
			davinci_fifo_data_trans(host, mmcsd_cfg.rw_threshold);
		}
		status = readl(host->base + DAVINCI_MMCST0);
		if (!status)
			break;
		qstatus |= status;
		if (host->data == NULL) {
			dev_dbg(mmc_dev(host->mmc), "Status is %x at end of "
				"ISR when host->data is NULL", status);
			break;
		}
	}

	if (qstatus & MMCSD_EVENT_BLOCK_XFERRED) {
		/* Block sent/received */
		if (host->data != NULL) {
			if ((host->do_dma == 0) && (host->bytes_left > 0)) {
				/* if datasize<mmcsd_cfg.rw_threshold
				 * no RX ints are generated
				 */
				davinci_fifo_data_trans(host,
						mmcsd_cfg.rw_threshold);
			}
			end_transfer = 1;
		} else {
			dev_warn(mmc_dev(host->mmc), "TC:host->data is NULL\n");
		}
	}

	if (qstatus & MMCSD_EVENT_ERROR_DATATIMEOUT) {
		/* Data timeout */
		if (host->data && host->new_card_state != 0) {
			host->data->error = -ETIMEDOUT;
			spin_lock_irqsave(&host->lock, flags);
			host->is_card_removed = 1;
			host->new_card_state = 0;
			host->is_card_initialized = 0;
			spin_unlock_irqrestore(&host->lock, flags);
			dev_dbg(mmc_dev(host->mmc), "MMCSD: Data timeout, "
				"CMD%d and status is %x\n",
				host->cmd->opcode, status);

			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			end_transfer = 1;
		}
	}

	if (qstatus & MMCSD_EVENT_ERROR_DATACRC) {
		u32 temp;
		/* DAT line portion is disabled and in reset state */
		temp = readl(host->base + DAVINCI_MMCCTL);

		writel(temp | MMCCTL_CMDRST,
			host->base + DAVINCI_MMCCTL);

		udelay(10);

		writel(temp & ~MMCCTL_CMDRST,
			host->base + DAVINCI_MMCCTL);

		/* Data CRC error */
		if (host->data) {
			host->data->error = -EILSEQ;
			dev_dbg(mmc_dev(host->mmc), "MMCSD: Data CRC error, "
				"bytes left %d\n", host->bytes_left);
			end_transfer = 1;
		} else {
			dev_dbg(mmc_dev(host->mmc), "MMCSD: Data CRC error\n");
		}
	}

	if (qstatus & MMCSD_EVENT_ERROR_CMDTIMEOUT) {
		if (host->do_dma)
			davinci_abort_dma(host);

		/* Command timeout */
		if (host->cmd) {
			/* Timeouts are normal in case of
			 * MMC_SEND_STATUS
			 */
			if (host->cmd->opcode != MMC_ALL_SEND_CID) {
				dev_dbg(mmc_dev(host->mmc), "MMCSD: CMD%d "
					"timeout, status %x\n",
					host->cmd->opcode, status);
				spin_lock_irqsave(&host->lock, flags);
				host->new_card_state = 0;
				host->is_card_initialized = 0;
				spin_unlock_irqrestore(&host->lock, flags);
			}
			host->cmd->error = -ETIMEDOUT;
			end_command = 1;
		}
	}

	if (qstatus & MMCSD_EVENT_ERROR_CMDCRC) {
		/* Command CRC error */
		dev_dbg(mmc_dev(host->mmc), "Command CRC error\n");
		if (host->cmd) {
			/* Ignore CMD CRC errors during high speed operation */
			if (host->mmc->ios.clock <= 25000000)
				host->cmd->error = -EILSEQ;
			end_command = 1;
		}
	}

	if (qstatus & MMCSD_EVENT_EOFCMD) {
		/* End of command phase */
		end_command = 1;
	}

	if (end_command)
		mmc_davinci_cmd_done(host, host->cmd);
	if (end_transfer)
		mmc_davinci_xfer_done(host, host->data);
	return 0;
}

static inline void handle_other_commands(
		struct mmc_davinci_host *host, unsigned int status)
{
	unsigned long flags;
	if (host->cmd_code == 13) {
		if (status & MMCSD_EVENT_EOFCMD) {
			spin_lock_irqsave(&host->lock, flags);
			host->new_card_state = 1;
			spin_unlock_irqrestore(&host->lock, flags);
		} else {
			spin_lock_irqsave(&host->lock, flags);
			host->is_card_removed = 1;
			host->new_card_state = 0;
			host->is_card_initialized = 0;
			spin_unlock_irqrestore(&host->lock, flags);
		}

		spin_lock_irqsave(&host->lock, flags);
		host->is_card_detect_progress = 0;
		spin_unlock_irqrestore(&host->lock, flags);

		if (host->is_req_queued_up) {
			mmc_davinci_request(host->mmc, host->que_mmc_request);
			spin_lock_irqsave(&host->lock, flags);
			host->is_req_queued_up = 0;
			spin_unlock_irqrestore(&host->lock, flags);
		}

	}

	if (host->cmd_code == 1 || host->cmd_code == 55) {
		if (status & MMCSD_EVENT_EOFCMD) {
			spin_lock_irqsave(&host->lock, flags);
			host->is_card_removed = 0;
			host->new_card_state = 1;
			host->is_card_initialized = 0;
			spin_unlock_irqrestore(&host->lock, flags);
		} else {

			spin_lock_irqsave(&host->lock, flags);
			host->is_card_removed = 1;
			host->new_card_state = 0;
			host->is_card_initialized = 0;
			spin_unlock_irqrestore(&host->lock, flags);
		}

		spin_lock_irqsave(&host->lock, flags);
		host->is_card_detect_progress = 0;
		spin_unlock_irqrestore(&host->lock, flags);

		if (host->is_req_queued_up) {
			mmc_davinci_request(host->mmc, host->que_mmc_request);
			spin_lock_irqsave(&host->lock, flags);
			host->is_req_queued_up = 0;
			spin_unlock_irqrestore(&host->lock, flags);
		}
	}

	if (host->cmd_code == 0) {
		if (status & MMCSD_EVENT_EOFCMD) {
			static int flag_sd_mmc;
			host->is_core_command = 0;

			if (flag_sd_mmc) {
				flag_sd_mmc = 0;
				host->cmd_code = 1;
				/* Issue cmd1 */
				writel(0x80300000,
					host->base + DAVINCI_MMCARGHL);
				writel(MMCCMD_RSPFMT_R3 | 1,
					host->base + DAVINCI_MMCCMD);
			} else {
				flag_sd_mmc = 1;
				host->cmd_code = 55;
				/* Issue cmd55 */
				writel(0x0,
					host->base + DAVINCI_MMCARGHL);
				writel(MMCCMD_RSPFMT_R1456 | 55,
					host->base + DAVINCI_MMCCMD);
			}

			dev_dbg(mmc_dev(host->mmc),
				"MMC-Probing mmc with cmd%d\n", host->cmd_code);
		} else {
			spin_lock_irqsave(&host->lock, flags);
			host->new_card_state = 0;
			host->is_card_initialized = 0;
			host->is_card_detect_progress = 0;
			spin_unlock_irqrestore(&host->lock, flags);
		}
	}
}

static irqreturn_t mmc_davinci_irq(int irq, void *dev_id)
{
	struct mmc_davinci_host *host = (struct mmc_davinci_host *)dev_id;
	unsigned int status;

	if (host->is_core_command) {
		if (host->cmd == NULL && host->data == NULL) {
			status = readl(host->base + DAVINCI_MMCST0);
			dev_dbg(mmc_dev(host->mmc),
				"Spurious interrupt 0x%04x\n", status);
			/* Disable the interrupt from mmcsd */
			writel(0, host->base + DAVINCI_MMCIM);
			return IRQ_HANDLED;
		}
	}
	do {
		status = readl(host->base + DAVINCI_MMCST0);
		if (status == 0)
			break;

		if (host->is_core_command) {
			if (handle_core_command(host, status))
				break;
		} else {
			handle_other_commands(host, status);
		}
	} while (1);
	return IRQ_HANDLED;
}

static struct mmc_host_ops mmc_davinci_ops = {
	.request = mmc_davinci_request,
	.set_ios = mmc_davinci_set_ios,
	.get_ro = mmc_davinci_get_ro
};

static int mmc_davinci_get_ro(struct mmc_host *mmc)
{
	return 0;
}

static void mmc_check_card(unsigned long data)
{
	struct mmc_davinci_host *host = (struct mmc_davinci_host *)data;
	unsigned long flags;
	struct mmc_card *card = NULL;

	if (host->mmc && host->mmc->card)
		card = host->mmc->card;

	if ((!host->is_card_detect_progress) || (!host->is_init_progress)) {
		if (host->is_card_initialized) {
			host->is_core_command = 0;
			host->cmd_code = 13;
			spin_lock_irqsave(&host->lock, flags);
			host->is_card_detect_progress = 1;
			spin_unlock_irqrestore(&host->lock, flags);

			/* Issue cmd13 */
			writel((card && mmc_card_sd(card))
				? (card->rca << 16) : 0x10000,
				host->base + DAVINCI_MMCARGHL);
			writel(MMCCMD_RSPFMT_R1456 | MMCCMD_PPLEN | 13,
				host->base + DAVINCI_MMCCMD);
		} else {
			host->is_core_command = 0;
			host->cmd_code = 0;
			spin_lock_irqsave(&host->lock, flags);
			host->is_card_detect_progress = 1;
			spin_unlock_irqrestore(&host->lock, flags);

			/* Issue cmd0 */
			writel(0, host->base + DAVINCI_MMCARGHL);
			writel(MMCCMD_INITCK, host->base + DAVINCI_MMCCMD);
		}
		writel(MMCSD_EVENT_EOFCMD
			| MMCSD_EVENT_ERROR_CMDCRC
			| MMCSD_EVENT_ERROR_DATACRC
			| MMCSD_EVENT_ERROR_CMDTIMEOUT
			| MMCSD_EVENT_ERROR_DATATIMEOUT,
			host->base + DAVINCI_MMCIM);
	}
}

static void davinci_mmc_check_status(unsigned long data)
{
	unsigned long flags;
	struct mmc_davinci_host *host = (struct mmc_davinci_host *)data;

	if (!host->is_card_busy) {
		if (host->old_card_state ^ host->new_card_state) {
			davinci_reinit_chan();
			init_mmcsd_host(host);
			mmc_detect_change(host->mmc, 0);
			spin_lock_irqsave(&host->lock, flags);
			host->old_card_state = host->new_card_state;
			spin_unlock_irqrestore(&host->lock, flags);
		} else
			mmc_check_card(data);
	}
	mod_timer(&host->timer, jiffies + MULTIPILER_TO_HZ * HZ);
}

static void init_mmcsd_host(struct mmc_davinci_host *host)
{
	/* DAT line portion is diabled and in reset state */
	writel(readl(host->base + DAVINCI_MMCCTL) | MMCCTL_DATRST,
		host->base + DAVINCI_MMCCTL);

	/* CMD line portion is diabled and in reset state */
	writel(readl(host->base + DAVINCI_MMCCTL) | MMCCTL_CMDRST,
		host->base + DAVINCI_MMCCTL);

	udelay(10);

	writel(0, host->base + DAVINCI_MMCCLK);
	writel(MMCCLK_CLKEN, host->base + DAVINCI_MMCCLK);

	writel(0xFFFF, host->base + DAVINCI_MMCTOR);
	writel(0xFFFF, host->base + DAVINCI_MMCTOD);

	writel(readl(host->base + DAVINCI_MMCCTL) & ~MMCCTL_DATRST,
		host->base + DAVINCI_MMCCTL);
	writel(readl(host->base + DAVINCI_MMCCTL) & ~MMCCTL_CMDRST,
		host->base + DAVINCI_MMCCTL);

	udelay(10);
}

static int davinci_mmcsd_probe(struct platform_device *pdev)
{
	struct mmc_davinci_host *host = NULL;
	struct mmc_host *mmc = NULL;
	struct resource *r, *mem = NULL;
	int ret = 0, irq = 0;
	size_t mem_size;

	ret = -ENODEV;
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!r || irq == NO_IRQ)
		goto out;

	ret = -EBUSY;
	mem_size = r->end - r->start + 1;
	mem = request_mem_region(r->start, mem_size, DRIVER_NAME);
	if (!mem)
		goto out;

	ret = -ENOMEM;
	mmc = mmc_alloc_host(sizeof(struct mmc_davinci_host), &pdev->dev);
	if (!mmc)
		goto out;

	host = mmc_priv(mmc);
	host->mmc = mmc;	/* Important */

	host->mem_res = mem;
	host->base = ioremap(r->start, SZ_4K);
	if (!host->base)
		goto out;

	spin_lock_init(&host->lock);

	ret = -ENXIO;
	host->clk = clk_get(NULL, "MMCSDCLK");
	if (host->clk) {
		clk_enable(host->clk);
		host->mmc_input_clk = clk_get_rate(host->clk);
	} else
		goto out;

	init_mmcsd_host(host);

	if (mmcsd_cfg.use_4bit_mode)
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	mmc->ops = &mmc_davinci_ops;
	mmc->f_min = 312500;
#ifdef CONFIG_MMC_HIGHSPEED /* FIXME: no longer used */
	mmc->f_max = 50000000;
	mmc->caps |= MMC_CAP_MMC_HIGHSPEED;
#else
	mmc->f_max = 25000000;
#endif
	mmc->ocr_avail = MMC_VDD_32_33;

#ifdef CONFIG_MMC_BLOCK_BOUNCE
       mmc->max_phys_segs = 1;
       mmc->max_hw_segs   = 1;
#else
	mmc->max_phys_segs = 2;
	mmc->max_hw_segs   = 2;
#endif
	mmc->max_blk_size  = 4095;  /* BLEN is 11 bits */
	mmc->max_blk_count = 65535; /* NBLK is 16 bits */
	mmc->max_req_size  = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size  = mmc->max_req_size;

	dev_dbg(mmc_dev(host->mmc), "max_phys_segs=%d\n", mmc->max_phys_segs);
	dev_dbg(mmc_dev(host->mmc), "max_hw_segs=%d\n", mmc->max_hw_segs);
	dev_dbg(mmc_dev(host->mmc), "max_blk_size=%d\n", mmc->max_blk_size);
	dev_dbg(mmc_dev(host->mmc), "max_req_size=%d\n", mmc->max_req_size);
	dev_dbg(mmc_dev(host->mmc), "max_seg_size=%d\n", mmc->max_seg_size);

	if (mmcsd_cfg.use_dma)
		if (davinci_acquire_dma_channels(host) != 0)
			goto out;

	host->use_dma = mmcsd_cfg.use_dma;
	host->irq = irq;
	host->sd_support = 1;

	ret = request_irq(irq, mmc_davinci_irq, 0, DRIVER_NAME, host);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, host);
	mmc_add_host(mmc);

	init_timer(&host->timer);
	host->timer.data = (unsigned long)host;
	host->timer.function = davinci_mmc_check_status;
	host->timer.expires = jiffies + MULTIPILER_TO_HZ * HZ;
	add_timer(&host->timer);

	dev_info(mmc_dev(host->mmc), "Using %s, %d-bit mode\n",
	    mmcsd_cfg.use_dma ? "DMA" : "PIO",
	    mmcsd_cfg.use_4bit_mode ? 4 : 1);

	return 0;

out:
	if (host) {
		if (host->edma_ch_details.cnt_chanel)
			davinci_release_dma_channels(host);

		if (host->clk) {
			clk_disable(host->clk);
			clk_put(host->clk);
		}

		if (host->base)
			iounmap(host->base);
	}

	if (mmc)
		mmc_free_host(mmc);

	if (mem)
		release_resource(mem);

	return ret;
}

static int davinci_mmcsd_remove(struct platform_device *pdev)
{
	struct mmc_davinci_host *host = platform_get_drvdata(pdev);
	unsigned long flags;

	platform_set_drvdata(pdev, NULL);
	if (host) {
		spin_lock_irqsave(&host->lock, flags);
		del_timer(&host->timer);
		spin_unlock_irqrestore(&host->lock, flags);

		mmc_remove_host(host->mmc);
		free_irq(host->irq, host);

		davinci_release_dma_channels(host);

		clk_disable(host->clk);
		clk_put(host->clk);

		iounmap(host->base);

		release_resource(host->mem_res);

		mmc_free_host(host->mmc);
	}

	return 0;
}

#ifdef CONFIG_PM
static int davinci_mmcsd_suspend(struct platform_device *pdev, pm_message_t msg)
{
	struct mmc_davinci_host *host = platform_get_drvdata(pdev);

	return mmc_suspend_host(host->mmc, msg);
}

static int davinci_mmcsd_resume(struct platform_device *pdev)
{
	struct mmc_davinci_host *host = platform_get_drvdata(pdev);

	return mmc_resume_host(host->mmc);
}

#else

#define davinci_mmcsd_suspend	NULL
#define davinci_mmcsd_resume	NULL

#endif

static struct platform_driver davinci_mmcsd_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = davinci_mmcsd_probe,
	.remove = davinci_mmcsd_remove,
	.suspend = davinci_mmcsd_suspend,
	.resume = davinci_mmcsd_resume,
};

static int davinci_mmcsd_init(void)
{
	return platform_driver_register(&davinci_mmcsd_driver);
}

static void __exit davinci_mmcsd_exit(void)
{
	platform_driver_unregister(&davinci_mmcsd_driver);
}

module_init(davinci_mmcsd_init);
module_exit(davinci_mmcsd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MMCSD driver for Davinci MMC controller");
