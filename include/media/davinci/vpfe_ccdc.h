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
 */

#ifndef _VPFE_CCDC_H
#define _VPFE_CCDC_H

#include <linux/davinci_user.h>

#define CCDC_PAD_SINK      0
#define CCDC_PAD_SOURCE    1

#define CCDC_PADS_NUM      2

#define DAVINCI_CCDC_NEVENTS 0

enum ccdc_input_entity {
	CCDC_INPUT_NONE,
	CCDC_INPUT_PARALLEL,
};

#define CCDC_OUTPUT_NONE	(0)
#define CCDC_OUTPUT_MEMORY	(1 << 0)
#define CCDC_OUTPUT_RESIZER	(1 << 1)
#define CCDC_OUTPUT_PREVIEWER	(1 << 2)

#define CCDC_NOT_CHAINED	0
#define CCDC_CHAINED		1

struct vpfe_ccdc_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[CCDC_PADS_NUM];
	struct v4l2_mbus_framefmt	formats[CCDC_PADS_NUM];
	enum ccdc_input_entity		input;
	unsigned int			output;

	struct ccdc_hw_device		*ccdc_dev;
	struct v4l2_rect		crop;

	/*added for independent video device */
	struct vpfe_video_device	video_out;
};

int dm644x_ccdc_init(struct platform_device *pdev);
int dm365_ccdc_init(struct platform_device *pdev);
int dm355_ccdc_init(struct platform_device *pdev);
void dm365_ccdc_remove(struct platform_device *pdev);
void dm644x_ccdc_remove(struct platform_device *pdev);
void dm355_ccdc_remove(struct platform_device *pdev);
void dump_dm365ccdc_regs(void);
int vpfe_ccdc_register_entities(struct vpfe_ccdc_device *ccdc,
				struct v4l2_device *v4l2_dev);
void vpfe_ccdc_unregister_entities(struct vpfe_ccdc_device *ccdc);
void vpfe_ccdc_cleanup(struct platform_device *pdev);
int vpfe_ccdc_init(struct vpfe_ccdc_device *vpfe_ccdc,
		   struct platform_device *pdev);
void vpfe_sbl_reset(struct vpfe_ccdc_device *vpfe_ccdc);
enum v4l2_field ccdc_get_fid(struct vpfe_device *vpfe_dev);

void ccdc_vidint1_isr(struct vpfe_ccdc_device *ccdc);
void ccdc_buffer_isr(struct vpfe_ccdc_device *ccdc);
#endif
