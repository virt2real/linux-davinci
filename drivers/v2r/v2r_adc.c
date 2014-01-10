#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include "v2r_adc.h"

#define DEVICE_NAME "v2r_adc"
#define ADC_VERSION "1.0"

void *dm365_adc_base;
 
int adc_single(unsigned int channel) {

	if (channel >= ADC_MAX_CHANNELS)
		return -1;

	//select channel
	iowrite32(1 << channel,dm365_adc_base + DM365_ADC_CHSEL);

	//start coversion
	iowrite32(DM365_ADC_ADCTL_BIT_START,dm365_adc_base + DM365_ADC_ADCTL);

	// Wait for conversion to start
	while (!(ioread32(dm365_adc_base + DM365_ADC_ADCTL) & DM365_ADC_ADCTL_BIT_BUSY)){
		cpu_relax();
	}

	// Wait for conversion to be complete.
	while ((ioread32(dm365_adc_base + DM365_ADC_ADCTL) & DM365_ADC_ADCTL_BIT_BUSY)){
		cpu_relax();
	}

	return ioread32(dm365_adc_base + DM365_ADC_AD0DAT + 4 * channel);
}

//static spinlock_t adc_lock = SPIN_LOCK_UNLOCKED;
DEFINE_SPINLOCK(adc_lock); // changed by Gol

static void adc_read_block(unsigned short *data, size_t length) {

	int i;

	spin_lock_irq(&adc_lock);

	for(i = 0; i < length; i++) {
		data[i] = adc_single(i);
	}

	spin_unlock_irq(&adc_lock);

}

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
static s32 proc_write_entry[ADC_MAX_CHANNELS];

static int adc_read_proc (int adc_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	int value = 0;
	int len = 0;

	if (adc_number >= ADC_MAX_CHANNELS) {
		printk("%s: wrong  channel (%d)\n", DEVICE_NAME, adc_number);
		return -ENOMEM;
	}

	
	value = adc_single(adc_number);
	len = sprintf(buf, "%d", value);

	return len;

}

/* *i'd line to use array init, but i don't know how get file id from unified functions */

static int adc_read_proc_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (0, buf, start, offset, count, eof, data); }
static int adc_read_proc_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (1, buf, start, offset, count, eof, data); }
static int adc_read_proc_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (2, buf, start, offset, count, eof, data); }
static int adc_read_proc_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (3, buf, start, offset, count, eof, data); }
static int adc_read_proc_4 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (4, buf, start, offset, count, eof, data); }
static int adc_read_proc_5 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (5, buf, start, offset, count, eof, data); }


static int adc_remove_proc_fs(void) {

	int i;
	char fn[10];

	for (i = 0; i < ADC_MAX_CHANNELS; i++) {

		if (proc_write_entry[i]) { 
			sprintf(fn, "%d", i);
			remove_proc_entry(fn, proc_parent);
		}

	}

	/* remove proc_fs directory */
	remove_proc_entry("v2r_adc",NULL);

	return 0;
}

static int adc_add_proc_fs(void) {

	proc_parent = proc_mkdir("v2r_adc", NULL);

	if (!proc_parent) {
		printk("%s: error creating proc entry (/proc/v2r_adc)\n", DEVICE_NAME);
		return 1;
	}

	proc_entry = create_proc_entry("0", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_0; proc_write_entry[0] = (s32) proc_entry; }
	proc_entry = create_proc_entry("1", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_1; proc_write_entry[1] = (s32) proc_entry; }
	proc_entry = create_proc_entry("2", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_2; proc_write_entry[2] = (s32) proc_entry; }
	proc_entry = create_proc_entry("3", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_3; proc_write_entry[3] = (s32) proc_entry; }
	proc_entry = create_proc_entry("4", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_4; proc_write_entry[4] = (s32) proc_entry; }
	proc_entry = create_proc_entry("5", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_5; proc_write_entry[5] = (s32) proc_entry; }
	return 0;
}

#endif /* CONFIG_PROC_FS */


/**********************************************************************
 end for PROC_FS 
**********************************************************************/


static ssize_t adc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {

	unsigned short data [ADC_MAX_CHANNELS];

	if (count < sizeof(unsigned short))
		return -ETOOSMALL;

	adc_read_block(data,ADC_MAX_CHANNELS);

	if (copy_to_user(buf, data, count))
		return -EFAULT;

	return count;

}

static int adc_open(struct inode *inode, struct file *file) {
	return 0;
}


static int adc_release(struct inode *inode, struct file *file) {
	return 0;
}


static const struct file_operations adc_fops = {
	.owner    = THIS_MODULE,
	.read   = adc_read,
	.open   = adc_open,
	.release  = adc_release,
};


static struct miscdevice adc_dev = {

	NVRAM_MINOR,
	"v2r_adc",
	&adc_fops

};


static int adc_init_module(void) {

	int ret;
	ret = misc_register(&adc_dev);

	printk("%s: device is created successfully\n", DEVICE_NAME);

	if (ret) {
		printk(KERN_ERR "%s: can't misc_register on minor=%d\n", DEVICE_NAME, NVRAM_MINOR);
		return ret;
	}

	ret = adc_add_proc_fs();

	if (ret) {
		misc_deregister(&adc_dev);
		printk(KERN_ERR "%s: can't create /proc/driver/v2r_adc\n", DEVICE_NAME);
		return ret;
	}

	if (!devm_request_mem_region(adc_dev.this_device, DM365_ADC_BASE, 64, "v2r_adc"))
		return -EBUSY;

	dm365_adc_base = devm_ioremap_nocache(adc_dev.this_device, DM365_ADC_BASE, 64); // Physical address,Number of bytes to be mapped.

	if (!dm365_adc_base)
		return -ENOMEM;

	return 0;
}

static void adc_exit_module(void) {

	adc_remove_proc_fs();

	misc_deregister(&adc_dev);

        printk("%s: exit\n", DEVICE_NAME);

}

module_init(adc_init_module);
module_exit(adc_exit_module);
MODULE_DESCRIPTION("Virt2real ADC driver module version 0.5");
MODULE_AUTHOR("Shlomo Kut,,, (shl...@infodraw.com)");
MODULE_LICENSE("GPL2");
