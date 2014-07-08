#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "v2r_ppm"
#define PPM_VERSION "0.1"

#define PPM_MAX_CHANNELS	8

/* for interrupts */
#define	GAP					4000
static unsigned long last_usec = 0;
static int channel_index = 0;

#define MAX_CHANNELS		12
/* end for interrupts */


/**********************************************************************
 for PROC_FS 
**********************************************************************/

#ifndef CONFIG_PROC_FS

static int ppm_add_proc_fs(void) {
	return 0;
}

static int ppm_remove_proc_fs(void) {
	return 0;
}

#else

static struct proc_dir_entry *proc_parent;
static struct proc_dir_entry *proc_entry;
static s32 proc_write_entry[PPM_MAX_CHANNELS*2 + 2];

/* text read */
static int ppm_read_proc (int adc_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

}

/* binary read */
static int ppm_read_proc_bin (int adc_number, char *buf, char **start, off_t offset, int count, int *eof, void *data ) {

}



/* command files */

/* get ADC run mode */
static int ppm_read_proc_mode (char *buf, char **start, off_t offset, int count, int *eof, void *data ) {
}

/* set ADC run mode */
static int ppm_write_proc_mode (struct file *file, const char *buf, int count, void *data) {
}


/* get ADC divider */
static int ppm_read_proc_div (char *buf, char **start, off_t offset, int count, int *eof, void *data ) {
	return 0;
}

/* set ADC divider */
static int ppm_write_proc_div (struct file *file, const char *buf, int count, void *data) {

}



/* *i'd line to use array init, but i don't know how get file id from unified functions */

/* text read */
static int ppm_read_proc_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc (0, buf, start, offset, count, eof, data); }
static int ppm_read_proc_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc (1, buf, start, offset, count, eof, data); }
static int ppm_read_proc_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc (2, buf, start, offset, count, eof, data); }
static int ppm_read_proc_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc (3, buf, start, offset, count, eof, data); }
static int ppm_read_proc_4 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc (4, buf, start, offset, count, eof, data); }
static int ppm_read_proc_5 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc (5, buf, start, offset, count, eof, data); }

/* binary read */
static int ppm_read_proc_bin_0 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc_bin (0, buf, start, offset, count, eof, data); }
static int ppm_read_proc_bin_1 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc_bin (1, buf, start, offset, count, eof, data); }
static int ppm_read_proc_bin_2 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc_bin (2, buf, start, offset, count, eof, data); }
static int ppm_read_proc_bin_3 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc_bin (3, buf, start, offset, count, eof, data); }
static int ppm_read_proc_bin_4 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc_bin (4, buf, start, offset, count, eof, data); }
static int ppm_read_proc_bin_5 (char *buf, char **start, off_t offset, int count, int *eof, void *data ) { return ppm_read_proc_bin (5, buf, start, offset, count, eof, data); }


static int ppm_remove_proc_fs(void) {

	int i;
	char fn[10];

	for (i = 0; i < (PPM_MAX_CHANNELS*2); i++) {

		if (proc_write_entry[i]) { 
			sprintf(fn, "%d", i);
			remove_proc_entry(fn, proc_parent);
		}

	}

	/* remove proc_fs directory */
	remove_proc_entry("v2r_ppm",NULL);

	return 0;
}

static int ppm_add_proc_fs(void) {

	proc_parent = proc_mkdir("v2r_ppm", NULL);

	if (!proc_parent) {
		printk("%s: error creating proc entry (/proc/v2r_ppm)\n", DEVICE_NAME);
		return 1;
	}

	/* text files */
	proc_entry = create_proc_entry("0", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_0; proc_write_entry[0] = (s32) proc_entry; }
	proc_entry = create_proc_entry("1", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_1; proc_write_entry[1] = (s32) proc_entry; }
	proc_entry = create_proc_entry("2", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_2; proc_write_entry[2] = (s32) proc_entry; }
	proc_entry = create_proc_entry("3", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_3; proc_write_entry[3] = (s32) proc_entry; }
	proc_entry = create_proc_entry("4", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_4; proc_write_entry[4] = (s32) proc_entry; }
	proc_entry = create_proc_entry("5", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_5; proc_write_entry[5] = (s32) proc_entry; }

	/* binary files */
	proc_entry = create_proc_entry("0b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_bin_0; proc_write_entry[6] = (s32) proc_entry; }
	proc_entry = create_proc_entry("1b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_bin_1; proc_write_entry[7] = (s32) proc_entry; }
	proc_entry = create_proc_entry("2b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_bin_2; proc_write_entry[8] = (s32) proc_entry; }
	proc_entry = create_proc_entry("3b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_bin_3; proc_write_entry[9] = (s32) proc_entry; }
	proc_entry = create_proc_entry("4b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_bin_4; proc_write_entry[10] = (s32) proc_entry; }
	proc_entry = create_proc_entry("5b", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_bin_5; proc_write_entry[11] = (s32) proc_entry; }

	/* command files */
	proc_entry = create_proc_entry("mode", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_mode; proc_entry-> write_proc = (void *) ppm_write_proc_mode; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[12] = (s32) proc_entry; }
	proc_entry = create_proc_entry("div", 0666, proc_parent); if (proc_entry) { proc_entry-> read_proc = ppm_read_proc_div; proc_entry-> write_proc = (void *) ppm_write_proc_div; proc_entry-> mode = S_IFREG | S_IRUGO; proc_write_entry[13] = (s32) proc_entry; }


	return 0;
}

#endif /* CONFIG_PROC_FS */


/**********************************************************************
 end for PROC_FS 
**********************************************************************/


static ssize_t ppm_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {

	unsigned short data [PPM_MAX_CHANNELS];

	if (count < sizeof(unsigned short))
		return -ETOOSMALL;


	if (copy_to_user(buf, data, count))
		return -EFAULT;

	return count;

}

static int ppm_open(struct inode *inode, struct file *file) {
	return 0;
}


static int ppm_release(struct inode *inode, struct file *file) {
	return 0;
}


static const struct file_operations ppm_fops = {
	.owner    = THIS_MODULE,
	.read   = ppm_read,
	.open   = ppm_open,
	.release  = ppm_release,
};


static struct miscdevice ppm_dev = {

	NVRAM_MINOR,
	"v2r_ppm",
	&ppm_fops

};


static irqreturn_t gpio_event_irq( int irq, void *dev_id ) {

	unsigned long usec;
	unsigned long delay;
	struct timeval time;
	do_gettimeofday(&time);


            usec = time.tv_sec * 1000000 + time.tv_usec;

            if (!last_usec) {
                last_usec = usec;
                return IRQ_HANDLED;
            }

            delay = usec - last_usec;
            last_usec = usec;

            if (delay > GAP) { // gap is 2 x max channel duration 
                // End of channels
                //printk("\n");
                channel_index = 0;

            } else {
                // Got next channel
                channel_index++;

                if (channel_index==1) printk("  %d:%5ld\n", channel_index, delay);
            };

	return IRQ_HANDLED;
}

static int ppm_init_module(void) {

	int ret;
	ret = misc_register(&ppm_dev);

	printk("%s: device is created successfully\n", DEVICE_NAME);

	if (ret) {
		printk(KERN_ERR "%s: can't misc_register on minor=%d\n", DEVICE_NAME, NVRAM_MINOR);
		//return ret;
	}

	ret = ppm_add_proc_fs();

	if (ret) {
		misc_deregister(&ppm_dev);
		printk(KERN_ERR "%s: can't create /proc/driver/v2r_ppm\n", DEVICE_NAME);
		return ret;
	}


int dir_err;
int irq_req_res;
int retval;
int interrupt_gpio;
interrupt_gpio=3;

channel_index = 0;

if(gpio_request(interrupt_gpio, "gpio_interrupt")){
  printk(KERN_ALERT "Unable to request gpio %d",interrupt_gpio);
}
if( (dir_err=gpio_direction_input(interrupt_gpio)) < 0 ){
  printk(KERN_ALERT "Impossible to set input direction");
}

if ( (irq_req_res = request_irq( gpio_to_irq(interrupt_gpio), gpio_event_irq, IRQF_TRIGGER_FALLING, "gpio_interrupt", NULL)) < 0) {
   if (irq_req_res == -EBUSY)
     retval=irq_req_res;
   else
     retval=-EINVAL;
 }




	return 0;
}

static void ppm_exit_module(void) {

	ppm_remove_proc_fs();

	misc_deregister(&ppm_dev);

        printk("%s: exit\n", DEVICE_NAME);

}

module_init(ppm_init_module);
module_exit(ppm_exit_module);
MODULE_DESCRIPTION("Virt2real PPM driver module version 0.1");
MODULE_LICENSE("GPL");
