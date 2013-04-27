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
#define ADC_VERSION "1.0"

 void *dm365_adc_base;
 
 int adc_single(unsigned int channel)
 {
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

 static void adc_read_block(unsigned short *data, size_t length)
 {
   int i;
   spin_lock_irq(&adc_lock);
     for(i = 0; i < length; i++) {
       data [i] = adc_single(i);
     }
   spin_unlock_irq(&adc_lock);
 }
 
 #ifndef CONFIG_PROC_FS
 static int adc_add_proc_fs(void)
 {
   return 0;
 }

 #else
 static int adc_proc_read(struct seq_file *seq, void *offset)
 {
   int i;
   unsigned short data [ADC_MAX_CHANNELS];

   adc_read_block(data,ADC_MAX_CHANNELS);

   for(i = 0; i < ADC_MAX_CHANNELS; i++) {
     seq_printf(seq, "0x%04X\n", data[i]);
   }

   return 0;
 }

 static int adc_proc_open(struct inode *inode, struct file *file)
 {
   return single_open(file, adc_proc_read, NULL);
 }

 static const struct file_operations adc_proc_fops = {
   .owner    = THIS_MODULE,
   .open   = adc_proc_open,
   .read   = seq_read,
   .release  = single_release,
 };

 static int adc_add_proc_fs(void)
 {
   if (!proc_create("driver/v2r_adc", 0, NULL, &adc_proc_fops))
     return -ENOMEM;
   return 0;
 }

 #endif /* CONFIG_PROC_FS */

 static ssize_t adc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
 {
   unsigned short data [ADC_MAX_CHANNELS];

   if (count < sizeof(unsigned short))
     return -ETOOSMALL;

   adc_read_block(data,ADC_MAX_CHANNELS);

   if (copy_to_user(buf, data, count))
     return -EFAULT;

   return count;

 }
 static int adc_open(struct inode *inode, struct file *file)
 {
   return 0;
 }
 static int adc_release(struct inode *inode, struct file *file)
 {
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


 static int adc_init_module(void)
 {
   int ret;
   ret = misc_register(&adc_dev);
   if (ret) {
     printk(KERN_ERR "v2r_adc: can't misc_register on minor=%d\n",
         NVRAM_MINOR);
     return ret;
   }
   ret = adc_add_proc_fs();
   if (ret) {
     misc_deregister(&adc_dev);
     printk(KERN_ERR "v2r_adc: can't create /proc/driver/v2r_adc\n");
     return ret;
   }
   if (!devm_request_mem_region(adc_dev.this_device, DM365_ADC_BASE,64,"v2r_adc"))
      return -EBUSY;

   dm365_adc_base = devm_ioremap_nocache(adc_dev.this_device,DM365_ADC_BASE, 64);// Physical address,Number of bytes to be mapped.
   if (!dm365_adc_base)
     return -ENOMEM;

   printk(KERN_INFO "TI Davinci ADC v" ADC_VERSION "\n");
   return 0;
 }

 static void adc_exit_module(void)
 {
   remove_proc_entry("driver/v2r_adc", NULL);
   misc_deregister(&adc_dev);
        printk( KERN_DEBUG "Module v2r_adc exit\n" );
 }

 module_init(adc_init_module);
 module_exit(adc_exit_module);
 //MODULE_DESCRIPTION("TI Davinci Dm365 Virt2real ADC");
 //MODULE_AUTHOR("Shlomo Kut,,, (shl...@infodraw.com)");
 //MODULE_LICENSE("GPL");
