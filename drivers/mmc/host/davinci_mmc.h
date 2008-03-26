/*
 *  linux/drivers/mmc/davinci.h
 *
 *  BRIEF MODULE DESCRIPTION
 *      DAVINCI MMC register and other definitions
 *
 *  Copyright (C) 2006 Texas Instruments.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 Modifications:
 ver. 1.0: Oct 2005, Purushotam Kumar   Initial version
 ver  1.2: Jan  2006, Purushotam Kumar   Added hot card remove insert support

 *
 */

#ifndef DAVINCI_MMC_H_
#define DAVINCI_MMC_H_

/**************************************************************************
 * Register Definitions
 **************************************************************************/

#define DAVINCI_MMCCTL       0x00 /* Control Register                  */
#define DAVINCI_MMCCLK       0x04 /* Memory Clock Control Register     */
#define DAVINCI_MMCST0       0x08 /* Status Register 0                 */
#define DAVINCI_MMCST1       0x0C /* Status Register 1                 */
#define DAVINCI_MMCIM        0x10 /* Interrupt Mask Register           */
#define DAVINCI_MMCTOR       0x14 /* Response Time-Out Register        */
#define DAVINCI_MMCTOD       0x18 /* Data Read Time-Out Register       */
#define DAVINCI_MMCBLEN      0x1C /* Block Length Register             */
#define DAVINCI_MMCNBLK      0x20 /* Number of Blocks Register         */
#define DAVINCI_MMCNBLC      0x24 /* Number of Blocks Counter Register */
#define DAVINCI_MMCDRR       0x28 /* Data Receive Register             */
#define DAVINCI_MMCDXR       0x2C /* Data Transmit Register            */
#define DAVINCI_MMCCMD       0x30 /* Command Register                  */
#define DAVINCI_MMCARGHL     0x34 /* Argument Register                 */
#define DAVINCI_MMCRSP01     0x38 /* Response Register 0 and 1         */
#define DAVINCI_MMCRSP23     0x3C /* Response Register 0 and 1         */
#define DAVINCI_MMCRSP45     0x40 /* Response Register 0 and 1         */
#define DAVINCI_MMCRSP67     0x44 /* Response Register 0 and 1         */
#define DAVINCI_MMCDRSP      0x48 /* Data Response Register            */
#define DAVINCI_MMCETOK      0x4C
#define DAVINCI_MMCCIDX      0x50 /* Command Index Register            */
#define DAVINCI_MMCCKC       0x54
#define DAVINCI_MMCTORC      0x58
#define DAVINCI_MMCTODC      0x5C
#define DAVINCI_MMCBLNC      0x60
#define DAVINCI_SDIOCTL      0x64
#define DAVINCI_SDIOST0      0x68
#define DAVINCI_SDIOEN       0x6C
#define DAVINCI_SDIOST       0x70
#define DAVINCI_MMCFIFOCTL   0x74 /* FIFO Control Register             */

/* DAVINCI_MMCCTL definitions */
#define MMCCTL_DATRST         (1 << 0)
#define MMCCTL_CMDRST         (1 << 1)
#define MMCCTL_WIDTH_4_BIT    (1 << 2)
#define MMCCTL_DATEG_DISABLED (0 << 6)
#define MMCCTL_DATEG_RISING   (1 << 6)
#define MMCCTL_DATEG_FALLING  (2 << 6)
#define MMCCTL_DATEG_BOTH     (3 << 6)
#define MMCCTL_PERMDR_LE      (0 << 9)
#define MMCCTL_PERMDR_BE      (1 << 9)
#define MMCCTL_PERMDX_LE      (0 << 10)
#define MMCCTL_PERMDX_BE      (1 << 10)

/* DAVINCI_MMCCLK definitions */
#define MMCCLK_CLKEN          (1 << 8)
#define MMCCLK_CLKRT_MASK     (0xFF << 0)

/* DAVINCI_MMCST0 definitions */
#define MMCST0_DATDNE         (1 << 0)
#define MMCST0_BSYDNE         (1 << 1)
#define MMCST0_RSPDNE         (1 << 2)
#define MMCST0_TOUTRD         (1 << 3)
#define MMCST0_TOUTRS         (1 << 4)
#define MMCST0_CRCWR          (1 << 5)
#define MMCST0_CRCRD          (1 << 6)
#define MMCST0_CRCRS          (1 << 7)
#define MMCST0_DXRDY          (1 << 9)
#define MMCST0_DRRDY          (1 << 10)
#define MMCST0_DATED          (1 << 11)
#define MMCST0_TRNDNE         (1 << 12)

/* DAVINCI_MMCST1 definitions */
#define MMCST1_BUSY           (1 << 0)

/* DAVINCI_MMCIM definitions */
#define MMCIM_EDATDNE         (1 << 0)
#define MMCIM_EBSYDNE         (1 << 1)
#define MMCIM_ERSPDNE         (1 << 2)
#define MMCIM_ETOUTRD         (1 << 3)
#define MMCIM_ETOUTRS         (1 << 4)
#define MMCIM_ECRCWR          (1 << 5)
#define MMCIM_ECRCRD          (1 << 6)
#define MMCIM_ECRCRS          (1 << 7)
#define MMCIM_EDXRDY          (1 << 9)
#define MMCIM_EDRRDY          (1 << 10)
#define MMCIM_EDATED          (1 << 11)
#define MMCIM_ETRNDNE         (1 << 12)

/* DAVINCI_MMCCMD definitions */
#define MMCCMD_CMD_MASK       (0x3F << 0)
#define MMCCMD_PPLEN          (1 << 7)
#define MMCCMD_BSYEXP         (1 << 8)
#define MMCCMD_RSPFMT_MASK    (3 << 9)
#define MMCCMD_RSPFMT_NONE    (0 << 9)
#define MMCCMD_RSPFMT_R1456   (1 << 9)
#define MMCCMD_RSPFMT_R2      (2 << 9)
#define MMCCMD_RSPFMT_R3      (3 << 9)
#define MMCCMD_DTRW           (1 << 11)
#define MMCCMD_STRMTP         (1 << 12)
#define MMCCMD_WDATX          (1 << 13)
#define MMCCMD_INITCK         (1 << 14)
#define MMCCMD_DCLR           (1 << 15)
#define MMCCMD_DMATRIG        (1 << 16)

/* DAVINCI_MMCFIFOCTL definitions */
#define MMCFIFOCTL_FIFORST    (1 << 0)
#define MMCFIFOCTL_FIFODIR_WR (1 << 1)
#define MMCFIFOCTL_FIFODIR_RD (0 << 1)
#define MMCFIFOCTL_FIFOLEV    (1 << 2) /* 0 = 128 bits, 1 = 256 bits */
#define MMCFIFOCTL_ACCWD_4    (0 << 3) /* access width of 4 bytes    */
#define MMCFIFOCTL_ACCWD_3    (1 << 3) /* access width of 3 bytes    */
#define MMCFIFOCTL_ACCWD_2    (2 << 3) /* access width of 2 bytes    */
#define MMCFIFOCTL_ACCWD_1    (3 << 3) /* access width of 1 byte     */


/*
 * Command types
 */
#define DAVINCI_MMC_CMDTYPE_BC	0
#define DAVINCI_MMC_CMDTYPE_BCR	1
#define DAVINCI_MMC_CMDTYPE_AC	2
#define DAVINCI_MMC_CMDTYPE_ADTC	3
#define EDMA_MAX_LOGICAL_CHA_ALLOWED 1

struct edma_ch_mmcsd {
	unsigned char cnt_chanel;
	unsigned int chanel_num[EDMA_MAX_LOGICAL_CHA_ALLOWED];
};

struct mmc_davinci_host {
	struct mmc_command *cmd;
	struct mmc_data *data;
	struct mmc_host *mmc;
	struct clk *clk;
	unsigned int mmc_input_clk;
	void __iomem *base;
	struct resource *mem_res;
	int irq;
	unsigned char bus_mode;

#define DAVINCI_MMC_DATADIR_NONE	0
#define DAVINCI_MMC_DATADIR_READ	1
#define DAVINCI_MMC_DATADIR_WRITE	2
	unsigned char data_dir;
	u8 *buffer;
	u32 bytes_left;

	bool use_dma;
	bool do_dma;

	struct timer_list timer;
	unsigned int is_core_command:1;
	unsigned int cmd_code;
	unsigned int old_card_state:1;

	unsigned char sd_support:1;

	struct edma_ch_mmcsd edma_ch_details;

	unsigned int sg_len;
	int sg_idx;
	unsigned int buffer_bytes_left;

	unsigned int option_read;
	unsigned int option_write;

	/* Indicates if card being used currently by linux core or not */
	unsigned int is_card_busy:1;

	/* Indicates if card probe(detection) is currently in progress */
	unsigned int is_card_detect_progress:1;

	/* Indicates if core is currently initializing the card or not */
	unsigned int is_init_progress:1;

	/* Indicate whether core request has been queued up or not because
	 * request has come when card detection/probe was in progress
	 */
	unsigned int is_req_queued_up:1;

	/* data structure to queue one request */
	struct mmc_request *que_mmc_request;

	/* tells whether card is initizlzed or not */
	int is_card_initialized:1;

	/* tells current state of card */
	unsigned int new_card_state:1;

	unsigned int is_card_removed:1;

	/* protect against mmc_check_card */
	spinlock_t lock;
};

struct mmcsd_config_def {
	unsigned short rw_threshold;
	unsigned short use_dma;
	unsigned short use_4bit_mode;
};

enum mmcsdevent {
	MMCSD_EVENT_EOFCMD = (1 << 2),
	MMCSD_EVENT_READ = (1 << 10),
	MMCSD_EVENT_WRITE = (1 << 9),
	MMCSD_EVENT_ERROR_CMDCRC = (1 << 7),
	MMCSD_EVENT_ERROR_DATACRC = ((1 << 6) | (1 << 5)),
	MMCSD_EVENT_ERROR_CMDTIMEOUT = (1 << 4),
	MMCSD_EVENT_ERROR_DATATIMEOUT = (1 << 3),
	MMCSD_EVENT_CARD_EXITBUSY = (1 << 1),
	MMCSD_EVENT_BLOCK_XFERRED = (1 << 0)
};

static void init_mmcsd_host(struct mmc_davinci_host *host);

static void davinci_fifo_data_trans(struct mmc_davinci_host *host, int n);

static void mmc_davinci_sg_to_buf(struct mmc_davinci_host *host);

static int mmc_davinci_send_dma_request(struct mmc_davinci_host *host,
					struct mmc_request *req);

static void mmc_davinci_xfer_done(struct mmc_davinci_host *host,
				  struct mmc_data *data);

static int mmc_davinci_get_ro(struct mmc_host *mmc);

static void davinci_abort_dma(struct mmc_davinci_host *host);

#endif /* DAVINCI_MMC_H_ */
