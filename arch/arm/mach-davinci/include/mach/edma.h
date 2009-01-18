/*
 *  BRIEF MODULE DESCRIPTION
 *      TI DAVINCI dma definitions
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
 *
 */

/*
 * The EDMA3 framework for DaVinci abstracts DMA Parameter RAM (PaRAM) slots
 * as logical DMA channels.  There are two types of logical channel:
 *
 *  Master	Triggers transfers, usually from a hardware event but
 *		also manually or by "chaining" from DMA completions.
 *		Not all PaRAM slots may be masters; and not all masters
 *		support hardware event triggering.
 *
 *  Slave	A master may be linked to a "slave" PaRAM slot, used to
 *		reload master parameters when a transfer finishes.  Any
 *		PaRAM slot may be such a link target.
 *
 * Each PaRAM slot holds a DMA transfer descriptor with destination and
 * source addresses, a link to the next PaRAM slot (if any), options for
 * the transfer, and instructions for updating those addresses.
 *
 * The EDMA Channel Controller (CC) maps requests from master channels
 * into physical Transfer Controller (TC) requests when the master
 * triggers.  The two physical DMA channels provided by the TC are thus
 * shared by many logical channels.
 *
 * DaVinci hardware also has a "QDMA" mechanism which is not currently
 * supported through this interface.  (DSP firmware uses it though.)
 */

#ifndef EDMA_H_
#define EDMA_H_

/* PaRAM slots are laid out like this */
struct edmacc_param {
	unsigned int opt;
	unsigned int src;
	unsigned int a_b_cnt;
	unsigned int dst;
	unsigned int src_dst_bidx;
	unsigned int link_bcntrld;
	unsigned int src_dst_cidx;
	unsigned int ccnt;
};

#define CCINT0_INTERRUPT     16
#define CCERRINT_INTERRUPT   17
#define TCERRINT0_INTERRUPT   18
#define TCERRINT1_INTERRUPT   19

/* fields in edmacc_param.opt */
#define SAM		BIT(0)
#define DAM		BIT(1)
#define SYNCDIM		BIT(2)
#define STATIC		BIT(3)
#define EDMA_FWID	(0x07 << 8)
#define TCCMODE		BIT(11)
#define TCC		(0x3f << 12)
#define TCINTEN		BIT(20)
#define ITCINTEN	BIT(21)
#define TCCHEN		BIT(22)
#define ITCCHEN		BIT(23)

#define TRWORD (0x7<<2)
#define PAENTRY (0x1ff<<5)


#define DAVINCI_EDMA_NUM_DMACH           64

#define DAVINCI_EDMA_NUM_PARAMENTRY     128
#define DAVINCI_EDMA_NUM_EVQUE            2
#define DAVINCI_EDMA_CHMAPEXIST           0
#define DAVINCI_EDMA_NUM_REGIONS          4
#define DAVINCI_EDMA_MEMPROTECT           0

#define DAVINCI_NUM_UNUSEDCH             21

#define TCC_ANY    -1

/* special values understood by davinci_request_dma() */
#define DAVINCI_EDMA_PARAM_ANY            -2
#define DAVINCI_DMA_CHANNEL_ANY           -1

/* Drivers should avoid using these symbolic names for dm644x
 * channels, and use platform_device IORESOURCE_DMA resources
 * instead.  (Other DaVinci chips have different peripherals
 * and thus have different DMA channel mappings.)
 */
#define DAVINCI_DMA_MCBSP_TX              2
#define DAVINCI_DMA_MCBSP_RX              3
#define DAVINCI_DMA_VPSS_HIST             4
#define DAVINCI_DMA_VPSS_H3A              5
#define DAVINCI_DMA_VPSS_PRVU             6
#define DAVINCI_DMA_VPSS_RSZ              7
#define DAVINCI_DMA_IMCOP_IMXINT          8
#define DAVINCI_DMA_IMCOP_VLCDINT         9
#define DAVINCI_DMA_IMCO_PASQINT         10
#define DAVINCI_DMA_IMCOP_DSQINT         11
#define DAVINCI_DMA_SPI_SPIX             16
#define DAVINCI_DMA_SPI_SPIR             17
#define DAVINCI_DMA_UART0_URXEVT0        18
#define DAVINCI_DMA_UART0_UTXEVT0        19
#define DAVINCI_DMA_UART1_URXEVT1        20
#define DAVINCI_DMA_UART1_UTXEVT1        21
#define DAVINCI_DMA_UART2_URXEVT2        22
#define DAVINCI_DMA_UART2_UTXEVT2        23
#define DAVINCI_DMA_MEMSTK_MSEVT         24
#define DAVINCI_DMA_MMCRXEVT             26
#define DAVINCI_DMA_MMCTXEVT             27
#define DAVINCI_DMA_I2C_ICREVT           28
#define DAVINCI_DMA_I2C_ICXEVT           29
#define DAVINCI_DMA_GPIO_GPINT0          32
#define DAVINCI_DMA_GPIO_GPINT1          33
#define DAVINCI_DMA_GPIO_GPINT2          34
#define DAVINCI_DMA_GPIO_GPINT3          35
#define DAVINCI_DMA_GPIO_GPINT4          36
#define DAVINCI_DMA_GPIO_GPINT5          37
#define DAVINCI_DMA_GPIO_GPINT6          38
#define DAVINCI_DMA_GPIO_GPINT7          39
#define DAVINCI_DMA_GPIO_GPBNKINT0       40
#define DAVINCI_DMA_GPIO_GPBNKINT1       41
#define DAVINCI_DMA_GPIO_GPBNKINT2       42
#define DAVINCI_DMA_GPIO_GPBNKINT3       43
#define DAVINCI_DMA_GPIO_GPBNKINT4       44
#define DAVINCI_DMA_TIMER0_TINT0         48
#define DAVINCI_DMA_TIMER1_TINT1         49
#define DAVINCI_DMA_TIMER2_TINT2         50
#define DAVINCI_DMA_TIMER3_TINT3         51
#define DAVINCI_DMA_PWM0                 52
#define DAVINCI_DMA_PWM1                 53
#define DAVINCI_DMA_PWM2                 54

/*ch_status paramater of callback function possible values*/
#define DMA_COMPLETE 1
#define DMA_CC_ERROR 2
#define DMA_TC1_ERROR 3
#define DMA_TC2_ERROR 4

enum address_mode {
	INCR = 0,
	FIFO = 1
};

enum fifo_width {
	W8BIT = 0,
	W16BIT = 1,
	W32BIT = 2,
	W64BIT = 3,
	W128BIT = 4,
	W256BIT = 5
};

enum dma_event_q {
	EVENTQ_0 = 0,
	EVENTQ_1 = 1,
	EVENTQ_DEFAULT = -1
};

enum sync_dimension {
	ASYNC = 0,
	ABSYNC = 1
};

int davinci_request_dma(int dev_id, const char *dev_name,
	void (*callback)(int lch, unsigned short ch_status, void *data),
	void *data, int *lch, int *tcc, enum dma_event_q);
void davinci_free_dma(int lch);

void davinci_set_dma_src_params(int lch, dma_addr_t src_port,
				enum address_mode mode, enum fifo_width);
void davinci_set_dma_dest_params(int lch, dma_addr_t dest_port,
				 enum address_mode mode, enum fifo_width);
void davinci_set_dma_src_index(int lch, s16 src_bidx, s16 src_cidx);
void davinci_set_dma_dest_index(int lch, s16 dest_bidx, s16 dest_cidx);
void davinci_set_dma_transfer_params(int lch, u16 acnt, u16 bcnt, u16 ccnt,
		u16 bcnt_rld, enum sync_dimension sync_mode);

void edma_write_slot(unsigned slot, const struct edmacc_param *params);
void edma_read_slot(unsigned slot, struct edmacc_param *params);

int davinci_start_dma(int lch);
void davinci_stop_dma(int lch);

/******************************************************************************
 * davinci_dma_link_lch - Link two Logical channels
 *
 * lch_head  - logical channel number, in which the link field is linked to the
 *             the param pointed to by lch_queue
 *             Can be a MasterChannel or SlaveChannel
 * lch_queue - logical channel number or the param entry number, which is to be
 *             linked to the lch_head
 *             Must be a SlaveChannel
 *
 *                     |---------------|
 *                     v               |
 *      Ex:    ch1--> ch2-->ch3-->ch4--|
 *
 *             ch1 must be a MasterChannel
 *
 *             ch2, ch3, ch4 must be SlaveChannels
 *
 * Note:       After channel linking,the user should not update any PaRam entry
 *             of MasterChannel ( In the above example ch1 )
 *
 *****************************************************************************/
void davinci_dma_link_lch(int lch_head, int lch_queue);

/******************************************************************************
 * davinci_dma_unlink_lch - unlink the two logical channels passed through by
 *                          setting the link field of head to 0xffff.
 *
 * lch_head  - logical channel number, from which the link field is to be
 *             removed
 * lch_queue - logical channel number or the param entry number,which is to be
 *             unlinked from lch_head
 *
 *****************************************************************************/
void davinci_dma_unlink_lch(int lch_head, int lch_queue);

void davinci_dma_getposition(int lch, dma_addr_t *src, dma_addr_t *dst);
void davinci_clean_channel(int lch);
void davinci_pause_dma(int lch);
void davinci_resume_dma(int lch);

int davinci_alloc_iram(unsigned size);
void davinci_free_iram(unsigned addr, unsigned size);
#endif
