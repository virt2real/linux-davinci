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

#include "prtcss.h"

#define TOTAL_GPIO 103

char * v2r_gpio_retBuffer;

/* GPIO functionality */

typedef struct  {
	int number;
	const char* gpio_name;
	int gpio_descriptor;
	int gpio_number;
} gpio;

#define TRUE  1
#define FALSE 0
#define NA    -1
#define PRTO  10000
#define PRTIO 20000
#define INP 0
#define OUT 1
#define EMPTY 0

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

// array for GPIO group
static gpio gpioGroupTable[TOTAL_GPIO+1];
static short gpioGroupTableCounter = 0;

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

    // counting bits
	for (nIndex = 0; nIndex < input_len; nIndex++){
		if (str[nIndex] == delimiter) nBites++;
	}

    result = kmalloc((size_t)(nBites+1)*sizeof(char*), GFP_KERNEL);

	for (nIndex = 0; nIndex <= input_len; nIndex++){
		if ((str[nTempIndex] == delimiter)||(str[nTempIndex] == 0)||(str[nTempIndex] == '\n')||(str[nTempIndex] == '\r') ){
			if (nTempIndex != 0){
				result[nTempByte] = kmalloc(nTempIndex+1, GFP_KERNEL);
				memcpy(result[nTempByte], str, nTempIndex);
				result[nTempByte][nTempIndex] = 0;
				nTempByte++;
				str += nTempIndex+1;
				nTempIndex = 0;
			} else
			{
				str++; nTempIndex = 0;
			}
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
	return !strcmp(str1, str2);
}

static int str_to_int(const char* str){
    int result = 0;
    if (kstrtoint(str, 10, &result)) {
    	// error in convertion
    	printk("Parse error %s\r\n", str);
    	return FALSE;
    }
    return result;
}

static int starts_with(const char* str1, const char* ref){
	int i = 0;
	int len = strlen(str1);
	const char* substr = 0;

	if (len <= strlen(ref)) {
		printk("starts_with len error\r\n");
		return NA;
    }

    len = strlen(ref);
    for (i = 0; i < len; i++){
		if (str1[i] != ref[i]) {
			//printk("starts with not equal error %d\r\n", i);
			return NA;
    	}
	}

    // here it a good candidate for perfect result. Lets get appropriate value

    substr = &str1[len];

    if (is_equal_string(substr, "true")) return TRUE;
    if (is_equal_string(substr, "false")) return FALSE;

    return str_to_int(substr);
}

/* the most stupid methods I have ever written, but it works! */

static void process_con(char** data, unsigned int ordinal){

	int tmp = NA;
	if (!(ordinal > 0 && ordinal <= TOTAL_GPIO)){
		printk("Wrong gpio number (%d)\r\n", ordinal);
		return;
	}

    if (data[2]){

		// check if direction needed is "input"

		if (is_equal_string(data[2], "input")){

			//printk("Configure con%d (GPIO %d) gpio as input\r\n", ordinal, gpiotable[ordinal].gpio_number);

			v2r_gpio_direction_input(gpiotable[ordinal].gpio_number);
			davinci_cfg_reg(gpiotable[ordinal].gpio_descriptor);
			return;

		}

		// if we are here - direction needed is "output"

		tmp = starts_with(data[2], "output:");

		if (tmp != NA) {

			if (gpiotable[ordinal].gpio_descriptor == NA){
				printk("GPIO descriptor is not available\r\n");
				return;
			}

			v2r_gpio_direction_output(gpiotable[ordinal].gpio_number, tmp);
			davinci_cfg_reg(gpiotable[ordinal].gpio_descriptor);
			return;

		} else {
			printk("Error output value\r\n");
		}

	}

	printk("Error setting gpio function\r\n");
}

static void process_set(char** data){

	int ordinal = -1;

	if (data[1]){

		//Any parse result present

		ordinal = starts_with(data[1], "gpio");

		//This is gpio
		if (ordinal > 0 && ordinal <= TOTAL_GPIO) {
			process_con(data, ordinal);
			return;
		}

	}

}

static void process_command(char** data){

	int gpio = -1;

	if (data[0]){

		//Any parse result present

		if (is_equal_string(data[0], "set")){

			//Process for set command
			process_set(data); return;
		}

		if (is_equal_string(data[0], "group")){

			if (is_equal_string(data[1], "clear")){

                        	//Process for group clear command
				gpioGroupTableCounter = -1; // just null a group size
				printk("v2r_gpio: gpio group cleared\n");
				return;

			}

			if (is_equal_string(data[1], "add")){

                        	//Process for group add command

				if (gpioGroupTableCounter >=TOTAL_GPIO) {
					printk("v2r_gpio: group is full\n");
					return;
				}

				gpio = starts_with(data[2], "gpio");

				if (!(gpio > 0 && gpio <= TOTAL_GPIO)){
					printk("Wrong gpio number (%d)\n", gpio);
					return;
				}

				gpioGroupTableCounter++;
				gpioGroupTable[gpioGroupTableCounter] = gpiotable[gpio];

				printk("v2r_gpio: added gpio%d into group. New group size is %d\n", gpio, gpioGroupTableCounter+1);
				return;

			}

		}


	}

	printk("Wrong V2R driver command\r\n");

}

///////////////////////////////////////////////////////////////////////////////
// Here the driver itself begins


MODULE_AUTHOR("Alexander V. Shadrin");
MODULE_LICENSE("GPL");

#define V2R_NDEVICES 1
#define V2R_BUFFER_SIZE 8192
#define V2R_DEVICE_NAME "v2r_gpio"

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

int v2r_gpio_open(struct inode *inode, struct file *filp){

	unsigned int mj = imajor(inode);
	unsigned int mn = iminor(inode);
	struct v2r_dev *dev = NULL;

	if (mj != v2r_major || mn < 0 || mn >= 1){
		//One and only device
		printk("No device found with minor=%d and major=%d\n", mj, mn);
		return -ENODEV; /* No such device */
	}

	/* store a pointer to struct v2r_dev here for other methods */

	dev = &v2r_devices[mn];
	filp->private_data = dev;
	if (inode->i_cdev != &dev->cdev){
		printk("v2r open: internal error\n");
		return -ENODEV; /* No such device */
	}

	/* if opened the 1st time, allocate the buffer */
	if (dev->data == NULL){
		dev->data = (unsigned char*)kzalloc(dev->buffer_size, GFP_KERNEL);
		if (dev->data == NULL){
			printk("v2r open(): out of memory\n");
			return -ENOMEM;
		}
		dev->buffer_data_size = 0;
	}

	return 0;
}

int v2r_gpio_release(struct inode *inode, struct file *filp){
	//printk("V2R release device\r\n");
	return 0;
}

ssize_t v2r_gpio_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
	volatile unsigned long i;
	volatile unsigned int counter = 0;

	struct v2r_dev *dev = (struct v2r_dev *)filp->private_data;
	ssize_t retval = 0;
	if (mutex_lock_killable(&dev->v2r_mutex)) return -EINTR;

	//printk("v2r_gpio device read from %d to %d\r\n", (long) *f_pos, (long) *f_pos + count - 1);
/*
	if (count > TOTAL_GPIO) count = TOTAL_GPIO;
	if (*f_pos > TOTAL_GPIO) goto out;
	if ((*f_pos + count) > TOTAL_GPIO) goto out;

	for (i = *f_pos; i < (*f_pos + count); i++) {
		int value;
		if (gpiotable[i].gpio_number == NA) value = 0;
		else
			value = gpio_get_value(gpiotable[i].gpio_number);
		if (value) v2r_gpio_retBuffer[counter] = '1'; else v2r_gpio_retBuffer[counter] = '0';
		counter++;
	}
*/
	// binary output
	volatile char bitcounter = 0;
	volatile char tempByte = 0;
	volatile char value;

//	for (i = 0; i <= TOTAL_GPIO; i++) {
	if (gpioGroupTableCounter != -1) {
	  counter = 1;
	  for (i = 0; i <= gpioGroupTableCounter; i++) {

		//value = gpio_get_value(gpiotable[i].gpio_number)? 1 : 0;
		value = gpio_get_value(gpioGroupTable[i].gpio_number)? 1 : 0;
		//printk("i=%d value=%d\n",i,value);
		tempByte |= (value << bitcounter);
		bitcounter++;
		if (bitcounter > 7) {
                	v2r_gpio_retBuffer[counter-1] = tempByte;
			//printk("\n(bits %d-%d) value[%d]=%d\n",counter*8,counter*8+7,counter,v2r_gpio_retBuffer[counter]);
			tempByte = 0;
			bitcounter = 0;
			counter++;
		}
	  }

	  if (bitcounter) {
		// if not all byte filled
		v2r_gpio_retBuffer[counter-1] = tempByte;
		counter++;
	  }

	} else {
		printk("v2r_gpio: empty gpio group\n");
	}

	//v2r_gpio_retBuffer[counter] = '\n';
	//counter++;

	if (copy_to_user(buf, v2r_gpio_retBuffer, counter-1) != 0){
		retval = -EFAULT;
		goto out;
	}

	//printk("\r\nv2r_gpio copied %d bytes to user\r\n", counter);
	retval = counter-1;
out:

  	mutex_unlock(&dev->v2r_mutex);
	return retval;
}

ssize_t v2r_gpio_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
	struct v2r_dev *dev = (struct v2r_dev *)filp->private_data;
	ssize_t retval = 0;
	char *tmp = 0;
	int nIndex = 0;
	char** data = 0;

	char gpio;
	char direction;
	char state;

	//printk("v2r_gpio write\r\n");
	if (mutex_lock_killable(&dev->v2r_mutex)) return -EINTR;
	if (*f_pos !=0) {
    /* Writing in the middle of the file is not allowed */
		printk("Writing in the middle (%d) of the file buffer is not allowed\r\n", (int)(*f_pos));
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

	// if first byte is not ASCII - it's a binary command
	switch (tmp[0]) {
		case 1:
			// set GPIO state
			if (count < 3) {
				printk("v2r_gpio: very short command\n");
				goto parseout;
			}

			/* 
			   tmp[0] - command
			   tmp[1] - GPIO number
			   tmp[2] - [bit:0] - direction, [bit:1] - state
			*/

			gpio = tmp[1];
			direction = tmp[2] & 0x01;
			state = (tmp[2] >> 1) & 0x01;

			if (!(gpio > 0 && gpio <= TOTAL_GPIO)){
				printk("Wrong gpio number (%d)\n", gpio);
				goto parseout;
			}

			if (direction) {
				v2r_gpio_direction_output(gpiotable[gpio].gpio_number, state);
				davinci_cfg_reg(gpiotable[gpio].gpio_descriptor);
			} else {
				v2r_gpio_direction_input(gpiotable[gpio].gpio_number);
				davinci_cfg_reg(gpiotable[gpio].gpio_descriptor);
			}

			printk("v2r_gpio: set GPIO %d direction %d state %d\n", gpio, direction, state);
			
			goto parseout;

			break;

		case 2:
			// clear GPIO group
			gpioGroupTableCounter = -1; // just null a group size
			printk("v2r_gpio: gpio group cleared\n");
			goto parseout;
			break;

		case 3:
			// add GPIO into group
			/* 
			   tmp[0] - command
			   tmp[1] - GPIO number
			*/

			if (gpioGroupTableCounter >=TOTAL_GPIO) {
				printk("v2r_gpio: group is full\n");
				goto parseout;
			}

			gpio = tmp[1];
			if (!(gpio > 0 && gpio <= TOTAL_GPIO)){
				printk("Wrong gpio number (%d)\n", gpio);
				goto parseout;
			}

			gpioGroupTableCounter++;
			gpioGroupTable[gpioGroupTableCounter] = gpiotable[gpio];

			printk("v2r_gpio: added gpio%d into group. New group size is %d\n", gpio, gpioGroupTableCounter+1);
			goto parseout;
			break;

		default:
			break;
	}


	// otherwise it a text command, tmp contains string to parse
	data = split(tmp, ' ');
	process_command(data);

parseout:

	memcpy(dev->data, "ok", 3);
	dev->buffer_data_size = 3;
	nIndex = 0;
	if (data) {
		while(data[nIndex]){
			//Release allocated memory
			kfree(data[nIndex]);
			nIndex++;
		}
		kfree(data);
	}

	kfree(tmp);
	*f_pos = 0;
	retval = count;
out:
  	mutex_unlock(&dev->v2r_mutex);
	return retval;
}

struct file_operations v2r_gpio_fops = {
	.owner =    THIS_MODULE,
	.read =     v2r_gpio_read,
	.write =    v2r_gpio_write,
	.open =     v2r_gpio_open,
	.release =  v2r_gpio_release,
};

static int v2r_construct_device(struct v2r_dev *dev, int minor, struct class *class){
	int err = 0;
	dev_t devno = MKDEV(v2r_major, minor);
	struct device *device = NULL;
	BUG_ON(dev == NULL || class == NULL);
	/* Memory is to be allocated when the device is opened the first time */
	printk("v2r_gpio construct device:%d\r\n", minor);
	dev->data = NULL;
	dev->buffer_size = v2r_buffer_size;
	mutex_init(&dev->v2r_mutex);
	cdev_init(&dev->cdev, &v2r_gpio_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err){
		printk("V2R Error %d while trying to add %s%d",	err, V2R_DEVICE_NAME, minor);
		return err;
	}
	device = device_create(class, NULL/*no parent device*/,  devno, NULL/*no additional data */,V2R_DEVICE_NAME);
	if (IS_ERR(device)) {
		err = PTR_ERR(device);
		printk("Error %d while trying to create %s%d", err, V2R_DEVICE_NAME, minor);
        cdev_del(&dev->cdev);
        return err;
    }
	printk("v2r_gpio device is created successfully\r\n");
	return 0;
}

static void v2r_destroy_device(struct v2r_dev *dev, int minor, struct class *class){
	BUG_ON(dev == NULL || class == NULL);
	printk("v2r destroy device: %d\r\n", minor);
	device_destroy(class, MKDEV(v2r_major, minor));
	cdev_del(&dev->cdev);
	kfree(dev->data);
	printk("v2r device is destroyed successfully\r\n");
	return;
}

static void v2r_cleanup_module(int devices_to_destroy){
	int i = 0;
	/* Get rid of character devices (if any exist) */
	printk("v2r_gpio cleanup module\r\n");
	if (v2r_devices) {
		for (i = 0; i < devices_to_destroy; ++i) {
			v2r_destroy_device(&v2r_devices[i], i, v2r_class);
        }
		kfree(v2r_devices);
	}
	if (v2r_class) class_destroy(v2r_class);
	if (v2r_gpio_retBuffer) kfree(v2r_gpio_retBuffer);
	unregister_chrdev_region(MKDEV(v2r_major, 0), v2r_ndevices);
	printk("v2r_gpio cleanup completed\r\n");
	return;
}

static int __init v2r_init_module(void){
	int err = 0;
	int i = 0;
	int devices_to_destroy = 0;
	dev_t dev = 0;
	printk("v2r_gpio module version 1.3 init\r\n");
	if (v2r_ndevices <= 0){
		printk("v2r invalid value of v2r_ndevices: %d\n", v2r_ndevices);
		return -EINVAL;
	}
	/* Get a range of minor numbers (starting with 0) to work with */
	err = alloc_chrdev_region(&dev, 0, v2r_ndevices, V2R_DEVICE_NAME);
	if (err < 0) {
		printk("v2r alloc_chrdev_region() failed\n");
		return err;
	}
	v2r_major = MAJOR(dev);
	printk("v2r_gpio device major: %d\r\n", v2r_major);
	/* Create device class (before allocation of the array of devices) */
	v2r_class = class_create(THIS_MODULE, V2R_DEVICE_NAME);
	if (IS_ERR(v2r_class)){
		err = PTR_ERR(v2r_class);
		printk("v2r_gpio class not created %d\r\n", err);
		goto fail;
    }
	/* Allocate the array of devices */
	v2r_devices = (struct v2r_dev *)kzalloc( v2r_ndevices * sizeof(struct v2r_dev), GFP_KERNEL);
	if (v2r_devices == NULL) {
		err = -ENOMEM;
		printk("v2r_gpio devices not allocated %d\r\n", err);
		goto fail;
	}
	/* Construct devices */
	for (i = 0; i < v2r_ndevices; ++i) {
		err = v2r_construct_device(&v2r_devices[i], i, v2r_class);
		if (err) {
			printk("v2r_gpio device is not created\r\n");
			devices_to_destroy = i;
			goto fail;
        }
	}

	v2r_gpio_retBuffer = kmalloc(TOTAL_GPIO+1, GFP_KERNEL);
	if (v2r_gpio_retBuffer==NULL) return -ENOMEM;

	// fill gpioGroupTable width default values - all GPIOs
	printk("v2r_gpio: fill default GPIO group array\n");
	for (i=0; i<=TOTAL_GPIO; i++) {
		gpioGroupTable[i] = gpiotable[i];
	}
	gpioGroupTableCounter = i; // save group size

	return 0; /* success */
fail:
	v2r_cleanup_module(devices_to_destroy);
	return err;
}

static void __exit v2r_exit_module(void){
	printk("v2r_gpio module exit\r\n");
	v2r_cleanup_module(v2r_ndevices);
	return;
}

module_init(v2r_init_module);
module_exit(v2r_exit_module);
