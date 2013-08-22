/*
 * Copyright (C) 2009 MontaVista Software Inc.
 * Copyright (C) 2006 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option)any later version.
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

#ifndef VPBE_FB_H
#define VPBE_FB_H

#include <linux/poll.h>
#include <linux/wait.h>

#define DAVINCIFB_NAME "davincifb"

/* There are 4 framebuffer devices, one per window. */
#define OSD0_FBNAME "dm_osd0_fb"
#define OSD1_FBNAME "dm_osd1_fb"
#define VID0_FBNAME "dm_vid0_fb"
#define VID1_FBNAME "dm_vid1_fb"

/*  Structure for each window */
struct vpbe_dm_win_info {
	struct fb_info *info;
	struct vpbe_dm_info *dm;
	enum osd_layer layer;
	unsigned xpos;
	unsigned ypos;
	unsigned own_window; /* Does the framebuffer driver own this window? */
	unsigned display_window;
	unsigned sdram_address;
	unsigned int pseudo_palette[16];
};

/*
 * Structure for the driver holding information of windows,
 *  memory base addresses etc.
 */
struct vpbe_dm_info {
	struct vpbe_dm_win_info win[4];

	wait_queue_head_t vsync_wait;
	unsigned int vsync_cnt;
	int timeout;
	struct venc_callback vsync_callback;

	unsigned char ram_clut[256][3];
	enum osd_pix_format yc_pixfmt;

	struct fb_videomode mode;
	struct vpbe_device *vpbe_dev;
};

#endif				/* ifndef DAVINCIFB__H */
