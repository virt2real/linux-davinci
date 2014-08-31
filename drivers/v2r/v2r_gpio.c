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

#define TRUE  1
#define FALSE 0
#define NA -1

#define NUMBEROFDEVICES 1
#define BUFFER_SIZE 8192
#define RETBUFFER_SIZE 8192
#define DEVICE_NAME "v2r_gpio"

#define TOTAL_GPIO 103
#define TOTAL_PWCTR 4

static int v2r_set_pwctr(int number, int value);
static int v2r_get_pwctr(int number);


/* The structure to represent 'v2r_gpio' devices.
 *  data - data buffer;
 *  buffer_size - size of the data buffer;
 *  buffer_data_size - amount of data actually present in the buffer
 *  device_mutex - a mutex to protect the fields of this structure;
 *  cdev - character device structure.
*/

struct gpio_dev {
	unsigned char *data;
	unsigned long buffer_size;
	unsigned long buffer_data_size;
	struct mutex device_mutex;
	struct cdev cdev;
};


typedef struct  {
	int number;
	const char* gpio_name;
	int gpio_descriptor;
	int gpio_number;
} gpio;


static gpio gpiotable[TOTAL_GPIO+1] = {
{ 0, "GPIO0",	DM365_GPIO0, 0},
{ 1, "GPIO1",	DM365_GPIO1, 1},
{ 2, "GPIO2",	DM365_GPIO2, 2},
{ 3, "GPIO3",	DM365_GPIO3, 3},
{ 4, "GPIO4",	DM365_GPIO4, 4},
{ 5, "GPIO5",	DM365_GPIO5, 5},
{ 6, "GPIO6",	DM365_GPIO6, 6},
{ 7, "GPIO7",	DM365_GPIO7, 7},
{ 8, "GPIO8",	DM365_GPIO8, 8},
{ 9, "GPIO9",	DM365_GPIO9, 9},
{ 10, "GPIO10",	DM365_GPIO10, 10},
{ 11, "GPIO11",	DM365_GPIO11, 11},
{ 12, "GPIO12",	DM365_GPIO12, 12},
{ 13, "GPIO13",	DM365_GPIO13, 13},
{ 14, "GPIO14",	DM365_GPIO14, 14},
{ 15, "GPIO15",	DM365_GPIO15, 15},
{ 16, "GPIO16",	DM365_GPIO16, 16},
{ 17, "GPIO17",	DM365_GPIO17, 17},
{ 18, "GPIO18",	DM365_GPIO18, 18},
{ 19, "GPIO19",	DM365_GPIO19, 19},
{ 20, "GPIO20",	DM365_GPIO20, 20},
{ 21, "GPIO21",	DM365_GPIO21, 21},
{ 22, "GPIO22",	DM365_GPIO22, 22},
{ 23, "GPIO23",	DM365_GPIO23, 23},
{ 24, "GPIO24",	DM365_GPIO24, 24},
{ 25, "GPIO25",	DM365_GPIO25, 25},
{ 26, "GPIO26",	DM365_GPIO26, 26},
{ 27, "GPIO27",	DM365_GPIO27, 27},
{ 28, "GPIO28",	DM365_GPIO28, 28},
{ 29, "GPIO29",	DM365_GPIO29, 29},
{ 30, "GPIO30",	DM365_GPIO30, 30},
{ 31, "GPIO31",	DM365_GPIO31, 31},
{ 32, "GPIO32",	DM365_GPIO32, 32},
{ 33, "GPIO33",	DM365_GPIO33, 33},
{ 34, "GPIO34",	DM365_GPIO34, 34},
{ 35, "GPIO35",	DM365_GPIO35, 35},
{ 36, "GPIO36",	DM365_GPIO36, 36},
{ 37, "GPIO37",	DM365_GPIO37, 37},
{ 38, "GPIO38",	DM365_GPIO38, 38},
{ 39, "GPIO39",	DM365_GPIO39, 39},
{ 40, "GPIO40",	DM365_GPIO40, 40},
{ 41, "GPIO41",	DM365_GPIO41, 41},
{ 42, "GPIO42",	DM365_GPIO42, 42},
{ 43, "GPIO43",	DM365_GPIO43, 43},
{ 44, "GPIO44",	DM365_GPIO44, 44},
{ 45, "GPIO45",	DM365_GPIO45, 45},
{ 46, "GPIO46",	DM365_GPIO46, 46},
{ 47, "GPIO47",	DM365_GPIO47, 47},
{ 48, "GPIO48",	DM365_GPIO48, 48},
{ 49, "GPIO49",	DM365_GPIO49, 49},
{ 50, "GPIO50",	DM365_GPIO50, 50},
{ 51, "GPIO51",	DM365_GPIO51, 51},
{ 52, "GPIO52",	DM365_GPIO52, 52},
{ 53, "GPIO53",	DM365_GPIO53, 53},
{ 54, "GPIO54",	DM365_GPIO54, 54},
{ 55, "GPIO55",	DM365_GPIO55, 55},
{ 56, "GPIO56",	DM365_GPIO56, 56},
{ 57, "GPIO57",	DM365_GPIO57, 57},
{ 58, "GPIO58",	DM365_GPIO58, 58},
{ 59, "GPIO59",	DM365_GPIO59, 59},
{ 60, "GPIO60",	DM365_GPIO60, 60},
{ 61, "GPIO61",	DM365_GPIO61, 61},
{ 62, "GPIO62",	DM365_GPIO62, 62},
{ 63, "GPIO63",	DM365_GPIO63, 63},
{ 64, "GPIO64",	DM365_GPIO64, 64},
{ 65, "GPIO65",	DM365_GPIO65, 65},
{ 66, "GPIO66",	DM365_GPIO66, 66},
{ 67, "GPIO67",	DM365_GPIO67, 67},
{ 68, "GPIO68",	DM365_GPIO68, 68},
{ 69, "GPIO69",	DM365_GPIO69, 69},
{ 70, "GPIO70",	DM365_GPIO70, 70},
{ 71, "GPIO71",	DM365_GPIO71, 71},
{ 72, "GPIO72",	DM365_GPIO72, 72},
{ 73, "GPIO73",	DM365_GPIO73, 73},
{ 74, "GPIO74",	DM365_GPIO74, 74},
{ 75, "GPIO75",	DM365_GPIO75, 75},
{ 76, "GPIO76",	DM365_GPIO76, 76},
{ 77, "GPIO77",	DM365_GPIO77, 77},
{ 78, "GPIO78",	DM365_GPIO78, 78},
{ 79, "GPIO79",	DM365_GPIO79, 79},
{ 80, "GPIO80",	DM365_GPIO80, 80},
{ 81, "GPIO81",	DM365_GPIO81, 81},
{ 82, "GPIO82",	DM365_GPIO82, 82},
{ 83, "GPIO83",	DM365_GPIO83, 83},
{ 84, "GPIO84",	DM365_GPIO84, 84},
{ 85, "GPIO85",	DM365_GPIO85, 85},
{ 86, "GPIO86",	DM365_GPIO86, 86},
{ 87, "GPIO87",	DM365_GPIO87, 87},
{ 88, "GPIO88",	DM365_GPIO88, 88},
{ 89, "GPIO89",	DM365_GPIO89, 89},
{ 90, "GPIO90",	DM365_GPIO90, 90},
{ 91, "GPIO91",	DM365_GPIO91, 91},
{ 92, "GPIO92",	DM365_GPIO92, 92},
{ 93, "GPIO93",	DM365_GPIO93, 93},
{ 94, "GPIO94",	DM365_GPIO94, 94},
{ 95, "GPIO95",	DM365_GPIO95, 95},
{ 96, "GPIO96",	DM365_GPIO96, 96},
{ 97, "GPIO97",	DM365_GPIO97, 97},
{ 98, "GPIO98",	DM365_GPIO98, 98},
{ 99, "GPIO99",	DM365_GPIO99, 99},
{ 100, "GPIO100", DM365_GPIO100, 100},
{ 101, "GPIO101", DM365_GPIO101, 101},
{ 102, "GPIO102", DM365_GPIO102, 102},
{ 103, "GPIO103", DM365_GPIO103, 103},
};

/* array and counter for GPIO group */
static gpio gpioGroupTable[TOTAL_GPIO+1];
static short gpioGroupTableCounter = 0;

static unsigned int gpio_major = 0;
static struct gpio_dev *gpio_devices = NULL;
static struct class *gpio_class = NULL;
static int numberofdevices = NUMBEROFDEVICES;
static char * v2r_gpio_retBuffer;

static char ** command_parts;
static int command_parts_counter;

static int output_mode = 0; // text mode default


/**********************************************************************
 for PROC_FS 
**********************************************************************/

#ifndef CONFIG_PROC_FS

static int gpio_add_proc_fs(void) {
	return 0;
}

static int gpio_remove_proc_fs(void) {
	return 0;
}

#else

static struct proc_dir_entry *proc_parent;
static struct proc_dir_entry *proc_entry;
static s32 proc_write_entry[TOTAL_GPIO + TOTAL_PWCTR + 2];

static int gpio_read_proc (int gpio_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	int len=0;
	int value = 0;

	value = gpio_get_value(gpiotable[gpio_number].gpio_number)? 1 : 0;

	len = sprintf(buf, "%d", value);
	
	return len;

}

static int gpio_write_proc (int gpio_number, struct file *file, const char *buf, int count, void *data ) {

	static int value = 0;
	static char proc_data[2];

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

	gpio_direction_output(gpiotable[gpio_number].gpio_number, value);
	davinci_cfg_reg(gpiotable[gpio_number].gpio_descriptor);

	return count;
}

static int pwctr_read_proc (int pwctr_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	int len=0;
	int value = 0;

	value = v2r_get_pwctr(pwctr_number) ? 1 : 0;

	len = sprintf(buf, "%d", value);
	
	return len;

}

static int pwctr_write_proc (int pwctr_number, struct file *file, const char *buf, int count, void *data ) {

	static int value = 0;
	static char proc_data[2];

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

	v2r_set_pwctr(pwctr_number, value);

	return count;
}


/* *i'd line to use array init, but i don't know how get file id from unified functions */

static int gpio_write_proc_0 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (0, file, buf, count, data); }
static int gpio_read_proc_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (0, buf, start, offset, count, eof, data); }
static int gpio_write_proc_1 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (1, file, buf, count, data); }
static int gpio_read_proc_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (1, buf, start, offset, count, eof, data); }
static int gpio_write_proc_2 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (2, file, buf, count, data); }
static int gpio_read_proc_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (2, buf, start, offset, count, eof, data); }
static int gpio_write_proc_3 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (3, file, buf, count, data); }
static int gpio_read_proc_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (3, buf, start, offset, count, eof, data); }
static int gpio_write_proc_4 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (4, file, buf, count, data); }
static int gpio_read_proc_4 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (4, buf, start, offset, count, eof, data); }
static int gpio_write_proc_5 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (5, file, buf, count, data); }
static int gpio_read_proc_5 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (5, buf, start, offset, count, eof, data); }
static int gpio_write_proc_6 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (6, file, buf, count, data); }
static int gpio_read_proc_6 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (6, buf, start, offset, count, eof, data); }
static int gpio_write_proc_7 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (7, file, buf, count, data); }
static int gpio_read_proc_7 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (7, buf, start, offset, count, eof, data); }
static int gpio_write_proc_8 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (8, file, buf, count, data); }
static int gpio_read_proc_8 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (8, buf, start, offset, count, eof, data); }
static int gpio_write_proc_9 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (9, file, buf, count, data); }
static int gpio_read_proc_9 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (9, buf, start, offset, count, eof, data); }
static int gpio_write_proc_10 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (10, file, buf, count, data); }
static int gpio_read_proc_10 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (10, buf, start, offset, count, eof, data); }
static int gpio_write_proc_11 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (11, file, buf, count, data); }
static int gpio_read_proc_11 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (11, buf, start, offset, count, eof, data); }
static int gpio_write_proc_12 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (12, file, buf, count, data); }
static int gpio_read_proc_12 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (12, buf, start, offset, count, eof, data); }
static int gpio_write_proc_13 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (13, file, buf, count, data); }
static int gpio_read_proc_13 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (13, buf, start, offset, count, eof, data); }
static int gpio_write_proc_14 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (14, file, buf, count, data); }
static int gpio_read_proc_14 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (14, buf, start, offset, count, eof, data); }
static int gpio_write_proc_15 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (15, file, buf, count, data); }
static int gpio_read_proc_15 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (15, buf, start, offset, count, eof, data); }
static int gpio_write_proc_16 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (16, file, buf, count, data); }
static int gpio_read_proc_16 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (16, buf, start, offset, count, eof, data); }
static int gpio_write_proc_17 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (17, file, buf, count, data); }
static int gpio_read_proc_17 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (17, buf, start, offset, count, eof, data); }
static int gpio_write_proc_18 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (18, file, buf, count, data); }
static int gpio_read_proc_18 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (18, buf, start, offset, count, eof, data); }
static int gpio_write_proc_19 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (19, file, buf, count, data); }
static int gpio_read_proc_19 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (19, buf, start, offset, count, eof, data); }
static int gpio_write_proc_20 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (20, file, buf, count, data); }
static int gpio_read_proc_20 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (20, buf, start, offset, count, eof, data); }
static int gpio_write_proc_21 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (21, file, buf, count, data); }
static int gpio_read_proc_21 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (21, buf, start, offset, count, eof, data); }
static int gpio_write_proc_22 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (22, file, buf, count, data); }
static int gpio_read_proc_22 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (22, buf, start, offset, count, eof, data); }
static int gpio_write_proc_23 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (23, file, buf, count, data); }
static int gpio_read_proc_23 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (23, buf, start, offset, count, eof, data); }
static int gpio_write_proc_24 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (24, file, buf, count, data); }
static int gpio_read_proc_24 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (24, buf, start, offset, count, eof, data); }
static int gpio_write_proc_25 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (25, file, buf, count, data); }
static int gpio_read_proc_25 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (25, buf, start, offset, count, eof, data); }
static int gpio_write_proc_26 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (26, file, buf, count, data); }
static int gpio_read_proc_26 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (26, buf, start, offset, count, eof, data); }
static int gpio_write_proc_27 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (27, file, buf, count, data); }
static int gpio_read_proc_27 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (27, buf, start, offset, count, eof, data); }
static int gpio_write_proc_28 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (28, file, buf, count, data); }
static int gpio_read_proc_28 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (28, buf, start, offset, count, eof, data); }
static int gpio_write_proc_29 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (29, file, buf, count, data); }
static int gpio_read_proc_29 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (29, buf, start, offset, count, eof, data); }
static int gpio_write_proc_30 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (30, file, buf, count, data); }
static int gpio_read_proc_30 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (30, buf, start, offset, count, eof, data); }
static int gpio_write_proc_31 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (31, file, buf, count, data); }
static int gpio_read_proc_31 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (31, buf, start, offset, count, eof, data); }
static int gpio_write_proc_32 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (32, file, buf, count, data); }
static int gpio_read_proc_32 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (32, buf, start, offset, count, eof, data); }
static int gpio_write_proc_33 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (33, file, buf, count, data); }
static int gpio_read_proc_33 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (33, buf, start, offset, count, eof, data); }
static int gpio_write_proc_34 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (34, file, buf, count, data); }
static int gpio_read_proc_34 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (34, buf, start, offset, count, eof, data); }
static int gpio_write_proc_35 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (35, file, buf, count, data); }
static int gpio_read_proc_35 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (35, buf, start, offset, count, eof, data); }
static int gpio_write_proc_36 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (36, file, buf, count, data); }
static int gpio_read_proc_36 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (36, buf, start, offset, count, eof, data); }
static int gpio_write_proc_37 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (37, file, buf, count, data); }
static int gpio_read_proc_37 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (37, buf, start, offset, count, eof, data); }
static int gpio_write_proc_38 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (38, file, buf, count, data); }
static int gpio_read_proc_38 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (38, buf, start, offset, count, eof, data); }
static int gpio_write_proc_39 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (39, file, buf, count, data); }
static int gpio_read_proc_39 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (39, buf, start, offset, count, eof, data); }
static int gpio_write_proc_40 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (40, file, buf, count, data); }
static int gpio_read_proc_40 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (40, buf, start, offset, count, eof, data); }
static int gpio_write_proc_41 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (41, file, buf, count, data); }
static int gpio_read_proc_41 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (41, buf, start, offset, count, eof, data); }
static int gpio_write_proc_42 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (42, file, buf, count, data); }
static int gpio_read_proc_42 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (42, buf, start, offset, count, eof, data); }
static int gpio_write_proc_43 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (43, file, buf, count, data); }
static int gpio_read_proc_43 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (43, buf, start, offset, count, eof, data); }
static int gpio_write_proc_44 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (44, file, buf, count, data); }
static int gpio_read_proc_44 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (44, buf, start, offset, count, eof, data); }
static int gpio_write_proc_45 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (45, file, buf, count, data); }
static int gpio_read_proc_45 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (45, buf, start, offset, count, eof, data); }
static int gpio_write_proc_46 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (46, file, buf, count, data); }
static int gpio_read_proc_46 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (46, buf, start, offset, count, eof, data); }
static int gpio_write_proc_47 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (47, file, buf, count, data); }
static int gpio_read_proc_47 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (47, buf, start, offset, count, eof, data); }
static int gpio_write_proc_48 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (48, file, buf, count, data); }
static int gpio_read_proc_48 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (48, buf, start, offset, count, eof, data); }
static int gpio_write_proc_49 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (49, file, buf, count, data); }
static int gpio_read_proc_49 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (49, buf, start, offset, count, eof, data); }
static int gpio_write_proc_50 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (50, file, buf, count, data); }
static int gpio_read_proc_50 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (50, buf, start, offset, count, eof, data); }
static int gpio_write_proc_51 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (51, file, buf, count, data); }
static int gpio_read_proc_51 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (51, buf, start, offset, count, eof, data); }
static int gpio_write_proc_52 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (52, file, buf, count, data); }
static int gpio_read_proc_52 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (52, buf, start, offset, count, eof, data); }
static int gpio_write_proc_53 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (53, file, buf, count, data); }
static int gpio_read_proc_53 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (53, buf, start, offset, count, eof, data); }
static int gpio_write_proc_54 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (54, file, buf, count, data); }
static int gpio_read_proc_54 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (54, buf, start, offset, count, eof, data); }
static int gpio_write_proc_55 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (55, file, buf, count, data); }
static int gpio_read_proc_55 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (55, buf, start, offset, count, eof, data); }
static int gpio_write_proc_56 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (56, file, buf, count, data); }
static int gpio_read_proc_56 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (56, buf, start, offset, count, eof, data); }
static int gpio_write_proc_57 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (57, file, buf, count, data); }
static int gpio_read_proc_57 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (57, buf, start, offset, count, eof, data); }
static int gpio_write_proc_58 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (58, file, buf, count, data); }
static int gpio_read_proc_58 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (58, buf, start, offset, count, eof, data); }
static int gpio_write_proc_59 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (59, file, buf, count, data); }
static int gpio_read_proc_59 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (59, buf, start, offset, count, eof, data); }
static int gpio_write_proc_60 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (60, file, buf, count, data); }
static int gpio_read_proc_60 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (60, buf, start, offset, count, eof, data); }
static int gpio_write_proc_61 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (61, file, buf, count, data); }
static int gpio_read_proc_61 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (61, buf, start, offset, count, eof, data); }
static int gpio_write_proc_62 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (62, file, buf, count, data); }
static int gpio_read_proc_62 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (62, buf, start, offset, count, eof, data); }
static int gpio_write_proc_63 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (63, file, buf, count, data); }
static int gpio_read_proc_63 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (63, buf, start, offset, count, eof, data); }
static int gpio_write_proc_64 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (64, file, buf, count, data); }
static int gpio_read_proc_64 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (64, buf, start, offset, count, eof, data); }
static int gpio_write_proc_65 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (65, file, buf, count, data); }
static int gpio_read_proc_65 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (65, buf, start, offset, count, eof, data); }
static int gpio_write_proc_66 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (66, file, buf, count, data); }
static int gpio_read_proc_66 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (66, buf, start, offset, count, eof, data); }
static int gpio_write_proc_67 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (67, file, buf, count, data); }
static int gpio_read_proc_67 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (67, buf, start, offset, count, eof, data); }
static int gpio_write_proc_68 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (68, file, buf, count, data); }
static int gpio_read_proc_68 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (68, buf, start, offset, count, eof, data); }
static int gpio_write_proc_69 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (69, file, buf, count, data); }
static int gpio_read_proc_69 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (69, buf, start, offset, count, eof, data); }
static int gpio_write_proc_70 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (70, file, buf, count, data); }
static int gpio_read_proc_70 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (70, buf, start, offset, count, eof, data); }
static int gpio_write_proc_71 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (71, file, buf, count, data); }
static int gpio_read_proc_71 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (71, buf, start, offset, count, eof, data); }
static int gpio_write_proc_72 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (72, file, buf, count, data); }
static int gpio_read_proc_72 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (72, buf, start, offset, count, eof, data); }
static int gpio_write_proc_73 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (73, file, buf, count, data); }
static int gpio_read_proc_73 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (73, buf, start, offset, count, eof, data); }
static int gpio_write_proc_74 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (74, file, buf, count, data); }
static int gpio_read_proc_74 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (74, buf, start, offset, count, eof, data); }
static int gpio_write_proc_75 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (75, file, buf, count, data); }
static int gpio_read_proc_75 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (75, buf, start, offset, count, eof, data); }
static int gpio_write_proc_76 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (76, file, buf, count, data); }
static int gpio_read_proc_76 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (76, buf, start, offset, count, eof, data); }
static int gpio_write_proc_77 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (77, file, buf, count, data); }
static int gpio_read_proc_77 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (77, buf, start, offset, count, eof, data); }
static int gpio_write_proc_78 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (78, file, buf, count, data); }
static int gpio_read_proc_78 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (78, buf, start, offset, count, eof, data); }
static int gpio_write_proc_79 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (79, file, buf, count, data); }
static int gpio_read_proc_79 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (79, buf, start, offset, count, eof, data); }
static int gpio_write_proc_80 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (80, file, buf, count, data); }
static int gpio_read_proc_80 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (80, buf, start, offset, count, eof, data); }
static int gpio_write_proc_81 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (81, file, buf, count, data); }
static int gpio_read_proc_81 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (81, buf, start, offset, count, eof, data); }
static int gpio_write_proc_82 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (82, file, buf, count, data); }
static int gpio_read_proc_82 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (82, buf, start, offset, count, eof, data); }
static int gpio_write_proc_83 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (83, file, buf, count, data); }
static int gpio_read_proc_83 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (83, buf, start, offset, count, eof, data); }
static int gpio_write_proc_84 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (84, file, buf, count, data); }
static int gpio_read_proc_84 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (84, buf, start, offset, count, eof, data); }
static int gpio_write_proc_85 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (85, file, buf, count, data); }
static int gpio_read_proc_85 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (85, buf, start, offset, count, eof, data); }
static int gpio_write_proc_86 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (86, file, buf, count, data); }
static int gpio_read_proc_86 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (86, buf, start, offset, count, eof, data); }
static int gpio_write_proc_87 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (87, file, buf, count, data); }
static int gpio_read_proc_87 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (87, buf, start, offset, count, eof, data); }
static int gpio_write_proc_88 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (88, file, buf, count, data); }
static int gpio_read_proc_88 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (88, buf, start, offset, count, eof, data); }
static int gpio_write_proc_89 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (89, file, buf, count, data); }
static int gpio_read_proc_89 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (89, buf, start, offset, count, eof, data); }
static int gpio_write_proc_90 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (90, file, buf, count, data); }
static int gpio_read_proc_90 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (90, buf, start, offset, count, eof, data); }
static int gpio_write_proc_91 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (91, file, buf, count, data); }
static int gpio_read_proc_91 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (91, buf, start, offset, count, eof, data); }
static int gpio_write_proc_92 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (92, file, buf, count, data); }
static int gpio_read_proc_92 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (92, buf, start, offset, count, eof, data); }
static int gpio_write_proc_93 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (93, file, buf, count, data); }
static int gpio_read_proc_93 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (93, buf, start, offset, count, eof, data); }
static int gpio_write_proc_94 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (94, file, buf, count, data); }
static int gpio_read_proc_94 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (94, buf, start, offset, count, eof, data); }
static int gpio_write_proc_95 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (95, file, buf, count, data); }
static int gpio_read_proc_95 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (95, buf, start, offset, count, eof, data); }
static int gpio_write_proc_96 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (96, file, buf, count, data); }
static int gpio_read_proc_96 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (96, buf, start, offset, count, eof, data); }
static int gpio_write_proc_97 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (97, file, buf, count, data); }
static int gpio_read_proc_97 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (97, buf, start, offset, count, eof, data); }
static int gpio_write_proc_98 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (98, file, buf, count, data); }
static int gpio_read_proc_98 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (98, buf, start, offset, count, eof, data); }
static int gpio_write_proc_99 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (99, file, buf, count, data); }
static int gpio_read_proc_99 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (99, buf, start, offset, count, eof, data); }
static int gpio_write_proc_100 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (100, file, buf, count, data); }
static int gpio_read_proc_100 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (100, buf, start, offset, count, eof, data); }
static int gpio_write_proc_101 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (101, file, buf, count, data); }
static int gpio_read_proc_101 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (101, buf, start, offset, count, eof, data); }
static int gpio_write_proc_102 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (102, file, buf, count, data); }
static int gpio_read_proc_102 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (102, buf, start, offset, count, eof, data); }
static int gpio_write_proc_103 (struct file *file, const char *buf, int count, void *data ) { return gpio_write_proc (103, file, buf, count, data); }
static int gpio_read_proc_103 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return gpio_read_proc (103, buf, start, offset, count, eof, data); }

static int pwctr_write_proc_0 (struct file *file, const char *buf, int count, void *data ) { return pwctr_write_proc (0, file, buf, count, data); }
static int pwctr_read_proc_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pwctr_read_proc (0, buf, start, offset, count, eof, data); }
static int pwctr_write_proc_1 (struct file *file, const char *buf, int count, void *data ) { return pwctr_write_proc (1, file, buf, count, data); }
static int pwctr_read_proc_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pwctr_read_proc (1, buf, start, offset, count, eof, data); }
static int pwctr_write_proc_2 (struct file *file, const char *buf, int count, void *data ) { return pwctr_write_proc (2, file, buf, count, data); }
static int pwctr_read_proc_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pwctr_read_proc (2, buf, start, offset, count, eof, data); }
static int pwctr_write_proc_3 (struct file *file, const char *buf, int count, void *data ) { return pwctr_write_proc (3, file, buf, count, data); }
static int pwctr_read_proc_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return pwctr_read_proc (3, buf, start, offset, count, eof, data); }


static int gpio_read_proc_all (char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	char buffer[TOTAL_GPIO + 1];
	int i;
	int len=0;
	int value = 0;

	for (i = 0; i <= TOTAL_GPIO; i++) {

		value = gpio_get_value(gpiotable[i].gpio_number);

		/* bers, eat this */
		buffer[i] = value ? '1' : '0';

	}

	len = sprintf(buf, "%s", buffer);
	
	return len;

}


static int gpio_remove_proc_fs(void) {

	int i;
	char fn[10];

	for (i = 0; i <= TOTAL_GPIO + TOTAL_PWCTR + 1; i++) {

		if (proc_write_entry[i]) { 
			sprintf(fn, "%d", i);
			remove_proc_entry(fn, proc_parent);
		}

	}

	/* remove proc_fs directory */
	remove_proc_entry("v2r_gpio",NULL);

	return 0;
}

static int gpio_add_proc_fs(void) {

	proc_parent = proc_mkdir("v2r_gpio", NULL);

	if (!proc_parent) {
		printk("%s: error creating proc entry (/proc/v2r_gpio)\n", DEVICE_NAME);
		return 1;
	}

	proc_entry = create_proc_entry("0", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_0; proc_entry-> write_proc = (void *) gpio_write_proc_0; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[0] = (s32) proc_entry; }
	proc_entry = create_proc_entry("1", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_1; proc_entry-> write_proc = (void *) gpio_write_proc_1; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[1] = (s32) proc_entry; }
	proc_entry = create_proc_entry("2", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_2; proc_entry-> write_proc = (void *) gpio_write_proc_2; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[2] = (s32) proc_entry; }
	proc_entry = create_proc_entry("3", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_3; proc_entry-> write_proc = (void *) gpio_write_proc_3; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[3] = (s32) proc_entry; }
	proc_entry = create_proc_entry("4", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_4; proc_entry-> write_proc = (void *) gpio_write_proc_4; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[4] = (s32) proc_entry; }
	proc_entry = create_proc_entry("5", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_5; proc_entry-> write_proc = (void *) gpio_write_proc_5; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[5] = (s32) proc_entry; }
	proc_entry = create_proc_entry("6", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_6; proc_entry-> write_proc = (void *) gpio_write_proc_6; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[6] = (s32) proc_entry; }
	proc_entry = create_proc_entry("7", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_7; proc_entry-> write_proc = (void *) gpio_write_proc_7; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[7] = (s32) proc_entry; }
	proc_entry = create_proc_entry("8", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_8; proc_entry-> write_proc = (void *) gpio_write_proc_8; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[8] = (s32) proc_entry; }
	proc_entry = create_proc_entry("9", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_9; proc_entry-> write_proc = (void *) gpio_write_proc_9; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[9] = (s32) proc_entry; }
	proc_entry = create_proc_entry("10", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_10; proc_entry-> write_proc = (void *) gpio_write_proc_10; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[10] = (s32) proc_entry; }
	proc_entry = create_proc_entry("11", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_11; proc_entry-> write_proc = (void *) gpio_write_proc_11; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[11] = (s32) proc_entry; }
	proc_entry = create_proc_entry("12", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_12; proc_entry-> write_proc = (void *) gpio_write_proc_12; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[12] = (s32) proc_entry; }
	proc_entry = create_proc_entry("13", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_13; proc_entry-> write_proc = (void *) gpio_write_proc_13; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[13] = (s32) proc_entry; }
	proc_entry = create_proc_entry("14", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_14; proc_entry-> write_proc = (void *) gpio_write_proc_14; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[14] = (s32) proc_entry; }
	proc_entry = create_proc_entry("15", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_15; proc_entry-> write_proc = (void *) gpio_write_proc_15; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[15] = (s32) proc_entry; }
	proc_entry = create_proc_entry("16", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_16; proc_entry-> write_proc = (void *) gpio_write_proc_16; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[16] = (s32) proc_entry; }
	proc_entry = create_proc_entry("17", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_17; proc_entry-> write_proc = (void *) gpio_write_proc_17; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[17] = (s32) proc_entry; }
	proc_entry = create_proc_entry("18", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_18; proc_entry-> write_proc = (void *) gpio_write_proc_18; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[18] = (s32) proc_entry; }
	proc_entry = create_proc_entry("19", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_19; proc_entry-> write_proc = (void *) gpio_write_proc_19; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[19] = (s32) proc_entry; }
	proc_entry = create_proc_entry("20", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_20; proc_entry-> write_proc = (void *) gpio_write_proc_20; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[20] = (s32) proc_entry; }
	proc_entry = create_proc_entry("21", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_21; proc_entry-> write_proc = (void *) gpio_write_proc_21; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[21] = (s32) proc_entry; }
	proc_entry = create_proc_entry("22", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_22; proc_entry-> write_proc = (void *) gpio_write_proc_22; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[22] = (s32) proc_entry; }
	proc_entry = create_proc_entry("23", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_23; proc_entry-> write_proc = (void *) gpio_write_proc_23; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[23] = (s32) proc_entry; }
	proc_entry = create_proc_entry("24", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_24; proc_entry-> write_proc = (void *) gpio_write_proc_24; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[24] = (s32) proc_entry; }
	proc_entry = create_proc_entry("25", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_25; proc_entry-> write_proc = (void *) gpio_write_proc_25; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[25] = (s32) proc_entry; }
	proc_entry = create_proc_entry("26", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_26; proc_entry-> write_proc = (void *) gpio_write_proc_26; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[26] = (s32) proc_entry; }
	proc_entry = create_proc_entry("27", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_27; proc_entry-> write_proc = (void *) gpio_write_proc_27; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[27] = (s32) proc_entry; }
	proc_entry = create_proc_entry("28", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_28; proc_entry-> write_proc = (void *) gpio_write_proc_28; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[28] = (s32) proc_entry; }
	proc_entry = create_proc_entry("29", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_29; proc_entry-> write_proc = (void *) gpio_write_proc_29; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[29] = (s32) proc_entry; }
	proc_entry = create_proc_entry("30", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_30; proc_entry-> write_proc = (void *) gpio_write_proc_30; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[30] = (s32) proc_entry; }
	proc_entry = create_proc_entry("31", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_31; proc_entry-> write_proc = (void *) gpio_write_proc_31; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[31] = (s32) proc_entry; }
	proc_entry = create_proc_entry("32", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_32; proc_entry-> write_proc = (void *) gpio_write_proc_32; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[32] = (s32) proc_entry; }
	proc_entry = create_proc_entry("33", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_33; proc_entry-> write_proc = (void *) gpio_write_proc_33; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[33] = (s32) proc_entry; }
	proc_entry = create_proc_entry("34", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_34; proc_entry-> write_proc = (void *) gpio_write_proc_34; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[34] = (s32) proc_entry; }
	proc_entry = create_proc_entry("35", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_35; proc_entry-> write_proc = (void *) gpio_write_proc_35; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[35] = (s32) proc_entry; }
	proc_entry = create_proc_entry("36", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_36; proc_entry-> write_proc = (void *) gpio_write_proc_36; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[36] = (s32) proc_entry; }
	proc_entry = create_proc_entry("37", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_37; proc_entry-> write_proc = (void *) gpio_write_proc_37; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[37] = (s32) proc_entry; }
	proc_entry = create_proc_entry("38", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_38; proc_entry-> write_proc = (void *) gpio_write_proc_38; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[38] = (s32) proc_entry; }
	proc_entry = create_proc_entry("39", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_39; proc_entry-> write_proc = (void *) gpio_write_proc_39; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[39] = (s32) proc_entry; }
	proc_entry = create_proc_entry("40", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_40; proc_entry-> write_proc = (void *) gpio_write_proc_40; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[40] = (s32) proc_entry; }
	proc_entry = create_proc_entry("41", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_41; proc_entry-> write_proc = (void *) gpio_write_proc_41; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[41] = (s32) proc_entry; }
	proc_entry = create_proc_entry("42", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_42; proc_entry-> write_proc = (void *) gpio_write_proc_42; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[42] = (s32) proc_entry; }
	proc_entry = create_proc_entry("43", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_43; proc_entry-> write_proc = (void *) gpio_write_proc_43; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[43] = (s32) proc_entry; }
	proc_entry = create_proc_entry("44", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_44; proc_entry-> write_proc = (void *) gpio_write_proc_44; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[44] = (s32) proc_entry; }
	proc_entry = create_proc_entry("45", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_45; proc_entry-> write_proc = (void *) gpio_write_proc_45; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[45] = (s32) proc_entry; }
	proc_entry = create_proc_entry("46", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_46; proc_entry-> write_proc = (void *) gpio_write_proc_46; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[46] = (s32) proc_entry; }
	proc_entry = create_proc_entry("47", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_47; proc_entry-> write_proc = (void *) gpio_write_proc_47; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[47] = (s32) proc_entry; }
	proc_entry = create_proc_entry("48", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_48; proc_entry-> write_proc = (void *) gpio_write_proc_48; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[48] = (s32) proc_entry; }
	proc_entry = create_proc_entry("49", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_49; proc_entry-> write_proc = (void *) gpio_write_proc_49; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[49] = (s32) proc_entry; }
	proc_entry = create_proc_entry("50", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_50; proc_entry-> write_proc = (void *) gpio_write_proc_50; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[50] = (s32) proc_entry; }
	proc_entry = create_proc_entry("51", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_51; proc_entry-> write_proc = (void *) gpio_write_proc_51; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[51] = (s32) proc_entry; }
	proc_entry = create_proc_entry("52", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_52; proc_entry-> write_proc = (void *) gpio_write_proc_52; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[52] = (s32) proc_entry; }
	proc_entry = create_proc_entry("53", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_53; proc_entry-> write_proc = (void *) gpio_write_proc_53; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[53] = (s32) proc_entry; }
	proc_entry = create_proc_entry("54", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_54; proc_entry-> write_proc = (void *) gpio_write_proc_54; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[54] = (s32) proc_entry; }
	proc_entry = create_proc_entry("55", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_55; proc_entry-> write_proc = (void *) gpio_write_proc_55; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[55] = (s32) proc_entry; }
	proc_entry = create_proc_entry("56", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_56; proc_entry-> write_proc = (void *) gpio_write_proc_56; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[56] = (s32) proc_entry; }
	proc_entry = create_proc_entry("57", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_57; proc_entry-> write_proc = (void *) gpio_write_proc_57; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[57] = (s32) proc_entry; }
	proc_entry = create_proc_entry("58", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_58; proc_entry-> write_proc = (void *) gpio_write_proc_58; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[58] = (s32) proc_entry; }
	proc_entry = create_proc_entry("59", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_59; proc_entry-> write_proc = (void *) gpio_write_proc_59; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[59] = (s32) proc_entry; }
	proc_entry = create_proc_entry("60", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_60; proc_entry-> write_proc = (void *) gpio_write_proc_60; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[60] = (s32) proc_entry; }
	proc_entry = create_proc_entry("61", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_61; proc_entry-> write_proc = (void *) gpio_write_proc_61; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[61] = (s32) proc_entry; }
	proc_entry = create_proc_entry("62", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_62; proc_entry-> write_proc = (void *) gpio_write_proc_62; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[62] = (s32) proc_entry; }
	proc_entry = create_proc_entry("63", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_63; proc_entry-> write_proc = (void *) gpio_write_proc_63; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[63] = (s32) proc_entry; }
	proc_entry = create_proc_entry("64", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_64; proc_entry-> write_proc = (void *) gpio_write_proc_64; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[64] = (s32) proc_entry; }
	proc_entry = create_proc_entry("65", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_65; proc_entry-> write_proc = (void *) gpio_write_proc_65; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[65] = (s32) proc_entry; }
	proc_entry = create_proc_entry("66", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_66; proc_entry-> write_proc = (void *) gpio_write_proc_66; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[66] = (s32) proc_entry; }
	proc_entry = create_proc_entry("67", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_67; proc_entry-> write_proc = (void *) gpio_write_proc_67; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[67] = (s32) proc_entry; }
	proc_entry = create_proc_entry("68", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_68; proc_entry-> write_proc = (void *) gpio_write_proc_68; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[68] = (s32) proc_entry; }
	proc_entry = create_proc_entry("69", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_69; proc_entry-> write_proc = (void *) gpio_write_proc_69; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[69] = (s32) proc_entry; }
	proc_entry = create_proc_entry("70", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_70; proc_entry-> write_proc = (void *) gpio_write_proc_70; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[70] = (s32) proc_entry; }
	proc_entry = create_proc_entry("71", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_71; proc_entry-> write_proc = (void *) gpio_write_proc_71; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[71] = (s32) proc_entry; }
	proc_entry = create_proc_entry("72", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_72; proc_entry-> write_proc = (void *) gpio_write_proc_72; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[72] = (s32) proc_entry; }
	proc_entry = create_proc_entry("73", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_73; proc_entry-> write_proc = (void *) gpio_write_proc_73; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[73] = (s32) proc_entry; }
	proc_entry = create_proc_entry("74", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_74; proc_entry-> write_proc = (void *) gpio_write_proc_74; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[74] = (s32) proc_entry; }
	proc_entry = create_proc_entry("75", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_75; proc_entry-> write_proc = (void *) gpio_write_proc_75; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[75] = (s32) proc_entry; }
	proc_entry = create_proc_entry("76", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_76; proc_entry-> write_proc = (void *) gpio_write_proc_76; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[76] = (s32) proc_entry; }
	proc_entry = create_proc_entry("77", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_77; proc_entry-> write_proc = (void *) gpio_write_proc_77; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[77] = (s32) proc_entry; }
	proc_entry = create_proc_entry("78", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_78; proc_entry-> write_proc = (void *) gpio_write_proc_78; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[78] = (s32) proc_entry; }
	proc_entry = create_proc_entry("79", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_79; proc_entry-> write_proc = (void *) gpio_write_proc_79; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[79] = (s32) proc_entry; }
	proc_entry = create_proc_entry("80", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_80; proc_entry-> write_proc = (void *) gpio_write_proc_80; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[80] = (s32) proc_entry; }
	proc_entry = create_proc_entry("81", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_81; proc_entry-> write_proc = (void *) gpio_write_proc_81; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[81] = (s32) proc_entry; }
	proc_entry = create_proc_entry("82", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_82; proc_entry-> write_proc = (void *) gpio_write_proc_82; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[82] = (s32) proc_entry; }
	proc_entry = create_proc_entry("83", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_83; proc_entry-> write_proc = (void *) gpio_write_proc_83; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[83] = (s32) proc_entry; }
	proc_entry = create_proc_entry("84", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_84; proc_entry-> write_proc = (void *) gpio_write_proc_84; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[84] = (s32) proc_entry; }
	proc_entry = create_proc_entry("85", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_85; proc_entry-> write_proc = (void *) gpio_write_proc_85; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[85] = (s32) proc_entry; }
	proc_entry = create_proc_entry("86", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_86; proc_entry-> write_proc = (void *) gpio_write_proc_86; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[86] = (s32) proc_entry; }
	proc_entry = create_proc_entry("87", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_87; proc_entry-> write_proc = (void *) gpio_write_proc_87; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[87] = (s32) proc_entry; }
	proc_entry = create_proc_entry("88", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_88; proc_entry-> write_proc = (void *) gpio_write_proc_88; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[88] = (s32) proc_entry; }
	proc_entry = create_proc_entry("89", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_89; proc_entry-> write_proc = (void *) gpio_write_proc_89; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[89] = (s32) proc_entry; }
	proc_entry = create_proc_entry("90", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_90; proc_entry-> write_proc = (void *) gpio_write_proc_90; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[90] = (s32) proc_entry; }
	proc_entry = create_proc_entry("91", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_91; proc_entry-> write_proc = (void *) gpio_write_proc_91; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[91] = (s32) proc_entry; }
	proc_entry = create_proc_entry("92", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_92; proc_entry-> write_proc = (void *) gpio_write_proc_92; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[92] = (s32) proc_entry; }
	proc_entry = create_proc_entry("93", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_93; proc_entry-> write_proc = (void *) gpio_write_proc_93; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[93] = (s32) proc_entry; }
	proc_entry = create_proc_entry("94", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_94; proc_entry-> write_proc = (void *) gpio_write_proc_94; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[94] = (s32) proc_entry; }
	proc_entry = create_proc_entry("95", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_95; proc_entry-> write_proc = (void *) gpio_write_proc_95; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[95] = (s32) proc_entry; }
	proc_entry = create_proc_entry("96", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_96; proc_entry-> write_proc = (void *) gpio_write_proc_96; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[96] = (s32) proc_entry; }
	proc_entry = create_proc_entry("97", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_97; proc_entry-> write_proc = (void *) gpio_write_proc_97; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[97] = (s32) proc_entry; }
	proc_entry = create_proc_entry("98", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_98; proc_entry-> write_proc = (void *) gpio_write_proc_98; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[98] = (s32) proc_entry; }
	proc_entry = create_proc_entry("99", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_99; proc_entry-> write_proc = (void *) gpio_write_proc_99; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[99] = (s32) proc_entry; }
	proc_entry = create_proc_entry("100", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_100; proc_entry-> write_proc = (void *) gpio_write_proc_100; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[100] = (s32) proc_entry; }
	proc_entry = create_proc_entry("101", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_101; proc_entry-> write_proc = (void *) gpio_write_proc_101; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[101] = (s32) proc_entry; }
	proc_entry = create_proc_entry("102", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_102; proc_entry-> write_proc = (void *) gpio_write_proc_102; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[102] = (s32) proc_entry; }
	proc_entry = create_proc_entry("103", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_103; proc_entry-> write_proc = (void *) gpio_write_proc_103; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[103] = (s32) proc_entry; }

	/* all */
	proc_entry = create_proc_entry("all", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = gpio_read_proc_all; proc_write_entry[104] = (s32) proc_entry; }

	/* pwctr 0-3 */
	proc_entry = create_proc_entry("pwctr0", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pwctr_read_proc_0; proc_entry-> write_proc = (void *) pwctr_write_proc_0; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[105] = (s32) proc_entry; }
	proc_entry = create_proc_entry("pwctr1", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pwctr_read_proc_1; proc_entry-> write_proc = (void *) pwctr_write_proc_1; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[106] = (s32) proc_entry; }
	proc_entry = create_proc_entry("pwctr2", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pwctr_read_proc_2; proc_entry-> write_proc = (void *) pwctr_write_proc_2; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[107] = (s32) proc_entry; }
	proc_entry = create_proc_entry("pwctr3", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = pwctr_read_proc_3; proc_entry-> write_proc = (void *) pwctr_write_proc_3; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[108] = (s32) proc_entry; }

	return 0;
}

#endif /* CONFIG_PROC_FS */


/**********************************************************************
 end for PROC_FS 
**********************************************************************/



/* 
  set PWCTR state 
*/

static int v2r_set_pwctr(int number, int value) {

	u8 result = 0;

	if (!(number >= 0 && number <= TOTAL_PWCTR)) {
		printk("%s: wrong RTP number (%d)\n", DEVICE_NAME, number);
		return 1;
	}

	result = davinci_rtcss_read(0x00);

	if (value)
		result |= 1 << number;
	else
		result &= !(1 << number);

	davinci_rtcss_write(result, 0x00);

	return 0;

}


/* 
  get PWCTR state 
*/

static int v2r_get_pwctr(int number) {

	u8 result = 0;

	if (!(number >= 0 && number <= TOTAL_PWCTR)) {
		printk("%s: wrong RTP number (%d)\n", DEVICE_NAME, number);
		return 1;
	}

	result = davinci_rtcss_read(0x00);

	return (result >> number) & 1;

}



/* 
  set GPIO state 
  direction =0 - input, =1 - output
*/

static int v2r_set_gpio(int gpio_number, int direction, int value) {

	if (!(gpio_number >= 0 && gpio_number <= TOTAL_GPIO)) {
		printk("%s: wrong GPIO number (%d)\n", DEVICE_NAME, gpio_number);
		return 1;
	}


	if (gpiotable[gpio_number].gpio_descriptor == NA) {
		printk("%s: GPIO descriptor is not available\n", DEVICE_NAME);
		return 1;
	}

	if (value > 1) {
		printk("%s: wrong value (%d)\n", DEVICE_NAME, value);
		return 1;
	}

	if (direction)
		gpio_direction_output(gpiotable[gpio_number].gpio_number, value);
	else 
		gpio_direction_input(gpiotable[gpio_number].gpio_number);

	davinci_cfg_reg(gpiotable[gpio_number].gpio_descriptor);

	return 0;
}

/*
  clear GPIO group
*/

static int group_clear(void) {

	gpioGroupTableCounter = -1; // just null a group size
	printk("%s: GPIO group cleared\n", DEVICE_NAME);

	return 0;
}

/*
  add GPIO into group
*/

static int group_add(int gpio_number) {

	if (gpioGroupTableCounter >=TOTAL_GPIO) {

		printk("%s: GPIO group is full\n", DEVICE_NAME);
		return 1;

	}

	if (!(gpio_number >= 0 && gpio_number <= TOTAL_GPIO)){

		printk("%s: wrong gpio number (%d)\n", DEVICE_NAME, gpio_number);
		return 1;

	}

	gpioGroupTableCounter++;
	gpioGroupTable[gpioGroupTableCounter] = gpiotable[gpio_number];

	printk("%s: added GPIO %d into group. New group size is %d\n", DEVICE_NAME, gpio_number, gpioGroupTableCounter+1);

	return 0;
}


/*
  add all GPIO into group
*/

static void group_init(void) {

	unsigned int i;

	for (i=0; i <= TOTAL_GPIO; i++) {

		gpioGroupTable[i] = gpiotable[i];

	}

	gpioGroupTableCounter = i-1;

	printk("%s: added all GPIO's into group\n", DEVICE_NAME);

}


static void gpio_parse_binary_command(char * buffer, unsigned int count) {

	int gpio_number = 0, direction = 0, value = 0;

	unsigned int i;

	switch (buffer[0]) {

		case 1:
			/* 
				set GPIO state
				buffer[1] - GPIO number
				buffer[2] - [bit:0] - direction, [bit:1] - state
			*/

			if (count < 2) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}

			gpio_number = buffer[1];
			direction = buffer[2] & 0x01;
			value = (buffer[2] >> 1) & 0x01;

			v2r_set_gpio(gpio_number, direction, value);

			break;

		case 2:

			/* clear GPIO group */

			group_clear();

			break;

		case 3:
			/* 
				add GPIO into group
				buffer[1] - GPIO number
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
				add all GPIO into group
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
				set PWCTR
				buffer[1] - PWCTR number
				buffer[2] - value
			*/

			if (count < 2) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, count);
				break;
			}

			v2r_set_pwctr(buffer[1], buffer[2]);

			break;


		default:
			printk("%s: i don't know this command\n", DEVICE_NAME);
		
	}


}

static void gpio_parse_command(char * string) {

	static char *part;
	static char *temp_string;
	int cmd_ok = 0;
	int i;

	int gpio_number = 0, direction = 0, value = 0;

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



	/* string like "set gpio 1 output 1" */
	if (!strcmp(command_parts[0], "set") && !strcmp(command_parts[1], "gpio")) {

		if (command_parts_counter < 3) {
			printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
			return;
		}

		if (!strcmp(command_parts[3], "output")) {
			direction = 1;
		} else if (!strcmp(command_parts[3], "input")) {
			direction = 0;
		}

		kstrtoint(command_parts[2], 10, &gpio_number);
		kstrtoint(command_parts[4], 10, &value);
		v2r_set_gpio(gpio_number, direction, value);
		cmd_ok = 1;
		goto out;
	}


	/* string like "set pwctr 1 1" */
	if (!strcmp(command_parts[0], "set") && !strcmp(command_parts[1], "pwctr")) {

		if (command_parts_counter < 3) {
			printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
			return;
		}

		kstrtoint(command_parts[2], 10, &gpio_number);
		kstrtoint(command_parts[3], 10, &value);
		v2r_set_pwctr(gpio_number, value);

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
		if (!strcmp(command_parts[1], "add") && !strcmp(command_parts[2], "gpio")) {

			if (command_parts_counter < 3) {
				printk("%s: too small arguments (%d)\n", DEVICE_NAME, command_parts_counter);
				return;
			}

			for (i = 3; i < command_parts_counter ; i++ ) {

				kstrtoint(command_parts[i], 10, &gpio_number);
				group_add(gpio_number);

			}

		}

		cmd_ok = 1;
		goto out;
	}


	/* string like "output bin" */
	/* or  like "output text" */
	if (!strcmp(command_parts[0], "output")) {

		if (!strcmp(command_parts[1], "bin"))
			output_mode = 1;
		else
		if (!strcmp(command_parts[1], "text"))
			output_mode = 0;
		
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



int gpio_open(struct inode *inode, struct file *filp) {

	unsigned int mj = imajor(inode);
	unsigned int mn = iminor(inode);
	struct gpio_dev *dev = NULL;

	if (mj != gpio_major || mn < 0 || mn >= 1){
		//One and only device
		printk("%s: no device found with minor=%d and major=%d\n", DEVICE_NAME, mj, mn);
		return -ENODEV; /* No such device */
	}

	/* store a pointer to struct gpio_dev here for other methods */

	dev = &gpio_devices[mn];
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

int gpio_release(struct inode *inode, struct file *filp) {

	// printk("%s: release device\n", DEVICE_NAME);
	return 0;

}


ssize_t gpio_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){

	volatile unsigned long i;
	volatile unsigned int counter;

	volatile char bitcounter;
	volatile char tempByte;
	volatile unsigned int value;

	struct gpio_dev *dev = (struct gpio_dev *)filp->private_data;
	ssize_t retval = 0;
	if (mutex_lock_killable(&dev->device_mutex)) return -EINTR;

	/* exit if empty group */
	if (gpioGroupTableCounter < 0) {

		v2r_gpio_retBuffer[0] = 0;
		counter = 0;
		goto show;
	}

	switch (output_mode) {

		case 0:
			/* text mode */

			//if (count > TOTAL_GPIO) count = TOTAL_GPIO;
			//if (*f_pos > TOTAL_GPIO) goto out;
			//if ((*f_pos + count) > TOTAL_GPIO) goto out;

			counter = 0;

			//for (i = *f_pos; i <= (*f_pos + count); i++) {
			for (i = 0; i <= gpioGroupTableCounter; i++) {

				value = gpio_get_value(gpioGroupTable[i].gpio_number);

				/* bers, eat this */
				v2r_gpio_retBuffer[counter] = value ? '1' : '0';

				counter++;
			}

			v2r_gpio_retBuffer[counter] = '\n';

			counter++;

			break;

		case 1:
			/* binary mode */

			counter = 0;
			bitcounter = 0;
			tempByte = 0;

			for (i = 0; i <= gpioGroupTableCounter; i++) {

				value = gpio_get_value(gpioGroupTable[i].gpio_number)? 1 : 0;

				tempByte |= value << bitcounter;
				bitcounter++;
				if (bitcounter > 7) {
	        	        	v2r_gpio_retBuffer[counter] = tempByte;

					tempByte = 0;
					bitcounter = 0;
					counter++;
				}
			}

			if (bitcounter) {
				// if not all byte filled
				v2r_gpio_retBuffer[counter] = tempByte;
				counter++;
			}

			break;

	}

show:

	if (copy_to_user(buf, v2r_gpio_retBuffer, counter) != 0){

		retval = -EFAULT;
		goto out;

	}

	retval = counter;
out:

  	mutex_unlock(&dev->device_mutex);
	return retval;
}




ssize_t gpio_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {

	struct gpio_dev *dev = (struct gpio_dev *)filp->private_data;
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
		gpio_parse_binary_command(command, count-1);
	} else {
		gpio_parse_command(command);
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

struct file_operations gpio_fops = {
	.owner =    THIS_MODULE,
	.read =     gpio_read,
	.write =    gpio_write,
	.open =     gpio_open,
	.release =  gpio_release,
};


static int gpio_construct_device(struct gpio_dev *dev, int minor, struct class *class) {

	int err = 0;
	dev_t devno = MKDEV(gpio_major, minor);
	struct device *device = NULL;

	BUG_ON(dev == NULL || class == NULL);

	/* Memory is to be allocated when the device is opened the first time */
	printk("%s: construct device:%d\n", DEVICE_NAME, minor);

	dev->data = NULL;
	dev->buffer_size = BUFFER_SIZE;
	mutex_init(&dev->device_mutex);
	cdev_init(&dev->cdev, &gpio_fops);
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

static void gpio_destroy_device(struct gpio_dev *dev, int minor, struct class *class) {

	BUG_ON(dev == NULL || class == NULL);

	printk("%s: destroy device %d\n", DEVICE_NAME, minor);

	device_destroy(class, MKDEV(gpio_major, minor));
	cdev_del(&dev->cdev);
	kfree(dev->data);

	printk("%s: device is destroyed successfully\n", DEVICE_NAME);

	return;

}


static void gpio_cleanup_module(int devices_to_destroy) {

	int i = 0;

	/* Get rid of character devices (if any exist) */

	printk("%s: cleanup module\n", DEVICE_NAME);

	if (gpio_devices) {

		for (i = 0; i < devices_to_destroy; ++i) {

			gpio_destroy_device(&gpio_devices[i], i, gpio_class);

	        }

		kfree(gpio_devices);

	}

	if (gpio_class) class_destroy(gpio_class);
	if (v2r_gpio_retBuffer) kfree(v2r_gpio_retBuffer);
	if (command_parts) kfree(command_parts);


	/* remove proc_fs files */
	gpio_remove_proc_fs();	

	unregister_chrdev_region(MKDEV(gpio_major, 0), numberofdevices);

	printk("%s: cleanup completed\n", DEVICE_NAME);

	return;

}

static int __init gpio_init_module (void) {

	int err = 0;
	int i = 0;
	int devices_to_destroy = 0;
	dev_t dev = 0;

	printk("Virt2real GPIO Driver version 0.3\n");

	if (gpio_add_proc_fs()) {
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

	gpio_major = MAJOR(dev);

	/* Create device class (before allocation of the array of devices) */

	gpio_class = class_create(THIS_MODULE, DEVICE_NAME);

	if (IS_ERR(gpio_class)) {

		err = PTR_ERR(gpio_class);
		printk("%s: class not created %d\n", DEVICE_NAME, err);
		goto fail;

	}

	/* Allocate the array of devices */

	gpio_devices = (struct gpio_dev *) kzalloc( numberofdevices * sizeof(struct gpio_dev), GFP_KERNEL);

	if (gpio_devices == NULL) {

		err = -ENOMEM;
		printk("%s: devices not allocated %d\n", DEVICE_NAME, err);
		goto fail;

	}

	/* Construct devices */
	for (i = 0; i < numberofdevices; ++i) {

		err = gpio_construct_device(&gpio_devices[i], i, gpio_class);
		if (err) {

			printk("%s: device is not created\n", DEVICE_NAME);
			devices_to_destroy = i;
			goto fail;

        	}
	}

	v2r_gpio_retBuffer = kmalloc(RETBUFFER_SIZE + 1, GFP_KERNEL);
	if (v2r_gpio_retBuffer == NULL) return -ENOMEM;


	command_parts = kmalloc(128, GFP_KERNEL);
	if (command_parts == NULL) return -ENOMEM;


	/* fill gpioGroupTable width default values - all GPIOs */
	group_init();

	return 0; /* success */

fail:

	gpio_cleanup_module(devices_to_destroy);
	return err;

}

static void __exit gpio_exit_module(void){

	gpio_cleanup_module(numberofdevices);
	
	return;

}

module_init(gpio_init_module);
module_exit(gpio_exit_module);

MODULE_DESCRIPTION("Virt2real GPIO Driver version 0.3");
MODULE_AUTHOR("Alexandr Shadrin");
MODULE_AUTHOR("Gol (gol@g0l.ru)");
MODULE_LICENSE("GPL v2");
