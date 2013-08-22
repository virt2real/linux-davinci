/*
* Copyright (C) 2008-2009 Texas Instruments Inc
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _VPFE_CAPTURE_H
#define _VPFE_CAPTURE_H

#ifdef __KERNEL__

/* Header files */
#include <media/v4l2-dev.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include <linux/clk.h>
#include <linux/i2c.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/videobuf-dma-contig.h>
#include <media/davinci/vpfe_types.h>
#include <media/davinci/imp_hw_if.h>
#include <media/davinci/dm365_ipipe.h>

#include "vpfe_video.h"
#include "vpfe_ccdc.h"
#include "vpfe_resizer.h"
#include "vpfe_previewer.h"
#include "vpfe_aew.h"
#include "vpfe_af.h"

#define VPFE_CAPTURE_NUM_DECODERS        5

/* Macros */
#define VPFE_MAJOR_RELEASE              0
#define VPFE_MINOR_RELEASE              0
#define VPFE_BUILD                      1
#define VPFE_CAPTURE_VERSION_CODE       ((VPFE_MAJOR_RELEASE << 16) | \
					(VPFE_MINOR_RELEASE << 8)  | \
					VPFE_BUILD)

#define CAPTURE_DRV_NAME		"vpfe-capture"

#define to_vpfe_device(ptr_module)				\
	container_of(ptr_module, struct vpfe_device, vpfe_##ptr_module)
#define to_device(ptr_module)						\
	(to_vpfe_device(ptr_module)->dev)

void dump_ccdc_regs(void);

struct vpfe_pixel_format {
	struct v4l2_fmtdesc fmtdesc;
	/* bytes per pixel */
	int bpp;
	/* decoder format */
	u32 subdev_pix_fmt;
};

struct vpfe_route {
	u32 input;
	u32 output;
};

enum vpfe_subdev_id {
	VPFE_SUBDEV_TVP5146 = 1,
	VPFE_SUBDEV_MT9T031 = 2,
	VPFE_SUBDEV_TVP7002 = 3,
	VPFE_SUBDEV_MT9P031 = 4,
};

struct vpfe_subdev_info {
	/* v4l2 subdev */
	struct v4l2_subdev *subdev;
	/* Sub device module name */
	char module_name[32];
	/* Sub device group id */
	int grp_id;
	/* Number of inputs supported */
	int num_inputs;
	/* inputs available at the sub device */
	struct v4l2_input *inputs;
	/* Sub dev routing information for each input */
	struct vpfe_route *routes;
	/* ccdc bus/interface configuration */
	struct vpfe_hw_if_param ccdc_if_params;
	/* i2c subdevice board info */
	struct i2c_board_info board_info;
	/* Is this a camera sub device ? */
	unsigned is_camera:1;
	/* check if sub dev supports routing */
	unsigned can_route:1;
	/* registered ? */
	unsigned registered:1;
};

struct vpfe_config_params {
	u8 min_numbuffers;
	u8 numbuffers;
	u32 min_bufsize;
	u32 device_bufsize;
	u32 video_limit;
};

struct vpfe_config {
	/* Number of sub devices connected to vpfe */
	int num_subdevs;
	/* information about each subdev */
	struct vpfe_subdev_info *sub_devs;
	/* evm card info */
	char *card_name;
	/* setup function for the input path */
	int (*setup_input)(enum vpfe_subdev_id id);
	/* number of clocks */
	int num_clocks;
	/* clocks used for vpfe capture */
	char *clocks[];
};

struct vpfe_device {
	/* V4l2 specific parameters */
	/* Identifies video device for this channel */
	/* sub devices */
	struct v4l2_subdev **sd;
	/* number of registered subdevs */
	unsigned int num_subdevs;
	/* vpfe cfg */
	struct vpfe_config *cfg;
	/* clock ptrs for vpfe capture */
	struct clk **clks;
	/* V4l2 device */
	struct v4l2_device v4l2_dev;
	/* parent device */
	struct device *pdev;
	/* IRQ number for DMA transfer completion at the image processor */
	unsigned int imp_dma_irq;
	/* CCDC IRQs used when CCDC/ISIF output to SDRAM */
	unsigned int ccdc_irq0;
	unsigned int ccdc_irq1;
	/* number of buffers in fbuffers */
	u32 numbuffers;
	struct vpfe_config_params	config_params;

	struct media_device		media_dev;
	struct vpfe_ccdc_device		vpfe_ccdc;
	struct vpfe_resizer_device	vpfe_resizer;
	struct vpfe_previewer_device	vpfe_previewer;
	struct vpfe_aew_device		vpfe_aew;
	struct vpfe_af_device		vpfe_af;
};

/* File handle structure */
struct vpfe_fh {
	struct vpfe_video_device *video;
	/* Indicates whether this file handle is doing IO */
	u8 io_allowed;
	/* Used to keep track priority of this instance */
	enum v4l2_priority prio;
};



void mbus_to_pix(const struct v4l2_mbus_framefmt *mbus,
			   struct v4l2_pix_format *pix);
irqreturn_t vpfe_isr(int irq, void *dev_id);
irqreturn_t vpfe_vdint1_isr(int irq, void *dev_id);
irqreturn_t vpfe_imp_dma_isr(int irq, void *dev_id);


#endif				/* End of __KERNEL__ */
#endif				/* _DAVINCI_VPFE_H */
