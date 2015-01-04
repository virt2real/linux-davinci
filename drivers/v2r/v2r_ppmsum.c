#ifdef V2R_DEBUG
	#define DEBUG
#endif

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


// Prototypes
static void start_timer_interrupts(void);
static void stop_timer_interrupts(void);
void set_timer3(unsigned long val);
static void dma_set_buffer_data(void);
static void dma_print_times_data (void);
static void dma_start(void);
static void dma_stop(void);

static int stop_flag=0;

/* Timer registers base address. Initially it is 0, but during initialization it gets its value */
static volatile unsigned long* TMR3_BASE = 0x00000000;
static int TMR3_IRQ = 0x00000000;
static volatile unsigned long* RTO_BASE = 0x00000000;

#define TRUE 	 1
#define FALSE 	 0
#define NA 		-1

/*
//Driver defines
#define MAX_PIN_NUMBER 88 //Number of connections to use on this platform
#define MAX_PWM_NUMBER 8 //Maximum PWM ports to use at this platform
#define GPIO_CONF_REGS_NUMBER 6//Number of GPIO configuration registers plus two
#define TIMER_FREQ 24000 //1000Hz
#define DEFAULT_PERIOD 20 //ms
*/

/*
//Error codes

#define SWPWM_OK               0  //Success
#define SWPWM_WRONGPIN         1  //Try to set wrong pin
#define SWPWM_WRONGVAL         2  //Try to set wrong value
#define SWPWM_BUSY             3  //Chosen PWN is currently in use
#define SWPWM_NO_RESOURCES     4  //All PWM resources are busy
#define SWPWM_UNKNOWN_ERROR    5  //Any other error
*/


//#define UART0_BASE    IO_ADDRESS((IO_PHYS + 0x20000))
#define PPMSUM_BUFFER_SIZE 8192


///Structure element to store driver configuration

static struct platform_driver davinci_rto_driver = {
	.driver		= {
		.name	= "davinci_rto_driver",
	},
};

struct rto_dma_dev_str {
	struct dma_chan		*dma_tx;
	int			dma_tx_chnum;
	void * 		pbase;
	
	dma_addr_t	tx_dma;
	
	
	void * 		timer_table;
	unsigned int 		len_timer_table;
	
	struct device * devptr;
};

static struct rto_dma_dev_str rto_dma_dev;


//time in 24 000 000
#define DEF_FRAME 	480000 // 20 ms
#define DEF_CH0		10000
#define DEF_CH1		20000
#define DEF_CH2		30000
#define DEF_CH3		40000
#define DEF_CH4		50000
#define DEF_CH5		60000
#define DEF_CH6		70000
#define DEF_CH7		80000
//#define DEF_PULSE	120 //10 ns
#define DEF_PULSE	9480 //420 mkS

struct ppmsum_str {
	unsigned long frame; 
	unsigned long ch[8];
	unsigned long pulse;
	//unsigned long endframe;
};

static struct ppmsum_str ppmsum = {
	DEF_FRAME, // frame = 20 ms
	{DEF_CH0,DEF_CH1,DEF_CH2,DEF_CH3,DEF_CH4,DEF_CH5,DEF_CH6,DEF_CH7}, //all chanels =0
	DEF_PULSE, //pulse
	//DEF_FRAME-(DEF_CH0+DEF_CH1+DEF_CH2+DEF_CH3+DEF_CH4+DEF_CH5+DEF_CH6+DEF_CH7+8*DEF_PULSE) //end frame calc!
};

static int ch_count=8;
module_param_named( channels, ch_count, int, 0 ); 
static int ns=0;
module_param_named( ns, ns, int, 0 ); 



static char** split(const char* str, char delimiter){
	int input_len = strlen(str);
	int nIndex = 0;
	int nBites = 0;
	int nTempIndex = 0;
	int nTempByte = 0;
	char** result; 
	//Counting bits
	for (nIndex = 0; nIndex < input_len; nIndex++){
		if (str[nIndex] == delimiter) nBites++;
	}
	result = (char**)kmalloc((nBites+2)*sizeof(char*),GFP_KERNEL);
	for (nIndex = 0; nIndex <= input_len; nIndex++){
		if ((str[nTempIndex] == delimiter)||(str[nTempIndex] == 0)||(str[nTempIndex] == '\n')||(str[nTempIndex] == '\r') ){
			 if (nTempIndex != 0){
			 result[nTempByte] = (char*)kmalloc(nTempIndex+1,GFP_KERNEL);
			 memcpy(result[nTempByte], str, nTempIndex);
			 result[nTempByte][nTempIndex] = 0;
			 nTempByte++;
			 str += nTempIndex+1;
			 nTempIndex = 0;
			 } else { str++; nTempIndex = 0;}
		} else {
			 nTempIndex++;
	}
	}
	result[nTempByte] = 0;
	for (nIndex = 0; nIndex < nTempByte; nIndex++){
		 pr_debug("Chunk %d = %s\n", nIndex, result[nIndex]);
	}
	return result;   
};

static int is_equal_string(const char* str1, const char* str2){
	int len = strlen(str1);
	int i = 0;
	if (len != strlen(str2)){
		 //pr_debug("IS EQUAL STRING LEN ERROR\n"); 
		 return FALSE;}
	for (i = 0; i < len; i++){
	   if (str1[i] != str2[i]) { 
		   //pr_debug("IS EQUAL STRING COMPARE ERROR %s %s %d\n", str1, str2, i); 
		   return FALSE;}
	}
	return TRUE;
}

static int str_to_int(const char* str){
	int len = strlen(str);
	int i = 0;
	unsigned int result = 0;
	if (!len || len > 9) return NA;
	for (i = 0; i < len; i++){
		char c = str[i];
		if (c < '0' || c > '9') return NA;
		result = result*10 + c - '0';
	}
	//dbg_print("str to int returned %d\n", result);
	return result;
}

static int starts_with(const char* str1, const char* ref){
	int i = 0;
	int len = strlen(str1);
	const char* substr = 0;
	if (len <= strlen(ref)) {pr_debug("starts with len error\n"); return NA;}
	len = strlen(ref);
	for (i = 0; i < len; i++){
	 if (str1[i] != ref[i]) {pr_debug("starts with not equal error %d\n", i); return NA;}
	}
	//Here it a good candidate for perfect result. Lets get appropriate value
	substr = &str1[len];
	pr_debug("Really starts with. Substr %s\n", substr);
	//if (is_equal_string(substr, "true")) return 1;
	//if (is_equal_string(substr, "false")) return 0;
	return str_to_int(substr);    
}

/*
Comands:
start - start send PPMSUM
stop - stop send PPMSUM

Set the value of the channel in the nanosecond: 
ch0=<value>
...
ch7=<value>

If <value> == 0, the channel is not being used
set frame time in the nanosecond:
frame=<value>

set pulse
pulse=<value> 


ppmsum {
	unsigned long frame;
	unsigned long ch[8];
	unsigned long pulse;
};


*/

int process_command(char** data){
	//if (!data[0]) return 0;
	int i;
	for (i=0;;i++) {
		if (!data[i]) {
			return 0;
		}
		if (is_equal_string(data[i], "start")){
			pr_debug("Start v2r_ppmsum\n");
			//start_timer_interrupts();
			dma_start();
			continue;
		}
		if (is_equal_string(data[i], "stop")){
			pr_debug("Stop v2r_ppmsum\n");
			//stop_timer_interrupts();
			dma_stop();
			continue;
		}
		// ch#=
		//if (is_equal_string(data[i], "ch")){
		if (strstr(data[i], "ch")!=NULL) {
			pr_debug("Set ch\n");
			if ((data[i][2] >='0') && (data[i][2] <'8')) {
				int position=(int)(data[i][2]-'0');
				pr_debug("Position=%d\n",(int)position);
				char tmpbuf[4] = "ch*=";
				tmpbuf[2]=data[i][2];
				int tmp_int=starts_with(data[i], tmpbuf);
				if (tmp_int >0) {
					if (ns) {
						ppmsum.ch[position]=convert_ns_to_timer(tmp_int);
					} else {
						ppmsum.ch[position]=tmp_int;
					}
				} else {
					printk("Channel time can not be == 0 !!!\n");
				}
				pr_debug("ch %d set to %ld", (int)position, ppmsum.ch[position]);
				continue;
			} else {
				pr_debug("error ch number\n");
				continue;
			}
			
		}
		if (strstr(data[i], "pulse=")!=NULL) {
			int tmp_int=starts_with(data[i], "pulse=");
			if (tmp_int >0) {
				if (ns) {
					ppmsum.pulse=convert_ns_to_timer(tmp_int);
				} else {
					ppmsum.pulse=tmp_int;
				}
			} else {
				printk("Pulse can not be == 0!!!\n");
			}
		}
#if !defined (____X86_____)
		if (strstr(data[i], "print")!=NULL) {
			dma_print_times_data();
		}
#endif
	}
/*
	if (is_equal_string(data[0], "start")){

		int gpio = 0;
		int duty = 0;
		if (!data[1] || !data[2]) return 0;
		gpio = starts_with(data[1], "con:");
		duty = starts_with(data[2], "duty:");
		if (gpio == NA || duty == NA) return 0;
		pr_debug("Starts con %d, duty %d\n", gpio, duty);
		//start_pwm(gpio, duty);
		pr_debug("Start PPMSUM");

	} else if (is_equal_string(data[0], "stop")){
		int gpio = 0;
		if (!data[1] ) return 0;
		gpio = starts_with(data[1], "con:");
		if (gpio == NA ) return 0;
		pr_debug("Stops con %d\n", gpio);
		//stop_pwm(gpio);

		pr_debug("Stop PPMSUM");
	} else if (is_equal_string(data[0], "period")){
		int period = 0;
		if (!data[1] ) return 0;
		period = starts_with(data[1], "");
		if (period == NA ) return 0;
		pr_debug("Period %d\n", period);
		//set_period(period);
	}
*/
	return 0;
}


#if !defined (____X86_____)



static void start_timer_interrupts(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	volatile davinci_rto* rptr = (volatile davinci_rto*)RTO_BASE;
	
	pr_debug("Start timer interrupts %x\n", (unsigned int)TMR3_BASE);
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	//set_timer3(65535);
	set_timer3(12000000);
	tptr->tim12 = 0;
	tptr->prd12 = 1000;//once we enable interrupts it really occurs at the same moment
	tptr->intctl_stat = 0;//Interrupts are disabled
	tptr->tcr = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE - ENABLE TIMER
	tptr->tgcr = TIMMOD_DUAL_UNCHAINED|BW_COMPATIBLE;
	tptr->tgcr |= (TIM12RS);//out from reset
	tptr->intctl_stat = CMP_INT_EN12;//Interrupts are enabled
	tptr->tcr = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE - ENABLE TIMER
	//once the timer starts it generates interrupt so we load correct prd value and so on
	tptr->intctl_stat |= CMP_INT_STAT12;

	schedule_timeout_interruptible(msecs_to_jiffies(100)); //delay 100ms
	rptr->ctrl_status  |= OPPATERNDATA_RTO0;
}
static void stop_timer_interrupts(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	volatile davinci_rto* rptr = (volatile davinci_rto*)RTO_BASE;

	pr_debug("Stop timer interrupts %x\n", (unsigned int)TMR3_BASE);
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	tptr->intctl_stat = 0;//Disable interrupts
	tptr->tcr = 0;//DISABLE TIMER
	tptr->tgcr &= ~(TIM12RS);//Put it into reset
	tptr->tim12 = 0;
	tptr->prd12 = 0xFFFFFFFF;
	//Now timer is in reset state and in its initial state

//stop !!!!
	rptr->ctrl_status  &=~OPPATERNDATA_RTO0;
	
}

//Initial timer settings
static void init_timer_3(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	tptr->tim12  = 0;//SET COUNTER
	tptr->prd12  = 0xFFFFFFFF;//SET INITIAL PERIOD TO MAX VALUE
	tptr->rel12  = 0xFFFFFFFF;//SET INITIAL RELOAD PERIOD TO MAX VALUE
	tptr->tcr    = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE
	tptr->tgcr   = TIMMOD_DUAL_UNCHAINED|BW_COMPATIBLE;  //ENABLE ONLY FIRS HALF OF TIMER
	
	//tptr->tgcr   = TIMMOD_DUAL_UNCHAINED;  //ENABLE ONLY FIRS HALF OF TIMER
	
				 //TIM34RS | //SECOND HALF IS IN RESET
				 //TIM12RS; //And the rest is in reset
	tptr->intctl_stat = 0;//ALL THE INTERRUPTS ARE DISABLED
}

/*
ppmsum {
	unsigned long frame;
	unsigned long ch[8];
	unsigned long pulse;
	unsigned long endframe;
};
*/

static void dma_set_buffer_data (void) {
	//pr_debug("dma_set_buffer_data\n");
	int i;
	unsigned long sum_chan=0;
	unsigned long *pbuf = (unsigned long *)rto_dma_dev.timer_table;
		for (i=0;i<ch_count;i++) {
			pbuf[i*2]=ppmsum.pulse;
			pbuf[i*2+1]=ppmsum.ch[i]-ppmsum.pulse;
			sum_chan+=ppmsum.ch[i];
		}
		pbuf[ch_count*2]=ppmsum.pulse;
		//pbuf[ch_count*2+1]=ppmsum.endframe;
		pbuf[ch_count*2+1]=ppmsum.frame-(sum_chan+ppmsum.pulse);
}

static void dma_print_times_data (void) {
	unsigned long *pbuf = (unsigned long *)rto_dma_dev.timer_table;
	int i;

	unsigned int sum=0;
	for (i=0;i<(ch_count*2+2);i++) {
		sum+=pbuf[i];
	}

	for (i=0;i<ch_count;i++) {
		printk("pbuf[%d]=%ld\t\t(%ld\tns)\n",i*2,pbuf[i*2], convert_timer_to_ns(pbuf[i*2]));
		printk("pbuf[%d]=%ld\t\t(%ld\tns)\n",i*2+1,pbuf[i*2+1],convert_timer_to_ns(pbuf[i*2+1]));
	}
	printk("pbuf[%d]=%ld\t\t(%ld\tns)\n",ch_count*2,pbuf[ch_count*2],convert_timer_to_ns(pbuf[ch_count*2]));
	printk("pbuf[%d]=%ld\t\t(%ld\tns)\n",ch_count*2+1,pbuf[ch_count*2+1], convert_timer_to_ns(pbuf[ch_count*2+1]));

	printk("sum=%d frame= %ld (%ld\tns)\n",sum,ppmsum.frame,convert_timer_to_ns(ppmsum.frame));
}



static void dma_release_rto (void) {
	stop_flag=1;
	schedule_timeout_interruptible(msecs_to_jiffies(25));
	dma_release_channel (rto_dma_dev.dma_tx);
	kfree(rto_dma_dev.timer_table);
}

static void dma_rto_tx_callback(void *data);

static inline struct dma_async_tx_descriptor *device_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
			size_t period_len,	enum dma_transfer_direction dir)
{
	return chan->device->device_prep_dma_cyclic(chan, buf_addr, buf_len,
			period_len, dir,DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
}

/*
static void davinci_rto_transer(void) {
	//dma_cap_mask_t mask;
	struct dma_slave_config dma_tx_conf = {
				.direction = DMA_MEM_TO_DEV,
				.dst_addr = (unsigned long)rto_dma_dev.pbase, // ******************************
				.dst_addr_width = sizeof(unsigned long),
				.dst_maxburst = 1,
			};
	static struct scatterlist sg_tx;
	struct dma_async_tx_descriptor *txdesc;

	dmaengine_slave_config(rto_dma_dev.dma_tx, &dma_tx_conf);
	sg_init_table(&sg_tx, 1);
	
	rto_dma_dev.tx_dma = dma_map_single(NULL, rto_dma_dev.timer_table,
				rto_dma_dev.len_timer_table, DMA_FROM_DEVICE);
	sg_dma_address(&sg_tx) = rto_dma_dev.tx_dma;
	sg_dma_len(&sg_tx) = rto_dma_dev.len_timer_table;
	txdesc = dmaengine_prep_slave_sg(rto_dma_dev.dma_tx,
				&sg_tx, 1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	txdesc->callback = dma_rto_tx_callback;
	txdesc->callback_param = NULL;
	dmaengine_submit(txdesc);
	dma_async_issue_pending(rto_dma_dev.dma_tx);
}
*/

static void dma_rto_tx_callback(void *data)
{
	if (stop_flag) {
		dmaengine_terminate_all(rto_dma_dev.dma_tx);
	} else {
		dma_set_buffer_data();
	}
}

static void init_dma_rto (void) {
	dma_cap_mask_t mask;
	struct dma_slave_config dma_tx_conf = {
				.direction = DMA_MEM_TO_DEV,
				.dst_addr = (unsigned long)rto_dma_dev.pbase, // ******************************
				.dst_addr_width = sizeof(unsigned long),
				.dst_maxburst = 1,
			};
	static struct scatterlist sg_tx;
	struct dma_async_tx_descriptor *txdesc;
	pr_debug ("dst_addr=%X\n",dma_tx_conf.dst_addr);
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	rto_dma_dev.dma_tx = dma_request_channel(mask, edma_filter_fn, &rto_dma_dev.dma_tx_chnum);
	if (rto_dma_dev.dma_tx!=NULL) {
		pr_debug ("Not Nulll dma_request_channel\n");
	} else {
		pr_debug ("Nulll dma_request_channel!!!\n");
	}
	dmaengine_slave_config(rto_dma_dev.dma_tx, &dma_tx_conf);
	sg_init_table(&sg_tx, 1);
	
	
	//allocated tx buffer
	//rto_dma_dev.len_timer_table=18*sizeof(unsigned long);//should be 16
	rto_dma_dev.len_timer_table=(2*ch_count+2)*sizeof(unsigned long);//should be 16
	rto_dma_dev.timer_table= kzalloc(rto_dma_dev.len_timer_table, GFP_KERNEL);
	dma_set_buffer_data();
	dma_print_times_data();
	
	
	/*
	if (!rto_dma_dev.timer_table)
		goto err_alloc_dummy_buf;
	*/
	/*
	rto_dma_dev.tx_dma = dma_map_single(dev, rto_dma_dev.timer_table,
				16*sizeof(unsigned long), DMA_FROM_DEVICE);
	*/
	
	rto_dma_dev.tx_dma = dma_map_single(rto_dma_dev.devptr, rto_dma_dev.timer_table,
				rto_dma_dev.len_timer_table, DMA_FROM_DEVICE);
	if (!rto_dma_dev.tx_dma) {
		pr_debug ("Null dma_map_single!!!\n");
	} else {
		pr_debug ("Not Null dma_map_single\n");
	}

	sg_dma_address(&sg_tx) = rto_dma_dev.tx_dma;
	sg_dma_len(&sg_tx) = rto_dma_dev.len_timer_table;
	
 #if 0
	txdesc = dmaengine_prep_slave_sg(rto_dma_dev.dma_tx,
				&sg_tx, 1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
#else
	txdesc = device_prep_dma_cyclic(rto_dma_dev.dma_tx, rto_dma_dev.tx_dma, rto_dma_dev.len_timer_table,
									rto_dma_dev.len_timer_table, DMA_MEM_TO_DEV);
#endif
	if (!txdesc) printk("ERROR dmaengine_prep_slave_sg\n");
	txdesc->callback = dma_rto_tx_callback;
	txdesc->callback_param = NULL;

	dmaengine_submit(txdesc);
	dma_async_issue_pending(rto_dma_dev.dma_tx);
}

static void dma_start (void) {
	//init_dma_rto();
	start_timer_interrupts();
}

static void dma_stop (void) {
	dma_release_rto();
	stop_timer_interrupts();
}

//Initial rto settings
static void init_rto(void){
	volatile davinci_rto* rptr = (volatile davinci_rto*)RTO_BASE;
	if (!RTO_BASE) return;//if timer3 base value not set we cant do any
	//rptr->ctrl_status  = SELECTBIT_RTO12 | DETECTBIT_FE_RTO | OUTPUTEMODE_RTO_TOGGLE | OPMASKDATA_RTO0 | OPPATERNDATA_RTO0;
	rptr->ctrl_status  = SELECTBIT_RTO12 | DETECTBIT_FE_RTO | OUTPUTEMODE_RTO_TOGGLE | OPMASKDATA_RTO0;
	rptr->ctrl_status |= ENABLE_RTO;
	init_dma_rto();
}

void set_timer3(unsigned long val){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	//Set next time time for timer to generate interrupt
	tptr->rel12 = val;
}
/*
static int irq_timer3(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return 0;//if timer3 base value not set we cant do any
	if (tptr->intctl_stat & CMP_INT_STAT12){//If our interrupt
		tptr->intctl_stat |= CMP_INT_STAT12;//clear status bit
		return 1;//and return 1
	} else {
		return 0;
	}
}
*/

#if 0
#define DAVINCI_ARM_INTC_BASE 0x01C48000
static void __iomem *davinci_fiq0_regs = 0;
extern void fiq_handler(void) __attribute__((interrupt("FIQ")));
void fiq_handler(void){
	volatile unsigned long *tptr=(volatile unsigned long*)(0xfec20844);
	volatile unsigned long *iptr=(volatile unsigned long*)(0xfec48000);
	*iptr |= (1<<IRQ_DM365_TINT6);
	*tptr |= CMP_INT_STAT12;
}
#else
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
/*
			//uart send &&&\r\n
		"ldr     r9, UART_ADR\n"
		"mov     r8, #'&'\n"
		"str     r10, [r9]\n"
		"str     r8, [r9]\n"
		"str     r8, [r9]\n"
		"mov     r8, #'\r'\n"
		"str     r8, [r9]\n"
		"mov     r8, #'\n'\n"
		"str     r8, [r9]\n"
		"subs    pc, lr, #4\n"
		"UART_ADR:    .word 0xfec20000\n"
*/
		"subs    pc, lr, #4\n"
		"IPTR_ADR:		.word 0xfec48000\n"
		"TPTR_ADR:		.word 0xfec20844\n"
	); 
}
#endif

static struct fiq_handler fh = {  name: "fiq-testing" };

static int register_fiq (int irq)
 {  
	struct pt_regs regs;
	 if (claim_fiq(&fh))  
	{   
		printk("couldn't claim FIQ.\n");   
		return -1;  
	}
	pr_debug("Claim FIQ!\n");
	regs.ARM_r2 = 0xfec48000;
	//set_fiq_handler(fiqhandler_start, fiqhandler_length);  
	set_fiq_handler(fiq_handler,0x100);
	set_fiq_regs(&regs);
	enable_fiq(irq);  
	return 0;
}

/*
static irqreturn_t timer3_interrupt(int irq, void *dev_id){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	tptr->intctl_stat |= CMP_INT_STAT12;
	if (tptr->intctl_stat & CMP_INT_STAT12){//If our interrupt
		tptr->intctl_stat |= CMP_INT_STAT12;//clear status bit
	}
	int handled = 0;
	if (irq_timer3()){//Our interrupt must be processed
		handled = IRQ_HANDLED;
	} else {
		handled = IRQ_NONE;
	}
	//pr_debug("IRQ!!!\n");
	return IRQ_RETVAL(handled);
}
*/


//Method to say Linux that we need this interrupt
static int timer3_irq_chain(unsigned int irq){
	int ret = 0;
	//ret = request_irq(irq, timer3_interrupt, 0, "timer3", 0);
	ret = register_fiq(irq);
	pr_debug("Chain interrupt returned %x\n", ret);
	return ret;
}

#endif // !defined (____X86_____)

/* FS block */
static int minor = 0;
module_param( minor, int, S_IRUGO );

static char *info_str = 
	"v2r_ppmsum device driver\nAutor Dolin Sergey aka dlinyj dliny@gmail.com\n"
	"Comands: \n"
	"start - start send PPMSUM\n"
	"stop - stop send PPMSUM\n"
	"\n"
	"Set the value of the channel in the nanosecond: \n"
	"ch0=<value>\n"
	"...\n"
	"ch7=<value>\n"
	"\n"
	"If <value> == 0, the channel is not being used\n"
	"set frame time in the nanosecond:\n"
	"frame=<value>\n"
	"set pulse\n"
	"pulse=<value>\n"
	"show current times:\n"
	"print\n"; //infostr
	
	
	
static ssize_t dev_read( struct file * file, char * buf,
						size_t count, loff_t *ppos ) {
	int len = strlen( info_str );
	if( count < len ) return -EINVAL;
	if( *ppos != 0 ) {
		return 0;
	}
	if( copy_to_user( buf, info_str, len ) ) return -EINVAL;
	*ppos = len;
	return len;
}


static ssize_t dev_write( struct file *file, const char *buf, size_t count, loff_t *ppos ) {
	ssize_t retval = 0;
	char *tmp = 0;
	int nIndex = 0;
	char** data = 0;
	pr_debug("v2r_ppmsum write\n");
//	if (mutex_lock_killable(&dev->v2rswpwm_mutex)) return -EINTR;//Locking the mutex to avoid simultanios accesses from different processes

	if (*ppos !=0) {
	// Writing in the middle of the file is not allowed 
		pr_debug("Writing in the middle (%d) of the file buffer is not allowed\n", (int)(*ppos));
		retval = -EINVAL;
		goto out;
	}
	if (count > PPMSUM_BUFFER_SIZE) count = PPMSUM_BUFFER_SIZE;//If we write a lot of data we have to cut the input
	tmp = kmalloc(count+1,GFP_KERNEL);
	if (tmp==NULL)	return -ENOMEM;
	if (copy_from_user(tmp,buf,count)){
		kfree(tmp);
		retval = -EFAULT;
		goto out;
	}
	tmp[count] = 0;//This is a string, that contains driver input buffer
	//dev->buffer_data_size = 0;
	//tmp contains string to parse
	data = split(tmp, ' ');
	process_command(data);


/*	
	memcpy(dev->data, "OK", 3);//Ufff that is really bad :(
	dev->buffer_data_size = 3;//%(
	*/
	nIndex = 0;
	while(data[nIndex]){//Release allocated memory
		kfree(data[nIndex]);
		nIndex++;
	}
	kfree(data);
	pr_debug("v2r_ppmsum-dev message %s\n", tmp);
	kfree(tmp);
	*ppos = 0;//Set file pointer to zero
	retval = count;
out:
 // 	mutex_unlock(&dev->v2rswpwm_mutex);//Unlocking the mutex
	return retval;
}

static ssize_t  dev_open (struct inode *inode, struct file *filp){
	//Major and minor numbers are defined by the driver itself during its initialization
//	unsigned int mj = imajor(inode);//get major number
//	unsigned int mn = iminor(inode);//get minor number
	pr_debug("open\n");
	return 0;
}

int dev_release(struct inode *inode, struct file *filp)
{
	pr_debug("dev_release\n");
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
	MISC_DYNAMIC_MINOR,    // автоматически выбираемое
	"v2r_ppmsum",
	&ppmsum_fops
};

#if !defined (____X86_____)
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
	pr_debug("TIMER3 res: %x, %x\n", res->start, res->end);
	rto_dma_dev.pbase=(void*)res->start+REL12_OFFSET; // !!!
	
	vaddr = ioremap(res->start, res->end - res->start);
	memptr = (volatile unsigned char*)vaddr;
	pr_debug("TIMER3 remap address: %x\n", (unsigned int)vaddr);
	TMR3_BASE = (volatile unsigned long*)vaddr;
	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	pr_debug("TIMER3 IRQ: %x\n", (unsigned int)res_irq->start);
	TMR3_IRQ = res_irq->start;
	init_timer_3();
	pr_debug("Timer 3 is inited\n");
	timer3_irq_chain(TMR3_IRQ);
	pr_debug("Timer interrupts %d is chained\n", TMR3_IRQ);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	clock = clk_get(&pdev->dev, "rto");//Enable clock for rto
	clk_enable(clock);
	pr_debug("RTO res: %x, %x\n", res->start, res->end);
	vaddr = ioremap(res->start, res->end - res->start);
	memptr = (volatile unsigned char*)vaddr;
	pr_debug("RTO remap address: %x\n", (unsigned int)vaddr);
	RTO_BASE = (volatile unsigned long*)vaddr;
	//
	//davinci_fiq0_regs = ioremap(DAVINCI_ARM_INTC_BASE, SZ_4K);
	//pr_debug("interrupt remap address: %x\n", (unsigned int)davinci_fiq0_regs);
	//
	davinci_cfg_reg(DM365_RTO0);
	init_rto();
	return 0;	
}
#endif

static int __init dev_init( void ) {
	int ret;
	int err;
	if( minor != 0 ) ppmsum_dev.minor = minor;
	ret = misc_register( &ppmsum_dev );
	if( ret ) printk( KERN_ERR "=== Unable to register misc device\n" );
	pr_debug("v2r_ppmsum init\n");
#if !defined (____X86_____)
	err = platform_driver_probe(&davinci_rto_driver, rtodrv_probe);
#endif
	pr_debug("error=%X\n",err);
	return ret;
}

static void __exit dev_exit( void ) {
	dma_release_rto();
	stop_timer_interrupts();
	disable_fiq(TMR3_IRQ);  
	release_fiq(&fh);

	platform_driver_unregister(&davinci_rto_driver);
	misc_deregister( &ppmsum_dev );
	pr_debug("v2r_ppmsum exit\n");
}
 
static int __init dev_init( void );
module_init( dev_init );

static void __exit dev_exit( void );
module_exit( dev_exit );

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR( "Dolin Sergey <dlinyj@gmail.com>" );
MODULE_VERSION( "0.1" );
