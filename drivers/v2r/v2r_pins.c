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

#include <linux/proc_fs.h>

#include "prtcss.h"

#define TOTAL_PINS 86
#define TOTAL_PWM  4

#define TRUE  1
#define FALSE 0
#define NA    -1
#define PRTO  10000
#define PRTIO 20000
#define INP 0
#define OUT 1
#define EMPTY 0

#define PWM0BASE 0x01C22000
#define PWM1BASE 0x01C22400
#define PWM2BASE 0x01C22800
#define PWM3BASE 0x01C22C00


#define NUMBEROFDEVICES 1
#define BUFFER_SIZE 8192
#define RETBUFFER_SIZE 8192
#define DEVICE_NAME "v2r_pins"


/* The structure to represent 'v2r_pins' devices.
 *  data - data buffer;
 *  buffer_size - size of the data buffer;
 *  buffer_data_size - amount of data actually present in the buffer
 *  device_mutex - a mutex to protect the fields of this structure;
 *  cdev - character device structure.
*/

struct pins_dev {
	unsigned char *data;
	unsigned long buffer_size;
	unsigned long buffer_data_size;
	struct mutex device_mutex;
	struct cdev cdev;
};


static unsigned int pins_major = 0;
static struct pins_dev *pins_devices = NULL;
static struct class *pins_class = NULL;
static int numberofdevices = NUMBEROFDEVICES;
static char * v2r_pins_retBuffer;

static char ** command_parts;
static int command_parts_counter;

static int output_mode = 0; // text mode default



typedef struct  {
	int number;
	const char* pin_name;
	int configurable;
	int gpio_descriptor;
	int gpio_number;
	int gpio_direction;
	int current_af_number;

	// alternative function 1

	const char* alt_func_name1;
	int alt_func_descriptor1;
	int alt_func_direction1;

	// alternative function 2

	const char* alt_func_name2;
	int alt_func_descriptor2;
	int alt_func_direction2;

	// alternative function 3

	const char* alt_func_name3;
	int alt_func_descriptor3;
	int alt_func_direction3;

} pincon;

/* array and counter for pins group */
static pincon pinsGroupTable[TOTAL_PINS+1];
static short pinsGroupTableCounter = 0;


static pincon ext_bus_pins[TOTAL_PINS+1] = {
    { 0, "ZERO FAKE PIN",  	FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 1, "GND",			FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 2, "UART0_TXD", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 3, "UART0_RXD", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 4, "AGND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 5, "ETHERNET1", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 6, "ETHERNET2", 		FALSE, NA,		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 7, "ETHERNET3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 8, "ETHERNET4", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    { 9, "ETHERNET5", 		FALSE, NA,		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {10, "GPIO15", 		TRUE,  DM365_GPIO15, 	15, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {11, "GPIO14", 		TRUE,  DM365_GPIO14, 	14, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {12, "GPIO13", 		TRUE,  DM365_GPIO13, 	13, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {13, "GPIO12", 		TRUE,  DM365_GPIO12, 	12, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {14, "GPIO11", 		TRUE,  DM365_GPIO11, 	11, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {15, "GPIO10", 		TRUE,  DM365_GPIO10, 	10, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {16, "GPIO90",		TRUE,  DM365_GPIO90, 	90, INP,  0, 	"pwm2", DM365_PWM2_G90, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {17, "GPIO89", 		TRUE,  DM365_GPIO89, 	89, INP,  0, 	"pwm2", DM365_PWM2_G89,	OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {18, "GPIO88", 		TRUE,  DM365_GPIO88, 	88, INP,  0, 	"pwm2", DM365_PWM2_G88,	OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {19, "GPIO87", 		TRUE,  DM365_GPIO87, 	87, INP,  0, 	"pwm2", DM365_PWM2_G87,	OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {20, "GPIO50", 		TRUE,  DM365_GPIO50, 	50, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {21, "PWR_VIN", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {22, "+3V3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {23, "RESET", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {24, "LINEOUT", 		FALSE, NA,		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {25, "GPIO1", 		TRUE,  DM365_GPIO1, 	1,  INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {26, "GPIO37", 		TRUE,  DM365_GPIO37,	37, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {27, "GPIO36", 		TRUE,  DM365_GPIO36,    36, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {28, "GPIO17", 		TRUE,  DM365_GPIO17,    17, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {29, "GPIO16", 		TRUE,  DM365_GPIO16,    16, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {30, "GPIO33", 		TRUE,  DM365_GPIO33, 	33, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {31, "GPIO32", 		TRUE,  DM365_GPIO32, 	32, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {32, "GPIO31", 		TRUE,  DM365_GPIO31, 	31, INP,  0,	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {33, "GPIO30", 		TRUE,  DM365_GPIO30, 	30, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {34, "GPIO29", 		TRUE,  DM365_GPIO29, 	29, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {35, "GPIO28", 		TRUE,  DM365_GPIO28, 	28, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {36, "GPIO27", 		TRUE,  DM365_GPIO27, 	27, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {37, "GPIO26", 		TRUE,  DM365_GPIO26, 	26, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {38, "GPIO2", 		TRUE,  DM365_GPIO2, 	2,  INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {39, "GPIO24",		TRUE,  DM365_GPIO24, 	24, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {40, "GPIO23", 		TRUE,  DM365_GPIO23, 	23, INP,  0, 	"pwm0", DM365_PWM0_G23, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {41, "GPIO22", 		TRUE,  DM365_GPIO22, 	22, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {42, "GPIO80", 		TRUE,  DM365_GPIO80, 	80, INP,  0, 	"pwm3", DM365_PWM3_G80, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {43, "GPIO92", 		TRUE,  DM365_GPIO92, 	92, INP,  0, 	"pwm0", DM365_PWM0, 	OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {44, "GPIO91", 		TRUE,  DM365_GPIO91, 	91, INP,  0, 	"pwm1", DM365_PWM1, 	OUT, EMPTY, NA, NA, EMPTY, NA, NA },

    {45, "TVOUT", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {46, "SP+", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {47, "SP-", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {48, "ADC0", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {49, "ADC1", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {50, "ADC2", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {51, "ADC3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {52, "ADC4", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {53, "ADC5", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },

    {54, "GPIO3", 		TRUE,  DM365_GPIO3, 	3, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {55, "GPIO4", 		TRUE,  DM365_GPIO4, 	4, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {56, "GPIO5", 		TRUE,  DM365_GPIO5, 	5, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {57, "GPIO6", 		TRUE,  DM365_GPIO6, 	6, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {58, "GPIO7", 		TRUE,  DM365_GPIO7, 	7, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {59, "GPIO8", 		TRUE,  DM365_GPIO8, 	8, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {60, "GPIO9", 		TRUE,  DM365_GPIO9, 	9, INP,  0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {61, "GPIO82", 		TRUE,  DM365_GPIO82, 	82, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {62, "GPIO79", 		TRUE,  DM365_GPIO79, 	79, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {63, "GPIO86", 		TRUE,  DM365_GPIO86, 	86, INP, 0, 	"pwm3", DM365_PWM3_G86, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {64, "GPIO85", 		TRUE,  DM365_GPIO85, 	85, INP, 0, 	"pwm3", DM365_PWM3_G85, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {65, "GPIO81", 		TRUE,  DM365_GPIO81, 	81, INP, 0, 	"pwm3", DM365_PWM3_G81, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {66, "AGND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {67, "+3V3", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {68, "PWR_VIN",		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {69, "DSP_GND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {70, "I2C_DATA", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {71, "I2C_CLK", 		FALSE, NA,		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {72, "COMPPR", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {73, "COMPY", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {74, "COMPPB", 		FALSE, NA, 		NA, OUT, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {75, "GPIO49", 		TRUE,  DM365_GPIO49, 	49, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {76, "GPIO48", 		TRUE,  DM365_GPIO48, 	48, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {77, "GPIO47", 		TRUE,  DM365_GPIO47, 	47, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {78, "GPIO46", 		TRUE,  DM365_GPIO46, 	46, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {79, "GPIO45", 		TRUE,  DM365_GPIO45, 	45, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {80, "GPIO44", 		TRUE,  DM365_GPIO44, 	44, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {81, "GPIO35", 		TRUE,  DM365_GPIO35, 	35, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {82, "GPIO84", 		TRUE,  DM365_GPIO84, 	84, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {83, "GPIO83", 		TRUE,  DM365_GPIO83, 	83, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {84, "GPIO25", 		TRUE,  DM365_GPIO25, 	25, INP, 0, 	"pwm1", DM365_PWM1_G25, OUT, EMPTY, NA, NA, EMPTY, NA, NA },
    {85, "GPIO34", 		TRUE,  DM365_GPIO34, 	34, INP, 0, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
    {86, "GND", 		FALSE, NA, 		NA, INP, NA, 	 EMPTY, NA, 		NA,  EMPTY, NA, NA, EMPTY, NA, NA },
};



/**********************************************************************
  for PWM
**********************************************************************/

typedef struct {

	volatile unsigned int pid;
	volatile unsigned int pcr;
	volatile unsigned int cfg;
	volatile unsigned int start;
	volatile unsigned int repeat;
	volatile unsigned int period;
	volatile unsigned int ph1d;

} PWM;


static volatile PWM* pwm[TOTAL_PWM] = {

	(volatile PWM*)IO_ADDRESS(PWM0BASE),
	(volatile PWM*)IO_ADDRESS(PWM1BASE),
	(volatile PWM*)IO_ADDRESS(PWM2BASE),
	(volatile PWM*)IO_ADDRESS(PWM3BASE)

};


static void v2r_init_pwm(void) {

	int i = 0;

	struct clk* clk_pwm0;
	struct clk* clk_pwm1;
	struct clk* clk_pwm2;
	struct clk* clk_pwm3;

	printk("%s: init v2r CON & PWM clocks\n", DEVICE_NAME);

	clk_pwm0 = clk_get(NULL, "pwm0");
	clk_enable(clk_pwm0);
	clk_pwm1 = clk_get(NULL, "pwm1");
	clk_enable(clk_pwm1);
	clk_pwm2 = clk_get(NULL, "pwm2");
	clk_enable(clk_pwm2);
	clk_pwm3 = clk_get(NULL, "pwm3");
	clk_enable(clk_pwm3);

	// set up and clear all PWM timers
	for (i = 0; i < TOTAL_PWM; i++) {

		pwm[i]->pcr = 0x1;
		pwm[i]->cfg = 0x12;
		pwm[i]->start = 0;
		pwm[i]->repeat = 0x0;
		pwm[i]->period = 0;
		pwm[i]->ph1d = 0x0;

	}

}

/**********************************************************************
 end for PWM
**********************************************************************/


/**********************************************************************
 for PROC_FS 
**********************************************************************/

#ifndef CONFIG_PROC_FS

static int pins_add_proc_fs(void) {
	return 0;
}

static int pins_remove_proc_fs(void) {
	return 0;
}

#else

static struct proc_dir_entry *proc_parent;
static struct proc_dir_entry *proc_entry;
static s32 proc_write_entry[TOTAL_PINS + TOTAL_PWM +2 ];

static int pins_read_proc (int pin_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	int len=0;
	int value = 0;

	if (ext_bus_pins[pin_number].gpio_descriptor == NA) {
		printk("%s: CON descriptor is not available\n", DEVICE_NAME);
		return -EFAULT;
	}

	value = gpio_get_value(ext_bus_pins[pin_number].gpio_number)? 1 : 0;

	len = sprintf(buf, "%d\n", value);
	
	return len;

}

static int pins_write_proc (int pin_number, struct file *file, const char *buf, int count, void *data ) {

	static int value = 0;
	static char proc_data[2];

	if (ext_bus_pins[pin_number].gpio_descriptor == NA) {
		printk("%s: CON descriptor is not available\n", DEVICE_NAME);
		return -EFAULT;
	}

	if(count > 1)
		count = 1;

	if(copy_from_user(proc_data, buf, count))
		return -EFAULT;

	if (proc_data[0] == 0) 
		value = 0;
	else if (proc_data[0] == 1) 
		value = 1;
	else
		kstrtoint(proc_data, 2, &value);

	gpio_direction_output(ext_bus_pins[pin_number].gpio_number, value);
	davinci_cfg_reg(ext_bus_pins[pin_number].gpio_descriptor);

	return count;
}


/* *i'd line to use array init, but i don't know how get file id from unified functions */

static int pins_write_proc_10 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (10, file, buf, count, data); }
static int pins_read_proc_10 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (10, buf, start, offset, count, eof, data); }
static int pins_write_proc_11 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (11, file, buf, count, data); }
static int pins_read_proc_11 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (11, buf, start, offset, count, eof, data); }
static int pins_write_proc_12 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (12, file, buf, count, data); }
static int pins_read_proc_12 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (12, buf, start, offset, count, eof, data); }
static int pins_write_proc_13 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (13, file, buf, count, data); }
static int pins_read_proc_13 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (13, buf, start, offset, count, eof, data); }
static int pins_write_proc_14 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (14, file, buf, count, data); }
static int pins_read_proc_14 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (14, buf, start, offset, count, eof, data); }
static int pins_write_proc_15 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (15, file, buf, count, data); }
static int pins_read_proc_15 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (15, buf, start, offset, count, eof, data); }
static int pins_write_proc_16 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (16, file, buf, count, data); }
static int pins_read_proc_16 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (16, buf, start, offset, count, eof, data); }
static int pins_write_proc_17 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (17, file, buf, count, data); }
static int pins_read_proc_17 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (17, buf, start, offset, count, eof, data); }
static int pins_write_proc_18 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (18, file, buf, count, data); }
static int pins_read_proc_18 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (18, buf, start, offset, count, eof, data); }
static int pins_write_proc_19 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (19, file, buf, count, data); }
static int pins_read_proc_19 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (19, buf, start, offset, count, eof, data); }
static int pins_write_proc_20 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (20, file, buf, count, data); }
static int pins_read_proc_20 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (20, buf, start, offset, count, eof, data); }

static int pins_write_proc_25 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (25, file, buf, count, data); }
static int pins_read_proc_25 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (25, buf, start, offset, count, eof, data); }
static int pins_write_proc_26 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (26, file, buf, count, data); }
static int pins_read_proc_26 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (26, buf, start, offset, count, eof, data); }
static int pins_write_proc_27 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (27, file, buf, count, data); }
static int pins_read_proc_27 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (27, buf, start, offset, count, eof, data); }
static int pins_write_proc_28 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (28, file, buf, count, data); }
static int pins_read_proc_28 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (28, buf, start, offset, count, eof, data); }
static int pins_write_proc_29 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (29, file, buf, count, data); }
static int pins_read_proc_29 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (29, buf, start, offset, count, eof, data); }
static int pins_write_proc_30 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (30, file, buf, count, data); }
static int pins_read_proc_30 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (30, buf, start, offset, count, eof, data); }
static int pins_write_proc_31 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (31, file, buf, count, data); }
static int pins_read_proc_31 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (31, buf, start, offset, count, eof, data); }
static int pins_write_proc_32 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (32, file, buf, count, data); }
static int pins_read_proc_32 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (32, buf, start, offset, count, eof, data); }
static int pins_write_proc_33 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (33, file, buf, count, data); }
static int pins_read_proc_33 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (33, buf, start, offset, count, eof, data); }
static int pins_write_proc_34 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (34, file, buf, count, data); }
static int pins_read_proc_34 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (34, buf, start, offset, count, eof, data); }
static int pins_write_proc_35 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (35, file, buf, count, data); }
static int pins_read_proc_35 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (35, buf, start, offset, count, eof, data); }
static int pins_write_proc_36 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (36, file, buf, count, data); }
static int pins_read_proc_36 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (36, buf, start, offset, count, eof, data); }
static int pins_write_proc_37 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (37, file, buf, count, data); }
static int pins_read_proc_37 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (37, buf, start, offset, count, eof, data); }
static int pins_write_proc_38 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (38, file, buf, count, data); }
static int pins_read_proc_38 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (38, buf, start, offset, count, eof, data); }
static int pins_write_proc_39 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (39, file, buf, count, data); }
static int pins_read_proc_39 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (39, buf, start, offset, count, eof, data); }
static int pins_write_proc_40 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (40, file, buf, count, data); }
static int pins_read_proc_40 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (40, buf, start, offset, count, eof, data); }
static int pins_write_proc_41 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (41, file, buf, count, data); }
static int pins_read_proc_41 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (41, buf, start, offset, count, eof, data); }
static int pins_write_proc_42 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (42, file, buf, count, data); }
static int pins_read_proc_42 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (42, buf, start, offset, count, eof, data); }
static int pins_write_proc_43 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (43, file, buf, count, data); }
static int pins_read_proc_43 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (43, buf, start, offset, count, eof, data); }
static int pins_write_proc_44 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (44, file, buf, count, data); }
static int pins_read_proc_44 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (44, buf, start, offset, count, eof, data); }

static int pins_write_proc_54 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (54, file, buf, count, data); }
static int pins_read_proc_54 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (54, buf, start, offset, count, eof, data); }
static int pins_write_proc_55 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (55, file, buf, count, data); }
static int pins_read_proc_55 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (55, buf, start, offset, count, eof, data); }
static int pins_write_proc_56 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (56, file, buf, count, data); }
static int pins_read_proc_56 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (56, buf, start, offset, count, eof, data); }
static int pins_write_proc_57 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (57, file, buf, count, data); }
static int pins_read_proc_57 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (57, buf, start, offset, count, eof, data); }
static int pins_write_proc_58 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (58, file, buf, count, data); }
static int pins_read_proc_58 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (58, buf, start, offset, count, eof, data); }
static int pins_write_proc_59 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (59, file, buf, count, data); }
static int pins_read_proc_59 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (59, buf, start, offset, count, eof, data); }
static int pins_write_proc_60 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (60, file, buf, count, data); }
static int pins_read_proc_60 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (60, buf, start, offset, count, eof, data); }
static int pins_write_proc_61 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (61, file, buf, count, data); }
static int pins_read_proc_61 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (61, buf, start, offset, count, eof, data); }
static int pins_write_proc_62 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (62, file, buf, count, data); }
static int pins_read_proc_62 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (62, buf, start, offset, count, eof, data); }
static int pins_write_proc_63 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (63, file, buf, count, data); }
static int pins_read_proc_63 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (63, buf, start, offset, count, eof, data); }
static int pins_write_proc_64 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (64, file, buf, count, data); }
static int pins_read_proc_64 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (64, buf, start, offset, count, eof, data); }
static int pins_write_proc_65 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (65, file, buf, count, data); }
static int pins_read_proc_65 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (65, buf, start, offset, count, eof, data); }

static int pins_write_proc_75 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (75, file, buf, count, data); }
static int pins_read_proc_75 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (75, buf, start, offset, count, eof, data); }
static int pins_write_proc_76 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (76, file, buf, count, data); }
static int pins_read_proc_76 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (76, buf, start, offset, count, eof, data); }
static int pins_write_proc_77 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (77, file, buf, count, data); }
static int pins_read_proc_77 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (77, buf, start, offset, count, eof, data); }
static int pins_write_proc_78 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (78, file, buf, count, data); }
static int pins_read_proc_78 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (78, buf, start, offset, count, eof, data); }
static int pins_write_proc_79 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (79, file, buf, count, data); }
static int pins_read_proc_79 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (79, buf, start, offset, count, eof, data); }
static int pins_write_proc_80 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (80, file, buf, count, data); }
static int pins_read_proc_80 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (80, buf, start, offset, count, eof, data); }
static int pins_write_proc_81 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (81, file, buf, count, data); }
static int pins_read_proc_81 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (81, buf, start, offset, count, eof, data); }
static int pins_write_proc_82 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (82, file, buf, count, data); }
static int pins_read_proc_82 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (82, buf, start, offset, count, eof, data); }
static int pins_write_proc_83 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (83, file, buf, count, data); }
static int pins_read_proc_83 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (83, buf, start, offset, count, eof, data); }
static int pins_write_proc_84 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (84, file, buf, count, data); }
static int pins_read_proc_84 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (84, buf, start, offset, count, eof, data); }
static int pins_write_proc_85 (struct file *file, const char *buf, int count, void *data ) { return pins_write_proc (85, file, buf, count, data); }
static int pins_read_proc_85 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc (85, buf, start, offset, count, eof, data); }

static int pins_read_proc_all (char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	char buffer[TOTAL_PINS + 1];
	int i;
	int len=0;
	int value = 0;

	for (i = 0; i <= TOTAL_PINS; i++) {

		value = gpio_get_value(ext_bus_pins[i].gpio_number);

		/* bers, eat this */
		buffer[i] = value ? '1' : '0';

	}

	len = sprintf(buf, "%s\n", buffer);
	
	return len;

}


static int pins_read_proc_pwm (int id, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	int len=0;
	int i;

	char pwmstr[10];
	char constr[5];
	char *list;

	list = kmalloc(50, GFP_KERNEL);
	memset(list, 0, 50);

	sprintf(pwmstr, "pwm%d", id);

	for (i = 1; i < TOTAL_PINS; i++) {
		if (!ext_bus_pins[i].alt_func_name1) continue;
		if ((ext_bus_pins[i].current_af_number == 1) && (!strcmp(ext_bus_pins[i].alt_func_name1, pwmstr))) {
			memset(constr, 0, sizeof(constr));
			sprintf(constr, "%d ", i);
			list = strcat(list, constr);
		}
	}

	len = sprintf(buf, "%d %d %d %d %X %s\n", id, pwm[id]->ph1d, pwm[id]->period, pwm[id]->repeat, pwm[id]->cfg, list);

	kfree(list);
	
	return len;

}

static int pins_read_proc_pwm0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc_pwm (0, buf, start, offset, count, eof, data); }
static int pins_read_proc_pwm1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc_pwm (1, buf, start, offset, count, eof, data); }
static int pins_read_proc_pwm2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc_pwm (2, buf, start, offset, count, eof, data); }
static int pins_read_proc_pwm3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pins_read_proc_pwm (3, buf, start, offset, count, eof, data); }


static int pins_remove_proc_fs(void) {

	int i;
	char fn[10];

	for (i = 0; i <= TOTAL_PINS + TOTAL_PWM + 2; i++) {

		if (proc_write_entry[i]) { 
			sprintf(fn, "%d", i);
			remove_proc_entry(fn, proc_parent);
		}

	}

	/* remove proc_fs directory */
	remove_proc_entry("v2r_pins",NULL);

	return 0;
}

static int pins_add_proc_fs(void) {

	proc_parent = proc_mkdir("v2r_pins", NULL);

	if (!proc_parent) {
		printk("%s: error creating proc entry (/proc/v2r_pins)\n", DEVICE_NAME);
		return 1;
	}

	/*	
	for (i = 1; i <= TOTAL_PINS; i++ ) {

		sprintf(procfilename, "%d", i);
		proc_entry = create_proc_entry(procfilename, 0666, proc_parent);

		if (!proc_entry) {

			printk("%s: error creating proc entry (/proc/v2r_pins/%d)\n", DEVICE_NAME, i);
			// return -ENOMEM;

		}

		proc_entry-> read_proc = read_proc ;
		proc_entry-> write_proc = write_proc;
		proc_entry-> owner = THIS_MODULE;
		proc_entry-> mode = S_IFREG | S_IRUGO;
		//proc_entry-> uid = 0;
		//proc_entry-> gid = 0;
		//proc_entry-> size = 10;

		proc_write_entry[i] = proc_entry;
	
	}
	*/

	proc_entry = create_proc_entry("10", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_10; proc_entry-> write_proc = (void *) pins_write_proc_10; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[10] = (s32) proc_entry; }
	proc_entry = create_proc_entry("11", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_11; proc_entry-> write_proc = (void *) pins_write_proc_11; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[11] = (s32) proc_entry; }
	proc_entry = create_proc_entry("12", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_12; proc_entry-> write_proc = (void *) pins_write_proc_12; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[12] = (s32) proc_entry; }
	proc_entry = create_proc_entry("13", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_13; proc_entry-> write_proc = (void *) pins_write_proc_13; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[13] = (s32) proc_entry; }
	proc_entry = create_proc_entry("14", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_14; proc_entry-> write_proc = (void *) pins_write_proc_14; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[14] = (s32) proc_entry; }
	proc_entry = create_proc_entry("15", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_15; proc_entry-> write_proc = (void *) pins_write_proc_15; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[15] = (s32) proc_entry; }
	proc_entry = create_proc_entry("16", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_16; proc_entry-> write_proc = (void *) pins_write_proc_16; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[16] = (s32) proc_entry; }
	proc_entry = create_proc_entry("17", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_17; proc_entry-> write_proc = (void *) pins_write_proc_17; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[17] = (s32) proc_entry; }
	proc_entry = create_proc_entry("18", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_18; proc_entry-> write_proc = (void *) pins_write_proc_18; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[18] = (s32) proc_entry; }
	proc_entry = create_proc_entry("19", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_19; proc_entry-> write_proc = (void *) pins_write_proc_19; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[19] = (s32) proc_entry; }
	proc_entry = create_proc_entry("20", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_20; proc_entry-> write_proc = (void *) pins_write_proc_20; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[20] = (s32) proc_entry; }

	proc_entry = create_proc_entry("25", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_25; proc_entry-> write_proc = (void *) pins_write_proc_25; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[25] = (s32) proc_entry; }
	proc_entry = create_proc_entry("26", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_26; proc_entry-> write_proc = (void *) pins_write_proc_26; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[26] = (s32) proc_entry; }
	proc_entry = create_proc_entry("27", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_27; proc_entry-> write_proc = (void *) pins_write_proc_27; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[27] = (s32) proc_entry; }
	proc_entry = create_proc_entry("28", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_28; proc_entry-> write_proc = (void *) pins_write_proc_28; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[28] = (s32) proc_entry; }
	proc_entry = create_proc_entry("29", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_29; proc_entry-> write_proc = (void *) pins_write_proc_29; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[29] = (s32) proc_entry; }
	proc_entry = create_proc_entry("30", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_30; proc_entry-> write_proc = (void *) pins_write_proc_30; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[30] = (s32) proc_entry; }
	proc_entry = create_proc_entry("31", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_31; proc_entry-> write_proc = (void *) pins_write_proc_31; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[31] = (s32) proc_entry; }
	proc_entry = create_proc_entry("32", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_32; proc_entry-> write_proc = (void *) pins_write_proc_32; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[32] = (s32) proc_entry; }
	proc_entry = create_proc_entry("33", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_33; proc_entry-> write_proc = (void *) pins_write_proc_33; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[33] = (s32) proc_entry; }
	proc_entry = create_proc_entry("34", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_34; proc_entry-> write_proc = (void *) pins_write_proc_34; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[34] = (s32) proc_entry; }
	proc_entry = create_proc_entry("35", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_35; proc_entry-> write_proc = (void *) pins_write_proc_35; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[35] = (s32) proc_entry; }
	proc_entry = create_proc_entry("36", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_36; proc_entry-> write_proc = (void *) pins_write_proc_36; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[36] = (s32) proc_entry; }
	proc_entry = create_proc_entry("37", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_37; proc_entry-> write_proc = (void *) pins_write_proc_37; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[37] = (s32) proc_entry; }
	proc_entry = create_proc_entry("38", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_38; proc_entry-> write_proc = (void *) pins_write_proc_38; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[38] = (s32) proc_entry; }
	proc_entry = create_proc_entry("39", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_39; proc_entry-> write_proc = (void *) pins_write_proc_39; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[39] = (s32) proc_entry; }
	proc_entry = create_proc_entry("40", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_40; proc_entry-> write_proc = (void *) pins_write_proc_40; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[40] = (s32) proc_entry; }
	proc_entry = create_proc_entry("41", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_41; proc_entry-> write_proc = (void *) pins_write_proc_41; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[41] = (s32) proc_entry; }
	proc_entry = create_proc_entry("42", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_42; proc_entry-> write_proc = (void *) pins_write_proc_42; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[42] = (s32) proc_entry; }
	proc_entry = create_proc_entry("43", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_43; proc_entry-> write_proc = (void *) pins_write_proc_43; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[43] = (s32) proc_entry; }
	proc_entry = create_proc_entry("44", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_44; proc_entry-> write_proc = (void *) pins_write_proc_44; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[44] = (s32) proc_entry; }

	proc_entry = create_proc_entry("54", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_54; proc_entry-> write_proc = (void *) pins_write_proc_54; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[54] = (s32) proc_entry; }
	proc_entry = create_proc_entry("55", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_55; proc_entry-> write_proc = (void *) pins_write_proc_55; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[55] = (s32) proc_entry; }
	proc_entry = create_proc_entry("56", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_56; proc_entry-> write_proc = (void *) pins_write_proc_56; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[56] = (s32) proc_entry; }
	proc_entry = create_proc_entry("57", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_57; proc_entry-> write_proc = (void *) pins_write_proc_57; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[57] = (s32) proc_entry; }
	proc_entry = create_proc_entry("58", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_58; proc_entry-> write_proc = (void *) pins_write_proc_58; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[58] = (s32) proc_entry; }
	proc_entry = create_proc_entry("59", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_59; proc_entry-> write_proc = (void *) pins_write_proc_59; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[59] = (s32) proc_entry; }
	proc_entry = create_proc_entry("60", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_60; proc_entry-> write_proc = (void *) pins_write_proc_60; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[60] = (s32) proc_entry; }
	proc_entry = create_proc_entry("61", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_61; proc_entry-> write_proc = (void *) pins_write_proc_61; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[61] = (s32) proc_entry; }
	proc_entry = create_proc_entry("62", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_62; proc_entry-> write_proc = (void *) pins_write_proc_62; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[62] = (s32) proc_entry; }
	proc_entry = create_proc_entry("63", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_63; proc_entry-> write_proc = (void *) pins_write_proc_63; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[63] = (s32) proc_entry; }
	proc_entry = create_proc_entry("64", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_64; proc_entry-> write_proc = (void *) pins_write_proc_64; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[64] = (s32) proc_entry; }
	proc_entry = create_proc_entry("65", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_65; proc_entry-> write_proc = (void *) pins_write_proc_65; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[65] = (s32) proc_entry; }

	proc_entry = create_proc_entry("75", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_75; proc_entry-> write_proc = (void *) pins_write_proc_75; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[75] = (s32) proc_entry; }
	proc_entry = create_proc_entry("76", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_76; proc_entry-> write_proc = (void *) pins_write_proc_76; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[76] = (s32) proc_entry; }
	proc_entry = create_proc_entry("77", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_77; proc_entry-> write_proc = (void *) pins_write_proc_77; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[77] = (s32) proc_entry; }
	proc_entry = create_proc_entry("78", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_78; proc_entry-> write_proc = (void *) pins_write_proc_78; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[78] = (s32) proc_entry; }
	proc_entry = create_proc_entry("79", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_79; proc_entry-> write_proc = (void *) pins_write_proc_79; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[79] = (s32) proc_entry; }
	proc_entry = create_proc_entry("80", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_80; proc_entry-> write_proc = (void *) pins_write_proc_80; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[80] = (s32) proc_entry; }
	proc_entry = create_proc_entry("81", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_81; proc_entry-> write_proc = (void *) pins_write_proc_81; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[81] = (s32) proc_entry; }
	proc_entry = create_proc_entry("82", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_82; proc_entry-> write_proc = (void *) pins_write_proc_82; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[82] = (s32) proc_entry; }
	proc_entry = create_proc_entry("83", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_83; proc_entry-> write_proc = (void *) pins_write_proc_83; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[83] = (s32) proc_entry; }
	proc_entry = create_proc_entry("84", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_84; proc_entry-> write_proc = (void *) pins_write_proc_84; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[84] = (s32) proc_entry; }
	proc_entry = create_proc_entry("85", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_85; proc_entry-> write_proc = (void *) pins_write_proc_85; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[85] = (s32) proc_entry; }

	proc_entry = create_proc_entry("all", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_all; proc_write_entry[86] = (s32) proc_entry; }

	proc_entry = create_proc_entry("pwm0", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_pwm0; proc_write_entry[87] = (s32) proc_entry; }
	proc_entry = create_proc_entry("pwm1", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_pwm1; proc_write_entry[88] = (s32) proc_entry; }
	proc_entry = create_proc_entry("pwm2", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_pwm2; proc_write_entry[89] = (s32) proc_entry; }
	proc_entry = create_proc_entry("pwm3", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pins_read_proc_pwm3; proc_write_entry[90] = (s32) proc_entry; }

	return 0;
}

#endif /* CONFIG_PROC_FS */


/**********************************************************************
 end for PROC_FS 
**********************************************************************/


/* 
  set pin state 
  direction =0 - input, =1 - output
*/

static int v2r_set_pin(int pin_number, int direction, int value) {

	if (!(pin_number >= 0 && pin_number <= TOTAL_PINS)) {
		printk("%s: wrong CON number (%d)\n", DEVICE_NAME, pin_number);
		return 1;
	}


	if (ext_bus_pins[pin_number].gpio_descriptor == NA) {
		printk("%s: CON descriptor is not available\n", DEVICE_NAME);
		return 1;
	}

	if (value > 1) {
		printk("%s: wrong value (%d)\n", DEVICE_NAME, value);
		return 1;
	}

	/* restore default pin function */
	if (ext_bus_pins[pin_number].current_af_number > 0) ext_bus_pins[pin_number].current_af_number = 0;

	if (direction)
		gpio_direction_output(ext_bus_pins[pin_number].gpio_number, value);
	else 
		gpio_direction_input(ext_bus_pins[pin_number].gpio_number);

	davinci_cfg_reg(ext_bus_pins[pin_number].gpio_descriptor);

	return 0;
}


/* 
  set pin pwm channel 
*/

static int v2r_pin_set_pwm(unsigned int pin_number) {

	davinci_cfg_reg(ext_bus_pins[pin_number].alt_func_descriptor1);

	ext_bus_pins[pin_number].current_af_number = 1;

	if (ext_bus_pins[pin_number].alt_func_direction1 == INP) {

		gpio_direction_input(ext_bus_pins[pin_number].gpio_number);

	} else if (ext_bus_pins[pin_number].alt_func_direction1 == OUT) {

		gpio_direction_output(ext_bus_pins[pin_number].gpio_number, FALSE);

	}

	printk("%s: CON%d (GPIO%d) set as %s\n", DEVICE_NAME, pin_number, ext_bus_pins[pin_number].gpio_number, ext_bus_pins[pin_number].alt_func_name1);

	return 0;
}


/* 
  set pwm values 
*/

static int v2r_set_pwm(unsigned int pwm_number, unsigned int duty, unsigned int period, unsigned int repeat) {

	// printk("%s: PWM%d set duty %d period %d\n", DEVICE_NAME, pwm_number, duty, period);

	if (pwm_number >= TOTAL_PWM){

		printk("%s: wrong pwm number (%d)\n", DEVICE_NAME, pwm_number);

		return 1;

	}

	/* duty must be smaller or equal then period */
	
	if (duty > period) duty = period;

	pwm[pwm_number]->period  = period;
	pwm[pwm_number]->ph1d  = duty;
	pwm[pwm_number]->repeat  = repeat;

	if (repeat)
	    pwm[pwm_number]->cfg  = (pwm[pwm_number]->cfg & 0xFFFFFD) | 1;
	else
	    pwm[pwm_number]->cfg  = (pwm[pwm_number]->cfg & 0xFFFFFE) | 2;

	pwm[pwm_number]->start  = 1;

	return 0;

}

static int v2r_cfg_pwm(unsigned int pwm_number, unsigned int cfg_and, unsigned int cfg_or) {

	if (pwm_number >= TOTAL_PWM){

	    printk("%s: wrong pwm number (%d)\n", DEVICE_NAME, pwm_number);
	    return 1;

	}

	pwm[pwm_number]->cfg  &= cfg_and;
	pwm[pwm_number]->cfg  |= cfg_or;
	pwm[pwm_number]->start = 1;

	return 0;

}


/*
  clear GPIO group
*/

static int group_clear(void) {

	pinsGroupTableCounter = -1; // just null a group size
	printk("%s: CON group cleared\n", DEVICE_NAME);

	return 0;
}

/*
  add GPIO into group
*/

static int group_add(int pin_number) {

	if (pinsGroupTableCounter >=TOTAL_PINS) {

		printk("%s: CON group is full\n", DEVICE_NAME);
		return 1;

	}

	if (!(pin_number >= 0 && pin_number <= TOTAL_PINS)){

		printk("%s: wrong CON number (%d)\n", DEVICE_NAME, pin_number);
		return 1;

	}

	pinsGroupTableCounter++;
	pinsGroupTable[pinsGroupTableCounter] = ext_bus_pins[pin_number];

	printk("%s: added CON %d into group. New group size is %d\n", DEVICE_NAME, pin_number, pinsGroupTableCounter+1);

	return 0;
}


/*
  add all CON into group
*/

static void group_init(void) {

	unsigned int i;

	for (i=0; i <= TOTAL_PINS; i++) {

		pinsGroupTable[i] = ext_bus_pins[i];

	}

	pinsGroupTableCounter = i-1;

	printk("%s: added all CON's into group\n", DEVICE_NAME);

}


static void pins_parse_binary_command(char * buffer, unsigned int count) {

	int pin_number = 0, direction = 0, value = 0;
	unsigned int duty = 0, period = 0, repeat = 0;

	unsigned int i;

	switch (buffer[0]) {

		case 1:
			/* 
				set CON state
				buffer[1] - GPIO number
				buffer[2] - [bit:0] - direction, [bit:1] - state
			*/

			if (count < 2) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}

			pin_number = buffer[1];
			direction = buffer[2] & 0x01;
			value = (buffer[2] >> 1) & 0x01;

			v2r_set_pin(pin_number, direction, value);

			break;

		case 2:

			/* clear GPIO group */

			group_clear();

			break;

		case 3:
			/* 
				add CON into group
				buffer[1] - CON number
			*/

			if (count < 1) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}

			for (i = 1; i < count; i++)

				group_add(buffer[i]);

			break;

		case 4:

			/* 
				add all CON into group
			*/

			group_init();

			break;


		case 5:
			/* 
				set output mode
				buffer[1] - mode
			*/

			if (count < 1) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}
			
			output_mode = buffer[1] ? 1 : 0 ;

			break;

		case 6:
			/* 
				set CON alt mode (PWM)
				buffer[1] - mode
			*/

			if (count < 1) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}
			
			v2r_pin_set_pwm(buffer[1]);

			break;

		case 7:
			/* 
				set PWM params
				buffer[1] - PWM number
				buffer[2:3] - duty
				buffer[4:5] - period
				buffer[6:7] - repeat (optional)
			*/

			if (count < 6) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}
			
			if ( count > 7 )
			    repeat = buffer[6] + (buffer[7] << 8);
			else
			    repeat = 0; // endless repeat

			duty = (buffer[3] << 8) + buffer[2];
			period = (buffer[5] << 8) + buffer[4];

			v2r_set_pwm(buffer[1], duty, period , repeat);

			break;

		case 8:
			/* 
				set full 32-bit PWM params
				buffer[1] - PWM number
				buffer[2:3:4:5] - duty
				buffer[6:7:8:9] - period
				buffer[10:11:12:13] - repeat (optional)
			*/

			if (count < 10) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}
			
			if ( count > 13 )
			    repeat = buffer[10] + (buffer[11] << 8) + (buffer[12] << 16) + (buffer[13] << 24);
			else
			    repeat = 0; // endless repeat

			duty = buffer[2] + (buffer[3] << 8) + (buffer[4] << 16) + (buffer[5] << 24);
			period = buffer[6] + (buffer[7] << 8) + (buffer[8] << 16) + (buffer[9] << 24);

			v2r_set_pwm(buffer[1], duty, period, repeat);

			break;


		default:
			printk("%s: i don't know this command\n", DEVICE_NAME);
		
	}


}

static void pins_parse_command(char * string) {

	static char *part;
	static char *temp_string;
	int cmd_ok = 0;
	int i;

	unsigned int pin_number = 0, direction = 0, value = 0, pwm_number = 0, duty = 0, period = 0, repeat = 0, cfg_and = 0, cfg_or = 0;

	// last symbol can be a \n symbol, we must clear him
	if (string[strlen(string)-1] == '\n') string[strlen(string)-1] = 0;

	temp_string = kstrdup(string, GFP_KERNEL);

	do {
		part = strsep(&temp_string, " ");
		if (part) {
			
			command_parts[command_parts_counter] = part;
			command_parts_counter++;
		}

	} while (part);


	/* string like "output text" */
	if (!strcmp(command_parts[0], "output")) {

		if (!strcmp(command_parts[1], "text")) 
			output_mode = 0;
		else
		if (!strcmp(command_parts[1], "bin")) 
			output_mode = 1;
		
		cmd_ok = 1;
		goto out;
	}

	/* string like "group clear" */
	if (!strcmp(command_parts[0], "group")) {

		if (!strcmp(command_parts[1], "clear")) 
			group_clear();
		else
		if (!strcmp(command_parts[1], "init")) 
			group_init();
		else
		if (!strcmp(command_parts[1], "add") && !strcmp(command_parts[2], "con")) {

			if (command_parts_counter < 3) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
				return;
			}

			for (i = 3; i < command_parts_counter ; i++ ) {

				kstrtoint(command_parts[i], 10, &pin_number);
				group_add(pin_number);

			}

		}

		cmd_ok = 1;
		goto out;
	}


	/* string like "set con 1 output 1" */
	/* or like "set con 1 pwm1" */
	if (!strcmp(command_parts[0], "set") && !strcmp(command_parts[1], "con")) {

		if (command_parts_counter < 3) {
			printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
			return;
		}

		if (kstrtoint(command_parts[2], 10, &pin_number)) {
			printk("%s: wrong pin number (%s)\n", DEVICE_NAME, command_parts[2]);
			return;
		}

		// only set con state
		if (!strcmp(command_parts[3], "output")) {

			direction = 1;
			kstrtoint(command_parts[4], 10, &value);
			v2r_set_pin(pin_number, direction, value);
			cmd_ok = 1;
			goto out;

		} else if (!strcmp(command_parts[3], "input")) {

			direction = 0;
			v2r_set_pin(pin_number, direction, 0);
			cmd_ok = 1;
			goto out;

		} else

		// or set con alt function
		if (!ext_bus_pins[pin_number].alt_func_name1) {
			printk("%s: wrong CON%d alt function (%s)\n", DEVICE_NAME, pin_number, command_parts[3]);
			return;
		}

		if (!strcmp(command_parts[3], ext_bus_pins[pin_number].alt_func_name1)) {

			if (ext_bus_pins[pin_number].alt_func_descriptor1 == NA) {

				printk("%s: wrong descriptor for CON%d alternative function\n", DEVICE_NAME, pin_number);
				return;

			}

			v2r_pin_set_pwm(pin_number);

			cmd_ok = 1;
			goto out;

		} else {
			printk("%s: wrong CON%d alt function (%s)\n", DEVICE_NAME, pin_number, command_parts[3]);
			return;
		}

	}


	/* string like "set pwm 1 duty 123 period 123" */
	/* changed on string like "set pwm 1 123 567" */
	/* added optional repeat, changed on string like "set pwm 1 123 567 897" */
	if (!strcmp(command_parts[0], "set") && !strcmp(command_parts[1], "pwm")) {

		if (command_parts_counter < 4) {
			printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
			return;
		}

		if (kstrtoint(command_parts[2], 10, &pwm_number)) {
			printk("%s: wrong PWM number (%s)\n", DEVICE_NAME, command_parts[2]);
			return;
		}

		if (kstrtoint(command_parts[3], 10, &duty)) {
			printk("%s: wrong duty (%s)\n", DEVICE_NAME, command_parts[3]);
			return;
		}
		
		if (kstrtoint(command_parts[4], 10, &period)) {
			printk("%s: wrong period (%s)\n", DEVICE_NAME, command_parts[4]);
			return;
		}

		if (command_parts_counter > 5) {

			if (kstrtoint(command_parts[5], 10, &repeat)) {
				printk("%s: wrong repeat (%s)\n", DEVICE_NAME, command_parts[5]);
				return;
			}
		}

		v2r_set_pwm(pwm_number, duty, period, repeat);
				
		cmd_ok = 1;
		goto out;
	}

	/* string like "cfg pwm 1 ffffffff fffffff0" */
	if (!strcmp(command_parts[0], "cfg") && !strcmp(command_parts[1], "pwm")) {

		if (command_parts_counter < 5) {
			printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
			return;
		}

		if (kstrtoint(command_parts[2], 10, &pwm_number)) {
			printk("%s: wrong PWM number (%s)\n", DEVICE_NAME, command_parts[2]);
			return;
		}

		if (kstrtoint(command_parts[3], 16, &cfg_and)) {
			printk("%s: wrong cfg_and (%s)\n", DEVICE_NAME, command_parts[3]);
			return;
		}

		if (kstrtoint(command_parts[4], 16, &cfg_or)) {
			printk("%s: wrong cfg_or (%s)\n", DEVICE_NAME, command_parts[4]);
			return;
		}

		v2r_cfg_pwm(pwm_number, cfg_and, cfg_or);
				
		cmd_ok = 1;
		goto out;
	}




out:

	if (!cmd_ok) {
		printk("%s: i don't know this command (%s)\n", DEVICE_NAME, string);
		return;
	}

	return;

}



int pins_open(struct inode *inode, struct file *filp) {

	unsigned int mj = imajor(inode);
	unsigned int mn = iminor(inode);
	struct pins_dev *dev = NULL;

	if (mj != pins_major || mn < 0 || mn >= 1){
		//One and only device
		printk("%s: no device found with minor=%d and major=%d\n", DEVICE_NAME, mj, mn);
		return -ENODEV; /* No such device */
	}

	/* store a pointer to struct pins_dev here for other methods */

	dev = &pins_devices[mn];
	filp->private_data = dev;
	if (inode->i_cdev != &dev->cdev) {
		printk("%s: open: internal error\n", DEVICE_NAME);
		return -ENODEV; /* No such device */
	}

	/* if opened the 1st time, allocate the buffer */
	if (dev->data == NULL){

		dev->data = (unsigned char*) kzalloc(dev->buffer_size, GFP_KERNEL);

		if (dev->data == NULL){
			printk("%s: open: out of memory\n", DEVICE_NAME);
			return -ENOMEM;
		}

		dev->buffer_data_size = 0;

	}

	return 0;
}

int pins_release(struct inode *inode, struct file *filp) {

	// printk("%s: release device\n", DEVICE_NAME);
	return 0;

}


ssize_t pins_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){

	volatile unsigned long i;
	volatile unsigned int counter;

	volatile char bitcounter;
	volatile char tempByte;
	volatile unsigned int value;

	struct pins_dev *dev = (struct pins_dev *)filp->private_data;
	ssize_t retval = 0;
	if (mutex_lock_killable(&dev->device_mutex)) return -EINTR;

	/* exit if empty group */
	if (pinsGroupTableCounter < 0) {

		v2r_pins_retBuffer[0] = 0;
		counter = 0;
		goto show;
	}

	switch (output_mode) {

		case 0:
			/* text mode */

			//if (count > TOTAL_PINS) count = TOTAL_PINS;
			//if (*f_pos > TOTAL_PINS) goto out;
			//if ((*f_pos + count) > TOTAL_PINS) goto out;

			counter = 0;

			//for (i = *f_pos; i <= (*f_pos + count); i++) {
			for (i = 0; i <= pinsGroupTableCounter; i++) {

				value = gpio_get_value(pinsGroupTable[i].gpio_number);

				/* bers, eat this */
				v2r_pins_retBuffer[counter] = value ? '1' : '0';

				counter++;
			}

			v2r_pins_retBuffer[counter] = '\n';

			counter++;

			break;

		case 1:
			/* binary mode */

			counter = 0;
			bitcounter = 0;
			tempByte = 0;

			for (i = 0; i <= pinsGroupTableCounter; i++) {

				value = gpio_get_value(pinsGroupTable[i].gpio_number)? 1 : 0;

				tempByte |= value << bitcounter;
				bitcounter++;
				if (bitcounter > 7) {
	        	        	v2r_pins_retBuffer[counter] = tempByte;

					tempByte = 0;
					bitcounter = 0;
					counter++;
				}
			}

			if (bitcounter) {
				// if not all byte filled
				v2r_pins_retBuffer[counter] = tempByte;
				counter++;
			}

			break;

	}

show:

	if (copy_to_user(buf, v2r_pins_retBuffer, counter) != 0){

		retval = -EFAULT;
		goto out;

	}

	retval = counter;
out:

  	mutex_unlock(&dev->device_mutex);
	return retval;
}




ssize_t pins_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {

	struct pins_dev *dev = (struct pins_dev *)filp->private_data;
	ssize_t retval = 0;
	char *command = 0;

	if (mutex_lock_killable(&dev->device_mutex)) return -EINTR;

	if (*f_pos !=0) {
		/* Writing in the middle of the file is not allowed */
		printk("%s: writing in the middle (%d) of the file buffer is not allowed\n", DEVICE_NAME, (int)(*f_pos));
		retval = -EINVAL;
        	goto out;
	}

	if (count > BUFFER_SIZE) 
		count = BUFFER_SIZE;

	command = kmalloc(count+1, GFP_KERNEL);

	if (command==NULL)
		return -ENOMEM;

	if (copy_from_user(command, buf, count)) {
		kfree(command);
		retval = -EFAULT;
		goto out;
	}

	command[count] = 0;


	// parse command
	command_parts_counter = 0;
	if (command[0] < 10) {
		pins_parse_binary_command(command, count-1);
	} else {
		pins_parse_command(command);
	}


	// make return to userspace string

	memcpy(dev->data, "ok\n", 3);
	dev->buffer_data_size = 3;

	kfree(command);
	*f_pos = 0;
	retval = count;
	//retval = 0;

out:
  	mutex_unlock(&dev->device_mutex);

	return retval;

}

struct file_operations pins_fops = {
	.owner =    THIS_MODULE,
	.read =     pins_read,
	.write =    pins_write,
	.open =     pins_open,
	.release =  pins_release,
};


static int pins_construct_device(struct pins_dev *dev, int minor, struct class *class) {

	int err = 0;
	dev_t devno = MKDEV(pins_major, minor);
	struct device *device = NULL;

	BUG_ON(dev == NULL || class == NULL);

	/* Memory is to be allocated when the device is opened the first time */
	printk("%s: construct device:%d\n", DEVICE_NAME, minor);

	dev->data = NULL;
	dev->buffer_size = BUFFER_SIZE;
	mutex_init(&dev->device_mutex);
	cdev_init(&dev->cdev, &pins_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);

	if (err){
		printk("%s: error %d while trying to add %d",	DEVICE_NAME, err, minor);
		return err;
	}

	device = device_create(class, NULL /*no parent device*/,  devno, NULL /*no additional data */, DEVICE_NAME);

	if (IS_ERR(device)) {
		err = PTR_ERR(device);
		printk("%s: error %d while trying to create %d", DEVICE_NAME, err, minor);
        	cdev_del(&dev->cdev);
        	return err;
	}

	printk("%s: device is created successfully\n", DEVICE_NAME);

	return 0;

}

static void pins_destroy_device(struct pins_dev *dev, int minor, struct class *class) {

	BUG_ON(dev == NULL || class == NULL);

	printk("%s: destroy device %d\n", DEVICE_NAME, minor);

	device_destroy(class, MKDEV(pins_major, minor));
	cdev_del(&dev->cdev);
	kfree(dev->data);

	printk("%s: device is destroyed successfully\n", DEVICE_NAME);

	return;

}


static void pins_cleanup_module(int devices_to_destroy) {

	int i = 0;

	/* Get rid of character devices (if any exist) */

	printk("%s: cleanup module\n", DEVICE_NAME);

	if (pins_devices) {

		for (i = 0; i < devices_to_destroy; ++i) {

			pins_destroy_device(&pins_devices[i], i, pins_class);

	        }

		kfree(pins_devices);

	}

	if (pins_class) class_destroy(pins_class);
	if (v2r_pins_retBuffer) kfree(v2r_pins_retBuffer);
	if (command_parts) kfree(command_parts);

	/* remove proc_fs files */
	pins_remove_proc_fs();	

	unregister_chrdev_region(MKDEV(pins_major, 0), numberofdevices);

	printk("%s: cleanup completed\n", DEVICE_NAME);

	return;

}

static int __init pins_init_module (void) {

	int err = 0;
	int i = 0;
	int devices_to_destroy = 0;
	dev_t dev = 0;

	printk("Virt2real CON driver module version 0.3\n");

	if (pins_add_proc_fs()) {
		printk(KERN_ERR "%s: can't create PROCFS files\n", DEVICE_NAME);
	}

	if (numberofdevices <= 0) {
		printk("%s: invalid value of numberofdevices: %d\n", DEVICE_NAME, numberofdevices);
		return -EINVAL;
	}

	/* Get a range of minor numbers (starting with 0) to work with */

	err = alloc_chrdev_region(&dev, 0, numberofdevices, DEVICE_NAME);

	if (err < 0) {
		printk("%s: alloc_chrdev_region() failed\n", DEVICE_NAME);
		return err;
	}

	pins_major = MAJOR(dev);

	/* Create device class (before allocation of the array of devices) */

	pins_class = class_create(THIS_MODULE, DEVICE_NAME);

	if (IS_ERR(pins_class)) {

		err = PTR_ERR(pins_class);
		printk("%s: class not created %d\n", DEVICE_NAME, err);
		goto fail;

	}

	/* Allocate the array of devices */

	pins_devices = (struct pins_dev *) kzalloc( numberofdevices * sizeof(struct pins_dev), GFP_KERNEL);

	if (pins_devices == NULL) {

		err = -ENOMEM;
		printk("%s: devices not allocated %d\n", DEVICE_NAME, err);
		goto fail;

	}

	/* Construct devices */
	for (i = 0; i < numberofdevices; ++i) {

		err = pins_construct_device(&pins_devices[i], i, pins_class);
		if (err) {

			printk("%s: device is not created\n", DEVICE_NAME);
			devices_to_destroy = i;
			goto fail;

        	}
	}

	// init PWMs (Thank's, Cap!)
	v2r_init_pwm();

	v2r_pins_retBuffer = kmalloc(RETBUFFER_SIZE + 1, GFP_KERNEL);
	if (v2r_pins_retBuffer == NULL) return -ENOMEM;


	command_parts = kmalloc(128, GFP_KERNEL);
	if (command_parts == NULL) return -ENOMEM;


	/* fill gpioGroupTable width default values - all GPIOs */
	group_init();

	return 0; /* success */

fail:

	pins_cleanup_module(devices_to_destroy);
	return err;

}

static void __exit pins_exit_module(void){

	pins_cleanup_module(numberofdevices);
	
	return;

}

module_init(pins_init_module);
module_exit(pins_exit_module);

MODULE_DESCRIPTION("Virt2real CON driver module version 0.3");
MODULE_AUTHOR("Alexandr Shadrin");
MODULE_AUTHOR("Gol (gol@g0l.ru)");
MODULE_LICENSE("GPL2");
