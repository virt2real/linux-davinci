/*
 * Copyright (C) 2010 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _VPBE_VENC_H
#define _VPBE_VENC_H

#include <media/v4l2-subdev.h>
#include <media/davinci/vpbe_types.h>

#define VPBE_VENC_SUBDEV_NAME "vpbe-venc"

/* venc events */
#define VENC_END_OF_FRAME	BIT(0)
#define VENC_FIRST_FIELD	BIT(1)
#define VENC_SECOND_FIELD	BIT(2)

/**
 * struct venc_callback
 * @next: used internally by the venc driver to maintain a linked list of
 *        callbacks
 * @mask: a bitmask specifying the venc event(s) for which the
 *        callback will be invoked
 * @handler: the callback routine
 * @arg: a null pointer that is passed as the second argument to the callback
 *       routine
 */
struct venc_callback {
	struct venc_callback *next;
	char *owner;
	unsigned mask;
	void (*handler) (unsigned event, void *arg);
	void *arg;
};

struct venc_platform_data {
	enum vpbe_types venc_type;
	int (*setup_pinmux)(enum v4l2_mbus_pixelcode if_type,
			    int field);
	int (*setup_clock)(enum vpbe_enc_timings_type type,
			__u64 mode);
	int (*setup_if_config)(enum v4l2_mbus_pixelcode pixcode);

	/* Number of LCD outputs supported */
	int num_lcd_outputs;
	enum v4l2_mbus_pixelcode if_params;
};

enum venc_ioctls {
	VENC_GET_FLD = 1,
	VENC_REG_CALLBACK,
	VENC_UNREG_CALLBACK,
	VENC_INTERRUPT,
	VENC_CONFIGURE,
};

void enable_lcd(void);
void enable_hd_clk(void);

#endif
