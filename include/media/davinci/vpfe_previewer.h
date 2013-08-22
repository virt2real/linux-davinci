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

#ifndef _VPFE_PREV_H
#define _VPFE_PREV_H

#define PREVIEWER_PAD_SINK	0
#define PREVIEWER_PAD_SOURCE	1

#define PREVIEWER_PADS_NUM 2

enum previewer_input_entity {
	PREVIEWER_INPUT_NONE,
	PREVIEWER_INPUT_MEMORY,
	PREVIEWER_INPUT_CCDC,
};

#define PREVIEWER_OUTPUT_NONE		(0)
#define PREVIEWER_OUTPUT_MEMORY		(1 << 0)
#define PREVIEWER_OUTPUT_RESIZER	(1 << 1)

struct vpfe_previewer_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[PREVIEWER_PADS_NUM];
	struct v4l2_mbus_framefmt	formats[PREVIEWER_PADS_NUM];
	enum previewer_input_entity	input;
	unsigned int			output;

	/* pointer to ipipe function pointers */
	struct imp_hw_interface		*imp_hw_if;
	struct prev_channel_config      prv_config;
	struct imp_logical_channel	channel;

	struct vpfe_video_device	video_in;
	struct vpfe_video_device	video_out;
};

void vpfe_previewer_cleanup(struct platform_device *pdev);
int vpfe_previewer_init(struct vpfe_previewer_device *vpfe_prev,
			struct platform_device *pdev);
void vpfe_previewer_unregister_entities
		    (struct vpfe_previewer_device *vpfe_prev);
int vpfe_previewer_register_entities(struct vpfe_previewer_device *vpfe_prev,
				     struct v4l2_device *v4l2_dev);

void prv_dma_isr(struct vpfe_previewer_device *previewer);
void prv_buffer_isr(struct vpfe_previewer_device *previewer);
#endif
