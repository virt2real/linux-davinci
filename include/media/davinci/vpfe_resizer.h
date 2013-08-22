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

#ifndef _VPFE_RSZ_H
#define _VPFE_RSZ_H

#define RESIZER_PAD_SINK	0
#define RESIZER_PAD_SOURCE	1

#define RESIZER_PADS_NUM	2

enum resizer_input_entity {
	RESIZER_INPUT_NONE,
	RESIZER_INPUT_MEMORY,
	RESIZER_INPUT_PREVIEWER,
};

#define RESIZER_OUTPUT_NONE	(0)
#define RESIZER_OUPUT_MEMORY	(1 << 0)

struct vpfe_resizer_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[RESIZER_PADS_NUM];
	struct v4l2_mbus_framefmt	formats[RESIZER_PADS_NUM];
	enum resizer_input_entity	input;
	unsigned int			output;

	/* pointer to ipipe function pointers */
	struct imp_hw_interface		*imp_hw_if;
	struct rsz_channel_config	chan_config;
	struct imp_logical_channel	channel;

	struct vpfe_video_device	video_in;
	struct vpfe_video_device	video_out;
};

void vpfe_resizer_cleanup(struct platform_device *pdev);
int vpfe_resizer_init(struct vpfe_resizer_device *vpfe_rsz,
		      struct platform_device *pdev);
void vpfe_resizer_unregister_entities(struct vpfe_resizer_device *vpfe_rsz);
int vpfe_resizer_register_entities(struct vpfe_resizer_device *vpfe_rsz,
				   struct v4l2_device *v4l2_dev);

void rsz_dma_isr(struct vpfe_resizer_device *resizer);
void rsz_buffer_isr(struct vpfe_resizer_device *resizer);
#endif
