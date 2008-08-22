/*
 * DaVinci EMAC Ethernet controller
 *
 * DaVinci EMAC ethernet controller is based upon CPPI 3.0 TI DMA engine
 * and this driver supports the following devices: DM644x as of now
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * ---------------------------------------------------------------------------
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
 * ---------------------------------------------------------------------------
 * History:
 * 0-5 A number of folks worked on this driver in bits and pieces but the major
 *     contribution came from Suraj Iyer and Anant Gole
 * 6.0 Anant Gole - rewrote the driver as per Linux conventions
 */

/** Pending Items in this driver:
 * 1. Replace emac mdio code with Linux generic mdio infrastructure
 * 2. Use Linux cache infrastcture for DMA'ed memory (dma_xxx functions)
 * 3. Add DM646x support (gigabit included)
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/highmem.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <mach/memory.h>
#include <mach/cpu.h>
#include <mach/hardware.h>

#include "davinci_emac_phy.h"

static int cfg_link_speed;
module_param(cfg_link_speed, int, 0);
MODULE_PARM_DESC(cfg_link_speed,
	"EMAC link speed : <0=Auto, 1=No Phy, 10, 100, 1000 mbps");

static int cfg_link_duplex;
module_param(cfg_link_duplex, int, 0);
MODULE_PARM_DESC(cfg_link_duplex,
	"EMAC link duplex: <0=Auto, 1=Unknown, 2=Half, 3=Full");

static int debug_level;
module_param(debug_level, int, 0);
MODULE_PARM_DESC(debug_level, "DaVinci EMAC debug level (NETIF_MSG bits)");

/* Netif debug messages possible */
#define DAVINCI_EMAC_DEBUG	(NETIF_MSG_DRV | \
				NETIF_MSG_PROBE | \
				NETIF_MSG_LINK | \
				NETIF_MSG_TIMER | \
				NETIF_MSG_IFDOWN | \
				NETIF_MSG_IFUP | \
				NETIF_MSG_RX_ERR | \
				NETIF_MSG_TX_ERR | \
				NETIF_MSG_TX_QUEUED | \
				NETIF_MSG_INTR | \
				NETIF_MSG_TX_DONE | \
				NETIF_MSG_RX_STATUS | \
				NETIF_MSG_PKTDATA | \
				NETIF_MSG_HW | \
				NETIF_MSG_WOL)

/* version info */
#define EMAC_MAJOR_VERSION	6
#define EMAC_MINOR_VERSION	0
#define EMAC_MODULE_VERSION	"6.0"
MODULE_VERSION(EMAC_MODULE_VERSION);
const char emac_version_string[] = "TI DaVinci EMAC Linux v6.0";

/* Configuration items */
#define EMAC_MDIO_DEBUG 		(0) /* use this debug with caution */
#define EMAC_DEF_PASS_CRC		(0) /* Do not pass CRC upto frames */
#define EMAC_DEF_QOS_EN 		(0) /* EMAC proprietary QoS disabled */
#define EMAC_DEF_NO_BUFF_CHAIN		(0) /* No buffer chain */
#define EMAC_DEF_MACCTRL_FRAME_EN	(0) /* Discard Maccontrol frames */
#define EMAC_DEF_SHORT_FRAME_EN 	(0) /* Discard short frames */
#define EMAC_DEF_ERROR_FRAME_EN 	(0) /* Discard error frames */
#define EMAC_DEF_PROM_EN		(0) /* Promiscous disabled */
#define EMAC_DEF_PROM_CH		(0) /* Promiscous channel is 0 */
#define EMAC_DEF_BCAST_EN		(1) /* Broadcast enabled */
#define EMAC_DEF_BCAST_CH		(0) /* Broadcast channel is 0 */
#define EMAC_DEF_MCAST_EN		(1) /* Multicast enabled */
#define EMAC_DEF_MCAST_CH		(0) /* Multicast channel is 0 */

#define EMAC_DEF_TXPRIO_FIXED		(1) /* TX Priority is fixed */
#define EMAC_DEF_TXPACING_EN		(0) /* TX pacing NOT supported*/

#define EMAC_DEF_BUFFER_OFFSET		(0) /* Buffer offset to DMA (future) */
#define EMAC_DEF_EXTRA_RXBUF_SIZE	(32)/* Extra bytes in each RX packet */
#define EMAC_DEF_MIN_ETHPKTSIZE 	(60) /* Minimum ethernet pkt size */
#define EMAC_DEF_MAX_FRAME_SIZE 	(1500 + 14 + 4 + 4 + \
					 (EMAC_DEF_EXTRA_RXBUF_SIZE))
#define EMAC_DEF_TX_CH			(0) /* Default 0th channel */
#define EMAC_DEF_RX_CH			(0) /* Default 0th channel */
#define EMAC_DEF_MDIO_TICK_MS		(10) /* typically 1 tick=1 ms) */
#define EMAC_DEF_MAX_TX_CH		(1) /* Max TX channels configured */
#define EMAC_DEF_MAX_RX_CH		(1) /* Max RX channels configured */
#define EMAC_POLL_WEIGHT		(64) /* Default NAPI poll weight */

/* Buffer descriptor parameters */
#define EMAC_DEF_TX_MAX_SERVICE 	(32) /* TX max service BD's */
#define EMAC_DEF_RX_MAX_SERVICE 	(64) /* should = netdev->weight */
#define EMAC_DEF_TX_NUM_BD		(128) /* Max num of TX BD's */
#define EMAC_DEF_RX_NUM_BD		(128) /* Max num of RX BD's */
#if ((EMAC_DEF_TX_NUM_BD + EMAC_DEF_RX_NUM_BD)	> 256)
#error "Error. DM644x has space for no more than total 256 (TX+RX) BD's"
#endif

/* Phy information */
#define EMAC_LINK_OFF		(0) /* Link down */
#define EMAC_LINK_ON		(1) /* Link up */
#define EMAC_SPEED_AUTO 	(0) /* Auto Negotiation */
#define EMAC_SPEED_NO_PHY	(1) /* No Phy - Always ON - Ext Sw */
#define EMAC_SPEED_10MBPS	(10) /* 10 Mbps */
#define EMAC_SPEED_100MBPS	(100) /* 100 Mbps */
#define EMAC_SPEED_1GBPS	(1000) /* 1 Gbpps */
#define EMAC_DUPLEX_UNKNOWN	(1) /* Not known - negotiating? */
#define EMAC_DUPLEX_HALF	(2) /* Half Duplex */
#define EMAC_DUPLEX_FULL	(3) /* Full Duplex */

/* config parameters that get tested for operation */
#define EMAC_MIN_FREQUENCY_FOR_10MBPS	(5500000)
#define EMAC_MIN_FREQUENCY_FOR_100MBPS	(55000000)
#define EMAC_MIN_FREQUENCY_FOR_1000MBPS (125000000)

/* TODO: This should come from platform data */
#define EMAC_EVM_PHY_MASK		(0x2)
#define EMAC_EVM_MLINK_MASK		(0)
#define EMAC_EVM_BUS_FREQUENCY		(76500000) /* PLL/6 i.e 76.5 MHz */
#define EMAC_EVM_MDIO_FREQUENCY 	(2200000) /* PHY bus frequency */

/* EMAC register related defines */
#define EMAC_ALL_MULTI_REG_VALUE	(0xFFFFFFFF)
#define EMAC_NUM_MULTICAST_BITS 	(64)
#define EMAC_TEARDOWN_VALUE		(0xFFFFFFFC)
#define EMAC_TX_CONTROL_TX_ENABLE_VAL	(0x1)
#define EMAC_RX_CONTROL_RX_ENABLE_VAL	(0x1)
#define EMAC_MAC_HOST_ERR_INTMASK_VAL	(0x2)
#define EMAC_RX_UNICAST_CLEAR_ALL	(0xFF)
#define EMAC_INT_MASK_CLEAR		(0xFF)

/* RX MBP register bit positions */
#define EMAC_RXMBP_PASSCRC_MASK 	(0x1 << 30)
#define EMAC_RXMBP_QOSEN_MASK		(0x1 << 29)
#define EMAC_RXMBP_NOCHAIN_MASK 	(0x1 << 28)
#define EMAC_RXMBP_CMFEN_MASK		(0x1 << 24)
#define EMAC_RXMBP_CSFEN_MASK		(0x1 << 23)
#define EMAC_RXMBP_CEFEN_MASK		(0x1 << 22)
#define EMAC_RXMBP_CAFEN_MASK		(0x1 << 21)
#define EMAC_RXMBP_PROMCH_SHIFT 	(16)
#define EMAC_RXMBP_PROMCH_MASK		(0x7 << 16)
#define EMAC_RXMBP_BROADEN_MASK 	(0x1 << 13)
#define EMAC_RXMBP_BROADCH_SHIFT	(8)
#define EMAC_RXMBP_BROADCH_MASK 	(0x7 << 8)
#define EMAC_RXMBP_MULTIEN_MASK 	(0x1 << 5)
#define EMAC_RXMBP_MULTICH_SHIFT	(0)
#define EMAC_RXMBP_MULTICH_MASK 	(0x7)
#define EMAC_RXMBP_CHMASK		(0x7)

/* EMAC register definitions/bit maps used */
# define EMAC_MBP_RXPROMISC		(0x00200000)
# define EMAC_MBP_PROMISCCH(ch) 	(((ch) & 0x7) << 16)
# define EMAC_MBP_RXBCAST		(0x00002000)
# define EMAC_MBP_BCASTCHAN(ch) 	(((ch) & 0x7) << 8)
# define EMAC_MBP_RXMCAST		(0x00000020)
# define EMAC_MBP_MCASTCHAN(ch) 	((ch) & 0x7)

/* EMAC mac_control register */
#define EMAC_MACCONTROL_TXPTYPE 	(0x200)
#define EMAC_MACCONTROL_TXPACEEN	(0x40)
#define EMAC_MACCONTROL_MIIEN		(0x20)
#define EMAC_MACCONTROL_GIGABITEN	(0x80)
#define EMAC_MACCONTROL_GIGABITEN_SHIFT (7)
#define EMAC_MACCONTROL_FULLDUPLEXEN	(0x1)

/* EMAC mac_status register */
#define EMAC_MACSTATUS_TXERRCODE_MASK	(0xF00000)
#define EMAC_MACSTATUS_TXERRCODE_SHIFT	(20)
#define EMAC_MACSTATUS_TXERRCH_MASK	(0x7)
#define EMAC_MACSTATUS_TXERRCH_SHIFT	(16)
#define EMAC_MACSTATUS_RXERRCODE_MASK	(0xF000)
#define EMAC_MACSTATUS_RXERRCODE_SHIFT	(12)
#define EMAC_MACSTATUS_RXERRCH_MASK	(0x7)
#define EMAC_MACSTATUS_RXERRCH_SHIFT	(8)

/* EMAC RX register masks */
#define EMAC_RX_MAX_LEN_MASK		(0xFFFF)
#define EMAC_RX_BUFFER_OFFSET_MASK	(0xFFFF)

/* MAC_IN_VECTOR (0x180) register bit fields */
#define EMAC_DM644X_MAC_IN_VECTOR_HOST_INT	      (0x20000)
#define EMAC_DM644X_MAC_IN_VECTOR_STATPEND_INT	      (0x10000)
#define EMAC_DM644X_MAC_IN_VECTOR_RX_INT_VEC	      (0xFF00)
#define EMAC_DM644X_MAC_IN_VECTOR_TX_INT_VEC	      (0xFF)

/* CPPI bit positions */
#define EMAC_CPPI_SOP_BIT		(1 << 31)
#define EMAC_CPPI_EOP_BIT		(1 << 30)
#define EMAC_CPPI_OWNERSHIP_BIT 	(1 << 29)
#define EMAC_CPPI_EOQ_BIT		(1 << 28)
#define EMAC_CPPI_TEARDOWN_COMPLETE_BIT (1 << 27)
#define EMAC_CPPI_PASS_CRC_BIT		(1 << 26)
#define EMAC_RX_BD_BUF_SIZE		(0xFFFF)
#define EMAC_BD_LENGTH_FOR_CACHE	(16) /* only CPPI bytes */
#define EMAC_RX_BD_PKT_LENGTH_MASK	(0xFFFF)

/* Max hardware defines */
#define EMAC_MAX_TXRX_CHANNELS		 (8)  /* Max hardware channels */
#define EMAC_DEF_MAX_MULTICAST_ADDRESSES (64) /* Max mcast addr's */

/* EMAC Peripheral Device Register Memory Layout structure */
#define EMAC_TXIDVER		0x0
#define EMAC_TXCONTROL		0x4
#define EMAC_TXTEARDOWN 	0x8
#define EMAC_RXIDVER		0x10
#define EMAC_RXCONTROL		0x14
#define EMAC_RXTEARDOWN 	0x18
#define EMAC_TXINTSTATRAW	0x80
#define EMAC_TXINTSTATMASKED	0x84
#define EMAC_TXINTMASKSET	0x88
#define EMAC_TXINTMASKCLEAR	0x8C
#define EMAC_MACINVECTOR	0x90

#define EMAC_RXINTSTATRAW	0xA0
#define EMAC_RXINTSTATMASKED	0xA4
#define EMAC_RXINTMASKSET	0xA8
#define EMAC_RXINTMASKCLEAR	0xAC
#define EMAC_MACINTSTATRAW	0xB0
#define EMAC_MACINTSTATMASKED	0xB4
#define EMAC_MACINTMASKSET	0xB8
#define EMAC_MACINTMASKCLEAR	0xBC

#define EMAC_RXMBPENABLE	0x100
#define EMAC_RXUNICASTSET	0x104
#define EMAC_RXUNICASTCLEAR	0x108
#define EMAC_RXMAXLEN		0x10C
#define EMAC_RXBUFFEROFFSET	0x110
#define EMAC_RXFILTERLOWTHRESH	0x114

#define EMAC_MACCONTROL 	0x160
#define EMAC_MACSTATUS		0x164
#define EMAC_EMCONTROL		0x168
#define EMAC_FIFOCONTROL	0x16C
#define EMAC_MACCONFIG		0x170
#define EMAC_SOFTRESET		0x174
#define EMAC_MACSRCADDRLO	0x1D0
#define EMAC_MACSRCADDRHI	0x1D4
#define EMAC_MACHASH1		0x1D8
#define EMAC_MACHASH2		0x1DC
#define EMAC_MACADDRLO		0x500
#define EMAC_MACADDRHI		0x504
#define EMAC_MACINDEX		0x508

/* EMAC HDP and Completion registors */
#define EMAC_TXHDP(ch)		(0x600 + (ch * 4))
#define EMAC_RXHDP(ch)		(0x620 + (ch * 4))
#define EMAC_TXCP(ch)		(0x640 + (ch * 4))
#define EMAC_RXCP(ch)		(0x660 + (ch * 4))

/* EMAC statistics registers */
#define EMAC_RXGOODFRAMES	0x200
#define EMAC_RXBCASTFRAMES	0x204
#define EMAC_RXMCASTFRAMES	0x208
#define EMAC_RXPAUSEFRAMES	0x20C
#define EMAC_RXCRCERRORS	0x210
#define EMAC_RXALIGNCODEERRORS	0x214
#define EMAC_RXOVERSIZED	0x218
#define EMAC_RXJABBER		0x21C
#define EMAC_RXUNDERSIZED	0x220
#define EMAC_RXFRAGMENTS	0x224
#define EMAC_RXFILTERED 	0x228
#define EMAC_RXQOSFILTERED	0x22C
#define EMAC_RXOCTETS		0x230
#define EMAC_TXGOODFRAMES	0x234
#define EMAC_TXBCASTFRAMES	0x238
#define EMAC_TXMCASTFRAMES	0x23C
#define EMAC_TXPAUSEFRAMES	0x240
#define EMAC_TXDEFERRED 	0x244
#define EMAC_TXCOLLISION	0x248
#define EMAC_TXSINGLECOLL	0x24C
#define EMAC_TXMULTICOLL	0x250
#define EMAC_TXEXCESSIVECOLL	0x254
#define EMAC_TXLATECOLL 	0x258
#define EMAC_TXUNDERRUN 	0x25C
#define EMAC_TXCARRIERSENSE	0x260
#define EMAC_TXOCTETS		0x264
#define EMAC_NETOCTETS		0x280
#define EMAC_RXSOFOVERRUNS	0x284
#define EMAC_RXMOFOVERRUNS	0x288
#define EMAC_RXDMAOVERRUNS	0x28C


/* DM644x specifics */
#define EMAC_BASE_REGS_OFFSET		(0x0)
#define EMAC_CONTROL_REGS_OFFSET	(0x1000)
#define EMAC_CONTROL_RAM_OFFSET 	(0x2000)
#define EMAC_MDIO_REGS_OFFSET		(0x4000)
#define EMAC_BUFFER_RAM_SIZE		(8 << 10) /* 8K */


/* EMAC DM644x control registers */
#define EMAC_CTRL_EWCTL 	(0x4)
#define EMAC_CTRL_EWINTTCNT	(0x8)


/** net_buf_obj: EMAC network bufferdata structure
 *
 * EMAC network buffer data structure
 */
struct emac_netbufobj {
	void *buf_token;
	char *data_ptr;
	int length;
};

/** net_pkt_obj: EMAC network packet data structure
 *
 * EMAC network packet data structure - supports buffer list (for future)
 */
struct emac_netpktobj {
	void *pkt_token; /* data token may hold tx/rx chan id */
	struct emac_netbufobj *buf_list; /* array of network buffer objects */
	int num_bufs;
	int pkt_length;
};

/** emac_tx_bd: EMAC TX Buffer descriptor data structure
 *
 * EMAC TX Buffer descriptor data structure
 */
struct emac_tx_bd {
	int h_next;
	int buff_ptr;
	int off_b_len;
	int mode; /* SOP, EOP, ownership, EOQ, teardown,Qstarv, length */
	void *next;
	void *buf_token;
};

/** emac_txch: EMAC TX Channel data structure
 *
 * EMAC TX Channel data structure
 */
struct emac_txch {
	/* Config related */
	u32 num_bd;
	u32 service_max;

	/* CPPI specific */
	u32 alloc_size;
	char *bd_mem;
	struct emac_tx_bd *bd_pool_head;
	struct emac_tx_bd *active_queue_head;
	struct emac_tx_bd *active_queue_tail;
	struct emac_tx_bd *last_hw_bdprocessed;
	u32 queue_active;
	u32 teardown_pending;
	u32 *tx_complete;

	/** statistics */
	u32 proc_count;     /* TX: # of times emac_tx_bdproc is called */
	u32 mis_queued_packets;
	u32 queue_reinit;
	u32 end_of_queue_add;
	u32 out_of_tx_bd;
	u32 no_active_pkts; /* IRQ when there were no packets to process */
	u32 active_queue_count;
};

/** emac_rx_bd: EMAC RX Buffer descriptor data structure
 *
 * EMAC RX Buffer descriptor data structure
 */
struct emac_rx_bd {
	int h_next;
	int buff_ptr;
	int off_b_len;
	int mode;
	void *next;
	void *data_ptr;
	void *buf_token;
};

/** emac_rxch: EMAC RX Channel data structure
 *
 * EMAC RX Channel data structure
 */
struct emac_rxch {
	/* configuration info */
	u32 num_bd;
	u32 service_max;
	u32 buf_size;
	char mac_addr[6];

	/** CPPI specific */
	u32 alloc_size;
	char *bd_mem;
	struct emac_rx_bd *bd_pool_head;
	struct emac_rx_bd *active_queue_head;
	struct emac_rx_bd *active_queue_tail;
	u32 queue_active;
	u32 teardown_pending;

	/* packet and buffer objects */
	struct emac_netpktobj pkt_queue;
	struct emac_netbufobj buf_queue;

	/** statistics */
	u32 proc_count; /* number of times emac_rx_bdproc is called */
	u32 processed_bd;
	u32 recycled_bd;
	u32 out_of_rx_bd;
	u32 out_of_rx_buffers;
	u32 queue_reinit;
	u32 end_of_queue_add;
	u32 end_of_queue;
	u32 mis_queued_packets;
};

/** emac_mdio: EMAC MDIO data structure
 *
 * EMAC MDIO data structure
 */
struct emac_mdio {
	u32 mdio_base_address;
	u32 mdio_reset_line;
	u32 mdio_intr_line;
	u32 phy_mask;
	u32 MLink_mask;
	u32 mdio_bus_frequency;
	u32 mdio_clock_frequency;
	u32 mdio_tick_msec;
};

/* emac_priv: EMAC private data structure
 *
 * EMAC adapter private data structure
 */
struct emac_priv {
	u32 msg_enable;
	struct net_device *ndev;
	struct platform_device *pdev;
	struct napi_struct napi;
	char mac_addr[6];
	char mac_str[20];
	spinlock_t tx_lock;
	spinlock_t rx_lock;
	u32 emac_base_regs;
	u32 emac_ctrl_regs;
	u32 emac_ctrl_ram;
	u32 mdio_regs;
	struct emac_mdio mdio;
	struct emac_txch *txch[EMAC_DEF_MAX_TX_CH];
	struct emac_rxch *rxch[EMAC_DEF_MAX_RX_CH];
	u32 link; /* 1=link on, 0=link off */
	u32 speed; /* 0=Auto Neg, 1=No PHY, 10,100, 1000 - mbps */
	u32 duplex; /* Link duplex: 1=Unknown, 2=Half, 3=Full */
	u32 rx_buf_size;
	u32 isr_count;
	struct net_device_stats net_dev_stats;
	u32 mac_hash1;
	u32 mac_hash2;
	u32 multicast_hash_cnt[EMAC_NUM_MULTICAST_BITS];
	u32 rx_addr_type;
	/* periodic timer required for MDIO polling */
	struct timer_list periodic_timer;
	u32 periodic_ticks;
	u32 timer_active;
};

/* clock frequency for EMAC */
static struct clk *emac_clk;
static unsigned long emac_bus_frequency;

/* EMAC internal utility function */
static u32 emac_base_addr;
static inline u32 emac_virt_to_phys(u32 addr)
{
	/* NOTE: must handle memory and IO addresses */
	if ((addr & 0xFFFF0000) == emac_base_addr)
		addr = io_v2p(addr);
	else
		addr = virt_to_phys((void *)addr);

	return addr;
}


/* Cache macros - Packet buffers would be from skb pool which is cached */
#define EMAC_VIRT2PHYS(x) emac_virt_to_phys((u32)x)
#define EMAC_VIRT_NOCACHE(addr) (addr)
#define EMAC_CACHE_INVALIDATE(addr, size) \
	dma_cache_maint((void *)addr, size, DMA_FROM_DEVICE)
#define EMAC_CACHE_WRITEBACK(addr, size) \
	dma_cache_maint((void *)addr, size, DMA_TO_DEVICE)
#define EMAC_CACHE_WRITEBACK_INVALIDATE(addr, size) \
	dma_cache_maint((void *)addr, size, DMA_BIDIRECTIONAL)

/* DM644x does not have BD's in cached memory - so no cache functions */
#define BD_CACHE_INVALIDATE(addr, size)
#define BD_CACHE_WRITEBACK(addr, size)
#define BD_CACHE_WRITEBACK_INVALIDATE(addr, size)

/* EMAC TX Host Error description strings */
static char *emac_txhost_errcodes[16] = {
	"No error", "SOP error", "Ownership bit not set in SOP buffer",
	"Zero Next Buffer Descriptor Pointer Without EOP",
	"Zero Buffer Pointer", "Zero Buffer Length", "Packet Length Error",
	"Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved"
};

/* EMAC RX Host Error description strings */
static char *emac_rxhost_errcodes[16] = {
	"No error", "Reserved", "Ownership bit not set in input buffer",
	"Reserved", "Zero Buffer Pointer", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved"
};

/* Helper macros */
#define EMAC_DEV &priv->ndev->dev
#define emac_read(reg)		davinci_readl((priv->emac_base_regs + (reg)))
#define emac_write(reg, val) \
	davinci_writel(val, (priv->emac_base_regs + (reg)))

#define emac_ctrl_read(reg)	davinci_readl((priv->emac_ctrl_regs + (reg)))
#define emac_ctrl_write(reg, val) \
	davinci_writel(val, (priv->emac_ctrl_regs + (reg)))


/**
 * emac_dump_regs: Dump important EMAC registers to debug terminal
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Executes ethtool set cmd & sets phy mode
 *
 */
static void emac_dump_regs(struct emac_priv *priv)
{
	/* Print important registers in EMAC */

	dev_info(EMAC_DEV, "EMACBasic registers\n");
	dev_info(EMAC_DEV, "EMAC: EWCTL: %08X, EWINTTCNT: %08X\n",
		emac_ctrl_read(EMAC_CTRL_EWCTL),
		emac_ctrl_read(EMAC_CTRL_EWINTTCNT));
	dev_info(EMAC_DEV, "EMAC: TXID: %08X %s, RXID: %08X %s\n",
		emac_read(EMAC_TXIDVER),
		((emac_read(EMAC_TXCONTROL)) ? "enabled" : "disabled"),
		emac_read(EMAC_RXIDVER),
		((emac_read(EMAC_RXCONTROL)) ? "enabled" : "disabled"));
	dev_info(EMAC_DEV, "EMAC: TXIntRaw:%08X, TxIntMasked: %08X, "\
		"TxIntMasSet: %08X\n", emac_read(EMAC_TXINTSTATRAW),
		emac_read(EMAC_TXINTSTATMASKED), emac_read(EMAC_TXINTMASKSET));
	dev_info(EMAC_DEV, "EMAC: RXIntRaw:%08X, RxIntMasked: %08X, "\
		"RxIntMasSet: %08X\n", emac_read(EMAC_RXINTSTATRAW),
		emac_read(EMAC_RXINTSTATMASKED), emac_read(EMAC_RXINTMASKSET));
	dev_info(EMAC_DEV, "EMAC: MacIntRaw:%08X, MacIntMasked: %08X, "\
		"MacInVector=%08X\n", emac_read(EMAC_MACINTSTATRAW),
		emac_read(EMAC_MACINTSTATMASKED), emac_read(EMAC_MACINVECTOR));
	dev_info(EMAC_DEV, "EMAC: EmuControl:%08X, FifoControl: %08X\n",
		emac_read(EMAC_EMCONTROL), emac_read(EMAC_FIFOCONTROL));
	dev_info(EMAC_DEV, "EMAC: MBPEnable:%08X, RXUnicastSet: %08X, "\
		"RXMaxLen=%08X\n", emac_read(EMAC_RXMBPENABLE),
		emac_read(EMAC_RXUNICASTSET), emac_read(EMAC_RXMAXLEN));
	dev_info(EMAC_DEV, "EMAC: MacControl:%08X, MacStatus: %08X, "\
		"MacConfig=%08X\n", emac_read(EMAC_MACCONTROL),
		emac_read(EMAC_MACSTATUS), emac_read(EMAC_MACCONFIG));
	dev_info(EMAC_DEV, "EMAC: TXHDP[0]:%08X, RXHDP[0]: %08X\n",
		emac_read(EMAC_TXHDP(0)), emac_read(EMAC_RXHDP(0)));
	dev_info(EMAC_DEV, "EMAC Statistics\n");
	dev_info(EMAC_DEV, "EMAC: rx_good_frames:%d\n",
		emac_read(EMAC_RXGOODFRAMES));
	dev_info(EMAC_DEV, "EMAC: rx_broadcast_frames:%d\n",
		emac_read(EMAC_RXBCASTFRAMES));
	dev_info(EMAC_DEV, "EMAC: rx_multicast_frames:%d\n",
		emac_read(EMAC_RXMCASTFRAMES));
	dev_info(EMAC_DEV, "EMAC: rx_pause_frames:%d\n",
		emac_read(EMAC_RXPAUSEFRAMES));
	dev_info(EMAC_DEV, "EMAC: rx_crcerrors:%d\n",
		emac_read(EMAC_RXCRCERRORS));
	dev_info(EMAC_DEV, "EMAC: rx_align_code_errors:%d\n",
		emac_read(EMAC_RXALIGNCODEERRORS));
	dev_info(EMAC_DEV, "EMAC: rx_oversized_frames:%d\n",
		emac_read(EMAC_RXOVERSIZED));
	dev_info(EMAC_DEV, "EMAC: rx_jabber_frames:%d\n",
		emac_read(EMAC_RXJABBER));
	dev_info(EMAC_DEV, "EMAC: rx_undersized_frames:%d\n",
		emac_read(EMAC_RXUNDERSIZED));
	dev_info(EMAC_DEV, "EMAC: rx_fragments:%d\n",
		emac_read(EMAC_RXFRAGMENTS));
	dev_info(EMAC_DEV, "EMAC: rx_filtered_frames:%d\n",
		emac_read(EMAC_RXFILTERED));
	dev_info(EMAC_DEV, "EMAC: rx_qos_filtered_frames:%d\n",
		emac_read(EMAC_RXQOSFILTERED));
	dev_info(EMAC_DEV, "EMAC: rx_octets:%d\n",
		emac_read(EMAC_RXOCTETS));
	dev_info(EMAC_DEV, "EMAC: tx_goodframes:%d\n",
		emac_read(EMAC_TXGOODFRAMES));
	dev_info(EMAC_DEV, "EMAC: tx_bcastframes:%d\n",
		emac_read(EMAC_TXBCASTFRAMES));
	dev_info(EMAC_DEV, "EMAC: tx_mcastframes:%d\n",
		emac_read(EMAC_TXMCASTFRAMES));
	dev_info(EMAC_DEV, "EMAC: tx_pause_frames:%d\n",
		emac_read(EMAC_TXPAUSEFRAMES));
	dev_info(EMAC_DEV, "EMAC: tx_deferred_frames:%d\n",
		emac_read(EMAC_TXDEFERRED));
	dev_info(EMAC_DEV, "EMAC: tx_collision_frames:%d\n",
		emac_read(EMAC_TXCOLLISION));
	dev_info(EMAC_DEV, "EMAC: tx_single_coll_frames:%d\n",
		emac_read(EMAC_TXSINGLECOLL));
	dev_info(EMAC_DEV, "EMAC: tx_mult_coll_frames:%d\n",
		emac_read(EMAC_TXMULTICOLL));
	dev_info(EMAC_DEV, "EMAC: tx_excessive_collisions:%d\n",
		emac_read(EMAC_TXEXCESSIVECOLL));
	dev_info(EMAC_DEV, "EMAC: tx_late_collisions:%d\n",
		emac_read(EMAC_TXLATECOLL));
	dev_info(EMAC_DEV, "EMAC: tx_underrun:%d\n",
		emac_read(EMAC_TXUNDERRUN));
	dev_info(EMAC_DEV, "EMAC: tx_carrier_sense_errors:%d\n",
		emac_read(EMAC_TXCARRIERSENSE));
	dev_info(EMAC_DEV, "EMAC: tx_octets:%d\n",
		emac_read(EMAC_TXOCTETS));
	dev_info(EMAC_DEV, "EMAC: net_octets:%d\n",
		emac_read(EMAC_NETOCTETS));
	dev_info(EMAC_DEV, "EMAC: rx_sof_overruns:%d\n",
		emac_read(EMAC_RXSOFOVERRUNS));
	dev_info(EMAC_DEV, "EMAC: rx_mof_overruns:%d\n",
		emac_read(EMAC_RXMOFOVERRUNS));
	dev_info(EMAC_DEV, "EMAC: rx_dma_overruns:%d\n",
		emac_read(EMAC_RXDMAOVERRUNS));
	dev_info(EMAC_DEV, "\n");
}

/*************************************************************************
 *  EMAC MDIO/Phy Functionality
 *************************************************************************/

/**
 * emac_netdev_set_ecmd: Set EMAC information to phy
 * @ndev: The DaVinci EMAC network adapter
 * @ecmd: ethtool command
 *
 * Executes ethtool set cmd & sets phy mode
 *
 */
static int emac_netdev_set_ecmd(struct net_device *ndev,
				struct ethtool_cmd *ecmd)
{
	int speed_duplex = 0;
	u32 phy_mode = 0;

	if (ecmd->autoneg)
		phy_mode |= NWAY_AUTO;

	speed_duplex = ecmd->speed + ecmd->duplex;
	switch (speed_duplex) {
	case 10:	/* HD10 */
		phy_mode |= NWAY_HD10;
		break;
	case 11: /* FD10 */
		phy_mode |= NWAY_FD10;
		break;
	case 100: /* HD100 */
		phy_mode |= NWAY_HD100;
		break;
	case 101:	/* FD100 */
		phy_mode |= NWAY_FD100;
		break;
	case 1000:	 /* HD100 */
		phy_mode |= NWAY_HD1000;
		break;
	case 1001:	/* FD1000 */
		phy_mode |= NWAY_FD1000;
		break;
	default:
		return -1;
	}
	emac_mdio_set_phy_mode(phy_mode);
	return 0;
}


/**
 * emac_netdev_get_ecmd: Get EMAC information from phy
 * @ndev: The DaVinci EMAC network adapter
 * @ecmd: ethtool command
 *
 * Executes ethtool get cmd & returns EMAC driver information from phy
 *
 */
static int emac_netdev_get_ecmd(struct net_device *ndev,
				struct ethtool_cmd *ecmd)
{
	int dplx = emac_mdio_get_duplex();

	/* Hard-coded, but should perhaps be retrieved from davinci_emac_phy */
	/* TODO: ecmd->supported = emac_mdio_supported_rate(); */
	/* TODO: ecmd->advertising = emac_mdio_autoneg_rate(); */
	/* TODO: ecmd->autoneg = emac_mdio_get_autoneg(); */
	ecmd->speed = emac_mdio_get_speed();
	ecmd->transceiver = XCVR_EXTERNAL;
	ecmd->port = PORT_MII;
	ecmd->phy_address = emac_mdio_get_phy_num();
	ecmd->duplex = (dplx == 3) ? DUPLEX_FULL : DUPLEX_HALF;
	return 0;
}

/**
 * emac_get_drvinfo: Get EMAC driver information
 * @ndev: The DaVinci EMAC network adapter
 * @info: ethtool info structure containing name and version
 *
 * Returns EMAC driver information (name and version)
 *
 */
static void emac_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	strcpy(info->driver, emac_version_string);
	strcpy(info->version, EMAC_MODULE_VERSION);
}

/**
 * emac_get_settings: Get EMAC settings
 * @ndev: The DaVinci EMAC network adapter
 * @ecmd: ethtool command
 *
 * Executes ethool get command
 *
 */
static int emac_get_settings(struct net_device *ndev,
			     struct ethtool_cmd *ecmd)
{
	return (emac_netdev_get_ecmd(ndev, ecmd));
}

/**
 * emac_set_settings: Set EMAC settings
 * @ndev: The DaVinci EMAC network adapter
 * @ecmd: ethtool command
 *
 * Executes ethool set command
 *
 */
static int emac_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	return (emac_netdev_set_ecmd(ndev, ecmd));
}

/**
 * emac_get_link: Get EMAC adapter link status
 * @ndev: The DaVinci EMAC network adapter
 *
 * Returns link status	link(1), no link (0)
 *
 */
static u32 emac_get_link(struct net_device *ndev)
{
	return (emac_mdio_is_linked());
}

/**
 * ethtool_ops: DaVinci EMAC Ethtool structure
 *
 * Ethtool support for EMAC adapter
 *
 */
struct ethtool_ops ethtool_ops = {
	.get_drvinfo = emac_get_drvinfo,
	.get_settings = emac_get_settings,
	.set_settings = emac_set_settings,
	.get_link = emac_get_link,
};


/**
 * emac_update_phystatus: Update Phy status
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Updates phy status and takes action for network queue if required
 * based upon link status
 *
 */
static void emac_update_phystatus(struct emac_priv *priv)
{
	u32 mac_control;
	u32 new_duplex;
	struct net_device *ndev = priv->ndev;

	mac_control = emac_read(EMAC_MACCONTROL);

	/* Get link status from MDIO */
	priv->link = emac_mdio_is_linked();
	priv->speed = emac_mdio_get_speed();
	new_duplex = emac_mdio_get_duplex();

	if (EMAC_SPEED_NO_PHY == cfg_link_speed) {
		priv->link = 1; /* Link always on when no phy */
		priv->duplex = EMAC_DUPLEX_UNKNOWN;
		mac_control |= (EMAC_MACCONTROL_FULLDUPLEXEN);
	}

	/* We get called only if link has changed (speed/duplex/status) */
	if ((priv->link) && (new_duplex != priv->duplex)) {
		priv->duplex = new_duplex;
		if (EMAC_DUPLEX_FULL == priv->duplex)
			mac_control |= (EMAC_MACCONTROL_FULLDUPLEXEN);
		else
			mac_control &= ~(EMAC_MACCONTROL_FULLDUPLEXEN);
	}
	/* Update mac_control if changed */
	emac_write(EMAC_MACCONTROL, mac_control);

	if (priv->link) {
		/* link ON */
		if (!netif_carrier_ok(ndev))
			netif_carrier_on(ndev);
	/* reactivate the transmit queue if it is stopped */
		if (netif_running(ndev) && netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
	} else {
		/* link OFF */
		if (netif_carrier_ok(ndev))
			netif_carrier_off(ndev);
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
	}
}

/* Auto all covers all speeds and duplex modes */
#define NWAY_AUTO_ALL (NWAY_AUTO | NWAY_HD10 | NWAY_FD10 | NWAY_HD100 | \
		       NWAY_FD100 | NWAY_HD1000 | NWAY_FD1000)

/**
 * emac_set_phymode: Set phy mode like speed, duplex, auto neg parameters
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Sets phy mode (parameters like speed, duplex, auto negotiation) on the PHY
 *
 */
static void emac_set_phymode(struct emac_priv *priv)
{
	u32 phy_mode;

	/* Check module and config params and set MDIO mode accordingly */
	if (EMAC_SPEED_NO_PHY == cfg_link_speed) {
		priv->speed = EMAC_SPEED_NO_PHY;
		priv->duplex = EMAC_DUPLEX_UNKNOWN;
		phy_mode = NWAY_NOPHY;
	} else if (EMAC_SPEED_AUTO == cfg_link_speed) {
		priv->speed = EMAC_SPEED_AUTO;
		priv->duplex = EMAC_DUPLEX_UNKNOWN;
		phy_mode = NWAY_AUTO_ALL;
	} else if (EMAC_SPEED_10MBPS == cfg_link_speed) {
		/* Check if bus speed allows 10mbps */
		if (priv->mdio.mdio_bus_frequency <=
		    EMAC_MIN_FREQUENCY_FOR_10MBPS) {
			if (netif_msg_drv(priv)) {
			dev_warn(EMAC_DEV, "DaVinci EMAC: EMAC Bus freq %d"\
				 " should be > than %d - for 10 mbps support",
				 priv->mdio.mdio_bus_frequency,
				 EMAC_MIN_FREQUENCY_FOR_10MBPS);
			}
		}
		priv->speed = EMAC_SPEED_10MBPS;
		if (EMAC_DUPLEX_HALF == cfg_link_duplex) {
			phy_mode = NWAY_HD10;
			priv->duplex = EMAC_DUPLEX_HALF;
		} else {
			phy_mode = NWAY_FD10;
			priv->duplex = EMAC_DUPLEX_FULL;
		}
	} else if (EMAC_SPEED_100MBPS == cfg_link_speed) {
		if (priv->mdio.mdio_bus_frequency <=
		    EMAC_MIN_FREQUENCY_FOR_100MBPS) {
			if (netif_msg_drv(priv)) {
				dev_warn(EMAC_DEV, "DaVinci EMAC: EMAC Bus "\
					 "freq %d should be greater than %d"\
					 " - for 100 mbps support",
					 priv->mdio.mdio_bus_frequency,
					 EMAC_MIN_FREQUENCY_FOR_100MBPS);
			}
		}
		priv->speed = EMAC_SPEED_100MBPS;
		if (EMAC_DUPLEX_HALF == cfg_link_duplex) {
			phy_mode = NWAY_HD100;
			priv->duplex = EMAC_DUPLEX_HALF;
		} else {
			phy_mode = NWAY_FD100;
			priv->duplex = EMAC_DUPLEX_FULL;
		}
	} else if (EMAC_SPEED_1GBPS == cfg_link_speed) {
		phy_mode = NWAY_AUTO_ALL; /* Temporarily */
		priv->speed = EMAC_SPEED_AUTO;
		priv->duplex = EMAC_DUPLEX_UNKNOWN;
	} else {
		phy_mode = NWAY_AUTO_ALL; /* Fall back if wrong params set */
		priv->speed = EMAC_SPEED_AUTO;
		priv->duplex = EMAC_DUPLEX_UNKNOWN;
	}
	emac_mdio_set_phy_mode(phy_mode);
	emac_update_phystatus(priv);
}

/**
 * hash_get: Calculate hash value from mac address
 * @addr: mac address to delete from hash table
 *
 * Calculates hash value from mac address
 *
 */
static u32 hash_get(u8 *addr)
{
	u32 hash;
	u8 tmpval;
	int cnt;
	hash = 0;

	for (cnt = 0; cnt < 2; cnt++) {
		tmpval = *addr++;
		hash ^= (tmpval >> 2) ^ (tmpval << 4);
		tmpval = *addr++;
		hash ^= (tmpval >> 4) ^ (tmpval << 2);
		tmpval = *addr++;
		hash ^= (tmpval >> 6) ^ (tmpval);
	}

	return (hash & 0x3F);
}

/**
 * hash_add: Hash function to add mac addr from hash table
 * @priv: The DaVinci EMAC private adapter structure
 * mac_addr: mac address to delete from hash table
 *
 * Adds mac address to the internal hash table
 *
 */
static int hash_add(struct emac_priv *priv, u8 *mac_addr)
{
	u32 rc = 0;
	u32 hash_bit;
	u32 hash_value = hash_get(mac_addr);

	if (hash_value >= EMAC_NUM_MULTICAST_BITS) {
		if (netif_msg_drv(priv)) {
			dev_err(EMAC_DEV, "DaVinci EMAC: hash_add(): Invalid "\
				"Hash %08x, should not be greater than %08x",
				hash_value, (EMAC_NUM_MULTICAST_BITS - 1));
		}
		return (-1);
	}

	/* set the hash bit only if not previously set */
	if (priv->multicast_hash_cnt[hash_value] == 0) {
		rc = 1; /* hash value changed */
		if (hash_value < 32) {
			hash_bit = (1 << hash_value);
			priv->mac_hash1 |= hash_bit;
		} else {
			hash_bit = (1 << (hash_value - 32));
			priv->mac_hash2 |= hash_bit;
		}
	}

	/* incr counter for num of mcast addr's mapped to "this" hash bit */
	++priv->multicast_hash_cnt[hash_value];

	return (rc);
}

/**
 * hash_del: Hash function to delete mac addr from hash table
 * @priv: The DaVinci EMAC private adapter structure
 * mac_addr: mac address to delete from hash table
 *
 * Removes mac address from the internal hash table
 *
 */
static int hash_del(struct emac_priv *priv, u8 *mac_addr)
{
	u32 hash_value;
	u32 hash_bit;

	hash_value = hash_get(mac_addr);
	if (priv->multicast_hash_cnt[hash_value] > 0) {
		/* dec cntr for num of mcast addr's mapped to this hash bit */
		--priv->multicast_hash_cnt[hash_value];
	}

	/* if counter still > 0, at least one multicast address refers
	 * to this hash bit. so return 0 */
	if (priv->multicast_hash_cnt[hash_value] > 0)
		return (0);

	if (hash_value < 32) {
		hash_bit = (1 << hash_value);
		priv->mac_hash1 &= ~hash_bit;
	} else {
		hash_bit = (1 << (hash_value - 32));
		priv->mac_hash2 &= ~hash_bit;
	}

	/* return 1 to indicate change in mac_hash registers reqd */
	return (1);
}

/* EMAC multicast operation */
#define EMAC_MULTICAST_ADD	0
#define EMAC_MULTICAST_DEL	1
#define EMAC_ALL_MULTI_SET	2
#define EMAC_ALL_MULTI_CLR	3

/**
 * emac_add_mcast: Set multicast address in the EMAC adapter (Internal)
 * @priv: The DaVinci EMAC private adapter structure
 * @action: multicast operation to perform
 * mac_addr: mac address to set
 *
 * Set multicast addresses in EMAC adapter - internal function
 *
 */
static void emac_add_mcast(struct emac_priv *priv, u32 action, u8 *mac_addr)
{
	int update = -1;

	switch (action) {
	case EMAC_MULTICAST_ADD:
		update = hash_add(priv, mac_addr);
		break;
	case EMAC_MULTICAST_DEL:
		update = hash_del(priv, mac_addr);
		break;
	case EMAC_ALL_MULTI_SET:
		update = 1;
		priv->mac_hash1 = EMAC_ALL_MULTI_REG_VALUE;
		priv->mac_hash2 = EMAC_ALL_MULTI_REG_VALUE;
		break;
	case EMAC_ALL_MULTI_CLR:
		update = 1;
		priv->mac_hash1 = 0;
		priv->mac_hash2 = 0;
		memset(&(priv->multicast_hash_cnt[0]), 0,
		sizeof(priv->multicast_hash_cnt[0]) *
		       EMAC_NUM_MULTICAST_BITS);
		break;
	default:
		if (netif_msg_drv(priv))
			dev_err(EMAC_DEV, "DaVinci EMAC: add_mcast"\
				": bad operation %d", action);
		break;
	}

	/* write to the hardware only if the register status chances */
	if (update > 0) {
		emac_write(EMAC_MACHASH1, priv->mac_hash1);
		emac_write(EMAC_MACHASH2, priv->mac_hash2);
	}
}

/**
 * emac_dev_mcast_set: Set multicast address in the EMAC adapter
 * @ndev: The DaVinci EMAC network adapter
 *
 * Set multicast addresses in EMAC adapter
 *
 */
static void emac_dev_mcast_set(struct net_device *ndev)
{
	u32 mbp_enable;
	struct emac_priv *priv = netdev_priv(ndev);

	mbp_enable = emac_read(EMAC_RXMBPENABLE);
	if (ndev->flags & IFF_PROMISC) {
		mbp_enable &= (~EMAC_MBP_PROMISCCH(EMAC_DEF_PROM_CH));
		mbp_enable |= (EMAC_MBP_RXPROMISC);
	} else {
		mbp_enable = (mbp_enable & ~EMAC_MBP_RXPROMISC);
		if ((ndev->flags & IFF_ALLMULTI) ||
		    (ndev->mc_count > EMAC_DEF_MAX_MULTICAST_ADDRESSES)) {
			mbp_enable = (mbp_enable | EMAC_MBP_RXMCAST);
			emac_add_mcast(priv, EMAC_ALL_MULTI_SET, 0);
		}
		if (ndev->mc_count > 0) {
			struct dev_mc_list *mc_ptr;
			mbp_enable = (mbp_enable | EMAC_MBP_RXMCAST);
			emac_add_mcast(priv, EMAC_ALL_MULTI_CLR, 0);
			/* program multicast address list into EMAC hardware */
			for (mc_ptr = ndev->mc_list; mc_ptr;
			     mc_ptr = mc_ptr->next) {
				emac_add_mcast(priv, EMAC_MULTICAST_ADD,
					       (u8 *)mc_ptr->dmi_addr);
			}
		} else {
			mbp_enable = (mbp_enable & ~EMAC_MBP_RXMCAST);
			emac_add_mcast(priv, EMAC_ALL_MULTI_CLR, 0);
		}
	}
	/* Set mbp config register */
	emac_write(EMAC_RXMBPENABLE, mbp_enable);
}


/**
 * emac_str_to_hexnum: String to hex number conversion (Internal)
 * @c: string to convert to hex number
 *
 * Internal helper function to convert string to hex number
 */
static unsigned char emac_str_to_hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else
		return 0;
}

/**
 * emac_str_to_ethaddr: ConvertGet EMAC mac address from platform
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Invokes a board specific function that provides MAC address for this
 * adapter. For DaVinci EVM, the function invoked is
 * davinci_get_macaddr()
 *
 * @TODO: use platform parameter to pass mac address in future
 *
 */static void emac_str_to_ethaddr(unsigned char *ea, unsigned char *str)
{
	int i;
	unsigned char num;

	for (i = 0; i < 6; i++) {
		if ((*str == '.') || (*str == ':'))
			str++;
		num = emac_str_to_hexnum(*str) << 4;
		++str;
		num |= (emac_str_to_hexnum(*str));
		++str;
		ea[i] = num;
	}
}

/* TODO: External function in init code - move this to platform config */
extern int davinci_get_macaddr(char *ptr);

/**
 * emac_eth_setup: Get EMAC mac address from platform
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Iinvokes a board specific function that provides MAC address for this
 * adapter. For DaVinci EVM, the function invoked is
 * davinci_get_macaddr()
 *
 * TODO: use platform parameter to pass mac address in future
 *
 */
static void emac_eth_setup(struct emac_priv *priv)
{
	/* TODO: Try to use platform structure for this */
	strcpy(priv->mac_str, "deadbeef");
	if (davinci_get_macaddr(&priv->mac_str[0]) != 0) {
		dev_warn(EMAC_DEV, "DaVinci EMAC: Error getting MAC addr\n");
		dev_warn(EMAC_DEV, "Def MAC address: 08.00.28.32.06.08");
	}
	emac_str_to_ethaddr(priv->mac_addr, priv->mac_str);
	return;
}

/**
 * emac_timer_cb: EMAC Phy timer callback
 * @priv: The DaVinci EMAC private adapter structure
 *
 * EMAC uses a PHY poll timer - this callback function probes the PHY
 * periodically to see if the status has changed. PHY auto-negotiation logic
 * is also handled via this callback in the PHY code
 *
 */
static void emac_timer_cb(struct emac_priv *priv)
{
	struct timer_list *p_timer = &priv->periodic_timer;
	u32 change;

	if (!netif_running(priv->ndev))
		return;

	if (1 == priv->timer_active) {
		if (EMAC_SPEED_NO_PHY != cfg_link_speed) {
			change = emac_mdio_tick();
			if (unlikely(1 == change))
				emac_update_phystatus(priv);
		}
		p_timer->expires = jiffies + priv->periodic_ticks;
		add_timer(p_timer);
	}
}


/*************************************************************************
 *  EMAC Hardware manipulation
 *************************************************************************/

/**
 * emac_int_disable: Disable EMAC module interrupt (from adapter)
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Disable EMAC interrupt on the adapter
 *
 */
static void emac_int_disable(struct emac_priv *priv)
{
	/* Set DM644x control registers for interrupt control */
	emac_ctrl_write(EMAC_CTRL_EWCTL, 0x0);
}

/**
 * emac_int_enable: Enable EMAC module interrupt (from adapter)
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Enable EMAC interrupt on the adapter
 *
 */
static void emac_int_enable(struct emac_priv *priv)
{
	/* Set DM644x control registers for interrupt control */
	emac_ctrl_write(EMAC_CTRL_EWCTL, 0x1);
}

/**
 * emac_irq: EMAC interrupt handler
 * @irq: interrupt number
 * @dev_id: EMAC network adapter data structure ptr
 *
 * EMAC Interrupt handler - we only schedule NAPI and not process any packets
 * here. EVen the interrupt status is checked (TX/RX/Err) in NAPI poll function
 *
 * Returns interrupt handled condition
 */
irqreturn_t emac_irq(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct emac_priv *priv = netdev_priv(ndev);

	++priv->isr_count;
	if (likely(netif_running(priv->ndev))) {
		emac_int_disable(priv);
		netif_rx_schedule(ndev, &priv->napi);
	} else {
		/* we are closing down, so dont process anything */
	}
	return IRQ_HANDLED;
}

/** EMAC on-chip buffer descriptor memory
 *
 * WARNING: Please note that the on chip memory is used for both TX and RX
 * buffer descriptor queues and is equally divided between TX and RX desc's
 * If the number of TX or RX descriptors change this memory pointers need
 * to be adjusted. If external memory is allocated then these pointers can
 * pointer to the memory
 *
 */

#define EMAC_TX_BD_MEM	(priv->emac_ctrl_ram)
#define EMAC_RX_BD_MEM	(priv->emac_ctrl_ram + (EMAC_BUFFER_RAM_SIZE >> 1))

/**
 * emac_init_txch: TX channel initialization
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device init to setup a TX channel (allocate buffer desc
 * create free pool and keep ready for transmission
 *
 * Returns success(0) or mem alloc failures error code
 */
static int emac_init_txch(struct emac_priv *priv, u32 ch)
{
	u32 cnt, bd_size;
	char *mem;
	struct emac_tx_bd *curr_bd;
	struct emac_txch *txch = NULL;

	txch = kzalloc(sizeof(struct emac_txch), GFP_KERNEL);
	if (NULL == txch) {
		dev_err(EMAC_DEV, "DaVinci EMAC: TX Ch mem alloc failed");
		return (-ENOMEM);
	}
	priv->txch[ch] = txch;
	txch->num_bd = EMAC_DEF_TX_NUM_BD;
	txch->service_max = EMAC_DEF_TX_MAX_SERVICE;
	txch->active_queue_head = 0;
	txch->active_queue_tail = 0;
	txch->queue_active = 0;
	txch->teardown_pending = 0;

	/* allocate memory for TX CPPI channel on a 4 byte boundry */
	txch->tx_complete = kzalloc(txch->service_max * sizeof(u32),
				    GFP_KERNEL);
	if (NULL == txch->tx_complete) {
		dev_err(EMAC_DEV, "DaVinci EMAC: Tx service mem alloc failed");
		kfree(txch);
		return (-ENOMEM);
	}

	/* allocate buffer descriptor pool align every BD on four word
	 * boundry for future requirements */
	bd_size = (sizeof(struct emac_tx_bd) + 0xF) & ~0xF;
	txch->alloc_size = (((bd_size * txch->num_bd) + 0xF) & ~0xF);

	/* alloc TX BD memory */
	txch->bd_mem = (char *) EMAC_TX_BD_MEM;
	memzero(txch->bd_mem, txch->alloc_size);

	/* initialize the BD linked list */
	mem = (char *)(((u32) txch->bd_mem + 0xF) & ~0xF);
	txch->bd_pool_head = 0;
	for (cnt = 0; cnt < txch->num_bd; cnt++) {
		curr_bd = (struct emac_tx_bd *) (mem + (cnt * bd_size));
		curr_bd->next = txch->bd_pool_head;
		txch->bd_pool_head = curr_bd;
	}

	/* reset statistics counters */
	txch->out_of_tx_bd = 0;
	txch->no_active_pkts = 0;
	txch->active_queue_count = 0;

	return (0);
}

/**
 * emac_cleanup_txch: Book-keep function to clean TX channel resources
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number
 *
 * Called to clean up TX channel resources
 *
 */
static void emac_cleanup_txch(struct emac_priv *priv, u32 ch)
{
	struct emac_txch *txch = priv->txch[ch];

	if (txch) {
		if (txch->bd_mem)
			txch->bd_mem = 0;
		if (txch->tx_complete) {
			kfree(txch->tx_complete);
			txch->tx_complete = 0;
		}
		kfree(txch);
		priv->txch[ch] = 0;
	}
}


/**
 * emac_net_tx_complete: TX packet completion function
 * @priv: The DaVinci EMAC private adapter structure
 * @net_data_tokens: packet token - skb pointer
 * @num_tokens: number of skb's to free
 * @ch: TX channel number
 *
 * Frees the skb once packet is transmitted
 *
 */
static int emac_net_tx_complete(struct emac_priv *priv,
				void **net_data_tokens,
				int num_tokens, u32 ch)
{
	u32 cnt;

	if (unlikely(num_tokens && netif_queue_stopped(priv->ndev)))
		netif_start_queue(priv->ndev);
	for (cnt = 0; cnt < num_tokens; cnt++) {
		struct sk_buff *skb = (struct sk_buff *)net_data_tokens[cnt];
		if (skb == NULL)
			continue;
		priv->net_dev_stats.tx_packets++;
		priv->net_dev_stats.tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
	}
	return (0);
}

/**
 * emac_txch_teardown: TX channel teardown
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number
 *
 * Called to teardown TX channel
 *
 */
static void emac_txch_teardown(struct emac_priv *priv, u32 ch)
{
	u32 teardown_cnt = 0xFFFFFFF0; /* Some high value */
	struct emac_txch *txch = priv->txch[ch];
	struct emac_tx_bd *curr_bd;

	while ((emac_read(EMAC_TXCP(ch)) & EMAC_TEARDOWN_VALUE) !=
	       EMAC_TEARDOWN_VALUE) {
		/* wait till tx teardown complete */
		cpu_relax(); /* TODO: check if this helps ... */
		--teardown_cnt;
		if (0 == teardown_cnt) {
			dev_err(EMAC_DEV, "EMAC: TX teardown aborted\n");
			break;
		}
	}
	emac_write(EMAC_TXCP(ch), EMAC_TEARDOWN_VALUE);

	/* process sent packets and return skb's to upper layer */
	if (1 == txch->queue_active) {
		curr_bd = txch->active_queue_head;
		while (curr_bd != NULL) {
			emac_net_tx_complete(priv, &(curr_bd->buf_token),
					     1, ch);
			if (curr_bd != txch->active_queue_tail)
				curr_bd = curr_bd->next;
			else
				break;
		}
		txch->bd_pool_head = txch->active_queue_head;
		txch->active_queue_head =
		txch->active_queue_tail = 0;
	}
}

/**
 * emac_stop_txch: Stop TX channel operation
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number
 *
 * Called to stop TX channel operation
 *
 */
static void emac_stop_txch(struct emac_priv *priv, u32 ch)
{
	struct emac_txch *txch = priv->txch[ch];

	if (txch) {
		txch->teardown_pending = 1;
		emac_write(EMAC_TXTEARDOWN, 0);
		emac_txch_teardown(priv, ch);
		txch->teardown_pending = 0;
		emac_write(EMAC_TXINTMASKCLEAR, (1 << ch));
	}
}

/**
 * emac_tx_bdproc: TX buffer descriptor (packet) processing
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number to process buffer descriptors for
 * @budget: number of packets allowed to process
 * @pending: indication to caller that packets are pending to process
 *
 * Processes TX buffer descriptors after packets are transmitted - checks
 * ownership bit on the TX * descriptor and requeues it to free pool & frees
 * the SKB buffer. Only "budget" number of packets are processed and
 * indication of pending packets provided to the caller
 *
 * Returns number of packets processed
 */
static int emac_tx_bdproc(struct emac_priv *priv, u32 ch, u32 budget,
			  u32 *pending)
{
	unsigned long flags;
	u32 frame_status;
	u32 pkts_processed = 0;
	u32 tx_complete_cnt = 0;
	struct emac_tx_bd *curr_bd;
	struct emac_txch *txch = priv->txch[ch];
	u32 *tx_complete_ptr = txch->tx_complete;

	*pending = 0;
	if (unlikely(1 == txch->teardown_pending)) {
		if (netif_msg_tx_err(priv) && net_ratelimit()) {
			dev_err(EMAC_DEV, "DaVinci EMAC:emac_tx_bdproc: "\
				"teardown pending\n");
		}
		return (0);  /* dont handle any pkt completions */
	}

	++txch->proc_count;
	spin_lock_irqsave(&priv->tx_lock, flags);
	curr_bd = txch->active_queue_head;
	if (0 == curr_bd) {
		emac_write(EMAC_TXCP(ch),
			   EMAC_VIRT2PHYS(txch->last_hw_bdprocessed));
		txch->no_active_pkts++;
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return (0);
	}
	BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
	frame_status = curr_bd->mode;
	while ((curr_bd) &&
	      ((frame_status & EMAC_CPPI_OWNERSHIP_BIT) == 0) &&
	      (pkts_processed < budget)) {
		emac_write(EMAC_TXCP(ch), EMAC_VIRT2PHYS(curr_bd));
		txch->active_queue_head = curr_bd->next;
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			if (curr_bd->next) {	/* misqueued packet */
				emac_write(EMAC_TXHDP(ch), curr_bd->h_next);
				++txch->mis_queued_packets;
			} else {
				txch->queue_active = 0; /* end of queue */
			}
		}
		*tx_complete_ptr = (u32) curr_bd->buf_token;
		++tx_complete_ptr;
		++tx_complete_cnt;
		curr_bd->next = txch->bd_pool_head;
		txch->bd_pool_head = curr_bd;
		--txch->active_queue_count;
		pkts_processed++;
		txch->last_hw_bdprocessed = curr_bd;
		curr_bd = txch->active_queue_head;
		if (curr_bd) {
			BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
			frame_status = curr_bd->mode;
		}
	} /* end of pkt processing loop */

	if ((pkts_processed == budget) &&
	    ((curr_bd) && ((frame_status & EMAC_CPPI_OWNERSHIP_BIT) == 0))) {
		*pending = 1;
	}

	emac_net_tx_complete(priv,
			     (void *)&txch->tx_complete[0],
			     tx_complete_cnt, ch);
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return (pkts_processed);
}

#define EMAC_ERR_TX_OUT_OF_BD -1

/**
 * emac_send: EMAC Transmit function (internal)
 * @priv: The DaVinci EMAC private adapter structure
 * @pkt: packet pointer (contains skb ptr)
 * @ch: TX channel number
 *
 * Called by the transmit function to queue the packet in EMAC hardware queue
 *
 * Returns success(0) or error code (typically out of desc's)
 */
static int emac_send(struct emac_priv *priv, struct emac_netpktobj *pkt, u32 ch)
{
	unsigned long flags;
	struct emac_tx_bd *curr_bd;
	struct emac_txch *txch;
	struct emac_netbufobj *buf_list;

	txch = priv->txch[ch];
	buf_list = pkt->buf_list;   /* get handle to the buffer array */

	/* check packet size and pad if short */
	if (pkt->pkt_length < EMAC_DEF_MIN_ETHPKTSIZE) {
		buf_list->length += (EMAC_DEF_MIN_ETHPKTSIZE - pkt->pkt_length);
		pkt->pkt_length = EMAC_DEF_MIN_ETHPKTSIZE;
	}

	spin_lock_irqsave(&priv->tx_lock, flags);
	curr_bd = txch->bd_pool_head;
	if (curr_bd == NULL) {
		txch->out_of_tx_bd++;
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return (EMAC_ERR_TX_OUT_OF_BD);
	}

	txch->bd_pool_head = curr_bd->next;
	curr_bd->buf_token = buf_list->buf_token;
	curr_bd->buff_ptr = EMAC_VIRT2PHYS((int *)buf_list->data_ptr);
	curr_bd->off_b_len = buf_list->length;
	curr_bd->h_next = 0;
	curr_bd->next = 0;
	curr_bd->mode = (EMAC_CPPI_SOP_BIT | EMAC_CPPI_OWNERSHIP_BIT |
			 EMAC_CPPI_EOP_BIT | pkt->pkt_length);

	/* flush the packet from cache if write back cache is present */
	BD_CACHE_WRITEBACK_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);

	/* send the packet */
	if (txch->active_queue_head == 0) {
		txch->active_queue_head = curr_bd;
		txch->active_queue_tail = curr_bd;
		if (1 != txch->queue_active) {
			emac_write(EMAC_TXHDP(ch), EMAC_VIRT2PHYS(curr_bd));
			txch->queue_active = 1;
		}
		++txch->queue_reinit;
	} else {
		register struct emac_tx_bd *tail_bd;
		register u32 frame_status;

		tail_bd = txch->active_queue_tail;
		tail_bd->next = curr_bd;
		txch->active_queue_tail = curr_bd;
		tail_bd = EMAC_VIRT_NOCACHE(tail_bd);
		tail_bd->h_next = (int)EMAC_VIRT2PHYS(curr_bd);
		frame_status = tail_bd->mode;
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			emac_write(EMAC_TXHDP(ch), EMAC_VIRT2PHYS(curr_bd));
			frame_status &= ~(EMAC_CPPI_EOQ_BIT);
			tail_bd->mode = frame_status;
			++txch->end_of_queue_add;
		}
	}
	txch->active_queue_count++;
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return (0);
}

/**
 * emac_dev_xmit: EMAC Transmit function
 * @skb: SKB pointer
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called by the system to transmit a packet  - we queue the packet in
 * EMAC hardware transmit queue
 *
 * Returns success(NETDEV_TX_OK) or error code (typically out of desc's)
 */
static int emac_dev_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int ret_code;
	struct emac_netbufobj tx_buf; /* buffer obj-only single frame support */
	struct emac_netpktobj tx_packet;  /* packet object */
	struct emac_priv *priv = netdev_priv(ndev);

	/* If no link, return */
	if (unlikely(!priv->link)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			dev_err(EMAC_DEV, "DaVinci EMAC: No link to transmit");
		return (NETDEV_TX_BUSY);
	}

	/* Build the buffer and packet objects - Since only single fragment is
	 * supported, need not set length and token in both packet & object.
	 * Doing so for completeness sake & to show that this needs to be done
	 * in multifragment case
	 */
	tx_packet.buf_list = &tx_buf;
	tx_packet.num_bufs = 1; /* only single fragment supported */
	tx_packet.pkt_length = skb->len;
	tx_packet.pkt_token = (void *)skb;
	tx_buf.length = skb->len;
	tx_buf.buf_token = (void *)skb;
	tx_buf.data_ptr = skb->data;
	EMAC_CACHE_WRITEBACK((unsigned long)skb->data, skb->len);
	ndev->trans_start = jiffies;
	ret_code = emac_send(priv, &tx_packet, EMAC_DEF_TX_CH);
	if (unlikely(ret_code != 0)) {
		if (ret_code == EMAC_ERR_TX_OUT_OF_BD) {
			if (netif_msg_tx_err(priv) && net_ratelimit())
				dev_err(EMAC_DEV, "DaVinci EMAC: xmit() fatal"\
					" err. Out of TX BD's");
			netif_stop_queue(priv->ndev);
		}
		priv->net_dev_stats.tx_dropped++;
		return (NETDEV_TX_BUSY);
	}

	return (NETDEV_TX_OK);
}


/**
 * emac_dev_tx_timeout: EMAC Transmit timeout function
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system detects that a skb timeout period has expired
 * potentially due to a fault in the adapter in not being able to send
 * it out on the wire. We teardown the TX channel assuming a hardware
 * error and re-initialize the TX channel for hardware operation
 *
 */
static void emac_dev_tx_timeout(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	if (netif_msg_tx_err(priv))
		dev_err(EMAC_DEV, "DaVinci EMAC: xmit timeout, restarting TX");

	priv->net_dev_stats.tx_errors++;
	emac_int_disable(priv);
	emac_stop_txch(priv, EMAC_DEF_TX_CH);
	emac_cleanup_txch(priv, EMAC_DEF_TX_CH);
	emac_init_txch(priv, EMAC_DEF_TX_CH);
	emac_write(EMAC_TXHDP(0), 0);
	emac_write(EMAC_TXINTMASKSET, (1 << EMAC_DEF_TX_CH));
	emac_int_enable(priv);
}


/**
 * emac_net_alloc_rx_buf: Allocate a skb for RX
 * @priv: The DaVinci EMAC private adapter structure
 * @buf_size: size of SKB data buffer to allocate
 * @data_token: data token returned (skb handle for storing in buffer desc)
 * @ch: RX channel number
 *
 * Called during RX channel setup - allocates skb buffer of required size
 * and provides the skb handle and allocated buffer data pointer to caller
 *
 * Returns skb data pointer or 0 on failure to alloc skb
 */
void *emac_net_alloc_rx_buf(struct emac_priv *priv, int buf_size,
		void **data_token, u32 ch)
{
	struct net_device *ndev = priv->ndev;
	struct sk_buff *p_skb;

	p_skb = dev_alloc_skb(buf_size);
	if (unlikely(0 == p_skb)) {
		if (netif_msg_rx_err(priv) && net_ratelimit())
			dev_err(EMAC_DEV, "DaVinci EMAC: failed to alloc skb");
		return (0);
	}

	/* set device pointer in skb and reserve space for extra bytes */
	p_skb->dev = ndev;
	skb_reserve(p_skb, EMAC_DEF_EXTRA_RXBUF_SIZE);
	*data_token = (void *) p_skb;
	EMAC_CACHE_WRITEBACK_INVALIDATE((unsigned long)p_skb->data, buf_size);
	return p_skb->data;
}

/**
 * emac_init_rxch: RX channel initialization
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @param: mac address for RX channel
 *
 * Called during device init to setup a RX channel (allocate buffers and
 * buffer descriptors, create queue and keep ready for reception
 *
 * Returns success(0) or mem alloc failures error code
 */
static int emac_init_rxch(struct emac_priv *priv, u32 ch, char *param)
{
	u32 cnt, bd_size;
	char *mem;
	struct emac_rx_bd *curr_bd;
	struct emac_rxch *rxch = NULL;

	rxch = kzalloc(sizeof(struct emac_rxch), GFP_KERNEL);
	if (NULL == rxch) {
		dev_err(EMAC_DEV, "DaVinci EMAC: RX Ch mem alloc failed");
		return (-ENOMEM);
	}
	priv->rxch[ch] = rxch;
	rxch->num_bd = EMAC_DEF_RX_NUM_BD;
	rxch->buf_size = priv->rx_buf_size;
	rxch->service_max = EMAC_DEF_RX_MAX_SERVICE;
	rxch->queue_active = 0;
	rxch->teardown_pending = 0;

	/* save mac address */
	mem = param;
	for (cnt = 0; cnt < 6; cnt++)
		rxch->mac_addr[cnt] = mem[cnt];

	/* allocate buffer descriptor pool align every BD on four word
	 * boundry for future requirements */
	bd_size = (sizeof(struct emac_rx_bd) + 0xF) & ~0xF;
	rxch->alloc_size = (((bd_size * rxch->num_bd) + 0xF) & ~0xF);
	rxch->bd_mem = (char *) EMAC_RX_BD_MEM;
	memzero(rxch->bd_mem, rxch->alloc_size);
	rxch->pkt_queue.buf_list = &rxch->buf_queue;

	/* allocate RX buffer and initialize the BD linked list */
	mem = (char *)(((u32) rxch->bd_mem + 0xF) & ~0xF);
	rxch->active_queue_head = 0;
	rxch->active_queue_tail = (struct emac_rx_bd *) mem;
	for (cnt = 0; cnt < rxch->num_bd; cnt++) {
		curr_bd = (struct emac_rx_bd *) (mem + (cnt * bd_size));
		/* for future use the last parameter contains the BD ptr */
		curr_bd->data_ptr = (void *)(emac_net_alloc_rx_buf(priv,
				    rxch->buf_size,
				    (void **)&curr_bd->buf_token,
				    EMAC_DEF_RX_CH));
		if (curr_bd->data_ptr == NULL) {
			dev_err(EMAC_DEV, "DaVinci EMAC: RX buf mem alloc " \
				"failed for ch %d\n", ch);
			kfree(rxch);
			return (-ENOMEM);
		}

		/* populate the hardware descriptor */
		curr_bd->h_next = EMAC_VIRT2PHYS(rxch->active_queue_head);
		curr_bd->buff_ptr = EMAC_VIRT2PHYS(curr_bd->data_ptr);
		curr_bd->off_b_len = rxch->buf_size;
		curr_bd->mode = EMAC_CPPI_OWNERSHIP_BIT;

		/* write back to hardware memory */
		BD_CACHE_WRITEBACK_INVALIDATE((u32) curr_bd,
					      EMAC_BD_LENGTH_FOR_CACHE);
		curr_bd->next = (void *) rxch->active_queue_head;
		rxch->active_queue_head = curr_bd;
	}

	/* At this point rxCppi->activeQueueHead points to the first
	   RX BD ready to be given to RX HDP and rxch->active_queue_tail
	   points to the last RX BD
	 */
	return (0);
}

/**
 * emac_rxch_teardown: RX channel teardown
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device stop to teardown RX channel
 *
 */
static void emac_rxch_teardown(struct emac_priv *priv, u32 ch)
{
	u32 teardown_cnt = 0xFFFFFFF0; /* Some high value */

	while ((emac_read(EMAC_RXCP(ch)) & EMAC_TEARDOWN_VALUE) !=
	       EMAC_TEARDOWN_VALUE) {
		/* wait till tx teardown complete */
		cpu_relax(); /* TODO: check if this helps ... */
		--teardown_cnt;
		if (0 == teardown_cnt) {
			dev_err(EMAC_DEV, "EMAC: RX teardown aborted\n");
			break;
		}
	}
	emac_write(EMAC_RXCP(ch), EMAC_TEARDOWN_VALUE);
}

/**
 * emac_stop_rxch: Stop RX channel operation
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device stop to stop RX channel operation
 *
 */
static void emac_stop_rxch(struct emac_priv *priv, u32 ch)
{
	struct emac_rxch *rxch = priv->rxch[ch];

	if (rxch) {
		rxch->teardown_pending = 1;
		emac_write(EMAC_RXTEARDOWN, ch);
		/* wait for teardown complete */
		emac_rxch_teardown(priv, ch);
		rxch->teardown_pending = 0;
		emac_write(EMAC_RXINTMASKCLEAR, (1 << ch));
	}
}


/**
 * emac_cleanup_rxch: Book-keep function to clean RX channel resources
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device stop to clean up RX channel resources
 *
 */
static void emac_cleanup_rxch(struct emac_priv *priv, u32 ch)
{
	struct emac_rxch *rxch = priv->rxch[ch];
	struct emac_rx_bd *curr_bd;

	if (rxch) {
		/* free the receive buffers previously allocated */
		curr_bd = rxch->active_queue_head;
		while (curr_bd) {
			if (curr_bd->buf_token) {
				dev_kfree_skb_any((struct sk_buff *)\
						  curr_bd->buf_token);
			}
			curr_bd = curr_bd->next;
		}
		if (rxch->bd_mem)
			rxch->bd_mem = 0;
		kfree(rxch);
		priv->rxch[ch] = 0;
	}
}

/**
 * emac_set_type0addr: Set EMAC Type0 mac address
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 *
 * Called internally to set Type0 mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_set_type0addr(struct emac_priv *priv, u32 ch, char *mac_addr)
{
	u32 val;
	val = ((mac_addr[0] << 8) | (mac_addr[1]));
	emac_write(EMAC_MACSRCADDRLO, val);

	val = ((mac_addr[2] << 24) | (mac_addr[3] << 16) | \
	       (mac_addr[4] << 8) | (mac_addr[5]));
	emac_write(EMAC_MACSRCADDRHI, val);
	val = emac_read(EMAC_RXUNICASTSET);
	val |= (1 << ch);
	emac_write(EMAC_RXUNICASTSET, val);
	val = emac_read(EMAC_RXUNICASTCLEAR);
	val &= ~(1 << ch);
	emac_write(EMAC_RXUNICASTCLEAR, val);
}


/**
 * emac_set_type1addr: Set EMAC Type1 mac address
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 *
 * Called internally to set Type1 mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_set_type1addr(struct emac_priv *priv, u32 ch, char *mac_addr)
{
	u32 val;
	emac_write(EMAC_MACINDEX, ch);
	val = ((mac_addr[5] << 8) | mac_addr[4]);
	emac_write(EMAC_MACADDRLO, val);
	val = ((mac_addr[3] << 24) | (mac_addr[2] << 16) | \
	       (mac_addr[1] << 8) | (mac_addr[0]));
	emac_write(EMAC_MACADDRHI, val);
	emac_set_type0addr(priv, ch, mac_addr);
}

/**
 * emac_set_type2addr: Set EMAC Type2 mac address
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 * @index: index into RX address entries
 * @match: match parameter for RX address matching logic
 *
 * Called internally to set Type2 mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_set_type2addr(struct emac_priv *priv, u32 ch,
			       char *mac_addr, int index, int match)
{
	u32 val;
	emac_write(EMAC_MACINDEX, index);
	val = ((mac_addr[3] << 24) | (mac_addr[2] << 16) | \
	       (mac_addr[1] << 8) | (mac_addr[0]));
	emac_write(EMAC_MACADDRHI, val);
	val = ((mac_addr[5] << 8) | mac_addr[4] | ((ch & 0x7) << 16) | \
	       (match << 19) | (1 << 20));
	emac_write(EMAC_MACADDRLO, val);
	emac_set_type0addr(priv, ch, mac_addr);
}

/**
 * emac_setmac: Set mac address in the adapter (internal function)
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 *
 * Called internally to set the mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_setmac(struct emac_priv *priv, u32 ch, char *mac_addr)
{
	if (priv->rx_addr_type == 0) {
		emac_set_type0addr(priv, ch, mac_addr);
	} else if (priv->rx_addr_type == 1) {
		u32 cnt;
		for (cnt = 0; cnt < EMAC_MAX_TXRX_CHANNELS; cnt++)
			emac_set_type1addr(priv, ch, mac_addr);
	} else if (priv->rx_addr_type == 2) {
		emac_set_type2addr(priv, ch, mac_addr, ch, 1);
		emac_set_type0addr(priv, ch, mac_addr);
	} else {
		if (netif_msg_drv(priv))
			dev_err(EMAC_DEV, "DaVinci EMAC: Wrong addressing\n");
	}
}

/**
 * emac_dev_setmac_addr: Set mac address in the adapter
 * @ndev: The DaVinci EMAC network adapter
 * @addr: MAC address to set in device
 *
 * Called by the system to set the mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static int emac_dev_setmac_addr(struct net_device *ndev, void *addr)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct emac_rxch *rxch = priv->rxch[EMAC_DEF_RX_CH];
	struct sockaddr *sa = addr;
	DECLARE_MAC_BUF(mac);

	/* Store mac addr in priv and rx channel and set it in EMAC hw */
	memcpy(priv->mac_addr, sa->sa_data, ndev->addr_len);
	memcpy(rxch->mac_addr, sa->sa_data, ndev->addr_len);
	memcpy(ndev->dev_addr, sa->sa_data, ndev->addr_len);
	emac_setmac(priv, EMAC_DEF_RX_CH, rxch->mac_addr);

	if (netif_msg_drv(priv))
		dev_notice(EMAC_DEV, "DaVinci EMAC: emac_dev_setmac_addr %s\n",
			   print_mac(mac, priv->mac_addr));

	return (0);
}

/**
 * emac_addbd_to_rx_queue: Recycle RX buffer descriptor
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number to process buffer descriptors for
 * @curr_bd: current buffer descriptor
 * @buffer: buffer pointer for descriptor
 * @buf_token: buffer token (stores skb information)
 *
 * Prepares the recycled buffer descriptor and addes it to hardware
 * receive queue - if queue empty this descriptor becomes the head
 * else addes the descriptor to end of queue
 *
 */
static void emac_addbd_to_rx_queue(struct emac_priv *priv, u32 ch,
				   struct emac_rx_bd *curr_bd, char *buffer,
				   void *buf_token)
{
	struct emac_rxch *rxch = priv->rxch[ch];

	/* populate the hardware descriptor */
	curr_bd->h_next = 0;
	curr_bd->buff_ptr = EMAC_VIRT2PHYS(buffer);
	curr_bd->off_b_len = rxch->buf_size;
	curr_bd->mode = EMAC_CPPI_OWNERSHIP_BIT;
	curr_bd->next = 0;
	curr_bd->data_ptr = buffer;
	curr_bd->buf_token = buf_token;

	/* write back  */
	BD_CACHE_WRITEBACK_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
	if (rxch->active_queue_head == 0) {
	rxch->active_queue_head = curr_bd;
	rxch->active_queue_tail = curr_bd;
	if (0 != rxch->queue_active) {
		emac_write(EMAC_RXHDP(ch),
			   EMAC_VIRT2PHYS(rxch->active_queue_head));
		rxch->queue_active = 1;
	}
	} else {
		struct emac_rx_bd *tail_bd;
		u32 frame_status;

		tail_bd = rxch->active_queue_tail;
		rxch->active_queue_tail = curr_bd;
		tail_bd->next = (void *)curr_bd;
		tail_bd = EMAC_VIRT_NOCACHE(tail_bd);
		tail_bd->h_next = EMAC_VIRT2PHYS(curr_bd);
		frame_status = tail_bd->mode;
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			emac_write(EMAC_RXHDP(ch), EMAC_VIRT2PHYS(curr_bd));
			frame_status &= ~(EMAC_CPPI_EOQ_BIT);
			tail_bd->mode = frame_status;
			++rxch->end_of_queue_add;
		}
	}
	++rxch->recycled_bd;
}

/**
 * emac_net_rx_cb: Prepares packet and sends to upper layer
 * @priv: The DaVinci EMAC private adapter structure
 * @net_pkt_list: Network packet list (received packets)
 *
 * Invalidates packet buffer memory and sends the received packet to upper
 * layer
 *
 * Returns success or appropriate error code (none as of now)
 */
static int emac_net_rx_cb(struct emac_priv *priv,
			  struct emac_netpktobj *net_pkt_list)
{
	struct sk_buff *p_skb;
	p_skb = (struct sk_buff *)net_pkt_list->pkt_token;
	/* set length of packet */
	skb_put(p_skb, net_pkt_list->pkt_length);
	EMAC_CACHE_INVALIDATE((unsigned long)p_skb->data, p_skb->len);
	p_skb->protocol = eth_type_trans(p_skb, priv->ndev);
	p_skb->dev->last_rx = jiffies;
	netif_receive_skb(p_skb);
	priv->net_dev_stats.rx_bytes += net_pkt_list->pkt_length;
	priv->net_dev_stats.rx_packets++;
	return (0);
}

/**
 * emac_rx_bdproc: RX buffer descriptor (packet) processing
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number to process buffer descriptors for
 * @budget: number of packets allowed to process
 * @pending: indication to caller that packets are pending to process
 *
 * Processes RX buffer descriptors - checks ownership bit on the RX buffer
 * descriptor, sends the receive packet to upper layer, allocates a new SKB
 * and recycles the buffer descriptor (requeues it in hardware RX queue).
 * Only "budget" number of packets are processed and indication of pending
 * packets provided to the caller.
 *
 * Returns number of packets processed (and indication of pending packets)
 */
static int emac_rx_bdproc(struct emac_priv *priv, u32 ch, u32 budget,
			  u32 *pending)
{
	unsigned long flags;
	u32 frame_status;
	u32 pkts_processed = 0;
	char *new_buffer;
	struct emac_rx_bd *curr_bd, *last_bd;
	struct emac_netpktobj *curr_pkt, pkt_obj;
	struct emac_netbufobj buf_obj;
	struct emac_netbufobj *rx_buf_obj;
	void *new_buf_token;
	struct emac_rxch *rxch = priv->rxch[ch];

	*pending = 0;
	if (unlikely(1 == rxch->teardown_pending))
		return (0);
	++rxch->proc_count;
	spin_lock_irqsave(&priv->rx_lock, flags);
	pkt_obj.buf_list = &buf_obj;
	curr_pkt = &pkt_obj;
	curr_bd = rxch->active_queue_head;
	BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
	frame_status = curr_bd->mode;

	while ((curr_bd) &&
	       ((frame_status & EMAC_CPPI_OWNERSHIP_BIT) == 0) &&
	       (pkts_processed < budget)) {

		new_buffer = (void *)(emac_net_alloc_rx_buf(priv,
					EMAC_DEF_MAX_FRAME_SIZE,
					&new_buf_token, EMAC_DEF_RX_CH));
		if (unlikely(0 == new_buffer)) {
			++rxch->out_of_rx_buffers;
			goto end_emac_rx_bdproc;
		}

		/* populate received packet data structure */
		rx_buf_obj = &curr_pkt->buf_list[0];
		rx_buf_obj->data_ptr = (char *)curr_bd->data_ptr;
		rx_buf_obj->length = curr_bd->off_b_len & EMAC_RX_BD_BUF_SIZE;
		rx_buf_obj->buf_token = curr_bd->buf_token;
		curr_pkt->pkt_token = curr_pkt->buf_list->buf_token;
		curr_pkt->num_bufs = 1;
		curr_pkt->pkt_length =
			(frame_status & EMAC_RX_BD_PKT_LENGTH_MASK);
		emac_write(EMAC_RXCP(ch), EMAC_VIRT2PHYS(curr_bd));
		++rxch->processed_bd;
		last_bd = curr_bd;
		curr_bd = last_bd->next;
		rxch->active_queue_head = curr_bd;

		/* check if end of RX queue ? */
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			if (curr_bd) {
				++rxch->mis_queued_packets;
				emac_write(EMAC_RXHDP(ch),
					   EMAC_VIRT2PHYS(curr_bd));
			} else {
				++rxch->end_of_queue;
				rxch->queue_active = 0;
			}
		}

		/* recycle BD */
		emac_addbd_to_rx_queue(priv, ch, last_bd, new_buffer,
				       new_buf_token);

		/* return the packet to the user - BD ptr passed in
		 * last parameter for potential *future* use */
		spin_unlock_irqrestore(&priv->rx_lock, flags);
		emac_net_rx_cb(priv, curr_pkt);
		spin_lock_irqsave(&priv->rx_lock, flags);
		curr_bd = rxch->active_queue_head;
		if (curr_bd) {
			BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
			frame_status = curr_bd->mode;
		}
		++pkts_processed;
	}

	if ((pkts_processed == budget) &&
	    ((curr_bd) && ((frame_status & EMAC_CPPI_OWNERSHIP_BIT) == 0))) {
		*pending = 1;
	}

end_emac_rx_bdproc:
	spin_unlock_irqrestore(&priv->rx_lock, flags);
	return (pkts_processed);
}

/**
 * emac_hw_enable: Enable EMAC hardware for packet transmission/reception
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Enables EMAC hardware for packet processing - enables PHY, enables RX
 * for packet reception and enables device interrupts and then NAPI
 *
 * Returns success (0) or appropriate error code (none right now)
 */
static int emac_hw_enable(struct emac_priv *priv)
{
	u32 ch, val, mbp_enable, mac_control;
	u32 mii_mod_id, mii_rev_maj, mii_rev_min;
	struct timer_list *p_timer = &priv->periodic_timer;

	/* Soft reset */
	emac_write(EMAC_SOFTRESET, 1);
	while (emac_read(EMAC_SOFTRESET))
		cpu_relax();

	/* Disable interrupt & Set pacing for more interrupts initially */
	emac_ctrl_write(EMAC_CTRL_EWCTL, 0x0);

	/* Set speed and duplex mode */
	priv->duplex = EMAC_DUPLEX_UNKNOWN;
	if (EMAC_SPEED_NO_PHY == cfg_link_speed)
		priv->speed = EMAC_SPEED_NO_PHY;
	else if (EMAC_SPEED_AUTO == cfg_link_speed)
		priv->speed = EMAC_SPEED_AUTO;

	priv->mdio.mdio_base_address = priv->mdio_regs;
	priv->mdio.mdio_reset_line = 0;
	priv->mdio.mdio_intr_line = 0;
	priv->mdio.phy_mask = EMAC_EVM_PHY_MASK;
	priv->mdio.MLink_mask = EMAC_EVM_MLINK_MASK;
	priv->mdio.mdio_bus_frequency = EMAC_EVM_BUS_FREQUENCY;
	priv->mdio.mdio_clock_frequency = EMAC_EVM_MDIO_FREQUENCY;
	priv->mdio.mdio_tick_msec = EMAC_DEF_MDIO_TICK_MS;

	/* start MDIO autonegotiation and set phy mode */
	emac_mdio_get_ver(priv->mdio.mdio_base_address,
			  &mii_mod_id, &mii_rev_maj, &mii_rev_min);
	emac_mdio_init(priv->mdio.mdio_base_address,
		       0, /* instance id */
		       priv->mdio.phy_mask,
		       priv->mdio.MLink_mask,
		       priv->mdio.mdio_bus_frequency,
		       priv->mdio.mdio_clock_frequency,
		       EMAC_MDIO_DEBUG); /* debug flag */

	/* set phy mode */
	emac_set_phymode(priv);

	/* start the tick timer */
	p_timer->expires = jiffies + priv->periodic_ticks;
	add_timer(&priv->periodic_timer);
	priv->timer_active = 1;

	/* Full duplex enable bit set when auto negotiation happens */
	mac_control =
		(((EMAC_DEF_TXPRIO_FIXED) ? (EMAC_MACCONTROL_TXPTYPE) : 0x0) |
		((priv->speed == 1000) ? EMAC_MACCONTROL_GIGABITEN : 0x0) |
		((EMAC_DEF_TXPACING_EN) ? (EMAC_MACCONTROL_TXPACEEN) : 0x0) |
		((priv->duplex == EMAC_DUPLEX_FULL) ? 0x1 : 0));
	emac_write(EMAC_MACCONTROL, mac_control);

	mbp_enable =
		(((EMAC_DEF_PASS_CRC) ? (EMAC_RXMBP_PASSCRC_MASK) : 0x0) |
		((EMAC_DEF_QOS_EN) ? (EMAC_RXMBP_QOSEN_MASK) : 0x0) |
		 ((EMAC_DEF_NO_BUFF_CHAIN) ? (EMAC_RXMBP_NOCHAIN_MASK) : 0x0) |
		 ((EMAC_DEF_MACCTRL_FRAME_EN) ? (EMAC_RXMBP_CMFEN_MASK) : 0x0) |
		 ((EMAC_DEF_SHORT_FRAME_EN) ? (EMAC_RXMBP_CSFEN_MASK) : 0x0) |
		 ((EMAC_DEF_ERROR_FRAME_EN) ? (EMAC_RXMBP_CEFEN_MASK) : 0x0) |
		 ((EMAC_DEF_PROM_EN) ? (EMAC_RXMBP_CAFEN_MASK) : 0x0) |
		 ((EMAC_DEF_PROM_CH & EMAC_RXMBP_CHMASK) << \
			EMAC_RXMBP_PROMCH_SHIFT) |
		 ((EMAC_DEF_BCAST_EN) ? (EMAC_RXMBP_BROADEN_MASK) : 0x0) |
		 ((EMAC_DEF_BCAST_CH & EMAC_RXMBP_CHMASK) << \
			EMAC_RXMBP_BROADCH_SHIFT) |
		 ((EMAC_DEF_MCAST_EN) ? (EMAC_RXMBP_MULTIEN_MASK) : 0x0) |
		 ((EMAC_DEF_MCAST_CH & EMAC_RXMBP_CHMASK) << \
			EMAC_RXMBP_MULTICH_SHIFT));
	emac_write(EMAC_RXMBPENABLE, mbp_enable);
	emac_write(EMAC_RXMAXLEN, (EMAC_DEF_MAX_FRAME_SIZE &
				   EMAC_RX_MAX_LEN_MASK));
	emac_write(EMAC_RXBUFFEROFFSET, (EMAC_DEF_BUFFER_OFFSET &
					 EMAC_RX_BUFFER_OFFSET_MASK));
	emac_write(EMAC_RXFILTERLOWTHRESH, 0);
	emac_write(EMAC_RXUNICASTCLEAR, EMAC_RX_UNICAST_CLEAR_ALL);
	priv->rx_addr_type = (emac_read(EMAC_MACCONFIG) >> 8) & 0xFF;

	val = emac_read(EMAC_TXCONTROL);
	val |= EMAC_TX_CONTROL_TX_ENABLE_VAL;
	emac_write(EMAC_TXCONTROL, val);
	val = emac_read(EMAC_RXCONTROL);
	val |= EMAC_RX_CONTROL_RX_ENABLE_VAL;
	emac_write(EMAC_RXCONTROL, val);
	emac_write(EMAC_MACINTMASKSET, EMAC_MAC_HOST_ERR_INTMASK_VAL);
;
	for (ch = 0; ch < EMAC_DEF_MAX_TX_CH; ch++) {
		emac_write(EMAC_TXHDP(ch), 0);
		emac_write(EMAC_TXINTMASKSET, (1 << ch));
	}
	for (ch = 0; ch < EMAC_DEF_MAX_RX_CH; ch++) {
		struct emac_rxch *rxch = priv->rxch[ch];
		emac_setmac(priv, ch, rxch->mac_addr);
		emac_write(EMAC_RXINTMASKSET, (1 << ch));
		rxch->queue_active = 1;
		emac_write(EMAC_RXHDP(ch),
			   EMAC_VIRT2PHYS(rxch->active_queue_head));
	}

	/* Enable MII */
	val = emac_read(EMAC_MACCONTROL);
	val |= (EMAC_MACCONTROL_MIIEN);
	emac_write(EMAC_MACCONTROL, val);
	emac_update_phystatus(priv);

	/* Enable NAPI and interrupts */
	napi_enable(&priv->napi);
	emac_int_enable(priv);
	return (0);

}

/**
 * emac_poll: EMAC NAPI Poll function
 * @ndev: The DaVinci EMAC network adapter
 * @budget: Number of receive packets to process (as told by NAPI layer)
 *
 * NAPI Poll function implemented to process packets as per budget. We check
 * the type of interrupt on the device and accordingly call the TX or RX
 * packet processing functions. We follow the budget for RX processing and
 * also put a cap on number of TX pkts processed through config param. The
 * NAPI schedule function is called if more packets pending.
 *
 * Returns number of packets received (in most cases; else TX pkts - rarely)
 */
static int emac_poll(struct napi_struct *napi, int budget)
{
	struct emac_priv *priv = container_of(napi, struct emac_priv, napi);
	struct net_device *ndev = priv->ndev;
	u32 status = 0;
	u32 num_pkts = 0;
	u32 txpending = 0;
	u32 rxpending = 0;

	if (!netif_running(ndev))
		return (0);

	/* Check interrupt vectors and call packet processing */
	status = emac_read(EMAC_MACINVECTOR);

	/* Since we support only 1 TX ch, for now check all TX int mask */
	if (status & EMAC_DM644X_MAC_IN_VECTOR_TX_INT_VEC) {
		num_pkts = emac_tx_bdproc(priv, EMAC_DEF_TX_CH,
					  EMAC_DEF_TX_MAX_SERVICE,
					  &txpending);
	} /* TX processing */

	/* Since we support only 1 TX ch, for now check all TX int mask */
	if (status & EMAC_DM644X_MAC_IN_VECTOR_RX_INT_VEC) {
		num_pkts = emac_rx_bdproc(priv, EMAC_DEF_RX_CH,
					  budget, &rxpending);
	} /* RX processing */

	if (txpending || rxpending) {
		if (likely(netif_rx_schedule_prep(ndev, &priv->napi))) {
			emac_int_disable(priv);
			__netif_rx_schedule(ndev, &priv->napi);
		}
	} else {
		netif_rx_complete(ndev, napi);
		emac_int_enable(priv);
	}

	if (unlikely(status & EMAC_DM644X_MAC_IN_VECTOR_HOST_INT)) {
		u32 ch, cause;
		dev_err(EMAC_DEV, "DaVinci EMAC: Fatal Hardware Error\n");
		netif_stop_queue(ndev);
		napi_disable(&priv->napi);

		status = emac_read(EMAC_MACSTATUS);
		cause = ((status & EMAC_MACSTATUS_TXERRCODE_MASK) >>
			 EMAC_MACSTATUS_TXERRCODE_SHIFT);
		if (cause) {
			ch = ((status & EMAC_MACSTATUS_TXERRCH_MASK) >>
			      EMAC_MACSTATUS_TXERRCH_SHIFT);
			if (net_ratelimit()) {
				dev_err(EMAC_DEV, "TX Host error %s on ch=%d\n",
					&emac_txhost_errcodes[cause][0], ch);
			}
		}
		cause = ((status & EMAC_MACSTATUS_RXERRCODE_MASK) >>
			 EMAC_MACSTATUS_RXERRCODE_SHIFT);
		if (cause) {
			ch = ((status & EMAC_MACSTATUS_RXERRCH_MASK) >>
			      EMAC_MACSTATUS_RXERRCH_SHIFT);
			if (netif_msg_hw(priv) && net_ratelimit())
				dev_err(EMAC_DEV, "RX Host error %s on ch=%d\n",
					&emac_rxhost_errcodes[cause][0], ch);
		}
	} /* Host error processing */

	return (num_pkts);
}


#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * emac_poll_controller: EMAC Poll controller function
 * @ndev: The DaVinci EMAC network adapter
 *
 * Polled functionality used by netconsole and others in non interrupt mode
 *
 */
void emac_poll_controller(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	emac_int_disable(priv);
	emac_irq(ndev->irq, priv);
	emac_int_enable(priv);
}
#endif

/*************************************************************************
 *  Linux Driver Model
 *************************************************************************/

/**
 * emac_devioctl: EMAC adapter ioctl
 * @ndev: The DaVinci EMAC network adapter
 * @ifrq: request parameter
 * @cmd: command parameter
 *
 * EMAC driver ioctl function
 *
 * Returns success(0) or appropriate error code
 */
static int emac_devioctl(struct net_device *ndev, struct ifreq *ifrq, int cmd)
{
	dev_warn(&ndev->dev, "DaVinci EMAC: ioctl not supported\n");

	if (!(netif_running(ndev)))
		return -EINVAL;

	/* TODO: Add phy read and write and private statistics get feature */

	return(-EOPNOTSUPP);
}

typedef void (*timer_tick_func) (unsigned long);

/**
 * emac_dev_open: EMAC device open
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system wants to start the interface. We init TX/RX channels
 * and enable the hardware for packet reception/transmission and start the
 * network queue.
 *
 * Returns 0 for a successful open, or appropriate error code
 */
static int emac_dev_open(struct net_device *ndev)
{
	u32 rc, cnt, ch;
	struct emac_priv *priv = netdev_priv(ndev);

	netif_carrier_off(ndev);
	emac_eth_setup(priv);
	for (cnt = 0; cnt <= ETH_ALEN; cnt++)
		ndev->dev_addr[cnt] = priv->mac_addr[cnt];

	spin_lock_init(&priv->tx_lock);
	spin_lock_init(&priv->rx_lock);

	/* Configuration items */
	priv->rx_buf_size = EMAC_DEF_MAX_FRAME_SIZE + EMAC_DEF_EXTRA_RXBUF_SIZE;

	/* initialize the timers for the net device */
	init_timer(&priv->periodic_timer);
	priv->periodic_ticks = (HZ * EMAC_DEF_MDIO_TICK_MS) / 1000;
	priv->periodic_timer.expires = 0;
	priv->timer_active = 0;
	priv->periodic_timer.data = (unsigned long) priv;
	priv->periodic_timer.function = (timer_tick_func) emac_timer_cb;

	/* Clear basic hardware */
	for (ch = 0; ch < EMAC_MAX_TXRX_CHANNELS; ch++) {
		emac_write(EMAC_TXHDP(ch), 0);
		emac_write(EMAC_RXHDP(ch), 0);
		emac_write(EMAC_RXHDP(ch), 0);
		emac_write(EMAC_RXINTMASKCLEAR, EMAC_INT_MASK_CLEAR);
		emac_write(EMAC_TXINTMASKCLEAR, EMAC_INT_MASK_CLEAR);
	}
	priv->mac_hash1 = 0;
	priv->mac_hash2 = 0;
	emac_write(EMAC_MACHASH1, 0);
	emac_write(EMAC_MACHASH2, 0);

	/* multi ch not supported - open 1 TX, 1RX ch by default */
	rc = emac_init_txch(priv, EMAC_DEF_TX_CH);
	if (0 != rc) {
		dev_err(EMAC_DEV, "DaVinci EMAC: emac_init_txch() failed");
		return (rc);
	}
	rc = emac_init_rxch(priv, EMAC_DEF_RX_CH, priv->mac_addr);
	if (0 != rc) {
		dev_err(EMAC_DEV, "DaVinci EMAC: emac_init_rxch() failed");
		return (rc);
	}

	/* Request IRQ */
	if (request_irq(ndev->irq, emac_irq, IRQF_DISABLED, ndev->name, ndev)) {
		dev_err(EMAC_DEV, "DaVinci EMAC: request_irq() failed");
		return (-EBUSY);
	}

	/* Start/Enable EMAC hardware */
	emac_hw_enable(priv);
	if (!netif_running(ndev)) /* debug only - to avoid compiler warning */
		emac_dump_regs(priv);

	if (netif_msg_drv(priv))
		dev_notice(EMAC_DEV, "DaVinci EMAC: Opened %s\n", ndev->name);

	return (0);
}

/**
 * emac_dev_stop: EMAC device stop
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system wants to stop or down the interface. We stop the network
 * queue, disable interrupts and cleanup TX/RX channels.
 *
 * We return the statistics in net_device_stats structure pulled from emac
 */
static int emac_dev_stop(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	/* inform the upper layers. */
	netif_stop_queue(ndev);
	napi_disable(&priv->napi);

	/* stop and delete the mdio tick timer */
	del_timer_sync(&priv->periodic_timer);
	priv->timer_active = 0;

	netif_carrier_off(ndev);
	emac_int_disable(priv);
	emac_stop_txch(priv, EMAC_DEF_TX_CH);
	emac_stop_rxch(priv, EMAC_DEF_RX_CH);
	emac_cleanup_txch(priv, EMAC_DEF_TX_CH);
	emac_cleanup_rxch(priv, EMAC_DEF_RX_CH);
	emac_write(EMAC_SOFTRESET, 1);

	/* Free IRQ */
	free_irq(ndev->irq, priv->ndev);
	if (netif_msg_drv(priv))
		dev_notice(EMAC_DEV, "DaVinci EMAC: %s stopped\n", ndev->name);

	return (0);
}

/**
 * emac_dev_getnetstats: EMAC get statistics function
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system wants to get statistics from the device.
 *
 * We return the statistics in net_device_stats structure pulled from emac
 */
static struct net_device_stats *emac_dev_getnetstats(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	/* update emac hardware stats and reset the registers*/

	priv->net_dev_stats.multicast += emac_read(EMAC_RXMCASTFRAMES);
	emac_write(EMAC_RXMCASTFRAMES, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.collisions += (emac_read(EMAC_TXCOLLISION) +
					   emac_read(EMAC_TXSINGLECOLL) +
					   emac_read(EMAC_TXMULTICOLL));
	emac_write(EMAC_TXCOLLISION, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_TXSINGLECOLL, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_TXMULTICOLL, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.rx_length_errors += (emac_read(EMAC_RXOVERSIZED) +
						emac_read(EMAC_RXJABBER) +
						emac_read(EMAC_RXUNDERSIZED));
	emac_write(EMAC_RXOVERSIZED, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_RXJABBER, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_RXUNDERSIZED, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.rx_over_errors += (emac_read(EMAC_RXSOFOVERRUNS) +
					       emac_read(EMAC_RXMOFOVERRUNS));
	emac_write(EMAC_RXSOFOVERRUNS, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_RXMOFOVERRUNS, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.rx_fifo_errors += emac_read(EMAC_RXDMAOVERRUNS);
	emac_write(EMAC_RXDMAOVERRUNS, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.tx_carrier_errors +=
		emac_read(EMAC_TXCARRIERSENSE);
	emac_write(EMAC_TXCARRIERSENSE, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.tx_fifo_errors = emac_read(EMAC_TXUNDERRUN);
	emac_write(EMAC_TXUNDERRUN, EMAC_ALL_MULTI_REG_VALUE);

	return (&priv->net_dev_stats);
}


/**
 * davinci_emac_probe: EMAC device probe
 * @pdev: The DaVinci EMAC device that we are removing
 *
 * Called when probing for emac devicesr. We get details of instances and
 * resource information from platform init and register a network device
 * and allocate resources necessary for driver to perform
 */
static int __devinit davinci_emac_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct resource *base_res;
	struct resource *irq_res;
	struct net_device *ndev;
	struct emac_priv *priv;

	/* obtain emac clock from kernel */
	emac_clk = clk_get(&pdev->dev, "EMACCLK");
	if (IS_ERR(emac_clk)) {
		printk(KERN_ERR "DaVinci EMAC: Failed to get EMAC clock\n");
		return (-EBUSY);
	}
	emac_bus_frequency = clk_get_rate(emac_clk);

	/* TODO: Probe PHY here if possible */

	ndev = alloc_etherdev(sizeof(struct emac_priv));
	if (!ndev) {
		printk(KERN_ERR "DaVinci EMAC: Error allocating net_device\n");
		clk_put(emac_clk);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ndev);
	priv = netdev_priv(ndev);
	priv->pdev = pdev;
	priv->ndev = ndev;
	priv->msg_enable = netif_msg_init(debug_level, DAVINCI_EMAC_DEBUG);

	/* Get EMAC platform data */
	base_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (base_res == NULL || irq_res == NULL) {
		dev_err(EMAC_DEV, "DaVinci EMAC: Error getting pform res\n");
		rc = -ENOENT;
		goto probe_quit;
	}
	priv->emac_base_regs = base_res->start;
	ndev->base_addr = IO_ADDRESS(base_res->start);
	emac_base_addr = (u32)ndev->base_addr; /* need to check virt2phys */
	ndev->irq = (int) irq_res->start;
	priv->emac_ctrl_regs = ((u32)(base_res->start) +
				EMAC_CONTROL_REGS_OFFSET);

	rc = ((u32)(priv->emac_base_regs) + EMAC_CONTROL_RAM_OFFSET);
	priv->emac_ctrl_ram = IO_ADDRESS(rc);
	priv->mdio_regs = IO_ADDRESS(((u32)(priv->emac_base_regs) +
				     EMAC_MDIO_REGS_OFFSET));

	/* Note that DaVinci EMAC address region is contingous */
	if (!request_mem_region((u32)priv->emac_base_regs,
				(base_res->end - base_res->start),
				ndev->name)) {
		dev_err(EMAC_DEV, "DaVinci EMAC: failed request_mem_region()");
		rc = -ENXIO;
		goto probe_quit;
	}

	/* populate the device structure */
	ndev->validate_addr = 0;
	ndev->open = emac_dev_open;   /*  i.e. start device  */
	ndev->stop = emac_dev_stop;
	ndev->do_ioctl = emac_devioctl;
	ndev->get_stats = emac_dev_getnetstats;
	ndev->set_multicast_list = emac_dev_mcast_set;
	ndev->hard_start_xmit = emac_dev_xmit;
	ndev->tx_timeout = emac_dev_tx_timeout;
	ndev->set_mac_address = emac_dev_setmac_addr;
#ifdef CONFIG_NET_POLL_CONTROLLER
	ndev->poll_controller = emac_poll_controller;
#endif
	netif_napi_add(ndev, &priv->napi, emac_poll, EMAC_POLL_WEIGHT);

	/* register the network device */
	rc = register_netdev(ndev);
	if (rc) {
		dev_err(EMAC_DEV, "DaVinci EMAC: Error in register_netdev\n");
		release_mem_region((u32)priv->emac_base_regs,
				   (base_res->end - base_res->start));
		rc = -ENODEV;
		goto probe_quit;
	}

	clk_enable(emac_clk);
	if (netif_msg_probe(priv)) {
		dev_notice(EMAC_DEV, "DaVinci EMAC Probe found device "\
			   "(regs: %p, irq: %d)\n",
			   (void *)priv->emac_base_regs, ndev->irq);
	}
	return (0);

probe_quit:
	clk_put(emac_clk);
	free_netdev(ndev);
	return (rc);
}

/**
 * davinci_emac_remove: EMAC device remove
 * @pdev: The DaVinci EMAC device that we are removing
 *
 * Called when removing the device driver. We disable clock usage and release
 * the resources taken up by the driver and unregister network device
 */
static int __devexit davinci_emac_remove(struct platform_device *pdev)
{
	struct resource *base_res;
	struct net_device *ndev = platform_get_drvdata(pdev);

	dev_notice(&ndev->dev, "DaVinci EMAC: davinci_emac_remove()\n");

	clk_disable(emac_clk);
	platform_set_drvdata(pdev, NULL);
	base_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(base_res->start, base_res->end - base_res->start);
	unregister_netdev(ndev);
	free_netdev(ndev);
	clk_disable(emac_clk);
	clk_put(emac_clk);

	return (0);
}

/**
 * davinci_emac_driver: EMAC platform driver structure
 *
 * We implement only probe and remove functions - suspend/resume and
 * others not supported by this module
 */
static struct platform_driver davinci_emac_driver = {
	.driver = {
		.name	 = "davinci_emac",
		.owner	 = THIS_MODULE,
	},
	.probe = davinci_emac_probe,
	.remove = davinci_emac_remove,
};


/**
 * davinci_emac_init: EMAC driver module init
 *
 * Called when initializing the driver. We register the driver with
 * the platform.
 */
static int __devinit davinci_emac_init(void)
{
	/* TODO: use mii/phy linux infrastructure and register mdio device */
	printk(KERN_INFO "%s driver initialized\n", emac_version_string);
	return (platform_driver_register(&davinci_emac_driver));
}

/**
 * davinci_emac_exit: EMAC driver module exit
 *
 * Called when exiting the driver completely. We unregister the driver with
 * the platform and exit
 */
void __devexit davinci_emac_exit(void)
{
	platform_driver_unregister(&davinci_emac_driver);
}

module_init(davinci_emac_init);
module_exit(davinci_emac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DaVinci EMAC Maintainer: Anant Gole <anantgole@ti.com>");
MODULE_DESCRIPTION("DaVinci EMAC Ethernet driver");
