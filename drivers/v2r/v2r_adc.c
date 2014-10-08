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
#define ADC_VERSION "1.1"

void *dm365_adc_base;

//static spinlock_t adc_lock = SPIN_LOCK_UNLOCKED;
DEFINE_SPINLOCK(adc_lock); // changed by Gol

static int mode = 0; // 0 - default mode value
static int divider = 1; // frequency divider

/* set mode function */
static void adc_set_mode(int mode) {

	if (mode) {
		/* free-run */
		iowrite32(DM365_ADC_ADCTL_BIT_START | DM365_ADC_ADCTL_BIT_SCNMD, dm365_adc_base + DM365_ADC_ADCTL);

		//select all channels
		iowrite32(1 << 0, dm365_adc_base + DM365_ADC_CHSEL);
		iowrite32(1 << 1, dm365_adc_base + DM365_ADC_CHSEL);
		iowrite32(1 << 2, dm365_adc_base + DM365_ADC_CHSEL);
		iowrite32(1 << 3, dm365_adc_base + DM365_ADC_CHSEL);
		iowrite32(1 << 4, dm365_adc_base + DM365_ADC_CHSEL);
		iowrite32(1 << 5, dm365_adc_base + DM365_ADC_CHSEL);

		printk("v2r_adc: set free-run mode\n");
	}
	else {
		/* one-shot */
		iowrite32(0, dm365_adc_base + DM365_ADC_ADCTL);
		printk("v2r_adc: set one-shot mode\n");
	}

}

/* get mode function */
static int adc_get_mode(void) {
	return mode;
}


/* set divider function */
static void adc_set_div (int value) {

	iowrite32(value, dm365_adc_base + DM365_ADC_SETDIV);
	printk("v2r_adc: new freq divider is %u\n", ioread32(dm365_adc_base + DM365_ADC_SETDIV));

}

/* get divider function */
static int adc_get_div (void) {

	return ioread32(dm365_adc_base + DM365_ADC_SETDIV);

}

 
int adc_single(unsigned int channel) {

	if (channel >= ADC_MAX_CHANNELS)
		return -1;

	// select channel
	iowrite32(1 << channel, dm365_adc_base + DM365_ADC_CHSEL);

	if (!mode) {
		// start coversion
		iowrite32(DM365_ADC_ADCTL_BIT_START, dm365_adc_base + DM365_ADC_ADCTL);

		// Wait for conversion to start
		while (!(ioread32(dm365_adc_base + DM365_ADC_ADCTL) & DM365_ADC_ADCTL_BIT_BUSY)){
			cpu_relax();
		}
	}

	// Wait for conversion to be complete.
	while ((ioread32(dm365_adc_base + DM365_ADC_ADCTL) & DM365_ADC_ADCTL_BIT_BUSY))
		cpu_relax();

	return ioread32(dm365_adc_base + DM365_ADC_AD0DAT + 4 * channel);
}

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
static s32 proc_write_entry[ADC_MAX_CHANNELS*2 + 2];

/* text read */
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

/* binary read */
static int adc_read_proc_bin (int adc_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

	int value = 0;
	int len = 0;

	if (adc_number >= ADC_MAX_CHANNELS) {
		printk("%s: wrong  channel (%d)\n", DEVICE_NAME, adc_number);
		return -ENOMEM;
	}
	
	value = adc_single(adc_number);
	len = sprintf(buf, "%c%c", value & 0xff, (value >> 8) & 0xff);

	return len;

}



/* command files */

/* get ADC run mode */
static int adc_read_proc_mode (char *buf, char **start, off_t offset, int count, int *eof, void *data ) {
	int len = sprintf(buf, "%d\n", adc_get_mode());
	return len;
}

/* set ADC run mode */
static int adc_write_proc_mode (struct file *file, const char *buf, int count, void *data) {

	static int value = 0;
	static char proc_data[2];

	if(copy_from_user(proc_data, buf, count))
		return -EFAULT;

	if (proc_data[0] == 0 || proc_data[0] == '0') 
		value = 0;
	else if (proc_data[0] == 1 || proc_data[0] == '1')
		value = 1;

	/* save mode */
	mode = value;

	/* set mode */
	adc_set_mode(mode);

	return count;
}


/* get ADC divider */
static int adc_read_proc_div (char *buf, char **start, off_t offset, int count, int *eof, void *data ) {
	int len = sprintf(buf, "%d\n", adc_get_div());
	return len;
}

/* set ADC divider */
static int adc_write_proc_div (struct file *file, const char *buf, int count, void *data) {

	static int value = 0;
	static char proc_data[10];

	if(copy_from_user(proc_data, buf, count))
		return -EFAULT;

	if (!kstrtoint(proc_data, 10, &value)) {

		/* save divider */
		divider = value;

		/* set divider */
		adc_set_div(divider);
	} else 
		return -EFAULT;

	return count;
}



/* *i'd line to use array init, but i don't know how get file id from unified functions */

/* text read */
static int adc_read_proc_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (0, buf, start, offset, count, eof, data); }
static int adc_read_proc_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (1, buf, start, offset, count, eof, data); }
static int adc_read_proc_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (2, buf, start, offset, count, eof, data); }
static int adc_read_proc_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (3, buf, start, offset, count, eof, data); }
static int adc_read_proc_4 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (4, buf, start, offset, count, eof, data); }
static int adc_read_proc_5 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc (5, buf, start, offset, count, eof, data); }

/* binary read */
static int adc_read_proc_bin_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc_bin (0, buf, start, offset, count, eof, data); }
static int adc_read_proc_bin_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc_bin (1, buf, start, offset, count, eof, data); }
static int adc_read_proc_bin_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc_bin (2, buf, start, offset, count, eof, data); }
static int adc_read_proc_bin_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc_bin (3, buf, start, offset, count, eof, data); }
static int adc_read_proc_bin_4 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc_bin (4, buf, start, offset, count, eof, data); }
static int adc_read_proc_bin_5 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return adc_read_proc_bin (5, buf, start, offset, count, eof, data); }


static int adc_remove_proc_fs(void) {

	int i;
	char fn[10];

	for (i = 0; i < (ADC_MAX_CHANNELS*2); i++) {

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

	/* text files */
	proc_entry = create_proc_entry("0", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_0; proc_write_entry[0] = (s32) proc_entry; }
	proc_entry = create_proc_entry("1", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_1; proc_write_entry[1] = (s32) proc_entry; }
	proc_entry = create_proc_entry("2", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_2; proc_write_entry[2] = (s32) proc_entry; }
	proc_entry = create_proc_entry("3", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_3; proc_write_entry[3] = (s32) proc_entry; }
	proc_entry = create_proc_entry("4", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_4; proc_write_entry[4] = (s32) proc_entry; }
	proc_entry = create_proc_entry("5", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_5; proc_write_entry[5] = (s32) proc_entry; }

	/* binary files */
	proc_entry = create_proc_entry("0b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_bin_0; proc_write_entry[6] = (s32) proc_entry; }
	proc_entry = create_proc_entry("1b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_bin_1; proc_write_entry[7] = (s32) proc_entry; }
	proc_entry = create_proc_entry("2b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_bin_2; proc_write_entry[8] = (s32) proc_entry; }
	proc_entry = create_proc_entry("3b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_bin_3; proc_write_entry[9] = (s32) proc_entry; }
	proc_entry = create_proc_entry("4b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_bin_4; proc_write_entry[10] = (s32) proc_entry; }
	proc_entry = create_proc_entry("5b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_bin_5; proc_write_entry[11] = (s32) proc_entry; }

	/* command files */
	proc_entry = create_proc_entry("mode", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_mode; proc_entry-> write_proc = (void *) adc_write_proc_mode; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[12] = (s32) proc_entry; }
	proc_entry = create_proc_entry("div", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = adc_read_proc_div; proc_entry-> write_proc = (void *) adc_write_proc_div; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[13] = (s32) proc_entry; }


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

	adc_set_mode (mode);
	adc_set_div  (divider);

	return 0;
}

static void adc_exit_module(void) {

	adc_remove_proc_fs();

	misc_deregister(&adc_dev);

        printk("%s: exit\n", DEVICE_NAME);

}

module_init(adc_init_module);
module_exit(adc_exit_module);
MODULE_DESCRIPTION("Virt2real ADC driver module version 1.1");
MODULE_AUTHOR("Shlomo Kut,,, (shl...@infodraw.com). Features added by Gol.");
MODULE_LICENSE("GPL v2");
