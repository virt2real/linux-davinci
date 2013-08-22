/*
 * Copyright (C) 2011 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Driver name : VPFE Capture driver
 *    VPFE Capture driver allows applications to capture and stream video
 *    frames on DaVinci SoCs (DM6446, DM355 etc) from a YUV source such as
 *    TVP5146 or  Raw Bayer RGB image data from an image sensor
 *    such as Microns' MT9T001, MT9T031 etc.
 *
 *    These SoCs have, in common, a Video Processing Subsystem (VPSS) that
 *    consists of a Video Processing Front End (VPFE) for capturing
 *    video/raw image data and Video Processing Back End (VPBE) for displaying
 *    YUV data through an in-built analog encoder or Digital LCD port. This
 *    driver is for capture through VPFE. A typical EVM using these SoCs have
 *    following high level configuration.
 *
 *    decoder(TVP5146/		YUV/
 *	MT9T001)   -->  Raw Bayer RGB ---> MUX -> VPFE (CCDC/ISIF)
 *			data input              |      |
 *							V      |
 *						      SDRAM    |
 *							       V
 *							   Image Processor
 *							       |
 *							       V
 *							     SDRAM
 *    The data flow happens from a decoder connected to the VPFE over a
 *    YUV embedded (BT.656/BT.1120) or separate sync or raw bayer rgb interface
 *    and to the input of VPFE through an optional MUX (if more inputs are
 *    to be interfaced on the EVM). The input data is first passed through
 *    CCDC (CCD Controller, a.k.a Image Sensor Interface, ISIF). The CCDC
 *    does very little or no processing on YUV data and does pre-process Raw
 *    Bayer RGB data through modules such as Defect Pixel Correction (DFC)
 *    Color Space Conversion (CSC), data gain/offset etc. After this, data
 *    can be written to SDRAM or can be connected to the image processing
 *    block such as IPIPE (on DM355/DM365 only).
 *
 *    Features supported
 *		- MMAP IO
 *		- USERPTR IO
 *		- Capture using TVP5146 over BT.656
 *		- support for interfacing decoders using sub device model
 *		- Work with DM365 or DM355 or DM6446 CCDC to do Raw Bayer
 *		  RGB/YUV data capture to SDRAM.
 *		- Chaining of Image Processor
 *		- SINGLE-SHOT mode
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <media/v4l2-common.h>
#include <media/v4l2-mediabus.h>

#include <media/media-entity.h>
#include <media/media-device.h>


#include <media/davinci/videohd.h>
#include <media/davinci/vpfe_capture.h>

#include <mach/cputype.h>

#include "ccdc_hw_device.h"

#define HD_IMAGE_SIZE		(1920 * 1080 * 2)
#define PAL_IMAGE_SIZE		(720 * 576 * 2)
#define SECOND_IMAGE_SIZE_MAX	(640 * 480 * 2)


static int debug;
static u32 numbuffers = 3;
static u32 bufsize = HD_IMAGE_SIZE + SECOND_IMAGE_SIZE_MAX;
static int interface;
static u32 cont_bufoffset;
static u32 cont_bufsize;

module_param(interface, bool, S_IRUGO);
module_param(numbuffers, uint, S_IRUGO);
module_param(bufsize, uint, S_IRUGO);
module_param(debug, bool, 0644);
module_param(cont_bufoffset, uint, S_IRUGO);
module_param(cont_bufsize, uint, S_IRUGO);

/**
 * VPFE capture can be used for capturing video such as from TVP5146 or TVP7002
 * and for capture raw bayer data from camera sensors such as mt9p031. At this
 * point there is problem in co-existence of mt9p031 and tvp5146 due to i2c
 * address collision. So set the variable below from bootargs to do either video
 * capture or camera capture.
 * interface = 0 - video capture (from TVP514x or such),
 * interface = 1 - Camera capture (from mt9p031 or such)
 * Re-visit this when we fix the co-existence issue
 */
MODULE_PARM_DESC(interface, "interface 0-1 (default:0)");
MODULE_PARM_DESC(numbuffers, "buffer count (default:3)");
MODULE_PARM_DESC(bufsize, "buffer size in bytes, (default:4147200 bytes)");
MODULE_PARM_DESC(debug, "Debug level 0-1");
MODULE_PARM_DESC(cont_bufoffset, "Capture buffer offset (default 0)");
MODULE_PARM_DESC(cont_bufsize, "Capture buffer size (default 0)");

MODULE_DESCRIPTION("VPFE Video for Linux Capture Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments");

/* lock for accessing ccdc information */
static DEFINE_MUTEX(ccdc_lock);

void mbus_to_pix(const struct v4l2_mbus_framefmt *mbus,
			   struct v4l2_pix_format *pix)
{
	/* TODO: revisit */
	switch (mbus->code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
		pix->pixelformat = V4L2_PIX_FMT_UYVY;
		pix->bytesperline = pix->width * 2;
		break;
	case V4L2_MBUS_FMT_YUYV8_2X8:
		pix->pixelformat = V4L2_PIX_FMT_UYVY;
		pix->bytesperline = pix->width * 2;
		break;
	case V4L2_MBUS_FMT_YUYV10_1X20:
		pix->pixelformat = V4L2_PIX_FMT_UYVY;
		pix->bytesperline = pix->width * 2;
		break;
	case V4L2_MBUS_FMT_SBGGR10_1X10:
		pix->pixelformat = V4L2_PIX_FMT_SBGGR16;
		pix->bytesperline = pix->width * 2;
		break;
	default:
		printk(KERN_ERR "invalid mbus code\n");
	}

	/* pitch should be 32 bytes aligned */
	pix->bytesperline = ALIGN(pix->bytesperline, 32);

	pix->sizeimage = pix->bytesperline * pix->height;
}
EXPORT_SYMBOL(mbus_to_pix);

/* ISR for VINT0*/
irqreturn_t vpfe_isr(int irq, void *dev_id)
{
	struct vpfe_device *vpfe_dev = dev_id;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_isr\n");

	ccdc_buffer_isr(&vpfe_dev->vpfe_ccdc);

	prv_buffer_isr(&vpfe_dev->vpfe_previewer);

	rsz_buffer_isr(&vpfe_dev->vpfe_resizer);

	return IRQ_HANDLED;
}

/* vpfe_vdint1_isr - isr handler for VINT1 interrupt */
irqreturn_t vpfe_vdint1_isr(int irq, void *dev_id)
{
	struct vpfe_device *vpfe_dev = dev_id;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_vdint1_isr\n");

	ccdc_vidint1_isr(&vpfe_dev->vpfe_ccdc);

	return IRQ_HANDLED;
}

irqreturn_t vpfe_imp_dma_isr(int irq, void *dev_id)
{
	struct vpfe_device *vpfe_dev = dev_id;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_imp_dma_isr\n");

	prv_dma_isr(&vpfe_dev->vpfe_previewer);

	rsz_dma_isr(&vpfe_dev->vpfe_resizer);

	return IRQ_HANDLED;
}

static struct vpfe_device *vpfe_initialize(void)
{
	struct vpfe_device *vpfe_dev;

	/* Allocate memory for device objects */
	vpfe_dev = kzalloc(sizeof(*vpfe_dev), GFP_KERNEL);

	/* initialize config settings */
	vpfe_dev->config_params.min_numbuffers = 3;
	vpfe_dev->config_params.numbuffers = 3;
	vpfe_dev->config_params.min_bufsize = 1280 * 720 * 2;
	vpfe_dev->config_params.device_bufsize = 1920 * 1080 * 2;

	/* Default number of buffers should be 3 */
	if ((numbuffers > 0) &&
	    (numbuffers < vpfe_dev->config_params.min_numbuffers))
		numbuffers = vpfe_dev->config_params.min_numbuffers;

	/*
	 * Set buffer size to min buffers size if invalid buffer size is
	 * given
	 */
	if (bufsize < vpfe_dev->config_params.min_bufsize)
		bufsize = vpfe_dev->config_params.min_bufsize;

	vpfe_dev->config_params.numbuffers = numbuffers;

	if (numbuffers)
		vpfe_dev->config_params.device_bufsize = ALIGN(bufsize, 4096);

	return vpfe_dev;
}

static void vpfe_disable_clock(struct vpfe_device *vpfe_dev)
{
	struct vpfe_config *vpfe_cfg = vpfe_dev->cfg;
	int i;

	for (i = 0; i < vpfe_cfg->num_clocks; i++) {
		clk_disable(vpfe_dev->clks[i]);
		clk_put(vpfe_dev->clks[i]);
	}

	kzfree(vpfe_dev->clks);
	v4l2_info(vpfe_dev->pdev->driver, "vpfe capture clocks disabled\n");
}

/**
 * vpfe_enable_clock() - Enable clocks for vpfe capture driver
 * @vpfe_dev - ptr to vpfe capture device
 *
 * Enables clocks defined in vpfe configuration. The function
 * assumes that at least one clock is to be defined which is
 * true as of now. re-visit this if this assumption is not true
 */
static int vpfe_enable_clock(struct vpfe_device *vpfe_dev)
{
	struct vpfe_config *vpfe_cfg = vpfe_dev->cfg;
	int ret = -EFAULT, i;

	if (!vpfe_cfg->num_clocks)
		return 0;

	vpfe_dev->clks = kzalloc(vpfe_cfg->num_clocks *
				   sizeof(struct clock *), GFP_KERNEL);

	if (NULL == vpfe_dev->clks) {
		v4l2_err(vpfe_dev->pdev->driver, "Memory allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < vpfe_cfg->num_clocks; i++) {
		if (NULL == vpfe_cfg->clocks[i]) {
			v4l2_err(vpfe_dev->pdev->driver,
				"clock %s is not defined in vpfe config\n",
				vpfe_cfg->clocks[i]);
			goto out;
		}

		vpfe_dev->clks[i] = clk_get(vpfe_dev->pdev,
					      vpfe_cfg->clocks[i]);
		if (NULL == vpfe_dev->clks[i]) {
			v4l2_err(vpfe_dev->pdev->driver,
				"Failed to get clock %s\n",
				vpfe_cfg->clocks[i]);
			goto out;
		}

		if (clk_enable(vpfe_dev->clks[i])) {
			v4l2_err(vpfe_dev->pdev->driver,
				"vpfe clock %s not enabled\n",
				vpfe_cfg->clocks[i]);
			goto out;
		}

		v4l2_info(vpfe_dev->pdev->driver, "vpss clock %s enabled",
			  vpfe_cfg->clocks[i]);
	}
	return 0;
out:
	for (i = 0; i < vpfe_cfg->num_clocks; i++) {
		if (vpfe_dev->clks[i])
			clk_put(vpfe_dev->clks[i]);
	}

	v4l2_err(vpfe_dev->pdev->driver,
				"failed to enable clocks\n");

	kzfree(vpfe_dev->clks);
	return ret;
}

static void vpfe_detach_irq(struct vpfe_device *vpfe_dev)
{
	free_irq(vpfe_dev->ccdc_irq0, vpfe_dev);
	free_irq(vpfe_dev->ccdc_irq1, vpfe_dev);
	free_irq(vpfe_dev->imp_dma_irq, vpfe_dev);
}

static int vpfe_attach_irq(struct vpfe_device *vpfe_dev)
{
	int ret = 0;

	ret = request_irq(vpfe_dev->ccdc_irq0, vpfe_isr, IRQF_DISABLED,
			"vpfe_capture0", vpfe_dev);
	if (ret < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"Error: requesting VINT0 interrupt\n");
		return ret;
	}

	ret = request_irq(vpfe_dev->ccdc_irq1,
					vpfe_vdint1_isr,
					IRQF_DISABLED,
					"vpfe_capture1", vpfe_dev);
	if (ret < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"Error: requesting VINT1 interrupt\n");
		free_irq(vpfe_dev->ccdc_irq0, vpfe_dev);
		return ret;
	}

	ret = request_irq(vpfe_dev->imp_dma_irq,
				vpfe_imp_dma_isr,
				IRQF_DISABLED,
				"Imp_Sdram_Irq",
				vpfe_dev);
	if (ret < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
				"Error: requesting IMP"
				" IRQ interrupt\n");
		free_irq(vpfe_dev->ccdc_irq1, vpfe_dev);
		free_irq(vpfe_dev->ccdc_irq0, vpfe_dev);
		return ret;
	}

	return 0;
}

static int register_i2c_devices(struct vpfe_device *vpfe_dev)
{
	struct vpfe_config *vpfe_cfg;
	struct i2c_adapter *i2c_adap;
	int i, k, ret;
	unsigned int num_subdevs;
	struct vpfe_subdev_info *sdinfo;

	vpfe_cfg = vpfe_dev->cfg;

	i2c_adap = i2c_get_adapter(1);
	num_subdevs = vpfe_cfg->num_subdevs;

	vpfe_dev->sd = kzalloc(sizeof(struct v4l2_subdev *) *num_subdevs,
			       GFP_KERNEL);

	if (NULL == vpfe_dev->sd) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"unable to allocate memory for subdevice pointers\n");
		return -ENOMEM;
	}

	for (i = 0, k = 0; i < num_subdevs; i++) {
		sdinfo = &vpfe_cfg->sub_devs[i];
		/**
		 * register subdevices based on interface setting. Currently
		 * tvp5146 and mt9p031 cannot co-exists due to i2c address
		 * conflicts. So only one of them is registered. Re-visit this
		 * once we have support for i2c switch handling in i2c driver
		 * framework
		 */

		if (interface == sdinfo->is_camera) {
			/* setup input path */
			if (vpfe_cfg->setup_input) {
				if (vpfe_cfg->setup_input(sdinfo->grp_id) < 0) {
					ret = -EFAULT;
					v4l2_info(&vpfe_dev->v4l2_dev, "could"
							" not setup input for %s\n",
							sdinfo->module_name);
					goto probe_sd_out;
				}
			}
			/* Load up the subdevice */
			vpfe_dev->sd[k] =
				v4l2_i2c_new_subdev_board(
						  &vpfe_dev->v4l2_dev,
						  i2c_adap,
						  &sdinfo->board_info,
						  NULL);//,1);
			if (vpfe_dev->sd[k]) {
				v4l2_info(&vpfe_dev->v4l2_dev,
						"v4l2 sub device %s registered\n",
						sdinfo->module_name);

				vpfe_dev->sd[k]->grp_id = sdinfo->grp_id;
				k++;

				sdinfo->registered = 1;
			}
			} else {
				v4l2_info(&vpfe_dev->v4l2_dev,
						"v4l2 sub device %s register fails\n",
						sdinfo->module_name);
			}
	}

	vpfe_dev->num_subdevs = k;
	return 0;

probe_sd_out:
	kzfree(vpfe_dev->sd);
	return ret;
}

static int vpfe_register_entities(struct vpfe_device *vpfe_dev)
{
	int ret, i;
	unsigned int flags = 0;

	/* register i2c devices first */
	ret = register_i2c_devices(vpfe_dev);
	if (ret)
		return ret;

	/* register rest of the sub-devs */
	ret = vpfe_ccdc_register_entities(&vpfe_dev->vpfe_ccdc,
					  &vpfe_dev->v4l2_dev);
	if (ret)
		return ret;

	ret = vpfe_previewer_register_entities(&vpfe_dev->vpfe_previewer,
					       &vpfe_dev->v4l2_dev);
	if (ret)
		goto out_ccdc_register;

	ret = vpfe_resizer_register_entities(&vpfe_dev->vpfe_resizer,
					     &vpfe_dev->v4l2_dev);
	if (ret)
		goto out_previewer_register;

	ret = vpfe_aew_register_entities(&vpfe_dev->vpfe_aew,
					 &vpfe_dev->v4l2_dev);
	if (ret)
		goto out_resizer_register;

	ret = vpfe_af_register_entities(&vpfe_dev->vpfe_af,
					&vpfe_dev->v4l2_dev);
	if (ret)
		goto out_aew_register;

	/* create links now, starting with external(i2c) entities */
	for (i = 0; i < vpfe_dev->num_subdevs; i++) {
		/* if entity has no pads (ex: amplifier),
		   cant establish link */
		if (vpfe_dev->sd[i]->entity.num_pads) {
			ret = media_entity_create_link(&vpfe_dev->sd[i]->entity,
				0, &vpfe_dev->vpfe_ccdc.subdev.entity,
				0, flags);
			if (ret < 0)
				goto out_resizer_register;
		}
	}

	ret = media_entity_create_link(&vpfe_dev->vpfe_ccdc.subdev.entity,
					1, &vpfe_dev->vpfe_aew.subdev.entity,
					0, flags);
	if (ret < 0)
		goto out_resizer_register;

	ret = media_entity_create_link(&vpfe_dev->vpfe_ccdc.subdev.entity,
					1, &vpfe_dev->vpfe_af.subdev.entity,
					0, flags);
	if (ret < 0)
		goto out_resizer_register;

	ret = media_entity_create_link(&vpfe_dev->vpfe_ccdc.subdev.entity,
				       1,
				       &vpfe_dev->vpfe_previewer.subdev.entity,
				       0, flags);
	if (ret < 0)
		goto out_resizer_register;

	ret = media_entity_create_link(&vpfe_dev->vpfe_previewer.subdev.entity,
				       1, &vpfe_dev->vpfe_resizer.subdev.entity,
				       0, flags);
	if (ret < 0)
		goto out_resizer_register;

	return 0;

out_aew_register:
	vpfe_aew_unregister_entities(&vpfe_dev->vpfe_aew);
out_resizer_register:
	vpfe_resizer_unregister_entities(&vpfe_dev->vpfe_resizer);
out_previewer_register:
	vpfe_previewer_unregister_entities(&vpfe_dev->vpfe_previewer);
out_ccdc_register:
	vpfe_ccdc_unregister_entities(&vpfe_dev->vpfe_ccdc);
	return ret;
}

static void vpfe_unregister_entities(struct vpfe_device *vpfe_dev)
{
	vpfe_ccdc_unregister_entities(&vpfe_dev->vpfe_ccdc);
	vpfe_previewer_unregister_entities(&vpfe_dev->vpfe_previewer);
	vpfe_resizer_unregister_entities(&vpfe_dev->vpfe_resizer);
	vpfe_aew_unregister_entities(&vpfe_dev->vpfe_aew);
	vpfe_af_unregister_entities(&vpfe_dev->vpfe_af);
}

static void vpfe_cleanup_modules(struct vpfe_device *vpfe_dev,
				 struct platform_device *pdev)
{
	vpfe_ccdc_cleanup(pdev);
	vpfe_previewer_cleanup(pdev);
	vpfe_resizer_cleanup(pdev);
	vpfe_aew_cleanup(pdev);
	vpfe_af_cleanup(pdev);
}

static int vpfe_initialize_modules(struct vpfe_device *vpfe_dev,
				   struct platform_device *pdev)
{
	int ret;

	ret = vpfe_ccdc_init(&vpfe_dev->vpfe_ccdc, pdev);
	if (ret)
		return ret;

	ret = vpfe_previewer_init(&vpfe_dev->vpfe_previewer, pdev);
	if (ret)
		goto out_ccdc_init;

	ret = vpfe_resizer_init(&vpfe_dev->vpfe_resizer, pdev);
	if (ret)
		goto out_previewer_init;

	ret = vpfe_aew_init(&vpfe_dev->vpfe_aew, pdev);
	if (ret)
		goto out_resizer_init;

	ret = vpfe_af_init(&vpfe_dev->vpfe_af, pdev);
	if (ret)
		goto out_resizer_init;

	return 0;

out_resizer_init:
	vpfe_resizer_cleanup(pdev);
out_previewer_init:
	vpfe_previewer_cleanup(pdev);
out_ccdc_init:
	vpfe_ccdc_cleanup(pdev);
	return ret;
}

/**
 * vpfe_probe : vpfe probe function
 * @pdev: platform device pointer
 *
 * This function creates device entries by register itself to the V4L2 driver
 * and initializes fields of each device objects
 */
static int vpfe_probe(struct platform_device *pdev)
{
	struct vpfe_config *vpfe_cfg;
	struct resource *res1;
	struct vpfe_device *vpfe_dev;
	int ret = -ENOMEM, err;
	unsigned long phys_end_kernel;
	size_t size;

	/* Get the pointer to the device object */
	vpfe_dev = vpfe_initialize();

	if (!vpfe_dev) {
		v4l2_err(pdev->dev.driver,
			"Failed to allocate memory for vpfe_dev\n");
		return ret;
	}

	vpfe_dev->pdev = &pdev->dev;

	if (cont_bufsize) {
		/* attempt to determine the end of Linux kernel memory */
		phys_end_kernel = virt_to_phys((void *)PAGE_OFFSET) +
			(num_physpages << PAGE_SHIFT);
		size = cont_bufsize;
		phys_end_kernel += cont_bufoffset;
		err = dma_declare_coherent_memory(&pdev->dev, phys_end_kernel,
				phys_end_kernel, size,
				DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE);
		if (!err) {
			dev_err(&pdev->dev, "Unable to declare MMAP memory.\n");
			ret = -ENOENT;
			goto probe_free_dev_mem;
		}
		vpfe_dev->config_params.video_limit = size;
	}

	if (NULL == pdev->dev.platform_data) {
		v4l2_err(pdev->dev.driver, "Unable to get vpfe config\n");
		ret = -ENOENT;
		goto probe_free_dev_mem;
	}

	vpfe_cfg = pdev->dev.platform_data;
	vpfe_dev->cfg = vpfe_cfg;
	if (NULL == vpfe_cfg->card_name ||
	    NULL == vpfe_cfg->sub_devs) {
		v4l2_err(pdev->dev.driver, "null ptr in vpfe_cfg\n");
		ret = -ENOENT;
		goto probe_free_dev_mem;
	}

	/* enable vpss clocks */
	ret = vpfe_enable_clock(vpfe_dev);
	if (ret)
		goto probe_free_dev_mem;

	mutex_lock(&ccdc_lock);

	if (vpfe_initialize_modules(vpfe_dev, pdev))
		goto probe_disable_clock;

	/* Get VINT0 irq resource */
	res1 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res1) {
		v4l2_err(pdev->dev.driver,
			 "Unable to get interrupt for VINT0\n");
		ret = -ENOENT;
		goto probe_out_ccdc_cleanup;
	}
	vpfe_dev->ccdc_irq0 = res1->start;

	/* Get VINT1 irq resource */
	res1 = platform_get_resource(pdev,
				IORESOURCE_IRQ, 1);
	if (!res1) {
		v4l2_err(pdev->dev.driver,
			 "Unable to get interrupt for VINT1\n");
		ret = -ENOENT;
		goto probe_out_ccdc_cleanup;
	}
	vpfe_dev->ccdc_irq1 = res1->start;

	/* Get PRVUINT irq resource */
	res1 = platform_get_resource(pdev,
				IORESOURCE_IRQ, 2);
	if (!res1) {
		v4l2_err(pdev->dev.driver,
			 "Unable to get interrupt for PRVUINT\n");
		ret = -ENOENT;
		goto probe_out_ccdc_cleanup;
	}
	vpfe_dev->imp_dma_irq = res1->start;

	vpfe_dev->media_dev.dev = vpfe_dev->pdev;
	strcpy((char *)&vpfe_dev->media_dev.model, "davinci-media");
	ret = media_device_register(&vpfe_dev->media_dev);
	if (ret)
		goto probe_out_ccdc_cleanup;

	vpfe_dev->v4l2_dev.mdev = &vpfe_dev->media_dev;

	ret = v4l2_device_register(&pdev->dev, &vpfe_dev->v4l2_dev);
	if (ret) {
		v4l2_err(pdev->dev.driver,
			"Unable to register v4l2 device.\n");
		goto probe_out_video_release;
	}
	v4l2_info(&vpfe_dev->v4l2_dev, "v4l2 device registered\n");

	/* register video device */
	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
		"trying to register vpfe device.\n");

	/* set the driver data in platform device */
	platform_set_drvdata(pdev, vpfe_dev);

	/* register subdevs/entities */
	if (vpfe_register_entities(vpfe_dev))
		goto probe_out_video_unregister;

	ret = vpfe_attach_irq(vpfe_dev);
	if (ret)
		goto probe_out_register_entities;

	mutex_unlock(&ccdc_lock);
	return 0;

probe_out_register_entities:
	vpfe_register_entities(vpfe_dev);
probe_out_video_unregister:
	/*TODO we need this?*/
probe_out_video_release:
	/*TODO we need this?*/
probe_out_ccdc_cleanup:
	vpfe_cleanup_modules(vpfe_dev, pdev);
probe_disable_clock:
	vpfe_disable_clock(vpfe_dev);
	mutex_unlock(&ccdc_lock);
probe_free_dev_mem:
	kzfree(vpfe_dev);
	return ret;
}

/*
 * vpfe_remove : It un-registers device from V4L2 driver
 */
static int vpfe_remove(struct platform_device *pdev)
{
	struct vpfe_device *vpfe_dev = platform_get_drvdata(pdev);

	v4l2_info(pdev->dev.driver, "vpfe_remove\n");

	kzfree(vpfe_dev->sd);
	vpfe_detach_irq(vpfe_dev);
	vpfe_unregister_entities(vpfe_dev);
	vpfe_cleanup_modules(vpfe_dev, pdev);
	v4l2_device_unregister(&vpfe_dev->v4l2_dev);
	vpfe_disable_clock(vpfe_dev);
	kzfree(vpfe_dev);
	return 0;
}

static struct platform_driver vpfe_driver = {
	.driver = {
		.name = CAPTURE_DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = vpfe_probe,
	.remove = vpfe_remove,
};

static __init int vpfe_init(void)
{
	printk(KERN_NOTICE "vpfe_init\n");
	/* Register driver to the kernel */
	return platform_driver_register(&vpfe_driver);
}

/**
 * vpfe_cleanup : This function un-registers device driver
 */
static void vpfe_cleanup(void)
{
	platform_driver_unregister(&vpfe_driver);
}

module_init(vpfe_init);
module_exit(vpfe_cleanup);
