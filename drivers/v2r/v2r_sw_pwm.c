/*
 * Driver use TIMER3 general capabilities. Nothing else uses this timer.
 * THis driver is based upon event queue. When driver is programmed it exposed it queue to
 * interrupt process.
 * When the driver is reprogrammed first the driver queue is updated and then the result is copied
 * to interuupt queue
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/nmi.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <mach/mux.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <asm/io.h>
#include <asm/irq.h>


#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>


#define PPM_MIN 800
#define PPM_MAX 4400

#define DEBUG_MESSAGES 1//if 0 then no debugging info at all

#ifdef DEBUG_MESSAGES
#define dbg_print printk
#else
#define dbg_print
#endif

//Driver defines
#define MAX_PIN_NUMBER 88 //Number of connections to use on this platform
#define MAX_PWM_NUMBER 8 //Maximum PWM ports to use at this platform
#define GPIO_CONF_REGS_NUMBER 6//Number of GPIO configuration registers plus two
#define TIMER_FREQ 24000 //*1000Hz
#define DEFAULT_PERIOD 20 //ms
#define TRUE 1
#define FALSE 0


//Error codes
#define NA                    -1  //Something to do with string
#define SWPWM_OK               0  //Success
#define SWPWM_WRONGPIN         1  //Try to set wrong pin
#define SWPWM_WRONGVAL         2  //Try to set wrong value
#define SWPWM_BUSY             3  //Chosen PWN is currently in use
#define SWPWM_NO_RESOURCES     4  //All PWM resources are busy
#define SWPWM_UNKNOWN_ERROR    5  //Any other error

//Timer registers base address. Initially it is 0, but during initialization it gets its value
static volatile unsigned long* TMR3_BASE = 0x00000000;
static int TMR3_IRQ = 0x00000000;
//Structure desribing timer registers - just for reference here but may be useful
typedef struct {
	volatile unsigned long pid12;//offset 0x00
	volatile unsigned long emumgt;// offset 0x04
	volatile unsigned long res1;// offset 0x08
	volatile unsigned long res2;// offset 0x0c
	volatile unsigned long tim12;//offset 0x10
	volatile unsigned long tim34;// offset 0x14
	volatile unsigned long prd12;// offset 0x18
	volatile unsigned long prd34;// offset 0x1c
	volatile unsigned long tcr;//offset 0x20
	volatile unsigned long tgcr;// offset 0x24
	volatile unsigned long wdtcr;// offset 0x28
	volatile unsigned long res3;// offset 0x2c
	volatile unsigned long res4;//offset 0x30
	volatile unsigned long rel12;// offset 0x34
	volatile unsigned long rel34;// offset 0x38
	volatile unsigned long cap12;// offset 0x3c
	volatile unsigned long cap34;//offset 0x40
	volatile unsigned long intctl_stat;// offset 0x44
} TIMER_REGS;
//Definition of timer 3 structure pointer
#define TIMER_PTR ((volatile TIMER_REGS*)(TMR3_BASE))
/*Definition of timer 3 register values
For example we can access to to TIM12 like TIM12 |= DESIRED_BIT;
 */
#define PID12  (*((volatile unsigned long*)(TMR3_BASE+0x00)))
#define TIM12  (*((volatile unsigned long*)(TMR3_BASE+0x10)))
#define PRD12  (*((volatile unsigned long*)(TMR3_BASE+0x18)))
#define TCR    (*((volatile unsigned long*)(TMR3_BASE+0x20)))
#define TGCR   (*((volatile unsigned long*)(TMR3_BASE+0x24)))
#define REL12  (*((volatile unsigned long*)(TMR3_BASE+0x34)))
#define CAP12  (*((volatile unsigned long*)(TMR3_BASE+0x3C)))
#define INTCTL (*((volatile unsigned long*)(TMR3_BASE+0x44)))


//Timer control register bits
#define CAPEVTMODE34MASK  (3<<28)
#define CAPMODE34         (1<<27)
#define READRSTMODE34     (1<<26)
#define CLKSRC34          (1<<24)
#define ENAMODE34OFFSET   (3<<22)
#define MD34_DISABLED     (0)
#define MD34_ONCE         (1<<22)
#define MD34_CONT         (1<<23)
#define MD34_CONT_RELOAD  (3<<22)
#define CAPEVTMODE12MASK  (3<<12)
#define CAPMODE12         (1<<11)
#define READRSTMODE12     (1<<10)
#define CLKSRC12          (1<<8)
#define ENAMODE12OFFSET   (3<<6)
#define MD12_DISABLED     (0)
#define MD12_ONCE         (1<<6)
#define MD12_CONT         (1<<7)
#define MD12_CONT_RELOAD  (3<<6)

//Timer global control register bits
#define TDDR34MASK            (0x0f<<12)
#define PSC34MASK             (0x0f<<8)
#define BW_COMPATIBLE         (1<<4)
#define TIMMODEMASK           (3<<2)
#define TIMMOD_DUAL_UNCHAINED (4)
#define TIM34RS               (2)
#define TIM12RS               (1)


//Timer interrupt and status control register bits
#define SET34                 (1<<31)
#define EVAL34                (1<<30)
#define EVT_INT_STAT34        (1<<19)
#define EVT_INT_EN34          (1<<18)
#define CMP_INT_STAT34        (1<<17)
#define CMP_INT_EN34          (1<<16)
#define SET12                 (1<<15)
#define EVAL12                (1<<14)
#define EVT_INT_STAT12        (1<<3)
#define EVT_INT_EN12          (1<<2)
#define CMP_INT_STAT12        (2)
#define CMP_INT_EN12          (1)


/*
 * NOTE!!!! WE USE TIME 3 DIRECTLY WITHOUT LINUX HELPERS
 */

//This structure controls final PWM configuraton to use it directly in interrupt
typedef struct {
	const int number;//Ordinal number
	const int gpio;
	const char* name;//Name of gpio pin
	volatile unsigned long reg;//Register address
	unsigned long mask;//Bit mask to control gpio pin
} TASK;

//GPIO REGISTERS DEFINITIONS
#define GPIO_01 ((0x01C67000+0xfd000000+0x14))
#define GPIO_23 ((0x01C67000+0xfd000000+0x3C))
#define GPIO_45 ((0x01C67000+0xfd000000+0x64))
#define GPIO_6  ((0x01C67000+0xfd000000+0x8C))

//LIST OF POSSIBLE TASKS
static TASK tasks[] = {
    { 0, -1, "ZERO FAKE PIN",  	0x00000000, 0x0000000 },
    { 1, -1,  "GND",			0x00000000, 0x0000000 },
    { 2, -1,  "UART0_TXD", 		0x00000000, 0x0000000 },
    { 3, -1,  "UART0_RXD", 		0x00000000, 0x0000000 },
    { 4, -1,  "AGND", 			0x00000000, 0x0000000 },
    { 5, -1,  "+3V3", 			0x00000000, 0x0000000 },
    { 6, -1,  "ETHERNET1", 		0x00000000, 0x0000000 },
    { 7, -1,  "ETHERNET2", 		0x00000000, 0x0000000 },
    { 8, -1,  "ETHERNET3", 		0x00000000, 0x0000000 },
    { 9, -1,  "ETHERNET4", 		0x00000000, 0x0000000 },
    {10, -1,  "ETHERNET5", 		0x00000000, 0x0000000 },
    {11, -1,  "ETHERNET6", 		0x00000000, 0x0000000 },
    {12, -1,  "PWCTRO0", 		0x00000000, 0x0000000 },
    {13, -1,  "PWCTRO1", 		0x00000000, 0x0000000 },
    {14, -1,  "PWCTRO3", 		0x00000000, 0x0000000 },
    {15, 90,  "PWMCON15", 		   GPIO_45,   (1<<26) },
    {16, 89,  "PWMCON16",		   GPIO_45,   (1<<25) },
    {17, 88,  "PWMCON17", 		   GPIO_45,   (1<<24) },
    {18, 87,  "PWMCON18", 		   GPIO_45,   (1<<23) },
    {19, 50,  "PWMCON19", 		   GPIO_23,   (1<<18) },
    {20, -1,  "PWR_VIN", 		0x00000000, 0x0000000 },
    {21, -1,  "+3V3", 			0x00000000, 0x0000000 },
    {22, 67,  "PWMCON22", 		   GPIO_45,    (1<<3) },
    {23, -1,  "RESET", 			0x00000000, 0x0000000 },
    {24, -1,  "LINEOUT", 		0x00000000, 0x0000000 },
    {25, 51,  "PWMCON25", 		   GPIO_23,   (1<<19) },
    {26, 103, "PWMCON26", 		    GPIO_6,    (1<<7) },
    {27, 102, "PWMCON27", 		    GPIO_6,    (1<<6) },
    {28, 101, "PWMCON28", 		    GPIO_6,    (1<<5) },
    {29, 100, "PWMCON29", 		    GPIO_6,    (1<<4) },
    {30, 33,  "PWMCON30", 		   GPIO_23,    (1<<1) },
    {31, 32,  "PWMCON31", 		   GPIO_23,       (1) },
    {32, 31,  "PWMCON32", 		   GPIO_01,   (1<<31) },
    {33, 30,  "PWMCON33", 		   GPIO_01,   (1<<30) },
    {34, 29,  "PWMCON34", 		   GPIO_01,   (1<<29) },
    {35, 28,  "PWMCON35", 		   GPIO_01,   (1<<28) },
    {36, 27,  "PWMCON36", 		   GPIO_01,   (1<<27) },
    {37, 26,  "PWMCON37", 		   GPIO_01,   (1<<26) },
    {38, 25,  "PWMCON38", 		   GPIO_01,   (1<<25) },
    {39, 24,  "PWMCON39",		   GPIO_01,   (1<<24) },
    {40, 23,  "PWMCON40", 		   GPIO_01,   (1<<23) },
    {41, 22,  "PWMCON41", 		   GPIO_01,   (1<<22) },
    {42, 80,  "PWMCON42", 		   GPIO_45,   (1<<16) },
    {43, 92,  "PWMCON43", 		   GPIO_45,   (1<<28) },
    {44, 91,  "PWMCON44", 		   GPIO_45,   (1<<27) },
    {45, -1,  "TVOUT", 			0x00000000, 0x0000000 },
    {46, -1,  "SP+", 			0x00000000, 0x0000000 },
    {47, -1,  "SP-", 			0x00000000, 0x0000000 },
    {48, -1,  "ADC0", 			0x00000000, 0x0000000 },
    {49, -1,  "ADC1", 			0x00000000, 0x0000000 },
    {50, -1,  "ADC2", 			0x00000000, 0x0000000 },
    {51, -1,  "ADC3", 			0x00000000, 0x0000000 },
    {52, -1,  "ADC4", 			0x00000000, 0x0000000 },
    {53, -1,  "ADC5", 			0x00000000, 0x0000000 },
    {54, -1,  "PWCTRIO0", 		0x00000000, 0x0000000 },
    {55, -1,  "PWCTRIO1", 		0x00000000, 0x0000000 },
    {56, -1,  "PWCTRIO2", 		0x00000000, 0x0000000 },
    {57, -1,  "PWCTRIO3", 		0x00000000, 0x0000000 },
    {58, -1,  "PWCTRIO4", 		0x00000000, 0x0000000 },
    {59, -1,  "PWCTRIO5", 		0x00000000, 0x0000000 },
    {60, -1,  "PWCTRIO6", 		0x00000000, 0x0000000 },
    {61, 82,  "PWMCON61", 		   GPIO_45,   (1<<18) },
    {62, 79,  "PWMCON62", 		   GPIO_45,   (1<<15) },
    {63, 86,  "PWMCON63", 		   GPIO_45,   (1<<22) },
    {64, 85,  "PWMCON64", 		   GPIO_45,   (1<<21) },
    {65, 81,  "PWMCON65", 		   GPIO_45,   (1<<17) },
    {66, 34,  "PWMCON66", 		   GPIO_23,    (1<<2) },
    {67, -1,  "AGND", 			0x00000000, 0x0000000 },
    {68, -1,  "+3V3", 			0x00000000, 0x0000000 },
    {69, -1,  "PWR_VIN",		0x00000000, 0x0000000 },
    {70, -1,  "DSP_GND", 		0x00000000, 0x0000000 },
    {71, -1,  "GND", 			0x00000000, 0x0000000 },
    {72, -1,  "I2C_DATA", 		0x00000000, 0x0000000 },
    {73, -1,  "I2C_CLK", 		0x00000000, 0x0000000 },
    {74, -1,  "COMPPR", 		0x00000000, 0x0000000 },
    {75, -1,  "COMPY", 			0x00000000, 0x0000000 },
    {76, -1,  "COMPPB", 		0x00000000, 0x0000000 },
    {77, 49,  "PWMCON18", 		   GPIO_23,   (1<<17) },
    {78, 48,  "PWMCON78", 		   GPIO_23,   (1<<16) },
    {79, 47,  "PWMCON79", 		   GPIO_23,   (1<<15) },
    {80, 46,  "PWMCON80", 		   GPIO_23,   (1<<14) },
    {81, 45,  "PWMCON81", 		   GPIO_23,   (1<<13) },
    {82, 44,  "PWMCON82", 		   GPIO_23,   (1<<12) },
    {83, 35,  "PWMCON83", 		   GPIO_23,    (1<<3) },
    {84, 84,  "PWMCON84", 		   GPIO_45,   (1<<20) },
    {85, 83,  "PWMCON85", 		   GPIO_45,   (1<<19) },
    {86, -1,  "PWR_VIN", 		0x00000000, 0x0000000 },
    {87, 73,  "LED1", 			   GPIO_45,   (1<<9)  },//Just for test
    {88, 74,  "LED2", 			   GPIO_45,   (1<<10) }//Just for test
};

//Global interrupt index. Use to switch state machine during timer operation
static unsigned int gInterruptIndex = 0;

//Initial timer settings
static void init_timer_3(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	gInterruptIndex = 0;//Global interrupt index is reseted
	tptr->tim12  = 0;//SET COUNTER
    tptr->prd12  = 0xFFFFFFFF;//SET INITIAL PERIOD TO MAX VALUE
    tptr->rel12  = 0xFFFFFFFF;//SET INITIAL RELOAD PERIOD TO MAX VALUE
    tptr->tcr    = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE
    tptr->tgcr   = TIMMOD_DUAL_UNCHAINED|BW_COMPATIBLE;  //ENABLE ONLY FIRS HALF OF TIMER
    	           //TIM34RS | //SECOND HALF IS IN RESET
    	           //TIM12RS; //And the rest is in reset
    tptr->intctl_stat = 0;//ALL THE INTERRUPTS ARE DISABLED
}


//Forward declaration of function
static void update_int_queue(void);

//Start timer operation
static void start_timer_interrupts(void);

//Stops the timer operation
static void stop_timer_interrupts(void);

void set_timer3(unsigned long val){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	//Set next time time for timer to generate interrupt
	tptr->rel12 = val;
}
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
///Driver PWM period value
static unsigned int swpwm_period = DEFAULT_PERIOD;

//Global flags
static unsigned char g_UpdateFlag = FALSE; //<Interrupt handler should implement changes
static unsigned char g_IsActivePWM = FALSE; //<Stores current PWM active status.

///Structure element to store driver configuration
typedef struct {
	unsigned char pin;//pin number according numbering scheme. 0xff - is terminating value
	unsigned char duty;// duty value
} GPIO_PWM;

//Memory pool to store the driver configuration
static GPIO_PWM pwmpins_pool[MAX_PWM_NUMBER] = {{0xff, 0xff}};//Here is the main pool for gpio pins

//Sort data inside pwmpins_pool
static void sort_pwm_pins(void){//Rather stupid method but effective when MAX_PWM_NUMBER is small
	int i = 0;
	int j = 0;
	//Using bubble is not effective but for few values it is the best way
	for (i  = 0; i < MAX_PWM_NUMBER; i++){
		for (j = 0; j < MAX_PWM_NUMBER - 1; j++){
			if (pwmpins_pool[j].duty > pwmpins_pool[j+1].duty){
				//Swap values
				GPIO_PWM tmp;
				tmp = pwmpins_pool[j+1];
				pwmpins_pool[j+1] = pwmpins_pool[j];
				pwmpins_pool[j] = tmp;
			}
		}
	}
}

//Find element with <pin> parameter in the pool
static GPIO_PWM* find_pwmpin(const unsigned char pin){
	int i = 0;
	for (i = 0; i < MAX_PWM_NUMBER; i++){
		if (pwmpins_pool[i].pin == pin) return &pwmpins_pool[i];
	}
	return 0;
}


///Allocates GPIO PIN within pool and set its value. Returns SWPWM_OK if ok, and error code if fail
int alloc_gpio(const unsigned char pin, const unsigned char duty){
	int i = 0;
	if (pin > MAX_PIN_NUMBER){
		dbg_print("alloc_gpio con number %d is greater then %d\r\n", pin, MAX_PIN_NUMBER);
		return -SWPWM_WRONGPIN;//we cannot alloc terminating pin
	}
	if (tasks[pin].reg == 0x00000000){
		dbg_print("alloc_gpio con number %d could not be modified\r\n", pin);
		return -SWPWM_WRONGPIN;
	}
	for (i = 0; i < MAX_PWM_NUMBER; i++){//if we find that this pin is allocated - use ready pin
		if (pwmpins_pool[i].pin == pin){
			pwmpins_pool[i].duty = duty;
			sort_pwm_pins();
			dbg_print("alloc_gpio con successfully reaasigned old pin %d\r\n", pin);
			return SWPWM_OK;//return good assinged pin
		}
	}
    //if there is no assigned pin - try to allocate new one
	if (gpio_request(tasks[pin].gpio, tasks[pin].name) != 0){
		dbg_print("alloc_gpio gpio_request failed %d\r\n", pin);
		return -SWPWM_BUSY;//If cannot be requested, that means that someone else is using this pin
	}
	gpio_direction_output(tasks[pin].gpio, 0);
	for (i = 0; i < MAX_PWM_NUMBER; i++){//if we find free unallocated pin - just use it
		if (pwmpins_pool[i].pin == 0xff){
			pwmpins_pool[i].pin = pin;
			pwmpins_pool[i].duty = duty;
			sort_pwm_pins();
			dbg_print("alloc_gpio con successfully assigned new pin %d\r\n", pin);
			return SWPWM_OK;//return good assinged pin
		}
	}
	return SWPWM_UNKNOWN_ERROR;
}

//Frees gpio from using it as PWM
void free_gpio(const unsigned char pin){
	GPIO_PWM* pwm = find_pwmpin(pin);
	if (!pwm) return;
	pwm->pin = 0xff;
	pwm->duty = 0xff;
	gpio_direction_input(tasks[pin].gpio);
	gpio_free(tasks[pin].gpio);
	sort_pwm_pins();
};

//Register elements
typedef struct {
	unsigned long reg;
	unsigned long val;
	unsigned char set;
} REG_VAL;

//Time element to build driver timer schedule
typedef struct {
	unsigned int  tm_size;
	unsigned long time[MAX_PWM_NUMBER+1];
} TIME_VAL;


//PWM configuration in driver space
//This array stores times to program times switches during its operation
static TIME_VAL drv_timing;
/*This array stores register values to program gpio during pwm operation
*/
static REG_VAL drv_queue[MAX_PWM_NUMBER+1][GPIO_CONF_REGS_NUMBER+1];

/*
Initially within the driver we program drv_timing and drv_queue and set g_UpdateFlag to true.
Then on finishing pwm operatin period interrupt handler if g_UpdateFlag is set reloads drv_... parameters to int_... parameter and then
interrupt handler proceeds to go
*/

//PWM configuration in interrupt space
//This array stores times to program times switches during its operation
static TIME_VAL int_timing;
//This array stores register values to program gpio during pwm operation
static REG_VAL int_queue[MAX_PWM_NUMBER+1][GPIO_CONF_REGS_NUMBER+1];

//
static void start_timer_interrupts(void){
	int i = 0;
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	dbg_print("Start timer interrupts %x\r\n", (unsigned int)TMR3_BASE);
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	if (drv_timing.tm_size < 2) return;//This means that some error occurs
	gInterruptIndex = 0;
	update_int_queue();
	set_timer3(int_timing.time[gInterruptIndex]);
	tptr->tim12 = 0;
	tptr->prd12 = 1000;//once we enable interrupts it really occurs at the same moment
	tptr->intctl_stat = 0;//Interrupts are disabled
	tptr->tcr = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE - ENABLE TIMER
	tptr->tgcr = TIMMOD_DUAL_UNCHAINED|BW_COMPATIBLE;
	tptr->tgcr |= (TIM12RS);//out from reset
	tptr->intctl_stat = CMP_INT_EN12;//Interrupts are enabled
	tptr->tcr = MD12_CONT_RELOAD;//MD12_CONT;//SET CONTINIOUS MODE - ENABLE TIMER
	//once the timer starts it generates interrupt so we load correct prd value and so on
}
static void stop_timer_interrupts(void){
	volatile TIMER_REGS* tptr = (volatile TIMER_REGS*)TMR3_BASE;
	REG_VAL* regs = drv_queue[0];
	int i = 0;
	dbg_print("Stop timer interrupts %x\r\n", (unsigned int)TMR3_BASE);
	if (!TMR3_BASE) return;//if timer3 base value not set we cant do any
	tptr->intctl_stat = 0;//Disable interrupts
	tptr->tcr = 0;//DISABLE TIMER
	tptr->tgcr &= ~(TIM12RS);//Put it into reset
	tptr->tim12 = 0;
	tptr->prd12 = 0xFFFFFFFF;
	gInterruptIndex = 0;
	//Setting GPIO pins configuration
	while (regs[i].reg != 0){//Set registers for gpio up and down
		volatile unsigned long* addr = (volatile unsigned long*)(regs[i].reg);
		if (regs[i].set){
			*addr |= regs[i].val;
			//dbg_print("set %x, %x\r\n", (int)addr, (int)(regs[i].val) );
		} else {
			*addr &= ~(regs[i].val);
			//dbg_print("reset %x, %x\r\n", (int)addr, (int)(regs[i].val) );
		}
		i++;
	}
    //Now timer is in reset state and in its initial state
}

//Update interrupt queue if driver queue was changed
static void update_int_queue(void){
	int i = 0, j = 0;
	//Copy from driver parameters to interrupt parameters
	memcpy(&int_timing, &drv_timing, sizeof(TIME_VAL));
	//May be some special cases when allocating memory for arrays so use stupid cycle to avoid problems
	for (i = 0; i < MAX_PWM_NUMBER+1; i++){
		for (j = 0; j < GPIO_CONF_REGS_NUMBER+1; j++){
			int_queue[i][j] = drv_queue[i][j];
		}
	}
}
//Logical interrupt handler
static void interrupt_proc(void){
	int i = 0;
	//Processing the queue
	REG_VAL* regs = int_queue[gInterruptIndex];
	//Setting GPIO pins configuration
	while (regs[i].reg != 0){//Set registers for gpio up and down
		volatile unsigned long* addr = (volatile unsigned long*)(regs[i].reg);
		if (regs[i].set){
			*addr |= regs[i].val;
			//dbg_print("set %x, %x\r\n", (int)addr, (int)(regs[i].val) );
		} else {
			*addr &= ~(regs[i].val);
			//dbg_print("reset %x, %x\r\n", (int)addr, (int)(regs[i].val) );
		}
		i++;
	}
	gInterruptIndex++;
	if ( gInterruptIndex == (int_timing.tm_size) ){//If the queue is over - tryt update the queue
		if (g_UpdateFlag){//If there are new data - just reload it into int_queue
			dbg_print("Update int queue\r\n");
			update_int_queue();
			g_UpdateFlag = FALSE;
		}
		gInterruptIndex = 0;
	}
	//Set next timer moment
	set_timer3(int_timing.time[gInterruptIndex]);
	//increment interrupt counter
	//Wow!!! Interrupt is handled
}
//Formal interrupt handler
static irqreturn_t timer3_interrupt(int irq, void *dev_id){
	int handled = 0;
	if (irq_timer3()){//Our interrupt must be processed
		//dbg_print("--->>>\r\n");
		interrupt_proc();
		handled = IRQ_HANDLED;
	} else {
		handled = IRQ_NONE;
	}
	return IRQ_RETVAL(handled);
}
//Method to say Linux that we need this interrupt
static int timer3_irq_chain(unsigned int irq){
	int ret = 0;
	ret = request_irq(irq, timer3_interrupt, 0, "timer3", 0);
	dbg_print("Chain interrupt returned %x\r\n", ret);
	return ret;
}
//Zeroes driver table to fill it with values later
static void reset_drv_table(void){
	int i = 0, j = 0;
	drv_timing.tm_size = 0;
	for (i = 0; i < MAX_PWM_NUMBER+1; i++){
		drv_timing.time[i] = 0xffffffff;
		for (j = 0; j < GPIO_CONF_REGS_NUMBER+1; j++) { drv_queue[i][j].reg = 0x00000000; drv_queue[i][j].val = 0x00000000; drv_queue[i][j].set = 0;}
	}
}
//Provides task into queue to set GPIO pin
static void gpio_set_task(REG_VAL* regs, const unsigned char pin){
	int i = 0;
	while(regs[i].reg != 0x0000000){//Remove all reset tasks
		if ( (regs[i].set == 0) && (regs[i].reg == tasks[pin].reg) ){//Find reset register
			regs[i].val &= ~(tasks[pin].mask);//Bit is removed
			break;
		}
		i++;
	}
	i = 0;
	while(regs[i].reg != 0x0000000){//Set bit in set task
		if (regs[i].set && (regs[i].reg == tasks[pin].reg) ){//if find? just add bit
			regs[i].val |= tasks[pin].mask;
			return;
		}
		i++;
	}
	//if not find - use new register
	regs[i].reg = tasks[pin].reg;
	regs[i].val = tasks[pin].mask;
	regs[i].set = TRUE;
}
//Provides task into queue to reset GPIO pin
static void gpio_reset_task(REG_VAL* regs, const unsigned char pin){
	int i = 0;
	while(regs[i].reg != 0x0000000){//Remove all reset tasks
		if ( (regs[i].set != 0) && (regs[i].reg == tasks[pin].reg) ){//Find set register
			regs[i].val &= ~(tasks[pin].mask);//Bit is removed
			break;
		}
		i++;
	}
	i = 0;
	while(regs[i].reg != 0x0000000){
		if ( (regs[i].set == 0) && (regs[i].reg == tasks[pin].reg) ){
			regs[i].val |= tasks[pin].mask;
			return;
		}
		i++;
	}
	regs[i].reg = tasks[pin].reg;
	regs[i].val = tasks[pin].mask;
	regs[i].set = FALSE;
}

//General method to recalculate driver operating tables
static int process_update(void){
	int reg_size = 0;
	unsigned char is_active_pwm = FALSE;
	unsigned char is_interrupted = FALSE;
	int i  = 0;
	reset_drv_table();
	drv_timing.tm_size = 0;
	//Here we have sorted map of GPIO pins;
	//Lets build driver table;
	//Building task tables;
	//Initial Element
	for (i = 0; i < MAX_PWM_NUMBER; i++){
		if (pwmpins_pool[i].duty !=0 && pwmpins_pool[i].duty != 0xff && pwmpins_pool[i].pin != 0xff){
			is_active_pwm = TRUE;
		}
		if (pwmpins_pool[i].duty == 0){
		    if (pwmpins_pool[i].pin != 0xFF) gpio_reset_task(drv_queue[0], pwmpins_pool[i].pin);
		} else {
		    if (pwmpins_pool[i].pin != 0xFF) gpio_set_task(drv_queue[0], pwmpins_pool[i].pin);
		}
		dbg_print("pool[%d]={%x,%x}\r\n", i, pwmpins_pool[i].pin, pwmpins_pool[i].duty);
	}
	reg_size = 0;
	//the rest of elements
	for (i = 0; i < MAX_PWM_NUMBER - 1; i++){
		if (pwmpins_pool[i].pin != 0xFF) gpio_reset_task(drv_queue[drv_timing.tm_size + 1], pwmpins_pool[i].pin);
		if (pwmpins_pool[i].duty != pwmpins_pool[i + 1].duty){
			//drv_timing.time[drv_timing.tm_size++] = (pwmpins_pool[i].duty*TIMER_FREQ*swpwm_period)/255;
			drv_timing.time[drv_timing.tm_size++] = TIMER_FREQ*(PPM_MIN/1000) + (TIMER_FREQ*((PPM_MAX - PPM_MIN)/1000)*pwmpins_pool[i].duty)/255;
			reg_size = 0;
			if (pwmpins_pool[i + 1].duty == 0xff){ i++;  is_interrupted = TRUE; break;}
		}
	}
	if (pwmpins_pool[i].pin != 0xFF){
		gpio_reset_task(drv_queue[drv_timing.tm_size + 1], pwmpins_pool[i].pin);
		if (!is_interrupted) {
		    //drv_timing.time[drv_timing.tm_size++] = (pwmpins_pool[i].duty*TIMER_FREQ*swpwm_period)/255;
		    //drv_timing.time[drv_timing.tm_size++] = (unsigned long) TIMER_FREQ*PPM_MIN + (TIMER_FREQ*(PPM_MAX - PPM_MIN)*pwmpins_pool[i].duty)/255;
		    drv_timing.time[drv_timing.tm_size++] = TIMER_FREQ*(PPM_MIN/1000) + (TIMER_FREQ*((PPM_MAX - PPM_MIN)/1000)*pwmpins_pool[i].duty)/255;

		}
	}
	drv_timing.time[drv_timing.tm_size++] = TIMER_FREQ*swpwm_period;
	//Here we have drv_timing and drv_queue valid values and these values may be copied
	//One more pass to get deltas of time
	for (i = drv_timing.tm_size - 1; i >= 1; i--){
		drv_timing.time[i] = drv_timing.time[i] - drv_timing.time[i-1];
	}
	g_UpdateFlag = TRUE;
	for (i = 0; i < drv_timing.tm_size; i++){
		int j = 0;
		REG_VAL* regs = drv_queue[i];
		//dbg_print("-->> drv_timing[%d]=%.8d----------------------------------------\r\n", i, drv_timing.time[i]);
		while(regs[j].reg != 0){
			//dbg_print("    %s reg=%x val=%x\r\n", regs[j].set ? "set" : "reset", regs[j].reg, regs[j].val);
			j++;
		}
		//dbg_print("-----------------------------------------------------------------\r\n");
	}
	if (is_active_pwm && !g_IsActivePWM){//There were no interrupts and now we get it
		g_IsActivePWM = is_active_pwm;
		start_timer_interrupts();
	}
	if (g_IsActivePWM && !is_active_pwm){//There were interrupts but after update we do not need it any more
		g_IsActivePWM = is_active_pwm;
		stop_timer_interrupts();
	}
	if (!g_IsActivePWM && !is_active_pwm){//Stub to initialize GPIO when interrupts are not enabled
		stop_timer_interrupts();
	}
	return SWPWM_OK;
}

/**
This method sets gpio to operate as pwm
@param gpio - sets pin number according to numbering scheme
@param duty - value from 0 to 255. 0 - output is always 0, 255 - output is always 1, 1-254 - intermediate values
*/
static int start_pwm(const unsigned char gpio, const unsigned char duty){
	int error = 0;
	dbg_print("start_pwm_func con=%d duty=%d\r\n", gpio, duty);
	if ( (error = alloc_gpio(gpio, duty)) != SWPWM_OK ){
		dbg_print("Alloc gpio error %d\r\n", error);
		return -error;
	}
	dbg_print("Start_PWM: Before process update\r\n");
	process_update();
	dbg_print("Start_PWM: After process update\r\n");
	return 0;
}

/**
This method disables gpio to operate as pwm
@param gpio - sets pin number according to numbering scheme
GPIO is configured as input and all resources, allocated with the device are freed
*/
static int stop_pwm(const unsigned char gpio){
	free_gpio(gpio);
	process_update();
	return 0;
}

/**
This method tunes SOFT PWM period
@param period - from 1 to 10000
*/
static int set_period(const unsigned int period){
	swpwm_period = period;
	process_update();
	return 0;
}

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
         dbg_print("Chunk %d = %s\r\n", nIndex, result[nIndex]);
    }
    return result;   
};
static int is_equal_string(const char* str1, const char* str2){
    int len = strlen(str1);
    int i = 0;
    if (len != strlen(str2)){ dbg_print("IS EQUAL STRING LEN ERROR\r\n"); return FALSE;}
    for (i = 0; i < len; i++){
       if (str1[i] != str2[i]) { dbg_print("IS EQUAL STRING COMPARE ERROR %s %s %d\r\n", str1, str2, i); return FALSE;}
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
    //dbg_print("str to int returned %d\r\n", result);
    return result;
}

static int starts_with(const char* str1, const char* ref){
    int i = 0;
    int len = strlen(str1);
    const char* substr = 0;
    if (len <= strlen(ref)) {dbg_print("starts with len error\r\n"); return NA;}
    len = strlen(ref);
    for (i = 0; i < len; i++){
       if (str1[i] != ref[i]) {dbg_print("starts with not equal error %d\r\n", i); return NA;}
    }
    //Here it a good candidate for perfect result. Lets get appropriate value
    substr = &str1[len];
    dbg_print("Really starts with. Substr %s\r\n", substr);
    //if (is_equal_string(substr, "true")) return 1;
    //if (is_equal_string(substr, "false")) return 0;
    return str_to_int(substr);    
}

int process_command(char** data){
	if (!data[0]) return 0;
	if (is_equal_string(data[0], "start")){
		int gpio = 0;
		int duty = 0;
		if (!data[1] || !data[2]) return 0;
		gpio = starts_with(data[1], "con:");
		duty = starts_with(data[2], "duty:");
		if (gpio == NA || duty == NA) return 0;
		dbg_print("Starts con %d, duty %d\r\n", gpio, duty);
		start_pwm(gpio, duty);
	} else if (is_equal_string(data[0], "stop")){
		int gpio = 0;
		if (!data[1] ) return 0;
		gpio = starts_with(data[1], "con:");
		if (gpio == NA ) return 0;
		dbg_print("Stops con %d\r\n", gpio);
		stop_pwm(gpio);
	} else if (is_equal_string(data[0], "period")){
		int period = 0;
		if (!data[1] ) return 0;
		period = starts_with(data[1], "");
		if (period == NA ) return 0;
		dbg_print("Period %d\r\n", period);
		set_period(period);
	}
	return 0;
}



MODULE_AUTHOR("Alexander V. Shadrin");
MODULE_LICENSE("GPL");
/**
This driver implements software pwm function
	period <VAL> - set pwm period in ms
	start con:<PIN> duty:<DUTY> - set pwm at <PIN> with <DUTY>
	stop <PIN> - stops pwm at <PIN>
	config - programs driver to spit a pwm config via read method - not implemented yet
All inputs and outputs share the same data buffer
*/
#define V2RSWPWM_NDEVICES 1 
#define V2RSWPWM_BUFFER_SIZE 8192
#define V2RSWPWM_DEVICE_NAME "v2r_sw_pwm"

/** The structure to represent 'v2rswpwm' devices. 
	data - data buffer;
	buffer_size - size of the data buffer;
	buffer_data_size - amount of data actually present in the buffer
	v2rswpwm_mutex - a mutex to protect the fields of this structure;
	cdev - character device structure.
*/
struct v2rswpwm_dev {
 	unsigned char *data;
 	unsigned long buffer_size; 
 	unsigned long buffer_data_size;  
 	struct mutex v2rswpwm_mutex; 
 	struct cdev cdev;
};
//driver variables
static unsigned int v2rswpwm_major = 0;
static struct v2rswpwm_dev *v2rswpwm_devices = NULL;
static struct class *v2rswpwm_class = NULL;
static int v2rswpwm_ndevices = V2RSWPWM_NDEVICES;

/** Opens a driver for subsequent operations
*/
int v2rswpwm_open(struct inode *inode, struct file *filp){
	//Major and minor numbers are defined by the driver itself during its initialization
	unsigned int mj = imajor(inode);//get major number
	unsigned int mn = iminor(inode);//get minor number
	struct v2rswpwm_dev *dev = NULL;
	dbg_print("V2RSWPWM open device\r\n");
	if (mj != v2rswpwm_major || mn < 0 || mn >= 1){//Only one device may exist
		dbg_print("No device found with minor=%d and major=%d\n", mj, mn);
		return -ENODEV; /* No such device */
	}
	/* store a pointer to struct v2rswpwm_dev here for other methods */
	dev = &v2rswpwm_devices[mn];
	filp->private_data = dev; //We use private data field in file descriptor to store currents driver information between its calls
	if (inode->i_cdev != &dev->cdev){//This may occur due to linux problem
		dbg_print("v2rswpwm open: internal error\n");
		return -ENODEV; /* No such device */
	}
	/* if opened the 1st time, allocate the buffer */
	if (dev->data == NULL){
		dev->data = (unsigned char*)kzalloc(V2RSWPWM_BUFFER_SIZE, GFP_KERNEL);//First time we have
		if (dev->data == NULL){//no way to allocate memory
			dbg_print("v2rswpwm open(): out of memory\n");
			return -ENOMEM;
		}
		dev->buffer_data_size = 0;//setting initial buffer position
	}
	dbg_print("V2RSWPWM device is opened successfully\r\n");
	return 0;//Everything is OK
}
/** Closes driver 
*/
int v2rswpwm_release(struct inode *inode, struct file *filp){
	//One time when driver has allocated its memory, this memory will never be released, however its not a leak because it is a singleton
	dbg_print("V2RSWPWM release device\r\n");
	return 0;
}
/** Reads data from user console (user input)
*/
ssize_t v2rswpwm_read(struct file *filp, char __user *buf, size_t count,	loff_t *f_pos){
	struct v2rswpwm_dev *dev = (struct v2rswpwm_dev *)filp->private_data;
	ssize_t retval = 0;
	dbg_print("v2rswpwm device read\r\n");
	if (mutex_lock_killable(&dev->v2rswpwm_mutex)) return -EINTR;//Mutex is required to avoid simultanious reads or writes from different processes
	if (*f_pos >= dev->buffer_data_size) goto out;/* EOF */
	if (*f_pos + count > dev->buffer_data_size)	count = dev->buffer_data_size - *f_pos;//Reads the rest for buffer 
	if (copy_to_user(buf, &(dev->data[*f_pos]), count) != 0){//Real copy from user space to system space
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	dbg_print("v2rswpwm copied %d bytes to user\r\n", count);
	retval = count;//Really read data size to return
out:
  	mutex_unlock(&dev->v2rswpwm_mutex);//Free mutex
	return retval;
}
/** Writes data to user console. Here we start main processing
*/
ssize_t v2rswpwm_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
	struct v2rswpwm_dev *dev = (struct v2rswpwm_dev *)filp->private_data;
	ssize_t retval = 0;
	char *tmp = 0;
	int nIndex = 0;
	char** data = 0;
	dbg_print("v2rswpwm write\r\n");
	if (mutex_lock_killable(&dev->v2rswpwm_mutex)) return -EINTR;//Locking the mutex to avoid simultanios accesses from different processes
	if (*f_pos !=0) {
    /* Writing in the middle of the file is not allowed */
		dbg_print("Writing in the middle (%d) of the file buffer is not allowed\r\n", (int)(*f_pos));
		retval = -EINVAL;
        goto out;
    }
	if (count > V2RSWPWM_BUFFER_SIZE) count = V2RSWPWM_BUFFER_SIZE;//If we write a lot of data we have to cut the input
	tmp = kmalloc(count+1,GFP_KERNEL);
	if (tmp==NULL)	return -ENOMEM;
	if (copy_from_user(tmp,buf,count)){
		kfree(tmp);
		retval = -EFAULT;
		goto out;
	}
	tmp[count] = 0;//This is a string, that contains driver input buffer
	dev->buffer_data_size = 0;
	//tmp contains string to parse
	data = split(tmp, ' ');
	process_command(data);
	memcpy(dev->data, "OK", 3);//Ufff that is really bad :(
	dev->buffer_data_size = 3;//%(
	nIndex = 0;
	while(data[nIndex]){//Release allocated memory
		kfree(data[nIndex]);
		nIndex++;
	}
	kfree(data);
	dbg_print("v2rswpwm-dev message %s\r\n", tmp);
	kfree(tmp);
	*f_pos = 0;//Set file pointer to zero
	retval = count;
out:
  	mutex_unlock(&dev->v2rswpwm_mutex);//Unlocking the mutex
	return retval;
}
///Standard file operation structure
struct file_operations v2rswpwm_fops = {
	.owner =    THIS_MODULE,
	.read =     v2rswpwm_read,
	.write =    v2rswpwm_write,
	.open =     v2rswpwm_open,
	.release =  v2rswpwm_release,
};
///Main method to construct the driver
static int v2rswpwm_construct_device(struct v2rswpwm_dev *dev, int minor, struct class *class){
	int err = 0;
	dev_t devno = MKDEV(v2rswpwm_major, minor);//Get major and minor numbers from linux kernel
	struct device *device = NULL;//Creating the device structure
	BUG_ON(dev == NULL || class == NULL);
	/* Memory is to be allocated when the device is opened the first time */
	dbg_print("v2rswpwm construct device:%d\r\n", minor);
	//Setting initial values
	dev->data = NULL;     
	dev->buffer_size = V2RSWPWM_BUFFER_SIZE;
	mutex_init(&dev->v2rswpwm_mutex);
	cdev_init(&dev->cdev, &v2rswpwm_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);//Adding the device to the system
	if (err){
		dbg_print("V2RSWPWM Error %d while trying to add %s%d",	err, V2RSWPWM_DEVICE_NAME, minor);
       	return err;
	}
	//Creating the device class and registering it into FS
	device = device_create(class, NULL/*no parent device*/,  devno, NULL/*no additional data */,V2RSWPWM_DEVICE_NAME);
	if (IS_ERR(device)) {
		err = PTR_ERR(device);
		dbg_print("Error %d while trying to create %s%d", err, V2RSWPWM_DEVICE_NAME, minor);
        cdev_del(&dev->cdev);
        return err;
    }
	dbg_print("v2rswpwm device is constructed successfully\r\n");
	return 0;
}
///Destroying the device
static void v2rswpwm_destroy_device(struct v2rswpwm_dev *dev, int minor, struct class *class){
	BUG_ON(dev == NULL || class == NULL);
	dbg_print("v2rswpwm destroy device: %d\r\n", minor);
	device_destroy(class, MKDEV(v2rswpwm_major, minor));
	cdev_del(&dev->cdev);
	kfree(dev->data);
	dbg_print("v2rswpwm device is destroyed successfully\r\n");
	return;
}
///This method is called during driver release (seems that it never happen)
static void v2rswpwm_cleanup_module(int devices_to_destroy){
	int i = 0;
	/* Get rid of character devices (if any exist) */
	dbg_print("v2rswpwm pins cleanup module\r\n");
	if (v2rswpwm_devices) {
		for (i = 0; i < devices_to_destroy; ++i) {
			v2rswpwm_destroy_device(&v2rswpwm_devices[i], i, v2rswpwm_class);
        }
		kfree(v2rswpwm_devices);
	}
	if (v2rswpwm_class) class_destroy(v2rswpwm_class);
	unregister_chrdev_region(MKDEV(v2rswpwm_major, 0), v2rswpwm_ndevices);
	dbg_print("v2rswpwm pins cleanup completed\r\n");
	return;

}
static struct platform_driver davinci_swpwm_driver = {
	.driver		= {
		.name	= "davinci_swpwm_driver",
	},
};
static int __init swpwmdrv_probe(struct platform_device *pdev){
	struct resource			*res;
	struct resource			*res_irq;
	void __iomem			*vaddr;
	volatile unsigned char* memptr = 0;
	struct clk* clock = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	clock = clk_get(&pdev->dev, "timer3");//Enable clock for timer3
	clk_enable(clock);
	dbg_print("TIMER3 res: %x, %x\r\n", res->start, res->end);
	vaddr = ioremap(res->start, res->end - res->start);
	memptr = (volatile unsigned char*)vaddr;
	dbg_print("TIMER3 remap address: %x\r\n", (unsigned int)vaddr);
	TMR3_BASE = (volatile unsigned long*)vaddr;
	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	dbg_print("TIMER3 IRQ: %x\r\n", (unsigned int)res_irq->start);
	TMR3_IRQ = res_irq->start;
	init_timer_3();
	dbg_print("Timer 3 is inited\r\n");
	timer3_irq_chain(TMR3_IRQ);
	dbg_print("Timer interrupts %d is chained\r\n", TMR3_IRQ);
	memset(pwmpins_pool, 0xff, sizeof(pwmpins_pool));
	return 0;
}

///Initial initialization of the driver during system startup
static int __init v2rswpwm_init_module(void){
	int err = 0;
	int i = 0;
	int devices_to_destroy = 0;
	dev_t dev = 0;
	dbg_print("v2rswpwm module init\r\n");
	err = platform_driver_probe(&davinci_swpwm_driver, swpwmdrv_probe);
	if (v2rswpwm_ndevices <= 0){
		dbg_print("V2RSWPWM Invalid value of v2rswpwm_ndevices: %d\n", v2rswpwm_ndevices);
		return -EINVAL;
	}
	/* Get a range of minor numbers (starting with 0) to work with */
	err = alloc_chrdev_region(&dev, 0, v2rswpwm_ndevices, V2RSWPWM_DEVICE_NAME);
	if (err < 0) {
		dbg_print("V2RSWPWM alloc_chrdev_region() failed\n");
		return err;
	}
	v2rswpwm_major = MAJOR(dev);
	dbg_print("v2rswpwm device major: %d\r\n", v2rswpwm_major);
	/* Create device class (before allocation of the array of devices) */
	v2rswpwm_class = class_create(THIS_MODULE, V2RSWPWM_DEVICE_NAME);
	if (IS_ERR(v2rswpwm_class)){
		err = PTR_ERR(v2rswpwm_class);
		dbg_print("v2rswpwm class not created %d\r\n", err);
		goto fail;
    }
	/* Allocate the array of devices */
	v2rswpwm_devices = (struct v2rswpwm_dev *)kzalloc( v2rswpwm_ndevices * sizeof(struct v2rswpwm_dev), GFP_KERNEL);
	if (v2rswpwm_devices == NULL) {
		err = -ENOMEM;
		dbg_print("v2rswpwm devices not allocated %d\r\n", err);
		goto fail;
	}
	/* Construct devices */
	for (i = 0; i < v2rswpwm_ndevices; ++i) {
		err = v2rswpwm_construct_device(&v2rswpwm_devices[i], i, v2rswpwm_class);
		if (err) {
			dbg_print("v2rswpwm device is not constructed\r\n");
			devices_to_destroy = i;
			goto fail;
        }
	}
	return 0; /* success */
fail:
	v2rswpwm_cleanup_module(devices_to_destroy);
	return err;
}

static void __exit v2rswpwm_exit_module(void){
	dbg_print("v2rswpwm module exit\r\n");
	v2rswpwm_cleanup_module(v2rswpwm_ndevices);
	return;
}

module_init(v2rswpwm_init_module);
module_exit(v2rswpwm_exit_module);

 

