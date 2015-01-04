#ifdef V2R_DEBUG
	#define DEBUG
#endif
#define DEBUG

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/edma.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/nmi.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/unistd.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/fiq.h>
#include <mach/mux.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/rto.h>
#include <mach/common.h>

#include "timers.h"

#define DEVICE_NAME "v2r_irsend"

#define TRUE 	 1
#define FALSE 	 0
#define NA 		-1

/* Prototypes */
static void start_timer_interrupts (void);
static void stop_timer_interrupts (void);
static void set_timer3 (unsigned long);
static void dma_start (void);
static void dma_stop (void);
static void dma_release_rto (void);
static int register_fiq (int);
static void dma_rto_transfer (void);
static unsigned int ir_data_send (void);

static int stop_flag = 0;
                      
/* Timer registers base address. Initially it is 0, but during initialization it gets its value */
static volatile unsigned long* TMR3_BASE = 0x00000000;
static int TMR3_IRQ = 0x00000000;
static volatile unsigned long* RTO_BASE = 0x00000000;

/* for IR */
static int SN_PULSE = 12670; //uS 39477 Hz 
// #define SN_PULSE 12670 //uS 39477 Hz 
// #define N_PULSE 13 //uS 38 kHz

static char * global_send_array;
static unsigned int first_value;
static DECLARE_WAIT_QUEUE_HEAD(wq);
static int ir_flag = 1;
/* end for IR */

static struct platform_driver davinci_rto_driver = {
	.driver		= {
		.name	= "davinci_rto_driver",
	},
};

struct rto_dma_dev_str {
	struct dma_chan	*dma_tx;
	int	dma_tx_chnum;
	void * pbase;
	dma_addr_t tx_dma;
	void * timer_table;
	unsigned int len_timer_table;
	struct device * devptr;
	struct completion done;
};

static struct rto_dma_dev_str rto_dma_dev;


///////////////////////////////////////////////////////////////////////////////
// Module params

/* RTO channel number for use (0-3) */
static int rto_channel = 0;
module_param_named(channel, rto_channel, int, 0);

/* IR pulse frequency */
static int pulsefreq = 0;
module_param_named(freq, pulsefreq, int, 0);


///////////////////////////////////////////////////////////////////////////////
// Timer functions

void set_timer3(unsigned long val){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	//Set next time time for timer to generate interrupt
	tptr->rel12 = val;
}

/* Method to say Linux that we need this interrupt */
static int timer3_irq_chain(unsigned int irq){
	int ret = 0;
	//ret = request_irq(irq, timer3_interrupt, 0, "timer3", 0);
	ret = register_fiq(irq);
	pr_debug("%s: chain interrupt returned %x\n", DEVICE_NAME, ret);
	return ret;
}

static void start_timer_interrupts(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	volatile davinci_rto* rptr = (volatile davinci_rto*)RTO_BASE;
	
	pr_debug("%s: start timer interrupts %x\n", DEVICE_NAME, (unsigned int)TMR3_BASE);
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	//set_timer3(65535);
	//set_timer3(convert_ms_to_timer((unsigned long)N_PULSE));
	set_timer3(first_value);
	//set_timer3(1000);
	tptr->tim12 = 0;
	tptr->prd12 = 1000; //once we enable interrupts it really occurs at the same moment
	tptr->intctl_stat = 0;//Interrupts are disabled
	tptr->tcr = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE - ENABLE TIMER
	tptr->tgcr = TIMMOD_DUAL_UNCHAINED|BW_COMPATIBLE;
	tptr->tgcr |= (TIM12RS);//out from reset
	tptr->intctl_stat = CMP_INT_EN12;//Interrupts are enabled
	tptr->tcr = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE - ENABLE TIMER
	// once the timer starts it generates interrupt so we load correct prd value and so on
	tptr->intctl_stat |= CMP_INT_STAT12;
	// schedule_timeout_interruptible(msecs_to_jiffies(100)); //delay 100ms

	switch (rto_channel) {
		default:
		case 0:
			rptr->ctrl_status  |= OPPATERNDATA_RTO0;
		break;
		case 1:
			rptr->ctrl_status  |= OPPATERNDATA_RTO1;
		break;
		case 2:
			rptr->ctrl_status  |= OPPATERNDATA_RTO2;
		break;
		case 3:
			rptr->ctrl_status  |= OPPATERNDATA_RTO3;
		break;
	}
}

static void stop_timer_interrupts (void) {
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	volatile davinci_rto* rptr = (volatile davinci_rto*)RTO_BASE;

	pr_debug("%s: stop timer interrupts %x\n", DEVICE_NAME, (unsigned int)TMR3_BASE);
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	tptr->intctl_stat = 0;//Disable interrupts
	tptr->tcr = 0;//DISABLE TIMER
	tptr->tgcr &= ~(TIM12RS);//Put it into reset
	tptr->tim12 = 0;
	tptr->prd12 = 0xFFFFFFFF;
	// Now timer is in reset state and in its initial state
	// stop
	/*
	switch (rto_channel) {
		default:
		case 0:
			rptr->ctrl_status  &=~OPPATERNDATA_RTO0;
		break;
		case 1:
			rptr->ctrl_status  &=~OPPATERNDATA_RTO1;
		break;
		case 2:
			rptr->ctrl_status  &=~OPPATERNDATA_RTO2;
		break;
		case 3:
			rptr->ctrl_status  &=~OPPATERNDATA_RTO3;
		break;
	}
	*/
	
}

//Initial timer settings
static void init_timer_3(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return; // if timer3 base value not set we cant do any
	tptr->tim12  = 0; // SET COUNTER
	tptr->prd12  = 0xFFFFFFFF; // SET INITIAL PERIOD TO MAX VALUE
	tptr->rel12  = 0xFFFFFFFF; // SET INITIAL RELOAD PERIOD TO MAX VALUE
	tptr->tcr    = MD12_CONT_RELOAD; // MD12_CONT; // SET CONTINIOUS MODE
	tptr->tgcr   = TIMMOD_DUAL_UNCHAINED|BW_COMPATIBLE; //ENABLE ONLY FIRS HALF OF TIMER
	
	//tptr->tgcr   = TIMMOD_DUAL_UNCHAINED;  // ENABLE ONLY FIRS HALF OF TIMER
				 	 //TIM34RS | // SECOND HALF IS IN RESET
				 	 //TIM12RS; // And the rest is in reset
	tptr->intctl_stat = 0; // ALL THE INTERRUPTS ARE DISABLED
}


///////////////////////////////////////////////////////////////////////////////
// DMA functions

static void dma_rto_tx_callback(void *data);

static void init_dma_rto (void) {
	dma_cap_mask_t mask;
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	rto_dma_dev.dma_tx = dma_request_channel(mask, edma_filter_fn, &rto_dma_dev.dma_tx_chnum);
	if (rto_dma_dev.dma_tx!=NULL) {
		pr_debug ("%s: Not NULL dma_request_channel\n", DEVICE_NAME);
	} else {
		pr_debug ("%s: NULL dma_request_channel!!!\n", DEVICE_NAME);
	}
	init_completion(&rto_dma_dev.done);
}

static void dma_start (void) {
	//init_dma_rto();
	//start_timer_interrupts();
	dma_rto_transfer();
}

static void dma_stop (void) {
	dma_release_rto();
	stop_timer_interrupts();
}

static void dma_release_rto (void) {
	stop_flag=1;
	schedule_timeout_interruptible(msecs_to_jiffies(25));
	dma_release_channel (rto_dma_dev.dma_tx);
//	kfree(rto_dma_dev.timer_table);
}

static inline struct dma_async_tx_descriptor *device_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
			size_t period_len,	enum dma_transfer_direction dir) {
	return chan->device->device_prep_dma_cyclic(chan, buf_addr, buf_len,
			period_len, dir,DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
}

static void dma_rto_tx_callback(void *data) {

	if (stop_flag) {
		dmaengine_terminate_all(rto_dma_dev.dma_tx);
	} 
	complete(&rto_dma_dev.done);
	stop_timer_interrupts();

	/*
	printk("dma_rto_tx_callback\n");
	unsigned long *array_of_pulses = (unsigned long *)rto_dma_dev.timer_table;
	int i;
	for (i=0;i<20;i++) {
		printk("array_of_pulses[%d]=%d ms\n", i , convert_timer_to_ms(array_of_pulses[i]));
	}
	*/
}

static void dma_rto_transfer(void) {
	struct dma_slave_config dma_tx_conf = {
				.direction = DMA_MEM_TO_DEV,
				.dst_addr = (unsigned long)rto_dma_dev.pbase,
				.dst_addr_width = sizeof(unsigned long),
				.dst_maxburst = 1,
			};
	static struct scatterlist sg_tx;
	struct dma_async_tx_descriptor *txdesc;
	pr_debug ("%s: dst_addr=%X\n", DEVICE_NAME, dma_tx_conf.dst_addr);
	INIT_COMPLETION(rto_dma_dev.done);
	dmaengine_slave_config(rto_dma_dev.dma_tx, &dma_tx_conf);
	sg_init_table(&sg_tx, 1);

	ir_data_send();
	pr_debug("%s: rto_dma_dev.len_timer_table=%d\n", DEVICE_NAME, rto_dma_dev.len_timer_table);
	rto_dma_dev.tx_dma = dma_map_single(rto_dma_dev.devptr, rto_dma_dev.timer_table,
				rto_dma_dev.len_timer_table, DMA_FROM_DEVICE);
	if (!rto_dma_dev.tx_dma) {
		pr_debug ("%s: NULL dma_map_single!!!\n", DEVICE_NAME);
	} else {
		pr_debug ("%s: Not NULL dma_map_single\n", DEVICE_NAME);
	}

	sg_dma_address(&sg_tx) = rto_dma_dev.tx_dma;
	sg_dma_len(&sg_tx) = rto_dma_dev.len_timer_table;
	
	txdesc = dmaengine_prep_slave_sg(rto_dma_dev.dma_tx,
		&sg_tx, 1, DMA_MEM_TO_DEV,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!txdesc) printk("%s: ERROR dmaengine_prep_slave_sg\n", DEVICE_NAME);
	txdesc->callback = dma_rto_tx_callback;
	txdesc->callback_param = NULL;

	dmaengine_submit(txdesc);
	dma_async_issue_pending(rto_dma_dev.dma_tx);
	
	start_timer_interrupts();
	
	wait_for_completion_interruptible(&(rto_dma_dev.done));
	//schedule_timeout_interruptible(msecs_to_jiffies(100)); //delay 100ms
	dma_unmap_single(rto_dma_dev.devptr, rto_dma_dev.tx_dma,
		rto_dma_dev.len_timer_table, DMA_TO_DEVICE);
	kfree(rto_dma_dev.timer_table);
	
	ir_flag = 1;
	wake_up_interruptible(&wq);
}


///////////////////////////////////////////////////////////////////////////////
// RTO functions

/* Initial RTO settings */
static void init_rto(void){
	volatile davinci_rto* rptr = (volatile davinci_rto*)RTO_BASE;
	if (!RTO_BASE) return; // if timer3 base value not set we cant do any

	switch (rto_channel) {
		default:
		case 0:
			rptr->ctrl_status  = SELECTBIT_RTO12 | DETECTBIT_FE_RTO | OUTPUTEMODE_RTO_TOGGLE | OPMASKDATA_RTO0;
		break;
		case 1:
			rptr->ctrl_status  = SELECTBIT_RTO12 | DETECTBIT_FE_RTO | OUTPUTEMODE_RTO_TOGGLE | OPMASKDATA_RTO1;
		break;
		case 2:
			rptr->ctrl_status  = SELECTBIT_RTO12 | DETECTBIT_FE_RTO | OUTPUTEMODE_RTO_TOGGLE | OPMASKDATA_RTO2;
		break;
		case 3:
			rptr->ctrl_status  = SELECTBIT_RTO12 | DETECTBIT_FE_RTO | OUTPUTEMODE_RTO_TOGGLE | OPMASKDATA_RTO3;
		break;
	}
	rptr->ctrl_status |= ENABLE_RTO;
	init_dma_rto();
}


///////////////////////////////////////////////////////////////////////////////
// FIQ functions

extern void fiq_handler(void) __attribute__((naked));

void fiq_handler(void){
	asm volatile (
		"ldr	r9, IPTR_ADR\n"//tptr
		"ldr	r8, [r9]\n"
		"orr	r8, r8, #32768\n" //0x8000
		"str	r8, [r9]\n" //*iptr |= (1<<IRQ_DM365_TINT6);
		"ldr	r9, TPTR_ADR\n"
		"ldr	r8, [r9]\n"
		"orr	r8, r8, #2\n"
		"str	r8, [r9]\n"
		"subs    pc, lr, #4\n"
		"IPTR_ADR:		.word 0xfec48000\n"
		"TPTR_ADR:		.word 0xfec20844\n"
	); 
}

static struct fiq_handler fh = { name: "fiq-testing" };

static int register_fiq (int irq) {  
	struct pt_regs regs;
	if (claim_fiq(&fh)) {   
		pr_debug("%s: couldn't claim FIQ\n", DEVICE_NAME);
		return -1;  
	}
	pr_debug("%s: claim FIQ!\n", DEVICE_NAME);
	regs.ARM_r2 = 0xfec48000;
	//set_fiq_handler(fiqhandler_start, fiqhandler_length);  
	set_fiq_handler(fiq_handler, 0x100);
	set_fiq_regs(&regs);
	enable_fiq(irq);  
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// IR functions

static unsigned int ir_data_send (void) {
	unsigned int *IRsignal = (unsigned int *) global_send_array;
	int i=0;
	int count_pulses=0;
	int j = 0;
	int * positive_pulse;
	int summ_all_pulses=0;
	unsigned long *array_of_pulses;

	int positive_count = 0;
	for (i = 0; IRsignal[i] != 0; i++) {
		if(!(i % 2)) {
			positive_count++;
		}
	}
	pr_debug("%s: positive impulses = %d\n", DEVICE_NAME, positive_count);
	//positive_pulse=calloc(positive_count,sizeof(int));
	positive_pulse=kmalloc(positive_count*sizeof(int), GFP_KERNEL);

	for (i = 0; IRsignal[i] != 0; i++) {
		if(!(i % 2)) {
			count_pulses=((IRsignal[i] * 1000) / (SN_PULSE));
			if (!(count_pulses % 2)) {
				count_pulses++;
			}
			positive_pulse[j] = count_pulses;
			//printk("count_pulses=%d j=%d\n",count_pulses,j);
			j++;
		}
	}
	
	for (i=0;i<j;i++) {
		summ_all_pulses+=positive_pulse[i];
	}
	summ_all_pulses+=positive_count;
	pr_debug("%s: size of pulse array=%d\n", DEVICE_NAME, summ_all_pulses);
	rto_dma_dev.len_timer_table=summ_all_pulses*sizeof(unsigned long);
	rto_dma_dev.timer_table= kzalloc(rto_dma_dev.len_timer_table, GFP_KERNEL);
	array_of_pulses = (unsigned long *)rto_dma_dev.timer_table;
	
	j=0;	
	for (i=0; IRsignal[i]!=0; i++) {
		if(i%2) { 
			array_of_pulses[j]=convert_ms_to_timer((unsigned long)IRsignal[i]);
			//printf("IRsignal[%d]=%d\n",i,IRsignal[i]);
			j++;
			if(j > summ_all_pulses) {
					printk("%s: wrong pulse sum\n", DEVICE_NAME);
					return -1;
				}
		} else {
			int m;
			for(m=0;m<positive_pulse[(i/2)];m++) {
				//array_of_pulses[j]=convert_ms_to_timer((unsigned long)N_PULSE);
				array_of_pulses[j]=convert_ns_to_timer((unsigned long)(SN_PULSE));
				j++;
				if(j > summ_all_pulses) {
					printk("%s: wrong pulse sum\n", DEVICE_NAME);
					return -1;
				}
			}
			//printf("m=%d i=%d positive_pulse[(i/2)=%d\n",m,i,positive_pulse[(i/2)]);
		}
		//printf("IRsignal[%d]=%d\n",i,IRsignal[i]);
	}
	pr_debug("%s: i=%d summ_all_pulses =%d, j=%d\n", DEVICE_NAME, i, summ_all_pulses, j);

/*
	array_of_pulses[0]=convert_ms_to_timer(200);
	array_of_pulses[20]=convert_ms_to_timer(200);
	array_of_pulses[j-20]=convert_ms_to_timer(100);
	
	
	array_of_pulses[j-2]=convert_ms_to_timer(100);
	//array_of_pulses[j-1]=convert_ms_to_timer(200);
	array_of_pulses[j-1]=convert_ms_to_timer(2000);
	//array_of_pulses[j]=convert_ms_to_timer(300);
*/
	kfree(positive_pulse);
/*	
	for (i=0; i<20; i++) {
		printk("%d %ld\n", i, convert_timer_to_ms(array_of_pulses[i]));
	}
*/	
	array_of_pulses[j]=convert_ms_to_timer(2000);
	rto_dma_dev.len_timer_table--;
	rto_dma_dev.timer_table=(void *)(&array_of_pulses[1]);
	first_value=array_of_pulses[0];

	return summ_all_pulses;
}


///////////////////////////////////////////////////////////////////////////////
// DEVFS functions

static int minor = 0;
module_param(minor, int, S_IRUGO);

static ssize_t dev_read( struct file * file, char * buf, size_t count, loff_t *ppos ) {
	char *info_str = "";
	int len = strlen(info_str);
	if(count < len) return -EINVAL;
	if( *ppos != 0 ) {
		return 0;
	}
	if( copy_to_user( buf, info_str, len ) ) return -EINVAL;
	*ppos = len;
	return len;
}

static ssize_t dev_write( struct file *file, const char *buf, size_t count, loff_t *ppos ) {
	wait_event_interruptible(wq, ir_flag != 0); 
	ir_flag = 0;   /* what happens if this is set to 0? */
	if ( (global_send_array=kmalloc( count+1, GFP_KERNEL) ) == NULL )
		return -ENOMEM;
	if ( copy_from_user( (void*)(global_send_array), (void*)buf, count ) != 0 ){
		printk( "%s: copy from user error\n", DEVICE_NAME);
		return -ENOMEM;
	}
	/*
	unsigned int *array_of_pulses =(unsigned int *)global_send_array;
	int i;
	for (i=0; array_of_pulses[i]!=0; i++) {
		printk("array[%d]=%i\n",i, array_of_pulses[i]);
	}
	*/
	dma_start();
	kfree(global_send_array);
	// mutex_unlock(&v2rswpwm_mutex);//Unlocking the mutex
	return count;
	// return ret;
}

static ssize_t dev_open (struct inode *inode, struct file *filp) {
	return 0;
}

int dev_release(struct inode *inode, struct file *filp) {
	return 0;
}

static const struct file_operations ppmsum_fops = {
	.owner   = THIS_MODULE,
	.open    = dev_open,
	.release = dev_release,
	.read    = dev_read,
	.write   = dev_write,
};

static struct miscdevice ppmsum_dev = {
	MISC_DYNAMIC_MINOR,
	DEVICE_NAME,
	&ppmsum_fops
};

static int __init rtodrv_probe(struct platform_device *pdev){
	struct resource			*res;
	struct resource			*res_irq;
	void __iomem			*vaddr;
	volatile unsigned char* memptr = 0;
	struct clk* clock = 0;
	rto_dma_dev.devptr = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	clock = clk_get(&pdev->dev, "timer3");//Enable clock for timer3
	clk_enable(clock);
	pr_debug("%s: TIMER3 res: %x, %x\n", DEVICE_NAME, res->start, res->end);
	rto_dma_dev.pbase=(void*)res->start+REL12_OFFSET;
	
	vaddr = ioremap(res->start, res->end - res->start);
	memptr = (volatile unsigned char*)vaddr;
	pr_debug("%s: TIMER3 remap address: %x\n", DEVICE_NAME, (unsigned int)vaddr);
	TMR3_BASE = (volatile unsigned long*)vaddr;
	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	pr_debug("%s: TIMER3 IRQ: 0x%X\n", DEVICE_NAME, (unsigned int)res_irq->start);
	TMR3_IRQ = res_irq->start;
	init_timer_3();
	pr_debug("%s: TIMER3 init done\n", DEVICE_NAME);
	timer3_irq_chain(TMR3_IRQ);
	pr_debug("%s: Timer interrupts %d is chained\n", DEVICE_NAME, TMR3_IRQ);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	clock = clk_get(&pdev->dev, "rto");//Enable clock for rto
	clk_enable(clock);
	pr_debug("%s: RTO res: %x, %x\n", DEVICE_NAME, res->start, res->end);
	vaddr = ioremap(res->start, res->end - res->start);
	memptr = (volatile unsigned char*)vaddr;
	pr_debug("%s: RTO remap address: %x\n", DEVICE_NAME, (unsigned int)vaddr);
	RTO_BASE = (volatile unsigned long*)vaddr;

	if (pulsefreq) {
		// TODO: calculate SN_PULSE from real freq
		SN_PULSE = pulsefreq;
	}
	printk("%s: using RTO channel %d\n", DEVICE_NAME, rto_channel);
	printk("%s: IR freq = %d Hz\n", DEVICE_NAME, SN_PULSE);

	//davinci_fiq0_regs = ioremap(DAVINCI_ARM_INTC_BASE, SZ_4K);
	//pr_debug("interrupt remap address: %x\n", (unsigned int)davinci_fiq0_regs);

	switch (rto_channel) {
		default:
		case 0:
			davinci_cfg_reg(DM365_RTO0);
		break;
		case 1:
			davinci_cfg_reg(DM365_RTO1);
		break;
		case 2:
			davinci_cfg_reg(DM365_RTO2);
		break;
		case 3:
			davinci_cfg_reg(DM365_RTO3);
		break;
	}

	init_rto();
	//device_create_file(&pdev->dev, &dev_attr_debug);
	return 0;	
}

static int __init dev_init (void) {
	int ret;
	int err;
	if(minor != 0) ppmsum_dev.minor = minor;
	ret = misc_register( &ppmsum_dev );
	if (ret) printk (KERN_ERR "%s: unable to register misc device\n", DEVICE_NAME);
	pr_debug("%s: module init\n", DEVICE_NAME);
	err = platform_driver_probe(&davinci_rto_driver, rtodrv_probe);
	return ret;
}

static void __exit dev_exit( void ) {
	misc_deregister( &ppmsum_dev );
	dma_release_rto();
	stop_timer_interrupts();
	disable_fiq(TMR3_IRQ);  
	release_fiq(&fh);
	platform_driver_unregister(&davinci_rto_driver);
	pr_debug("%s: module exit\n", DEVICE_NAME);
}
 
static int __init dev_init(void);
module_init(dev_init);

static void __exit dev_exit(void);
module_exit(dev_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Dolin Sergey <dlinyj@gmail.com>");
MODULE_AUTHOR("Rewrited by Gol <hypergol@gmail.com>");
MODULE_VERSION("0.2");
