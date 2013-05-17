#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <mach/mux.h>
#include <mach/hardware.h>
#include <mach/gpio.h>

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


#define TOTAL_PINS 88
#define TOTAL_PWM  4

#define PWM0BASE 0x01C22000
#define PWM1BASE 0x01C22400
#define PWM2BASE 0x01C22800
#define PWM3BASE 0x01C22C00

//PWM FUNCTIONALITY
typedef struct {
    volatile unsigned int pid;
    volatile unsigned int pcr;
    volatile unsigned int cfg;
    volatile unsigned int start;
    volatile unsigned int repeat;
    volatile unsigned int period;
    volatile unsigned int ph1d;
} PWM;
static volatile PWM* pwm[4] = {
    (volatile PWM*)IO_ADDRESS(0x01C22000),
    (volatile PWM*)IO_ADDRESS(0x01C22400),
    (volatile PWM*)IO_ADDRESS(0x01C22800),
    (volatile PWM*)IO_ADDRESS(0x01C22C00)
};
static void v2rinit_pwm(void){
     int i = 0;
     struct clk* clk_pwm0;
     struct clk* clk_pwm1;
     struct clk* clk_pwm2;
     struct clk* clk_pwm3;
     printk("INIT V2Rpwm functions:\r\n");
     clk_pwm0 = clk_get(NULL, "pwm0"); 
     clk_enable(clk_pwm0);
     clk_pwm1 = clk_get(NULL, "pwm1"); 
     clk_enable(clk_pwm1);
     clk_pwm2 = clk_get(NULL, "pwm2"); 
     clk_enable(clk_pwm2);
     clk_pwm3 = clk_get(NULL, "pwm3"); 
     clk_enable(clk_pwm3);

     // added by Gol
     
     printk("- CLK_PWM0 = %ld\n", clk_get_rate(clk_pwm0));
     printk("- CLK_PWM1 = %ld\n", clk_get_rate(clk_pwm1));
     printk("- CLK_PWM2 = %ld\n", clk_get_rate(clk_pwm2));
     printk("- CLK_PWM3 = %ld\n", clk_get_rate(clk_pwm3));
     
     // end added by Gol
     
     for (i = 0; i < TOTAL_PWM; i++){
	//pwm[i].pid
	pwm[i]->pcr = 0x1;
        pwm[i]->cfg = 0x12;
        pwm[i]->start = 1;
        pwm[i]->repeat = 0x0;
        pwm[i]->period = 0;
        pwm[i]->ph1d = 0x0;
     }
};
static void v2rstart_pwm(unsigned int number, long duty, long period){
     if (number >= TOTAL_PWM){
          //printk("Error in setting pwm number\r\n");
          return;
     }
     //if (duty > 0xFF) duty = 0xFF*3; // commented by Gol
     //pwm[number]->pcr   = 0x01;
     //pwm[number]->cfg   = 0x12;//Continious mode

     pwm[number]->period  = period; // added by Gol
     pwm[number]->ph1d  = duty;
     //pwm[number]->start = 1;
     //printk("Starting pwm %d, duty %ld, period %ld\r\n", number, duty, period);
    
};
static void v2rstop_pwm(int number){
     //printk("Stopping pwm %d\r\n", number);
     if (number >= TOTAL_PWM){
          //printk("Error in setting pwm number\r\n");
          return;
     }
     pwm[number]->pcr   = 0x00;
     pwm[number]->cfg   = 0x00;//Disable mode
};

//PRTCSS FUNTIONALITY
#define PRTCSS_BASE 0x01C69000
/* PRTC interface registers */
#define DAVINCI_PRTCIF_PID			0x00
#define DAVINCI_PRTCIF_CTLR			0x04
#define DAVINCI_PRTCIF_LDATA			0x08
#define DAVINCI_PRTCIF_UDATA			0x0C
#define DAVINCI_PRTCIF_INTEN			0x10
#define DAVINCI_PRTCIF_INTFLG			0x14

/* DAVINCI_PRTCIF_CTLR bit fields */
#define DAVINCI_PRTCIF_CTLR_BUSY		BIT(31)
#define DAVINCI_PRTCIF_CTLR_SIZE		BIT(25)
#define DAVINCI_PRTCIF_CTLR_DIR			BIT(24)
#define DAVINCI_PRTCIF_CTLR_BENU_MSB		BIT(23)
#define DAVINCI_PRTCIF_CTLR_BENU_3RD_BYTE	BIT(22)
#define DAVINCI_PRTCIF_CTLR_BENU_2ND_BYTE	BIT(21)
#define DAVINCI_PRTCIF_CTLR_BENU_LSB		BIT(20)
#define DAVINCI_PRTCIF_CTLR_BENU_MASK		(0x00F00000)
#define DAVINCI_PRTCIF_CTLR_BENL_MSB		BIT(19)
#define DAVINCI_PRTCIF_CTLR_BENL_3RD_BYTE	BIT(18)
#define DAVINCI_PRTCIF_CTLR_BENL_2ND_BYTE	BIT(17)
#define DAVINCI_PRTCIF_CTLR_BENL_LSB		BIT(16)
#define DAVINCI_PRTCIF_CTLR_BENL_MASK		(0x000F0000)

/* DAVINCI_PRTCIF_INTEN bit fields */
#define DAVINCI_PRTCIF_INTEN_RTCSS		BIT(1)
#define DAVINCI_PRTCIF_INTEN_RTCIF		BIT(0)
#define DAVINCI_PRTCIF_INTEN_MASK		(DAVINCI_PRTCIF_INTEN_RTCSS \
						| DAVINCI_PRTCIF_INTEN_RTCIF)

/* DAVINCI_PRTCIF_INTFLG bit fields */
#define DAVINCI_PRTCIF_INTFLG_RTCSS		BIT(1)
#define DAVINCI_PRTCIF_INTFLG_RTCIF		BIT(0)
#define DAVINCI_PRTCIF_INTFLG_MASK		(DAVINCI_PRTCIF_INTFLG_RTCSS \
						| DAVINCI_PRTCIF_INTFLG_RTCIF)

/* PRTC subsystem registers */
#define DAVINCI_PRTCSS_RTC_INTC_EXTENA1		(0x0C)
#define DAVINCI_PRTCSS_RTC_CTRL			(0x10)
#define DAVINCI_PRTCSS_RTC_WDT			(0x11)
#define DAVINCI_PRTCSS_RTC_TMR0			(0x12)
#define DAVINCI_PRTCSS_RTC_TMR1			(0x13)
#define DAVINCI_PRTCSS_RTC_CCTRL		(0x14)
#define DAVINCI_PRTCSS_RTC_SEC			(0x15)
#define DAVINCI_PRTCSS_RTC_MIN			(0x16)
#define DAVINCI_PRTCSS_RTC_HOUR			(0x17)
#define DAVINCI_PRTCSS_RTC_DAY0			(0x18)
#define DAVINCI_PRTCSS_RTC_DAY1			(0x19)
#define DAVINCI_PRTCSS_RTC_AMIN			(0x1A)
#define DAVINCI_PRTCSS_RTC_AHOUR		(0x1B)
#define DAVINCI_PRTCSS_RTC_ADAY0		(0x1C)
#define DAVINCI_PRTCSS_RTC_ADAY1		(0x1D)
#define DAVINCI_PRTCSS_RTC_CLKC_CNT		(0x20)

/* DAVINCI_PRTCSS_RTC_INTC_EXTENA1 */
#define DAVINCI_PRTCSS_RTC_INTC_EXTENA1_MASK	(0x07)

/* DAVINCI_PRTCSS_RTC_CTRL bit fields */
#define DAVINCI_PRTCSS_RTC_CTRL_WDTBUS		BIT(7)
#define DAVINCI_PRTCSS_RTC_CTRL_WEN		BIT(6)
#define DAVINCI_PRTCSS_RTC_CTRL_WDRT		BIT(5)
#define DAVINCI_PRTCSS_RTC_CTRL_WDTFLG		BIT(4)
#define DAVINCI_PRTCSS_RTC_CTRL_TE		BIT(3)
#define DAVINCI_PRTCSS_RTC_CTRL_TIEN		BIT(2)
#define DAVINCI_PRTCSS_RTC_CTRL_TMRFLG		BIT(1)
#define DAVINCI_PRTCSS_RTC_CTRL_TMMD		BIT(0)

/* DAVINCI_PRTCSS_RTC_CCTRL bit fields */
#define DAVINCI_PRTCSS_RTC_CCTRL_CALBUSY	BIT(7)
#define DAVINCI_PRTCSS_RTC_CCTRL_DAEN		BIT(5)
#define DAVINCI_PRTCSS_RTC_CCTRL_HAEN		BIT(4)
#define DAVINCI_PRTCSS_RTC_CCTRL_MAEN		BIT(3)
#define DAVINCI_PRTCSS_RTC_CCTRL_ALMFLG		BIT(2)
#define DAVINCI_PRTCSS_RTC_CCTRL_AIEN		BIT(1)
#define DAVINCI_PRTCSS_RTC_CCTRL_CAEN		BIT(0)

static inline void davinci_rtcif_write(u32 val, u32 addr){
     writel(val, IO_ADDRESS(PRTCSS_BASE) + addr);
}

static inline u32 davinci_rtcif_read(u32 addr){
     return readl(IO_ADDRESS(PRTCSS_BASE) + addr);
}

static inline void davinci_rtcif_wait(void){
     while (davinci_rtcif_read(DAVINCI_PRTCIF_CTLR) & DAVINCI_PRTCIF_CTLR_BUSY) cpu_relax();
}

static inline void davinci_rtcss_write(unsigned long val, u8 addr){
     davinci_rtcif_write(DAVINCI_PRTCIF_CTLR_BENL_LSB | addr, DAVINCI_PRTCIF_CTLR);
     davinci_rtcif_write(val, DAVINCI_PRTCIF_LDATA);
     davinci_rtcif_wait();
}

static inline u8 davinci_rtcss_read(u8 addr){
     davinci_rtcif_wait();
     davinci_rtcif_write(DAVINCI_PRTCIF_CTLR_DIR|DAVINCI_PRTCIF_CTLR_BENL_LSB|addr, DAVINCI_PRTCIF_CTLR);
     davinci_rtcif_wait();
     return davinci_rtcif_read(DAVINCI_PRTCIF_LDATA);
}
static void v2rprto_set(unsigned int port, unsigned int value){
     u8 result = 0;
     if (port >= 4){
          //printk("PWCTRO pin number error\r\n");
          return; 
     }
     result = davinci_rtcss_read(0x00);
     if (value){
          result |= (1<<port);
     } else {
          result &= ~(1<<port);
     }
     davinci_rtcss_write(result, 0x00);
     return;   
};
static void v2rprtio_set_direction_input(unsigned int number){
     u8 result = 0;
     if (number >= 7){
          //printk("PWCTRIO pin number error\r\n");
          return; 
     }
     result = davinci_rtcss_read(0x02);
     result &= ~(1<<number);//Set it as input
     davinci_rtcss_write(result, 0x02);
};
static void v2rprtio_set_direction_output(unsigned int number, unsigned int value){
     u8 result = 0;
     if (number >= 7){
          //printk("PWCTRIO pin number error\r\n");
          return; 
     }
     //SET OUTPUT VALUE
     result = davinci_rtcss_read(0x01);
     if (value) number |= (1<<number);
     else number &= ~(1<<number);
     davinci_rtcss_write(result, 0x01);
     //SET DIRECTION VALUE
     result = davinci_rtcss_read(0x02);
     result |= (1<<number);//Set it as output
     davinci_rtcss_write(result, 0x02);
};
typedef struct  {
   int number;
   const char* pin_name;
   int configurable;
   int gpio_descriptor;
   int gpio_number;
   int gpio_direction;
   int current_af_number;
//Alternative function 1
   const char* alt_func_name1;
   int alt_func_descriptor1;
   int alt_func_direction1;
//Alternative function 2
   const char* alt_func_name2;
   int alt_func_descriptor2;
   int alt_func_direction2;
//Alternative function 3
   const char* alt_func_name3;
   int alt_func_descriptor3;
   int alt_func_direction3;
} pincon;
#define TRUE  1
#define FALSE 0
#define NA    -1
#define PRTO  10000
#define PRTIO 20000
#define INP 0
#define OUT 1
#define EMPTY 0

static pincon ext_bus_pins[TOTAL_PINS+1] = {
    { 0, "ZERO FAKE PIN",  	FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 1, "GND",			FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 2, "UART0_TXD", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 3, "UART0_RXD", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 4, "AGND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 5, "+3V3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 6, "ETHERNET1", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 7, "ETHERNET2", 		FALSE, NA,		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 8, "ETHERNET3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    { 9, "ETHERNET4", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {10, "ETHERNET5", 		FALSE, NA,		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {11, "ETHERNET6", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {12, "PWCTRO0", 		TRUE,  PRTO,		 0, OUT,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {13, "PWCTRO1", 		TRUE,  PRTO, 		 1, OUT,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {14, "PWCTRO3", 		TRUE,  PRTO, 		 3, OUT,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {15, "GPIO90", 		TRUE,  DM365_GPIO90, 	90, INP,  0, 	"pwm2", DM365_PWM2_G90, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {16, "GPIO89",		TRUE,  DM365_GPIO89, 	89, INP,  0, 	"pwm2", DM365_PWM2_G89, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {17, "GPIO88", 		TRUE,  DM365_GPIO88, 	88, INP,  0, 	"pwm2", DM365_PWM2_G88, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {18, "GPIO87", 		TRUE,  DM365_GPIO87, 	87, INP,  0, 	"pwm2", DM365_PWM2_G87, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {19, "GPIO50", 		TRUE,  DM365_GPIO50, 	50, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {20, "PWR_VIN", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {21, "+3V3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {22, "GPIO67", 		TRUE,  DM365_GPIO67, 	67, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {23, "RESET", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {24, "LINEOUT", 		FALSE, NA,		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {25, "GPIO51", 		TRUE,  DM365_GPIO51, 	51, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {26, "GPIO103", 		TRUE,  DM365_GPIO103,  103, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {27, "GPIO102", 		TRUE,  DM365_GPIO102,  102, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {28, "GPIO101", 		TRUE,  DM365_GPIO101,  101, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {29, "GPIO100", 		TRUE,  DM365_GPIO100,  100, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {30, "GPIO33", 		TRUE,  DM365_GPIO33, 	33, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {31, "GPIO32", 		TRUE,  DM365_GPIO32, 	32, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {32, "GPIO31", 		TRUE,  DM365_GPIO31, 	31, INP,  0,	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {33, "GPIO30", 		TRUE,  DM365_GPIO30, 	30, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {34, "GPIO29", 		TRUE,  DM365_GPIO29, 	29, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {35, "GPIO28", 		TRUE,  DM365_GPIO28, 	28, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {36, "GPIO27", 		TRUE,  DM365_GPIO27, 	27, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {37, "GPIO26", 		TRUE,  DM365_GPIO26, 	26, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    //{38, "GPIO25", 		TRUE,  DM365_GPIO25, 	25, INP,  0, 	"pwm1", DM365_PWM1_G25, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {38, "UART1_TXD2", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},

    {39, "GPIO24",		TRUE,  DM365_GPIO24, 	24, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {40, "GPIO23", 		TRUE,  DM365_GPIO23, 	23, INP,  0, 	"pwm0", DM365_PWM0_G23, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {41, "GPIO22", 		TRUE,  DM365_GPIO22, 	22, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {42, "GPIO80", 		TRUE,  DM365_GPIO80, 	80, INP,  0, 	"pwm3", DM365_PWM3_G80, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {43, "GPIO92", 		TRUE,  DM365_GPIO92, 	92, INP,  0, 	"pwm0", DM365_PWM0, 	OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {44, "GPIO91", 		TRUE,  DM365_GPIO91, 	91, INP,  0, 	"pwm1", DM365_PWM1, 	OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {45, "TVOUT", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {46, "SP+", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {47, "SP-", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {48, "ADC0", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {49, "ADC1", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {50, "ADC2", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {51, "ADC3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {52, "ADC4", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {53, "ADC5", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {54, "PWCTRIO0", 		TRUE,  PRTIO, 		 0, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {55, "PWCTRIO1", 		TRUE,  PRTIO, 		 1, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {56, "PWCTRIO2", 		TRUE,  PRTIO, 		 2, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {57, "PWCTRIO3", 		TRUE,  PRTIO, 		 3, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {58, "PWCTRIO4", 		TRUE,  PRTIO, 		 4, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {59, "PWCTRIO5", 		TRUE,  PRTIO, 		 5, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {60, "PWCTRIO6", 		TRUE,  PRTIO, 		 6, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {61, "GPIO82", 		TRUE,  DM365_GPIO82, 	82, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {62, "GPIO79", 		TRUE,  DM365_GPIO79, 	79, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {63, "GPIO86", 		TRUE,  DM365_GPIO86, 	86, INP, 0, 	"pwm3", DM365_PWM3_G86, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {64, "GPIO85", 		TRUE,  DM365_GPIO85, 	85, INP, 0, 	"pwm3", DM365_PWM3_G85, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {65, "GPIO81", 		TRUE,  DM365_GPIO81, 	81, INP, 0, 	"pwm3", DM365_PWM3_G81, OUT, EMPTY, NA, NA, EMPTY, NA, NA},
    {66, "GPIO34", 		TRUE,  DM365_GPIO34, 	34, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {67, "AGND", 		FALSE, NA, 		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {68, "+3V3", 		FALSE, NA, 		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {69, "PWR_VIN",		FALSE, NA, 		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {70, "DSP_GND", 		FALSE, NA, 		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {71, "GND", 		FALSE, NA, 		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {72, "I2C_DATA", 		FALSE, NA, 		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {73, "I2C_CLK", 		FALSE, NA,		-1, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {74, "COMPPR", 		FALSE, NA, 		-1, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {75, "COMPY", 		FALSE, NA, 		-1, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {76, "COMPPB", 		FALSE, NA, 		-1, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {77, "GPIO49", 		TRUE,  DM365_GPIO49, 	49, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {78, "GPIO48", 		TRUE,  DM365_GPIO48, 	48, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {79, "GPIO47", 		TRUE,  DM365_GPIO47, 	47, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {80, "GPIO46", 		TRUE,  DM365_GPIO46, 	46, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {81, "GPIO45", 		TRUE,  DM365_GPIO45, 	45, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {82, "GPIO44", 		TRUE,  DM365_GPIO44, 	44, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {83, "GPIO35", 		TRUE,  DM365_GPIO35, 	35, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {84, "GPIO84", 		TRUE,  DM365_GPIO84, 	84, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {85, "GPIO83", 		TRUE,  DM365_GPIO83, 	83, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {86, "PWR_VIN", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {87, "AGND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
    {88, "GND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA},
 };

//GPIO functionality
static void v2r_gpio_direction_output(unsigned int number, unsigned int value){
    gpio_direction_output(number, value);
    return;
}
static void v2r_gpio_direction_input(unsigned int number){
    gpio_direction_input(number);
    return;
}


static char** split(const char* str, char delimiter){
    unsigned int input_len = strlen(str);
    int nIndex = 0;
    int nBites = 0;
    int nTempIndex = 0;
    int nTempByte = 0;
    char** result; 
    //Counting bits
    for (nIndex = 0; nIndex < input_len; nIndex++){
        if (str[nIndex] == delimiter) nBites++;
    }
    result = kmalloc( (size_t)(nBites+1)*sizeof(char*),GFP_KERNEL);
    for (nIndex = 0; nIndex <= input_len; nIndex++){
        if ((str[nTempIndex] == delimiter)||(str[nTempIndex] == 0)||(str[nTempIndex] == '\n')||(str[nTempIndex] == '\r') ){
             if (nTempIndex != 0){
		     result[nTempByte] = kmalloc(nTempIndex+1,GFP_KERNEL);
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
         //printk("Chunk %d = %s\r\n", nIndex, result[nIndex]);
    }
    return result;   
};
static int is_equal_string(const char* str1, const char* str2){
    int len = strlen(str1);
    int i = 0;
    if (len != strlen(str2)){ 
	//printk("IS EQUAL STRING LEN ERROR\r\n"); 
	return FALSE;
    }
    for (i = 0; i < len; i++){
       if (str1[i] != str2[i]) { 
        //printk("IS EQUAL STRING COMPARE ERROR %s %s %d\r\n", str1, str2, i); 
        return FALSE;
    }
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
    //printk("str to int returned %d\r\n", result);
    return result;
}
static int starts_with(const char* str1, const char* ref){
    int i = 0;
    int len = strlen(str1);
    const char* substr = 0;
    if (len <= strlen(ref)) {
	//printk("starts with len error\r\n"); 
	return NA;
    }
    len = strlen(ref);
    for (i = 0; i < len; i++){
       if (str1[i] != ref[i]) {
        //printk("starts with not equal error %d\r\n", i); 
        return NA;
    }
    }
    //Here it a good candidate for perfect result. Lets get appropriate value
    substr = &str1[len];
    //printk("Really starts with. Substr %s\r\n", substr);
    if (is_equal_string(substr, "true")) return 1;
    if (is_equal_string(substr, "false")) return 0;
    return str_to_int(substr);    
}
//The most stupid methods I have ever written
static void process_con(char** data, unsigned int ordinal){
    int tmp = NA;
    if (!(ordinal > 0 && ordinal <= TOTAL_PINS)){
         //printk("Wrong pin number\r\n");
         return;
    }
    if (!ext_bus_pins[ordinal].configurable){
         //printk("Requested pin is not configurable\r\n");
         return;
    }
    if (data[2]){
       if (is_equal_string(data[2], "input")){
           if ((ext_bus_pins[ordinal].gpio_descriptor == PRTO) || (ext_bus_pins[ordinal].gpio_descriptor == NA)){
               //printk("This pin cannot be configured as input\r\n");
               return;
           }
           if (ext_bus_pins[ordinal].gpio_descriptor == PRTIO){
                //printk("Configure con%d (PRTIO %d) pin as input\r\n", ordinal, ext_bus_pins[ordinal].gpio_number);
                v2rprtio_set_direction_input(ext_bus_pins[ordinal].gpio_number);
                return;
           }
           //printk("Configure con%d (GPIO %d) pin as input\r\n", ordinal, ext_bus_pins[ordinal].gpio_number);
           v2r_gpio_direction_input(ext_bus_pins[ordinal].gpio_number);
           davinci_cfg_reg(ext_bus_pins[ordinal].gpio_descriptor);
           return;
       } else {
           //printk("Error input %s\r\n", data[2]);
       }
       tmp = starts_with(data[2], "output:");
       //printk("TEMP VALUE %d\r\n", tmp);
       if (tmp != NA) {
           //printk("Prepare to set gpio output\r\n");
           if (ext_bus_pins[ordinal].gpio_descriptor == NA){
                //printk("GPIO descriptor is not available\r\n");
                return;
           }
           if (ext_bus_pins[ordinal].gpio_descriptor == PRTO){
	        //printk("Setting con%d (PRTO%d) output value to %d\r\n", ordinal, ext_bus_pins[ordinal].gpio_number, tmp);
                v2rprto_set(ext_bus_pins[ordinal].gpio_number, tmp);
                return;
           }
           if (ext_bus_pins[ordinal].gpio_descriptor == PRTIO){
	        //printk("Setting con%d (PRTIO%d) output value to %d\r\n", ordinal, ext_bus_pins[ordinal].gpio_number, tmp);
                v2rprtio_set_direction_output(ext_bus_pins[ordinal].gpio_number, tmp);
                return;
           }
           //printk("Setting con%d (GPIO%d) output value to %d\r\n", ordinal, ext_bus_pins[ordinal].gpio_number, tmp);
	   v2r_gpio_direction_output(ext_bus_pins[ordinal].gpio_number, tmp);
           return;
       } else {
           //printk("Error output value\r\n");
       }
       
       if (ext_bus_pins[ordinal].alt_func_name1 && is_equal_string(data[2], ext_bus_pins[ordinal].alt_func_name1)){
           if (ext_bus_pins[ordinal].alt_func_descriptor1 == NA){
                //printk("Wrong descriptor for con%d alternative function\r\n", ordinal);
                return;
           }
           //printk("Setting con%d (GPIO%d) alternative function to %s\r\n", ordinal, ext_bus_pins[ordinal].gpio_number, ext_bus_pins[ordinal].alt_func_name1);
           davinci_cfg_reg(ext_bus_pins[ordinal].alt_func_descriptor1);
           if (ext_bus_pins[ordinal].alt_func_direction1 == INP){
               v2r_gpio_direction_input(ext_bus_pins[ordinal].gpio_number);
           } else if (ext_bus_pins[ordinal].alt_func_direction1 == OUT){
               v2r_gpio_direction_output(ext_bus_pins[ordinal].gpio_number, FALSE);
           }
           return;
       }
    } 
    //printk("Error setting con function\r\n");
}
static void process_pwm(char** data, unsigned int ordinal){
     int duty = -1;
     long period = -1;
     if (!data[2]) {
        //printk("Error pwm parameter\r\n"); 
        return;
     }
     duty = starts_with(data[2], "duty:");
     if (duty < 0 || duty > 255){
         //printk("Error setting duty cycle value for PWM\r\n");
     }

     // added by Gol
     if (data[3]) {
         period = starts_with(data[3], "period:");
     }
     //printk("period cycle value for PWM set to %d\r\n", period);
     // end added by Gol
     
     //printk("Stopping PWM%d\r\n", ordinal);
     //v2rstop_pwm(ordinal);
     //printk("Starting PWM%d with duty cycle value%d\r\n", ordinal, duty);
     v2rstart_pwm(ordinal, duty, period); // changed by Gol, add period
}
static void process_set(char** data){
      int ordinal = -1;
      if (data[1]){//Any parse result present
          //printk("Begin set processing\r\n");
          ordinal = starts_with(data[1], "con");//This is connection
          //printk("Ordinal = %d\r\n", ordinal);
          if (ordinal > 0 && ordinal <= TOTAL_PINS) { 
            //printk("Processing CON%d\r\n", ordinal); 
            process_con(data, ordinal); 
            return; 
	  }
          ordinal = starts_with(data[1], "pwm");//This is pwm hardware
          if (ordinal >= 0 && ordinal < 4) { 
            //printk("Processing PWM%d\r\n", ordinal); 
            process_pwm(data, ordinal); 
            return; 
          }
      }
      //printk("Error setting pin type\r\n");
}

static void process_command(char** data){
      if (data[0]){//Any parse result present
          if (is_equal_string(data[0], "set")){//Process for set command
                //printk("Processing set command\r\n");
		process_set(data); return; 
	  }
      }
      //printk("Wrong V2R driver command\r\n");
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Here the driver itself begins 


MODULE_AUTHOR("Alexander V. Shadrin");
MODULE_LICENSE("GPL");

#define V2R_NDEVICES 1 
#define V2R_BUFFER_SIZE 8192
#define V2R_DEVICE_NAME "v2r_pins"

/* The structure to represent 'v2r' devices. 
 *  data - data buffer;
 *  buffer_size - size of the data buffer;
 *  buffer_data_size - amount of data actually present in the buffer
 *  v2r_mutex - a mutex to protect the fields of this structure;
 *  cdev - character device structure.
*/

struct v2r_dev {
 	unsigned char *data;
 	unsigned long buffer_size; 
 	unsigned long buffer_data_size;  
 	struct mutex v2r_mutex; 
 	struct cdev cdev;
};

#define v2r_buffer_size V2R_BUFFER_SIZE
static unsigned int v2r_major = 0;
static struct v2r_dev *v2r_devices = NULL;
static struct class *v2r_class = NULL;
static int v2r_ndevices = V2R_NDEVICES;


int v2r_open(struct inode *inode, struct file *filp){
	unsigned int mj = imajor(inode);
	unsigned int mn = iminor(inode);
	struct v2r_dev *dev = NULL;
	//printk("V2R open device\r\n");
	if (mj != v2r_major || mn < 0 || mn >= 1){//One and only device
		//printk("No device found with minor=%d and major=%d\n", mj, mn);
		return -ENODEV; /* No such device */
	}
	/* store a pointer to struct v2r_dev here for other methods */
	dev = &v2r_devices[mn];
	filp->private_data = dev; 
	if (inode->i_cdev != &dev->cdev){
		//printk("v2r open: internal error\n");
		return -ENODEV; /* No such device */
	}
	/* if opened the 1st time, allocate the buffer */
	if (dev->data == NULL){
		dev->data = (unsigned char*)kzalloc(dev->buffer_size, GFP_KERNEL);
		if (dev->data == NULL){
			//printk("v2r open(): out of memory\n");
			return -ENOMEM;
		}
		dev->buffer_data_size = 0;
	}
	//printk("V2R device is opened successfully\r\n");
	return 0;
}

int v2r_release(struct inode *inode, struct file *filp){
	//printk("V2R release device\r\n");
	return 0;
}

ssize_t v2r_read(struct file *filp, char __user *buf, size_t count,	loff_t *f_pos){
	struct v2r_dev *dev = (struct v2r_dev *)filp->private_data;
	ssize_t retval = 0;
	//printk("v2r_pins device read\r\n");
	if (mutex_lock_killable(&dev->v2r_mutex)) return -EINTR;
	if (*f_pos >= dev->buffer_data_size) goto out;/* EOF */
	if (*f_pos + count > dev->buffer_data_size)	count = dev->buffer_data_size - *f_pos;
	if (copy_to_user(buf, &(dev->data[*f_pos]), count) != 0){
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	//printk("v2r_pins copied %d bytes to user\r\n", count);
	retval = count;
out:
  	mutex_unlock(&dev->v2r_mutex);
	return retval;
}
     
ssize_t v2r_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
	struct v2r_dev *dev = (struct v2r_dev *)filp->private_data;
	ssize_t retval = 0;
	char *tmp = 0;
	int nIndex = 0;
	char** data = 0;
	//printk("v2r_pins write\r\n");
	if (mutex_lock_killable(&dev->v2r_mutex)) return -EINTR;
	if (*f_pos !=0) {
    /* Writing in the middle of the file is not allowed */
		//printk("Writing in the middle (%d) of the file buffer is not allowed\r\n", (int)(*f_pos));
		retval = -EINVAL;
        goto out;
    }
	if (count > V2R_BUFFER_SIZE) count = V2R_BUFFER_SIZE;
	tmp = kmalloc(count+1,GFP_KERNEL);
	if (tmp==NULL)	return -ENOMEM;
	if (copy_from_user(tmp,buf,count)){
		kfree(tmp);
		retval = -EFAULT;
		goto out;
	}
	tmp[count] = 0;
	dev->buffer_data_size = 0;
	//tmp contains string to parse
	data = split(tmp, ' ');
	process_command(data);
	memcpy(dev->data, "OK", 3);
	dev->buffer_data_size = 3;
	nIndex = 0;
	while(data[nIndex]){//Release allocated memory
		kfree(data[nIndex]);
		nIndex++;
	}
	kfree(data);
	//printk("v2r-dev message %s\r\n", tmp);
	kfree(tmp);
	*f_pos = 0;
	retval = count;
out:
  	mutex_unlock(&dev->v2r_mutex);
	return retval;
}

struct file_operations v2r_fops = {
	.owner =    THIS_MODULE,
	.read =     v2r_read,
	.write =    v2r_write,
	.open =     v2r_open,
	.release =  v2r_release,
};

static int v2r_construct_device(struct v2r_dev *dev, int minor, struct class *class){
	int err = 0;
	dev_t devno = MKDEV(v2r_major, minor);
	struct device *device = NULL;
	BUG_ON(dev == NULL || class == NULL);
	/* Memory is to be allocated when the device is opened the first time */
	//printk("v2r_pins construct device:%d\r\n", minor);
	dev->data = NULL;     
	dev->buffer_size = v2r_buffer_size;
	mutex_init(&dev->v2r_mutex);
	cdev_init(&dev->cdev, &v2r_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err){
		//printk("V2R Error %d while trying to add %s%d",	err, V2R_DEVICE_NAME, minor);
        	return err;
	}
	device = device_create(class, NULL/*no parent device*/,  devno, NULL/*no additional data */,V2R_DEVICE_NAME);
	if (IS_ERR(device)) {
		err = PTR_ERR(device);
		//printk("Error %d while trying to create %s%d", err, V2R_DEVICE_NAME, minor);
        cdev_del(&dev->cdev);
        return err;
    }
	//printk("v2r_pins device is constructed successfully\r\n");
	return 0;
}

static void v2r_destroy_device(struct v2r_dev *dev, int minor, struct class *class){
	BUG_ON(dev == NULL || class == NULL);
	//printk("v2r destroy device: %d\r\n", minor);
	device_destroy(class, MKDEV(v2r_major, minor));
	cdev_del(&dev->cdev);
	kfree(dev->data);
	//printk("v2r device is destroyed successfully\r\n");
	return;
}

static void v2r_cleanup_module(int devices_to_destroy){
	int i = 0;
	/* Get rid of character devices (if any exist) */
	//printk("v2r pins cleanup module\r\n");
	if (v2r_devices) {
		for (i = 0; i < devices_to_destroy; ++i) {
			v2r_destroy_device(&v2r_devices[i], i, v2r_class);
        }
		kfree(v2r_devices);
	}
	if (v2r_class) class_destroy(v2r_class);
	unregister_chrdev_region(MKDEV(v2r_major, 0), v2r_ndevices);
	//printk("v2r pins cleanup completed\r\n");
	return;

}

static int __init v2r_init_module(void){
	int err = 0;
	int i = 0;
	int devices_to_destroy = 0;
	dev_t dev = 0;
	printk("V2R_PINS MODULE version 1.0 INIT\r\n");
	if (v2r_ndevices <= 0){
		//printk("V2R Invalid value of v2r_ndevices: %d\n", v2r_ndevices);
		return -EINVAL;
	}
	/* Get a range of minor numbers (starting with 0) to work with */
	err = alloc_chrdev_region(&dev, 0, v2r_ndevices, V2R_DEVICE_NAME);
	if (err < 0) {
		//printk("V2R alloc_chrdev_region() failed\n");
		return err;
	}
	v2r_major = MAJOR(dev);
	//printk("v2r_pins device major: %d\r\n", v2r_major);
	/* Create device class (before allocation of the array of devices) */
	v2r_class = class_create(THIS_MODULE, V2R_DEVICE_NAME);
	if (IS_ERR(v2r_class)){
		err = PTR_ERR(v2r_class);
		//printk("v2r_pins class not created %d\r\n", err);
		goto fail;
    }
	/* Allocate the array of devices */
	v2r_devices = (struct v2r_dev *)kzalloc( v2r_ndevices * sizeof(struct v2r_dev), GFP_KERNEL);
	if (v2r_devices == NULL) {
		err = -ENOMEM;
		//printk("v2r_pins devices not allocated %d\r\n", err);
		goto fail;
	}
	/* Construct devices */
	for (i = 0; i < v2r_ndevices; ++i) {
		err = v2r_construct_device(&v2r_devices[i], i, v2r_class);
		if (err) {
			//printk("v2r_pins device is not constructed\r\n");
			devices_to_destroy = i;
			goto fail;
        }
	}
	v2rinit_pwm();
	return 0; /* success */
fail:
	v2r_cleanup_module(devices_to_destroy);
	return err;
}

static void __exit v2r_exit_module(void){
	printk("V2R_PINS module exit\r\n");
	v2r_cleanup_module(v2r_ndevices);
	return;
}

module_init(v2r_init_module);
module_exit(v2r_exit_module);

 

