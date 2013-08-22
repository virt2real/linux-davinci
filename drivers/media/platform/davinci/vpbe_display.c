/*
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <mach/cputype.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/davinci/vpbe_display.h>
#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe.h>
#include <media/davinci/vpbe_venc.h>
#include <media/davinci/vpbe_osd.h>
#include "vpbe_venc_regs.h"

#define VPBE_DISPLAY_DRIVER "vpbe-v4l2"

static int debug;
static u32 video2_numbuffers = 3;
static u32 video3_numbuffers = 3;
static u32 cont2_bufoffset;
static u32 cont2_bufsize;
static u32 cont3_bufoffset;
static u32 cont3_bufsize;

#define VPBE_DISPLAY_SD_BUF_SIZE (720*576*2)
#define VPBE_DISPLAY_HD_BUF_SIZE (1920*1080*2)
#define VPBE_DEFAULT_NUM_BUFS 3

/*static u32 video2_bufsize = VPBE_DISPLAY_SD_BUF_SIZE;*/
static u32 video2_bufsize = VPBE_DISPLAY_HD_BUF_SIZE;
static u32 video3_bufsize = VPBE_DISPLAY_SD_BUF_SIZE;

module_param(video2_numbuffers, uint, S_IRUGO);
module_param(video3_numbuffers, uint, S_IRUGO);
module_param(video2_bufsize, uint, S_IRUGO);
module_param(video3_bufsize, uint, S_IRUGO);
module_param(cont2_bufoffset, uint, S_IRUGO);
module_param(cont2_bufsize, uint, S_IRUGO);
module_param(cont3_bufoffset, uint, S_IRUGO);
module_param(cont3_bufsize, uint, S_IRUGO);
module_param(debug, int, 0644);

MODULE_PARM_DESC(cont2_bufoffset, "Display offset (default 0)");
MODULE_PARM_DESC(cont2_bufsize, "Display buffer size (default 0)");
MODULE_PARM_DESC(cont3_bufoffset, "Display offset (default 0)");
MODULE_PARM_DESC(cont3_bufsize, "Display buffer size (default 0)");

static struct buf_config_params display_buf_config_params = {
	.min_numbuffers = VPBE_DEFAULT_NUM_BUFS,
	.numbuffers[0] = VPBE_DEFAULT_NUM_BUFS,
	.numbuffers[1] = VPBE_DEFAULT_NUM_BUFS,
	.min_bufsize[0] = VPBE_DISPLAY_HD_BUF_SIZE,
	.min_bufsize[1] = VPBE_DISPLAY_SD_BUF_SIZE,
	.layer_bufsize[0] = VPBE_DISPLAY_HD_BUF_SIZE,
	.layer_bufsize[1] = VPBE_DISPLAY_SD_BUF_SIZE,
};

static	struct vpbe_device *vpbe_dev;
static	struct osd_state *osd_device;
static int vpbe_display_nr[] = { 2, 3 };

static struct v4l2_capability vpbe_display_videocap = {
	.driver = VPBE_DISPLAY_DRIVER,
	.bus_info = "platform",
	.version = VPBE_DISPLAY_VERSION_CODE,
	.capabilities = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING,
};

static u8 layer_first_int[VPBE_DISPLAY_MAX_DEVICES];

static int venc_is_second_field()
{
	int ret = 0;
	int val;
	ret = v4l2_subdev_call(vpbe_dev->venc,
			       core,
			       ioctl,
			       VENC_GET_FLD,
			       &val);
	if (ret < 0) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			 "Error in getting Field ID 0\n");
	}
	return val;
}

/*
 * vpbe_display_isr()
 * ISR function. It changes status of the displayed buffer, takes next buffer
 * from the queue and sets its address in VPBE registers
 */
static void vpbe_display_isr(unsigned int event, void *disp_obj)
{
	unsigned long jiffies_time = get_jiffies_64();
	struct timeval timevalue;
	int i, fid;
	unsigned long addr = 0;
	struct vpbe_display_obj *layer = NULL;
	struct vpbe_display *disp_dev = (struct vpbe_display *)disp_obj;

	/* Convert time represention from jiffies to timeval */
	jiffies_to_timeval(jiffies_time, &timevalue);

	for (i = 0; i < VPBE_DISPLAY_MAX_DEVICES; i++) {
		layer = disp_dev->dev[i];
		/* If streaming is started in this layer */
		if (!layer->started)
			continue;
		/* Check the field format */
		if ((V4L2_FIELD_NONE == layer->pix_fmt.field) &&
		    (event & VENC_END_OF_FRAME)) {
			/* Progressive mode */
			if (layer_first_int[i]) {
				layer_first_int[i] = 0;
				continue;
			}
			/*
			 * Mark status of the cur_frm to
			 * done and unlock semaphore on it
			 */
			if (layer->cur_frm != layer->next_frm) {
				layer->cur_frm->ts = timevalue;
				layer->cur_frm->state = VIDEOBUF_DONE;
				wake_up_interruptible(
					&layer->cur_frm->done);
				/* Make cur_frm pointing to next_frm */
				layer->cur_frm = layer->next_frm;
			}
			/* Get the next buffer from buffer queue */
			spin_lock(&disp_dev->dma_queue_lock);
			if (!list_empty(&layer->dma_queue)) {
				layer->next_frm =
				    list_entry(layer->dma_queue.next,
				       struct videobuf_buffer, queue);
				/* Remove that buffer from the buffer queue */
				list_del(&layer->next_frm->queue);
				/* Mark status of the buffer as active */
				layer->next_frm->state = VIDEOBUF_ACTIVE;

				addr = videobuf_to_dma_contig(layer->next_frm);
				osd_device->ops.start_layer(osd_device,
						    layer->layer_info.id,
						    addr, disp_dev->cbcr_ofst);
			}
			spin_unlock(&disp_dev->dma_queue_lock);
		} else {
			/*
			 * Interlaced mode
			 * If it is first interrupt, ignore it
			 */
			if (layer_first_int[i]) {
				layer_first_int[i] = 0;
				return;
			}

			layer->field_id ^= 1;
			if (event & OSD_FIRST_FIELD)
				fid = 0;
			else if (event & OSD_SECOND_FIELD)
				fid = 1;
			else
				return;

			/*
			 * If field id does not match with stored
			 * field id
			 */
			if (fid != layer->field_id) {
				/* Make them in sync */
				if (0 == fid)
					layer->field_id = fid;

				return;
			}
			/*
			 * device field id and local field id are
			 * in sync. If this is even field
			 */
			if (0 == fid) {
				if (layer->cur_frm == layer->next_frm)
					continue;
				/*
				 * one frame is displayed If next frame is
				 * available, release cur_frm and move on
				 * copy frame display time
				 */
				layer->cur_frm->ts = timevalue;
				/* Change status of the cur_frm */
				layer->cur_frm->state = VIDEOBUF_DONE;
				/* unlock semaphore on cur_frm */
				wake_up_interruptible(&layer->cur_frm->done);
				/* Make cur_frm pointing to next_frm */
				layer->cur_frm = layer->next_frm;
			} else if (1 == fid) {	/* odd field */

			  if (list_empty(&layer->dma_queue)
				    || (layer->cur_frm != layer->next_frm))
					continue;

				/*
				 * one field is displayed configure
				 * the next frame if it is available
				 * otherwise hold on current frame
				 * Get next from the buffer queue
				 */
				spin_lock(&disp_dev->dma_queue_lock);
				layer->next_frm = list_entry(
							layer->dma_queue.next,
							struct	videobuf_buffer,
							queue);

				/* Remove that from the buffer queue */
				list_del(&layer->next_frm->queue);

				/* Mark state of the frame to active */
				layer->next_frm->state = VIDEOBUF_ACTIVE;
				addr = videobuf_to_dma_contig(layer->next_frm);
				osd_device->ops.start_layer(osd_device,
						layer->layer_info.id,
						addr,
						disp_dev->cbcr_ofst);
				spin_unlock(&disp_dev->dma_queue_lock);
			}
		}
	}
}

/* interrupt service routine */
static irqreturn_t venc_isr(int irq, void *arg)
{
	static unsigned last_event;
	unsigned event = 0;
	int ret = 0;

	if (venc_is_second_field())
		event |= VENC_SECOND_FIELD;
	else
		event |= VENC_FIRST_FIELD;

	if (event == (last_event & ~VENC_END_OF_FRAME)) {
		/*
		* If the display is non-interlaced, then we need to flag the
		* end-of-frame event at every interrupt regardless of the
		* value of the FIDST bit.  We can conclude that the display is
		* non-interlaced if the value of the FIDST bit is unchanged
		* from the previous interrupt.
		*/
		event |= VENC_END_OF_FRAME;
	} else if (event == VENC_SECOND_FIELD) {
		/* end-of-frame for interlaced display */
		event |= VENC_END_OF_FRAME;
	}
	last_event = event;

	vpbe_display_isr(event, arg);

	ret = v4l2_subdev_call(vpbe_dev->venc,
			core,
			ioctl,
			VENC_INTERRUPT,
			&event);
	if (ret < 0) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			 "Error in getting Field ID 0\n");
	}

	return IRQ_HANDLED;
}

/*
 * vpbe_buffer_prepare()
 * This is the callback function called from videobuf_qbuf() function
 * the buffer is prepared and user space virtual address is converted into
 * physical address
 */
static int vpbe_buffer_prepare(struct videobuf_queue *q,
				  struct videobuf_buffer *vb,
				  enum v4l2_field field)
{
	struct vpbe_fh *fh = q->priv_data;
	struct vpbe_display_obj *layer = fh->layer;
	unsigned long addr;
	int ret = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
				"vpbe_buffer_prepare\n");

	/* If buffer is not initialized, initialize it */
	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		vb->width = layer->pix_fmt.width;
		vb->height = layer->pix_fmt.height;
		vb->size = layer->pix_fmt.sizeimage;
		vb->field = field;

		ret = videobuf_iolock(q, vb, NULL);
		if (ret < 0) {
			v4l2_err(&vpbe_dev->v4l2_dev, "Failed to map \
				user address\n");
			return -EINVAL;
		}

		addr = videobuf_to_dma_contig(vb);

		if (q->streaming) {
			if (!IS_ALIGNED(addr, 8)) {
				v4l2_err(&vpbe_dev->v4l2_dev,
					"buffer_prepare:offset is \
					not aligned to 32 bytes\n");
				return -EINVAL;
			}
		}
		vb->state = VIDEOBUF_PREPARED;
	}
	return 0;
}

/*
 * vpbe_buffer_setup()
 * This function allocates memory for the buffers
 */
static int vpbe_buffer_setup(struct videobuf_queue *q,
				unsigned int *count,
				unsigned int *size)
{
	/* Get the file handle object and layer object */
	struct vpbe_fh *fh = q->priv_data;
	struct vpbe_display_obj *layer = fh->layer;
	int buf_size;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "vpbe_buffer_setup\n");

	*size = layer->pix_fmt.sizeimage;
	buf_size =
		display_buf_config_params.layer_bufsize[layer->device_id];
	/*
	 * For MMAP, limit the memory allocation as per bootarg
	 * configured buffer size
	 */
	if (V4L2_MEMORY_MMAP == layer->memory)
		if (*size > buf_size)
			*size = buf_size;

	/* Checking if the buffer size exceeds the available buffer */
	if (display_buf_config_params.video_limit[layer->device_id]) {
		while (*size * *count >
		(display_buf_config_params.video_limit[layer->device_id]))
			(*count)--;
	}

	/* Store number of buffers allocated in numbuffer member */
	if (*count < display_buf_config_params.min_numbuffers)
		*count = layer->numbuffers =
			display_buf_config_params.numbuffers[layer->device_id];

	return 0;
}

/*
 * vpbe_buffer_queue()
 * This function adds the buffer to DMA queue
 */
static void vpbe_buffer_queue(struct videobuf_queue *q,
				 struct videobuf_buffer *vb)
{
	/* Get the file handle object and layer object */
	struct vpbe_fh *fh = q->priv_data;
	struct vpbe_display_obj *layer = fh->layer;
	struct vpbe_display *disp = fh->disp_dev;
	unsigned long flags;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"vpbe_buffer_queue\n");

	/* add the buffer to the DMA queue */
	spin_lock_irqsave(&disp->dma_queue_lock, flags);
	list_add_tail(&vb->queue, &layer->dma_queue);
	spin_unlock_irqrestore(&disp->dma_queue_lock, flags);
	/* Change state of the buffer */
	vb->state = VIDEOBUF_QUEUED;
}

/*
 * vpbe_buffer_release()
 * This function is called from the videobuf layer to free memory allocated to
 * the buffers
 */
static void vpbe_buffer_release(struct videobuf_queue *q,
				   struct videobuf_buffer *vb)
{
	/* Get the file handle object and layer object */
	struct vpbe_fh *fh = q->priv_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"vpbe_buffer_release\n");

	if (V4L2_MEMORY_USERPTR != layer->memory)
		videobuf_dma_contig_free(q, vb);

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops video_qops = {
	.buf_setup = vpbe_buffer_setup,
	.buf_prepare = vpbe_buffer_prepare,
	.buf_queue = vpbe_buffer_queue,
	.buf_release = vpbe_buffer_release,
};

static
struct vpbe_display_obj*
_vpbe_display_get_other_win(struct vpbe_display *disp_dev,
			struct vpbe_display_obj *layer)
{
	enum vpbe_display_device_id thiswin, otherwin;
	thiswin = layer->device_id;

	otherwin = (thiswin == VPBE_DISPLAY_DEVICE_0) ?
	VPBE_DISPLAY_DEVICE_1 : VPBE_DISPLAY_DEVICE_0;
	return disp_dev->dev[otherwin];
}

static int vpbe_set_video_display_params(struct vpbe_display *disp_dev,
			struct vpbe_display_obj *layer)
{
	struct osd_layer_config *cfg  = &layer->layer_info.config;
	unsigned long addr;
	int ret = 0;

	addr = videobuf_to_dma_contig(layer->cur_frm);
	/* Set address in the display registers */
	osd_device->ops.start_layer(osd_device,
				    layer->layer_info.id,
				    addr,
				    disp_dev->cbcr_ofst);

	ret = osd_device->ops.enable_layer(osd_device,
				layer->layer_info.id, 0);
	if (ret < 0) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			"Error in enabling osd window layer 0\n");
		return -1;
	}

	/* Enable the window */
	layer->layer_info.enable = 1;
	if (cfg->pixfmt == PIXFMT_NV12) {
		struct vpbe_display_obj *otherlayer =
			_vpbe_display_get_other_win(disp_dev, layer);

		ret = osd_device->ops.enable_layer(osd_device,
				otherlayer->layer_info.id, 1);
		if (ret < 0) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Error in enabling osd window layer 1\n");
			return -1;
		}
		otherlayer->layer_info.enable = 1;
	}
	return 0;
}

static void
vpbe_disp_calculate_scale_factor(struct vpbe_display *disp_dev,
			struct vpbe_display_obj *layer,
			int expected_xsize, int expected_ysize)
{
	struct display_layer_info *layer_info = &layer->layer_info;
	struct v4l2_pix_format *pixfmt = &layer->pix_fmt;
	struct osd_layer_config *cfg  = &layer->layer_info.config;
	int h_scale = 0, v_scale = 0, h_exp = 0, v_exp = 0, temp;
	v4l2_std_id standard_id = vpbe_dev->current_timings.timings.std_id;

	/*
	 * Application initially set the image format. Current display
	 * size is obtained from the vpbe display controller. expected_xsize
	 * and expected_ysize are set through S_CROP ioctl. Based on this,
	 * driver will calculate the scale factors for vertical and
	 * horizontal direction so that the image is displayed scaled
	 * and expanded. Application uses expansion to display the image
	 * in a square pixel. Otherwise it is displayed using displays
	 * pixel aspect ratio.It is expected that application chooses
	 * the crop coordinates for cropped or scaled display. if crop
	 * size is less than the image size, it is displayed cropped or
	 * it is displayed scaled and/or expanded.
	 *
	 * to begin with, set the crop window same as expected. Later we
	 * will override with scaled window size
	 */

	cfg->xsize = pixfmt->width;
	cfg->ysize = pixfmt->height;
	layer_info->h_zoom = ZOOM_X1;	/* no horizontal zoom */
	layer_info->v_zoom = ZOOM_X1;	/* no horizontal zoom */
	layer_info->h_exp = H_EXP_OFF;	/* no horizontal zoom */
	layer_info->v_exp = V_EXP_OFF;	/* no horizontal zoom */

	if (pixfmt->width < expected_xsize) {
		h_scale = vpbe_dev->current_timings.xres / pixfmt->width;
		if (h_scale < 2)
			h_scale = 1;
		else if (h_scale >= 4)
			h_scale = 4;
		else
			h_scale = 2;
		cfg->xsize *= h_scale;
		if (cfg->xsize < expected_xsize) {
			if ((standard_id & V4L2_STD_525_60) ||
			(standard_id & V4L2_STD_625_50)) {
				temp = (cfg->xsize *
					VPBE_DISPLAY_H_EXP_RATIO_N) /
					VPBE_DISPLAY_H_EXP_RATIO_D;
				if (temp <= expected_xsize) {
					h_exp = 1;
					cfg->xsize = temp;
				}
			}
		}
		if (h_scale == 2)
			layer_info->h_zoom = ZOOM_X2;
		else if (h_scale == 4)
			layer_info->h_zoom = ZOOM_X4;
		if (h_exp)
			layer_info->h_exp = H_EXP_9_OVER_8;
	} else {
		/* no scaling, only cropping. Set display area to crop area */
		cfg->xsize = expected_xsize;
	}

	if (pixfmt->height < expected_ysize) {
		v_scale = expected_ysize / pixfmt->height;
		if (v_scale < 2)
			v_scale = 1;
		else if (v_scale >= 4)
			v_scale = 4;
		else
			v_scale = 2;
		cfg->ysize *= v_scale;
		if (cfg->ysize < expected_ysize) {
			if ((standard_id & V4L2_STD_625_50)) {
				temp = (cfg->ysize *
					VPBE_DISPLAY_V_EXP_RATIO_N) /
					VPBE_DISPLAY_V_EXP_RATIO_D;
				if (temp <= expected_ysize) {
					v_exp = 1;
					cfg->ysize = temp;
				}
			}
		}
		if (v_scale == 2)
			layer_info->v_zoom = ZOOM_X2;
		else if (v_scale == 4)
			layer_info->v_zoom = ZOOM_X4;
		if (v_exp)
			layer_info->h_exp = V_EXP_6_OVER_5;
	} else {
		/* no scaling, only cropping. Set display area to crop area */
		cfg->ysize = expected_ysize;
	}
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"crop display xsize = %d, ysize = %d\n",
		cfg->xsize, cfg->ysize);
}

static void vpbe_disp_adj_position(struct vpbe_display *disp_dev,
			struct vpbe_display_obj *layer,
			int top, int left)
{
	struct osd_layer_config *cfg = &layer->layer_info.config;

	cfg->xpos = cfg->ypos = 0;
	if (left + cfg->xsize <= vpbe_dev->current_timings.xres)
		cfg->xpos = left;
	if (top + cfg->ysize <= vpbe_dev->current_timings.yres)
		cfg->ypos = top;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"new xpos = %d, ypos = %d\n",
		cfg->xpos, cfg->ypos);
}

static int vpbe_disp_check_window_params(struct vpbe_display *disp_dev,
			struct v4l2_rect *c)
{
	if ((c->width == 0) ||
	  ((c->width + c->left) > vpbe_dev->current_timings.xres) ||
	  (c->height == 0) || ((c->height + c->top) >
	  vpbe_dev->current_timings.yres)) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid crop values\n");
		return -1;
	}
	if ((c->height & 0x1) && (vpbe_dev->current_timings.interlaced)) {
		v4l2_err(&vpbe_dev->v4l2_dev,
		"window height must be even for interlaced display\n");
		return -1;
	}
	return 0;
}

/**
 * vpbe_try_format()
 * If user application provides width and height, and have bytesperline set
 * to zero, driver calculates bytesperline and sizeimage based on hardware
 * limits. If application likes to add pads at the end of each line and
 * end of the buffer , it can set bytesperline to line size and sizeimage to
 * bytesperline * height of the buffer. If driver fills zero for active
 * video width and height, and has requested user bytesperline and sizeimage,
 * width and height is adjusted to maximum display limit or buffer width
 * height which ever is lower
 */
static int vpbe_try_format(struct vpbe_display *disp_dev,
			struct v4l2_pix_format *pixfmt, int check)
{
	int min_sizeimage, bpp, min_height = 1, min_width = 32,
		max_width, max_height, user_info = 0;

	if ((pixfmt->pixelformat != V4L2_PIX_FMT_UYVY) &&
	    (pixfmt->pixelformat != V4L2_PIX_FMT_NV12))
		/* choose default as V4L2_PIX_FMT_UYVY */
		pixfmt->pixelformat = V4L2_PIX_FMT_UYVY;

	/* Check the field format */
	if (pixfmt->field == V4L2_FIELD_ANY) {
		if (vpbe_dev->current_timings.interlaced)
			pixfmt->field = V4L2_FIELD_INTERLACED;
		else
			pixfmt->field = V4L2_FIELD_NONE;
	}

	if (pixfmt->field == V4L2_FIELD_INTERLACED)
		min_height = 2;

	if (pixfmt->pixelformat == V4L2_PIX_FMT_NV12)
		bpp = 1;
	else
		bpp = 2;

	max_width = vpbe_dev->current_timings.xres;
	max_height = vpbe_dev->current_timings.yres;

	min_width /= bpp;

	if (!pixfmt->width && !pixfmt->bytesperline) {
		v4l2_err(&vpbe_dev->v4l2_dev, "bytesperline and width"
			" cannot be zero\n");
		return -EINVAL;
	}

	/* if user provided bytesperline, it must provide sizeimage as well */
	if (pixfmt->bytesperline && !pixfmt->sizeimage) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			"sizeimage must be non zero, when user"
			" provides bytesperline\n");
		return -EINVAL;
	}

	/* adjust bytesperline as per hardware - multiple of 32 */
	if (!pixfmt->width)
		pixfmt->width = pixfmt->bytesperline / bpp;

	if (!pixfmt->bytesperline)
		pixfmt->bytesperline = pixfmt->width * bpp;
	else
		user_info = 1;
	pixfmt->bytesperline = ((pixfmt->bytesperline + 31) & ~31);

	if (pixfmt->width < min_width) {
		if (check) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"height is less than minimum,"
				"input width = %d, min_width = %d\n",
				pixfmt->width, min_width);
			return -EINVAL;
		}
		pixfmt->width = min_width;
	}

	if (pixfmt->width > max_width) {
		if (check) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"width is more than maximum,"
				"input width = %d, max_width = %d\n",
				pixfmt->width, max_width);
			return -EINVAL;
		}
		pixfmt->width = max_width;
	}

	/*
	 * If height is zero, then atleast we need to have sizeimage
	 * to calculate height
	 */
	if (!pixfmt->height) {
		if (user_info) {
			if (pixfmt->pixelformat == V4L2_PIX_FMT_NV12) {
				/*
				 * for NV12 format, sizeimage is y-plane size
				 * + CbCr plane which is half of y-plane
				 */
				pixfmt->height = pixfmt->sizeimage /
						(pixfmt->bytesperline +
						(pixfmt->bytesperline >> 1));
			} else
				pixfmt->height = pixfmt->sizeimage/
						pixfmt->bytesperline;
		}
	}

	if (pixfmt->height > max_height) {
		if (check && !user_info) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"height is more than maximum,"
				"input height = %d, max_height = %d\n",
				pixfmt->height, max_height);
			return -EINVAL;
		}
		pixfmt->height = max_height;
	}

	if (pixfmt->height < min_height) {
		if (check && !user_info) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"width is less than minimum,"
				"input height = %d, min_height = %d\n",
				pixfmt->height, min_height);
			return -EINVAL;
		}
		pixfmt->height = min_width;
	}

	/* if user has not provided bytesperline calculate it based on width */
	if (!user_info)
		pixfmt->bytesperline = (((pixfmt->width * bpp) + 31) & ~31);

	if (pixfmt->pixelformat == V4L2_PIX_FMT_NV12)
		min_sizeimage = pixfmt->bytesperline * pixfmt->height +
				(pixfmt->bytesperline * pixfmt->height >> 1);
	else
		min_sizeimage = pixfmt->bytesperline * pixfmt->height;

	if (pixfmt->sizeimage < min_sizeimage) {
		if (check && user_info) {
			v4l2_err(&vpbe_dev->v4l2_dev, "sizeimage is less, %d\n",
				min_sizeimage);
			return -EINVAL;
		}
		pixfmt->sizeimage = min_sizeimage;
	}
	return 0;
}

static int vpbe_display_g_priority(struct file *file, void *priv,
				enum v4l2_priority *p)
{
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	*p = v4l2_prio_max(&layer->prio);

	return 0;
}

static int vpbe_display_s_priority(struct file *file, void *priv,
				enum v4l2_priority p)
{
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;
	int ret;

	ret = v4l2_prio_change(&layer->prio, &fh->prio, p);

	return ret;
}

static int vpbe_display_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"VIDIOC_QUERYCAP, layer id = %d\n", layer->device_id);
	*cap = vpbe_display_videocap;

	return 0;
}

static int vpbe_display_s_crop(struct file *file, void *priv,
			     const struct v4l2_crop *crop)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;
	struct vpbe_display *disp_dev = video_drvdata(file);
	struct osd_layer_config *cfg = &layer->layer_info.config;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"VIDIOC_S_CROP, layer id = %d\n", layer->device_id);

	if (crop->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		struct v4l2_rect *rect = &crop->c;

		if (rect->top < 0 || rect->left < 0) {
			v4l2_err(&vpbe_dev->v4l2_dev, "Error:S_CROP params\n");
			return -EINVAL;
		}

		if (vpbe_disp_check_window_params(disp_dev, rect)) {
			v4l2_err(&vpbe_dev->v4l2_dev, "Error:S_CROP params\n");
			return -EINVAL;
		}
		osd_device->ops.get_layer_config(osd_device,
				layer->layer_info.id, cfg);

		vpbe_disp_calculate_scale_factor(disp_dev, layer,
						rect->width,
						rect->height);
		vpbe_disp_adj_position(disp_dev, layer, rect->top,
						rect->left);
		ret = osd_device->ops.set_layer_config(osd_device,
					layer->layer_info.id, cfg);
		if (ret < 0) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Error in set layer config:\n");
			return -EINVAL;
		}

		/* apply zooming and h or v expansion */
		osd_device->ops.set_zoom(osd_device,
				layer->layer_info.id,
				layer->layer_info.h_zoom,
				layer->layer_info.v_zoom);
		ret = osd_device->ops.set_vid_expansion(osd_device,
				layer->layer_info.h_exp,
				layer->layer_info.v_exp);
		if (ret < 0) {
			v4l2_err(&vpbe_dev->v4l2_dev,
			"Error in set vid expansion:\n");
			return -EINVAL;
		}

		if ((layer->layer_info.h_zoom != ZOOM_X1) ||
			(layer->layer_info.v_zoom != ZOOM_X1) ||
			(layer->layer_info.h_exp != H_EXP_OFF) ||
			(layer->layer_info.v_exp != V_EXP_OFF))
			/* Enable expansion filter */
			osd_device->ops.set_interpolation_filter(osd_device, 1);
		else
			osd_device->ops.set_interpolation_filter(osd_device, 0);

	} else {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}

	return ret;
}

static int vpbe_display_g_crop(struct file *file, void *priv,
			     struct v4l2_crop *crop)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;
	struct osd_layer_config *cfg = &layer->layer_info.config;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"VIDIOC_G_CROP, layer id = %d\n",
			layer->device_id);

	if (crop->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		struct v4l2_rect *rect = &crop->c;
		if (ret)
			return ret;
		osd_device->ops.get_layer_config(osd_device,
					layer->layer_info.id, cfg);
		rect->top = cfg->ypos;
		rect->left = cfg->xpos;
		rect->width = cfg->xsize;
		rect->height = cfg->ysize;
	} else {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buf type\n");
		ret = -EINVAL;
	}

	return ret;
}

static int vpbe_display_cropcap(struct file *file, void *priv,
			      struct v4l2_cropcap *cropcap)
{
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_CROPCAP ioctl\n");

	cropcap->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	cropcap->bounds.left = 0;
	cropcap->bounds.top = 0;
	cropcap->bounds.width = vpbe_dev->current_timings.xres;
	cropcap->bounds.height = vpbe_dev->current_timings.yres;
	cropcap->pixelaspect = vpbe_dev->current_timings.aspect;
	cropcap->defrect = cropcap->bounds;
	return 0;
}

static int vpbe_display_g_fmt(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"VIDIOC_G_FMT, layer id = %d\n",
			layer->device_id);

	/* If buffer type is video output */
	if (V4L2_BUF_TYPE_VIDEO_OUTPUT == fmt->type) {
		struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
		/* Fill in the information about format */
		*pixfmt = layer->pix_fmt;
	} else {
		v4l2_err(&vpbe_dev->v4l2_dev, "invalid type\n");
		return -EINVAL;
	}

	return 0;
}

static int vpbe_display_enum_fmt(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *fmt)
{
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;
	unsigned int index = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
				"VIDIOC_ENUM_FMT, layer id = %d\n",
				layer->device_id);
	if (fmt->index > 0) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid format index\n");
		return -EINVAL;
	}

	/* Fill in the information about format */
	index = fmt->index;
	memset(fmt, 0, sizeof(*fmt));
	fmt->index = index;
	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (index == 0) {
		strcpy(fmt->description, "YUV 4:2:2 - UYVY");
		fmt->pixelformat = V4L2_PIX_FMT_UYVY;
	} else if (index == 1) {
		strcpy(fmt->description, "Y/CbCr 4:2:0");
		fmt->pixelformat = V4L2_PIX_FMT_NV12;
	}
	return 0;
}

static int vpbe_display_s_fmt(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display *disp_dev = video_drvdata(file);
	struct vpbe_display_obj *layer = fh->layer;
	struct osd_layer_config *cfg  = &layer->layer_info.config;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"VIDIOC_S_FMT, layer id = %d\n",
			layer->device_id);

	/* If streaming is started, return error */
	if (layer->started) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Streaming is started\n");
		return -EBUSY;
	}
	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != fmt->type) {
		v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "invalid type\n");
		return -EINVAL;
	} else {
		struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
		/* Check for valid pixel format */
		ret = vpbe_try_format(disp_dev, pixfmt, 1);
		if (ret)
			return ret;

		/* YUV420 is requested, check availability of the
		other video window */

		layer->pix_fmt = *pixfmt;

		/* Get osd layer config */
		osd_device->ops.get_layer_config(osd_device,
				layer->layer_info.id, cfg);
		/* Store the pixel format in the layer object */
		cfg->xsize = pixfmt->width;
		cfg->ysize = pixfmt->height;
		cfg->line_length = pixfmt->bytesperline;
		cfg->ypos = 0;
		cfg->xpos = 0;
		cfg->interlaced = vpbe_dev->current_timings.interlaced;

		/* Change of the default pixel format for both video windows */
		if (V4L2_PIX_FMT_NV12 == pixfmt->pixelformat) {
			struct vpbe_display_obj *otherlayer;
			cfg->pixfmt = PIXFMT_NV12;
			otherlayer = _vpbe_display_get_other_win(disp_dev,
								 layer);
			otherlayer->layer_info.config.pixfmt = PIXFMT_NV12;
		}

		/* Set the layer config in the osd window */
		ret = osd_device->ops.set_layer_config(osd_device,
					layer->layer_info.id, cfg);
		if (ret < 0) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				 "Error in S_FMT params:\n");
			return -EINVAL;
		}

		/* Readback and fill the local copy of current pix format */
		osd_device->ops.get_layer_config(osd_device,
				layer->layer_info.id, cfg);

		/* verify if readback values are as expected */
		if (layer->pix_fmt.width != cfg->xsize ||
			layer->pix_fmt.height != cfg->ysize ||
			layer->pix_fmt.bytesperline != layer->layer_info.
			config.line_length || (cfg->interlaced &&
			layer->pix_fmt.field != V4L2_FIELD_INTERLACED) ||
			(!cfg->interlaced && layer->pix_fmt.field !=
			V4L2_FIELD_NONE)) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				 "mismatch:layer conf params:\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int vpbe_display_try_fmt(struct file *file, void *priv,
				  struct v4l2_format *fmt)
{
	struct vpbe_display *disp_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_TRY_FMT\n");

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT == fmt->type) {
		struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
		/* Check for valid field format */
		return  vpbe_try_format(disp_dev, pixfmt, 0);
	}
	v4l2_err(&vpbe_dev->v4l2_dev, "invalid type\n");
	return -EINVAL;
}

/**
 * vpbe_display_s_std - Set the given standard in the encoder
 *
 * Sets the standard if supported by the current encoder. Return the status.
 * 0 - success & -EINVAL on error
 */
static int vpbe_display_s_std(struct file *file, void *priv,
				v4l2_std_id *std_id)
{
	struct vpbe_fh *fh = priv;
	struct vpbe_display_obj *layer = fh->layer;
	int ret = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_S_STD\n");

	/* If streaming is started, return error */
	if (layer->started) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Streaming is started\n");
		return -EBUSY;
	}
	if (NULL != vpbe_dev->ops.s_std) {
		ret = vpbe_dev->ops.s_std(vpbe_dev, std_id);
		if (ret) {
			v4l2_err(&vpbe_dev->v4l2_dev,
			"Failed to set standard for sub devices\n");
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * vpbe_display_g_std - Get the standard in the current encoder
 *
 * Get the standard in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int vpbe_display_g_std(struct file *file, void *priv,
				v4l2_std_id *std_id)
{
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,	"VIDIOC_G_STD\n");

	/* Get the standard from the current encoder */
	if (vpbe_dev->current_timings.timings_type & VPBE_ENC_STD) {
		*std_id = vpbe_dev->current_timings.timings.std_id;
		return 0;
	}
	return -EINVAL;
}

/**
 * vpbe_display_enum_output - enumerate outputs
 *
 * Enumerates the outputs available at the vpbe display
 * returns the status, -EINVAL if end of output list
 */
static int vpbe_display_enum_output(struct file *file, void *priv,
				    struct v4l2_output *output)
{
	int ret = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,	"VIDIOC_ENUM_OUTPUT\n");

	/* Enumerate outputs */

	if (NULL != vpbe_dev->ops.enum_outputs) {
		ret = vpbe_dev->ops.enum_outputs(vpbe_dev, output);
		if (ret) {
			v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
				"Failed to enumerate outputs\n");
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * vpbe_display_s_output - Set output to
 * the output specified by the index
 */
static int vpbe_display_s_output(struct file *file, void *priv,
				unsigned int i)
{
	struct vpbe_fh *fh = priv;
	struct vpbe_display_obj *layer = fh->layer;
	int ret = 0;
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,	"VIDIOC_S_OUTPUT\n");
	/* If streaming is started, return error */
	if (layer->started) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Streaming is started\n");
		return -EBUSY;
	}
	if (NULL != vpbe_dev->ops.set_output) {
		ret = vpbe_dev->ops.set_output(vpbe_dev, i);
		if (ret) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Failed to set output for sub devices\n");
			return -EINVAL;
		}
	}
	return ret;
}

/**
 * vpbe_display_g_output - Get output from subdevice
 * for a given by the index
 */
static int vpbe_display_g_output(struct file *file, void *priv,
				unsigned int *i)
{
	int ret = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_G_OUTPUT\n");
	/* Get the standard from the current encoder */

	if (NULL != vpbe_dev->ops.get_output) {
		ret = vpbe_dev->ops.get_output(vpbe_dev);
		if (ret) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Failed to get output for sub devices\n");
			return -EINVAL;
		}
	}

	*i = vpbe_dev->current_out_index;

	return 0;
}

/**
 * vpbe_display_enum_dv_presets - Enumerate the dv presets
 *
 * enum the preset in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int
vpbe_display_enum_dv_presets(struct file *file, void *priv,
			struct v4l2_dv_enum_preset *preset)
{
	int ret = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_ENUM_DV_PRESETS\n");

	/* Enumerate outputs */

	if (NULL != vpbe_dev->ops.enum_dv_presets) {
		ret = vpbe_dev->ops.enum_dv_presets(vpbe_dev, preset);
		if (ret) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Failed to enumerate dv presets info\n");
			return -EINVAL;
		}
	}

	return ret;
}

/**
 * vpbe_display_s_dv_preset - Set the dv presets
 *
 * Set the preset in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int
vpbe_display_s_dv_preset(struct file *file, void *priv,
				struct v4l2_dv_preset *preset)
{
	struct vpbe_fh *fh = priv;
	struct vpbe_display_obj *layer = fh->layer;
	int ret = 0;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_S_DV_PRESETS\n");


	/* If streaming is started, return error */
	if (layer->started) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Streaming is started\n");
		return -EBUSY;
	}

	/* Set the given standard in the encoder */
	if (NULL != vpbe_dev->ops.s_dv_preset) {
		ret = vpbe_dev->ops.s_dv_preset(vpbe_dev, preset);
		if (ret) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Failed to set the dv presets info\n");
			return -EINVAL;
		}
	}
	/* set the current norm to zero to be consistent. If STD is used
	 * v4l2 layer will set the norm properly on successful s_std call
	 */
	layer->video_dev->current_norm = 0;
	return ret;
}

/**
 * vpbe_display_g_dv_preset - Set the dv presets
 *
 * Get the preset in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int
vpbe_display_g_dv_preset(struct file *file, void *priv,
				struct v4l2_dv_preset *dv_preset)
{
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_G_DV_PRESETS\n");

	/* Get the given standard in the encoder */

	if (vpbe_dev->current_timings.timings_type &
				VPBE_ENC_DV_PRESET) {
		dv_preset->preset =
			vpbe_dev->current_timings.timings.dv_preset;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int vpbe_display_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type buf_type)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"VIDIOC_STREAMOFF,layer id = %d\n",
			layer->device_id);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf_type) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	/* If io is allowed for this file handle, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpbe_dev->v4l2_dev, "No io_allowed\n");
		return -EACCES;
	}

	/* If streaming is not started, return error */
	if (!layer->started) {
		v4l2_err(&vpbe_dev->v4l2_dev, "streaming not started in layer"
			" id = %d\n", layer->device_id);
		return -EINVAL;
	}

	osd_device->ops.disable_layer(osd_device,
			layer->layer_info.id);
	layer->started = 0;
	ret = videobuf_streamoff(&layer->buffer_queue);

	return ret;
}

static int vpbe_display_streamon(struct file *file, void *priv,
			 enum v4l2_buf_type buf_type)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display *disp_dev = video_drvdata(file);
	struct vpbe_display_obj *layer = fh->layer;

	osd_device->ops.disable_layer(osd_device,
			layer->layer_info.id);

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "VIDIOC_STREAMON, layerid=%d\n",
						layer->device_id);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf_type) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	/* If file handle is not allowed IO, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpbe_dev->v4l2_dev, "No io_allowed\n");
		return -EACCES;
	}
	/* If Streaming is already started, return error */
	if (layer->started) {
		v4l2_err(&vpbe_dev->v4l2_dev, "layer is already streaming\n");
		return -EBUSY;
	}

	/*
	 * Call videobuf_streamon to start streaming
	 * in videobuf
	 */
	ret = videobuf_streamon(&layer->buffer_queue);
	if (ret) {
		v4l2_err(&vpbe_dev->v4l2_dev,
		"error in videobuf_streamon\n");
		return ret;
	}
	/* If buffer queue is empty, return error */
	if (list_empty(&layer->dma_queue)) {
		v4l2_err(&vpbe_dev->v4l2_dev, "buffer queue is empty\n");
		goto streamoff;
	}
	/* Get the next frame from the buffer queue */
	layer->next_frm = layer->cur_frm = list_entry(layer->dma_queue.next,
				struct videobuf_buffer, queue);
	/* Remove buffer from the buffer queue */
	list_del(&layer->cur_frm->queue);
	/* Mark state of the current frame to active */
	layer->cur_frm->state = VIDEOBUF_ACTIVE;
	/* Initialize field_id and started member */
	layer->field_id = 0;

	/* Set parameters in OSD and VENC */
	ret = vpbe_set_video_display_params(disp_dev, layer);
	if (ret < 0)
		goto streamoff;

	/*
	 * if request format is yuv420 semiplanar, need to
	 * enable both video windows
	 */
	layer->started = 1;

	layer_first_int[layer->device_id] = 1;

	return ret;
streamoff:
	ret = videobuf_streamoff(&layer->buffer_queue);
	return ret;
}

static int vpbe_display_dqbuf(struct file *file, void *priv,
		      struct v4l2_buffer *buf)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"VIDIOC_DQBUF, layer id = %d\n",
		layer->device_id);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}
	/* If this file handle is not allowed to do IO, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpbe_dev->v4l2_dev, "No io_allowed\n");
		return -EACCES;
	}
	if (file->f_flags & O_NONBLOCK)
		/* Call videobuf_dqbuf for non blocking mode */
		ret = videobuf_dqbuf(&layer->buffer_queue, buf, 1);
	else
		/* Call videobuf_dqbuf for blocking mode */
		ret = videobuf_dqbuf(&layer->buffer_queue, buf, 0);
	return ret;
}

static int vpbe_display_qbuf(struct file *file, void *priv,
		     struct v4l2_buffer *p)
{
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"VIDIOC_QBUF, layer id = %d\n",
		layer->device_id);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != p->type) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	/* If this file handle is not allowed to do IO, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpbe_dev->v4l2_dev, "No io_allowed\n");
		return -EACCES;
	}

	return videobuf_qbuf(&layer->buffer_queue, p);
}

static int vpbe_display_querybuf(struct file *file, void *priv,
			 struct v4l2_buffer *buf)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
		"VIDIOC_QUERYBUF, layer id = %d\n",
		layer->device_id);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	/* Call videobuf_querybuf to get information */
	ret = videobuf_querybuf(&layer->buffer_queue, buf);

	return ret;
}

static int vpbe_display_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *req_buf)
{
	int ret = 0;
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "vpbe_display_reqbufs\n");

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != req_buf->type) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	/* If io users of the layer is not zero, return error */
	if (0 != layer->io_usrs) {
		v4l2_err(&vpbe_dev->v4l2_dev, "not IO user\n");
		return -EBUSY;
	}
	/* Initialize videobuf queue as per the buffer type */
	videobuf_queue_dma_contig_init(&layer->buffer_queue,
				&video_qops,
				vpbe_dev->pdev,
				&layer->irqlock,
				V4L2_BUF_TYPE_VIDEO_OUTPUT,
				layer->pix_fmt.field,
				sizeof(struct videobuf_buffer),
				fh, NULL);

	/* Set io allowed member of file handle to TRUE */
	fh->io_allowed = 1;
	/* Increment io usrs member of layer object to 1 */
	layer->io_usrs = 1;
	/* Store type of memory requested in layer object */
	layer->memory = req_buf->memory;
	/* Initialize buffer queue */
	INIT_LIST_HEAD(&layer->dma_queue);
	/* Allocate buffers */
	ret = videobuf_reqbufs(&layer->buffer_queue, req_buf);

	return ret;
}

/*
 * vpbe_display_mmap()
 * It is used to map kernel space buffers into user spaces
 */
static int vpbe_display_mmap(struct file *filep, struct vm_area_struct *vma)
{
	/* Get the layer object and file handle object */
	struct vpbe_fh *fh = filep->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "vpbe_display_mmap\n");
	return videobuf_mmap_mapper(&layer->buffer_queue, vma);
}

/* vpbe_display_poll(): It is used for select/poll system call
 */
static unsigned int vpbe_display_poll(struct file *filep, poll_table *wait)
{
	unsigned int err = 0;
	struct vpbe_fh *fh = filep->private_data;
	struct vpbe_display_obj *layer = fh->layer;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "vpbe_display_poll\n");
	if (layer->started)
		err = videobuf_poll_stream(filep, &layer->buffer_queue, wait);
	return err;
}

static int vpbe_display_cfg_layer_default(enum vpbe_display_device_id id,
			struct vpbe_display *disp_dev)
{
	int err = 0;
	struct osd_layer_config *layer_config;
	struct vpbe_display_obj *layer = disp_dev->dev[id];
	struct osd_layer_config *cfg  = &layer->layer_info.config;

	/* First claim the layer for this device */
	err = osd_device->ops.request_layer(osd_device,
					    layer->layer_info.id);
	if (err < 0) {
		/* Couldn't get layer */
		v4l2_err(&vpbe_dev->v4l2_dev,
			"Display Manager failed to allocate layer\n");
		return -EBUSY;
	}

	layer_config = cfg;
	/* Set the default image and crop values */
	layer_config->pixfmt = PIXFMT_YCbCrI;
	layer->pix_fmt.pixelformat = V4L2_PIX_FMT_UYVY;
	layer->pix_fmt.bytesperline = layer_config->line_length =
	    vpbe_dev->current_timings.xres * 2;

	layer->pix_fmt.width = layer_config->xsize =
		vpbe_dev->current_timings.xres;
	layer->pix_fmt.height = layer_config->ysize =
		vpbe_dev->current_timings.yres;
	layer->pix_fmt.sizeimage =
		layer->pix_fmt.bytesperline * layer->pix_fmt.height;
	layer_config->xpos = 0;
	layer_config->ypos = 0;
	layer_config->interlaced = vpbe_dev->current_timings.interlaced;

	/*
	 * turn off ping-pong buffer and field inversion to fix
	 * the image shaking problem in 1080I mode
	 */

	if (cfg->interlaced)
		layer->pix_fmt.field = V4L2_FIELD_INTERLACED;
	else
		layer->pix_fmt.field = V4L2_FIELD_NONE;

	err = osd_device->ops.set_layer_config(osd_device,
				layer->layer_info.id,
				layer_config);
	if (err < 0) {
		/* Couldn't set layer */
		v4l2_err(&vpbe_dev->v4l2_dev,
			"Display Manager failed to set osd layer\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * vpbe_display_open()
 * It creates object of file handle structure and stores it in private_data
 * member of filepointer
 */
static int vpbe_display_open(struct file *file)
{
	int minor = iminor(file->f_path.dentry->d_inode);
	struct vpbe_display *disp_dev = video_drvdata(file);
	struct vpbe_display_obj *layer;
	struct vpbe_fh *fh = NULL;
	int found = -1;
	int i = 0;

	/* Check for valid minor number */
	for (i = 0; i < VPBE_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the layer object */
		layer = disp_dev->dev[i];
		if (minor == layer->video_dev->minor) {
			found = i;
			break;
		}
	}

	/* If not found, return error no device */
	if (0 > found) {
		v4l2_err(&vpbe_dev->v4l2_dev, "device not found\n");
		return -ENODEV;
	}

	/* Allocate memory for the file handle object */
	fh = kmalloc(sizeof(struct vpbe_fh), GFP_KERNEL);
	if (fh == NULL) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			"unable to allocate memory for file handle object\n");
		return -ENOMEM;
	}
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"vpbe display open plane = %d\n",
			layer->device_id);

	/* store pointer to fh in private_data member of filep */
	file->private_data = fh;
	fh->layer = layer;
	fh->disp_dev = disp_dev;

	if (!layer->usrs) {
		/* Configure the default values for the layer */
		if (vpbe_display_cfg_layer_default(layer->device_id,
						disp_dev)) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				"Unable to configure video layer"
				" for id = %d\n", layer->device_id);
			return -EINVAL;
		}
	}
	/* Increment layer usrs counter */
	layer->usrs++;
	/* Set io_allowed member to false */
	fh->io_allowed = 0;
	/* Initialize priority of this instance to default priority */
	fh->prio = V4L2_PRIORITY_UNSET;
	v4l2_prio_open(&layer->prio, &fh->prio);
	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev,
			"vpbe display device opened successfully\n");
	return 0;
}

/*
 * vpbe_display_release()
 * This function deletes buffer queue, frees the buffers and the davinci
 * display file * handle
 */
static int vpbe_display_release(struct file *file)
{
	/* Get the layer object and file handle object */
	struct vpbe_fh *fh = file->private_data;
	struct vpbe_display_obj *layer = fh->layer;
	struct osd_layer_config *cfg  = &layer->layer_info.config;
	struct vpbe_display *disp_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "vpbe_display_release\n");
	/* If this is doing IO and other layer are not closed */
	if ((layer->usrs != 1) && fh->io_allowed) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Close other instances\n");
		return -EAGAIN;
	}

	/* if this instance is doing IO */
	if (fh->io_allowed) {
		/* Reset io_usrs member of layer object */
		layer->io_usrs = 0;

		osd_device->ops.disable_layer(osd_device,
				layer->layer_info.id);
		layer->started = 0;
		/* Free buffers allocated */
		videobuf_queue_cancel(&layer->buffer_queue);
		videobuf_mmap_free(&layer->buffer_queue);
	}

	/* Decrement layer usrs counter */
	layer->usrs--;
	/* If this file handle has initialize encoder device, reset it */
	if (!layer->usrs) {
		if (cfg->pixfmt == PIXFMT_NV12) {
			struct vpbe_display_obj *otherlayer;
			otherlayer =
			_vpbe_display_get_other_win(disp_dev, layer);
			osd_device->ops.disable_layer(osd_device,
					otherlayer->layer_info.id);
			osd_device->ops.release_layer(osd_device,
					otherlayer->layer_info.id);
		}
		osd_device->ops.disable_layer(osd_device,
				layer->layer_info.id);
		osd_device->ops.release_layer(osd_device,
				layer->layer_info.id);
	}
	/* Close the priority */
	v4l2_prio_close(&layer->prio, fh->prio);
	file->private_data = NULL;

	/* Free memory allocated to file handle object */
	kfree(fh);

	disp_dev->cbcr_ofst = 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vpbe_display_g_register(struct file *file, void *priv,
			struct v4l2_dbg_register *reg)
{
	struct v4l2_dbg_match *match = &reg->match;

	if (match->type >= 2) {
		v4l2_subdev_call(vpbe_dev->venc,
				 core,
				 g_register,
				 reg);
	}

	return 0;
}

static int vpbe_display_s_register(struct file *file, void *priv,
			struct v4l2_dbg_register *reg)
{
	return 0;
}
#endif

/* vpbe capture ioctl operations */
static const struct v4l2_ioctl_ops vpbe_ioctl_ops = {
	.vidioc_querycap	 = vpbe_display_querycap,
	.vidioc_g_fmt_vid_out    = vpbe_display_g_fmt,
	.vidioc_enum_fmt_vid_out = vpbe_display_enum_fmt,
	.vidioc_s_fmt_vid_out    = vpbe_display_s_fmt,
	.vidioc_try_fmt_vid_out  = vpbe_display_try_fmt,
	.vidioc_reqbufs		 = vpbe_display_reqbufs,
	.vidioc_querybuf	 = vpbe_display_querybuf,
	.vidioc_qbuf		 = vpbe_display_qbuf,
	.vidioc_dqbuf		 = vpbe_display_dqbuf,
	.vidioc_streamon	 = vpbe_display_streamon,
	.vidioc_streamoff	 = vpbe_display_streamoff,
	.vidioc_cropcap		 = vpbe_display_cropcap,
	.vidioc_g_crop		 = vpbe_display_g_crop,
	.vidioc_s_crop		 = vpbe_display_s_crop,
	.vidioc_g_priority	 = vpbe_display_g_priority,
	.vidioc_s_priority	 = vpbe_display_s_priority,
	.vidioc_s_std		 = vpbe_display_s_std,
	.vidioc_g_std		 = vpbe_display_g_std,
	.vidioc_enum_output	 = vpbe_display_enum_output,
	.vidioc_s_output	 = vpbe_display_s_output,
	.vidioc_g_output	 = vpbe_display_g_output,
	.vidioc_s_dv_preset	 = vpbe_display_s_dv_preset,
	.vidioc_g_dv_preset	 = vpbe_display_g_dv_preset,
	.vidioc_enum_dv_presets	 = vpbe_display_enum_dv_presets,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register	 = vpbe_display_g_register,
	.vidioc_s_register	 = vpbe_display_s_register,
#endif
};

static struct v4l2_file_operations vpbe_fops = {
	.owner = THIS_MODULE,
	.open = vpbe_display_open,
	.release = vpbe_display_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vpbe_display_mmap,
	.poll = vpbe_display_poll
};

static int vpbe_device_get(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	if (strcmp("vpbe_controller", pdev->name) == 0)
		vpbe_dev = platform_get_drvdata(pdev);

	if (strcmp("vpbe-osd", pdev->name) == 0)
		osd_device = platform_get_drvdata(pdev);

	return 0;
}

/*Configure the channels, buffer size */
static int init_vpbe_layer_objects(int i)
{
	int free_buffer_index;

	/* Default number of buffers should be 3 */
	if ((video2_numbuffers > 0) &&
	    (video2_numbuffers < display_buf_config_params.min_numbuffers))
		video2_numbuffers = display_buf_config_params.min_numbuffers;
	if ((video3_numbuffers > 0) &&
	    (video3_numbuffers < display_buf_config_params.min_numbuffers))
		video3_numbuffers = display_buf_config_params.min_numbuffers;

	/*
	 * Set buffer size to min buffers size if invalid
	 * buffer size is given
	 */
	if (video2_bufsize <
	    display_buf_config_params.min_bufsize[VPBE_DISPLAY_DEVICE_0])
		video2_bufsize =
		display_buf_config_params.min_bufsize[VPBE_DISPLAY_DEVICE_0];

	if (video3_bufsize <
	    display_buf_config_params.min_bufsize[VPBE_DISPLAY_DEVICE_1])
		video3_bufsize =
		display_buf_config_params.min_bufsize[VPBE_DISPLAY_DEVICE_1];

	/* set number of buffers, they could come from boot/args */
	display_buf_config_params.numbuffers[VPBE_DISPLAY_DEVICE_0] =
		video2_numbuffers;
	display_buf_config_params.numbuffers[VPBE_DISPLAY_DEVICE_1] =
		video3_numbuffers;

	/*set size of buffers, they could come from bootargs*/
	display_buf_config_params.layer_bufsize[VPBE_DISPLAY_DEVICE_0] =
		video2_bufsize;
	display_buf_config_params.layer_bufsize[VPBE_DISPLAY_DEVICE_1] =
		video3_bufsize;

	if (display_buf_config_params.numbuffers[0] == 0)
		printk(KERN_ERR "no vid2 buffer allocated\n");
	if (display_buf_config_params.numbuffers[1] == 0)
		printk(KERN_ERR "no vid3 buffer allocated\n");
	free_buffer_index = display_buf_config_params.numbuffers[i - 1];

	return 0;
}


/*
 * vpbe_display_probe()
 * This function creates device entries by register itself to the V4L2 driver
 * and initializes fields of each layer objects
 */
static int vpbe_display_probe(struct platform_device *pdev)
{
	int i, j = 0, k, err = 0;
	struct vpbe_display *disp_dev;
	struct video_device *vbd = NULL;
	struct vpbe_display_obj *vpbe_display_layer = NULL;
	struct resource *res;
	int irq;
	unsigned long phys_end_kernel;
	size_t size;

	printk(KERN_DEBUG "vpbe_display_probe\n");

	/* Allocate memory for vpbe_display */
	disp_dev = kzalloc(sizeof(struct vpbe_display), GFP_KERNEL);
	if (!disp_dev) {
		printk(KERN_ERR "ran out of memory\n");
		return -ENOMEM;
	}

	/*
	* Initialising the memory from the input arguments file for
	* contiguous memory buffers and avoid defragmentation
	*/

	if (cont2_bufsize) {
		/* attempt to determine the end of Linux kernel memory */
		phys_end_kernel = virt_to_phys((void *)PAGE_OFFSET) +
			(num_physpages << PAGE_SHIFT);
		phys_end_kernel += cont2_bufoffset;
		size = cont2_bufsize;

		err = dma_declare_coherent_memory(&pdev->dev, phys_end_kernel,
			phys_end_kernel,
			size,
			DMA_MEMORY_MAP |
			DMA_MEMORY_EXCLUSIVE);

		if (!err) {
			dev_err(&pdev->dev, "Unable to declare MMAP memory.\n");
			err = -ENOMEM;
			goto probe_out;
		}
	}

	if (cont3_bufsize) {
		/* attempt to determine the end of Linux kernel memory */
		phys_end_kernel = virt_to_phys((void *)PAGE_OFFSET) +
			(num_physpages << PAGE_SHIFT);
			phys_end_kernel += cont3_bufoffset;
			size = cont3_bufsize;

		err = dma_declare_coherent_memory(&pdev->dev, phys_end_kernel,
			phys_end_kernel,
			size,
			DMA_MEMORY_MAP |
			DMA_MEMORY_EXCLUSIVE);

		if (!err) {
			dev_err(&pdev->dev, "Unable to declare MMAP memory.\n");
			err = -ENOMEM;
			goto probe_out;
		}
	}

	/* Allocate memory for four plane display objects */
	for (i = 0; i < VPBE_DISPLAY_MAX_DEVICES; i++) {
		disp_dev->dev[i] =
		    kmalloc(sizeof(struct vpbe_display_obj), GFP_KERNEL);
		/* If memory allocation fails, return error */
		if (!disp_dev->dev[i]) {
			printk(KERN_ERR "ran out of memory\n");
			err = -ENOMEM;
			goto probe_out;
		}
		spin_lock_init(&disp_dev->dev[i]->irqlock);
		mutex_init(&disp_dev->dev[i]->opslock);
	}
	spin_lock_init(&disp_dev->dma_queue_lock);

	err = init_vpbe_layer_objects(i);
	if (err) {
		printk(KERN_ERR "Error initializing vpbe display\n");
		return err;
	}

	/*
	 * Scan all the platform devices to find the vpbe
	 * controller device and get the vpbe_dev object
	 */
	err = bus_for_each_dev(&platform_bus_type, NULL, NULL,
			vpbe_device_get);
	if (err < 0)
		return err;

	/* Initialize the vpbe display controller */
	if (NULL != vpbe_dev->ops.initialize) {
		err = vpbe_dev->ops.initialize(&pdev->dev, vpbe_dev);
		if (err) {
			v4l2_err(&vpbe_dev->v4l2_dev, "Error initing vpbe\n");
			err = -ENOMEM;
			goto probe_out;
		}
	}

	/* check the name of davinci device */
	if (vpbe_dev->cfg->module_name != NULL)
		strcpy(vpbe_display_videocap.card,
			vpbe_dev->cfg->module_name);

	for (i = 0; i < VPBE_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the layer object */
		vpbe_display_layer = disp_dev->dev[i];
		/* Allocate memory for video device */
		vbd = video_device_alloc();
		if (vbd == NULL) {
			for (j = 0; j < i; j++) {
				video_device_release(
				disp_dev->dev[j]->video_dev);
			}
			v4l2_err(&vpbe_dev->v4l2_dev, "ran out of memory\n");
			err = -ENOMEM;
			goto probe_out;
		}
		/* Initialize field of video device */
		vbd->release	= video_device_release;
		vbd->fops	= &vpbe_fops;
		vbd->ioctl_ops	= &vpbe_ioctl_ops;
		vbd->minor	= -1;
		vbd->v4l2_dev   = &vpbe_dev->v4l2_dev;
		vbd->lock	= &vpbe_display_layer->opslock;

		vbd->tvnorms	= (V4L2_STD_525_60 | V4L2_STD_625_50);
		vbd->current_norm =
			vpbe_dev->current_timings.timings.std_id;

		snprintf(vbd->name, sizeof(vbd->name),
			 "DaVinci_VPBE Display_DRIVER_V%d.%d.%d",
			 (VPBE_DISPLAY_VERSION_CODE >> 16) & 0xff,
			 (VPBE_DISPLAY_VERSION_CODE >> 8) & 0xff,
			 (VPBE_DISPLAY_VERSION_CODE) & 0xff);

		/* Set video_dev to the video device */
		vpbe_display_layer->video_dev = vbd;
		vpbe_display_layer->device_id = i;

		vpbe_display_layer->layer_info.id =
		    ((i == VPBE_DISPLAY_DEVICE_0) ? WIN_VID0 : WIN_VID1);
		if (display_buf_config_params.numbuffers[i] == 0)
			vpbe_display_layer->memory = V4L2_MEMORY_USERPTR;
		else
			vpbe_display_layer->memory = V4L2_MEMORY_MMAP;

		/* Initialize field of the display layer objects */
		vpbe_display_layer->usrs = 0;
		vpbe_display_layer->io_usrs = 0;
		vpbe_display_layer->started = 0;

		/* Initialize prio member of layer object */
		v4l2_prio_init(&vpbe_display_layer->prio);

		/* Register video device */
		v4l2_info(&vpbe_dev->v4l2_dev,
		       "Trying to register VPBE display device.\n");
		v4l2_info(&vpbe_dev->v4l2_dev,
				"layer=%x,layer->video_dev=%x\n",
				(int)vpbe_display_layer,
				(int)&vpbe_display_layer->video_dev);

		err = video_register_device(vpbe_display_layer->
					    video_dev,
					    VFL_TYPE_GRABBER,
					    vpbe_display_nr[i]);
		if (err)
			goto probe_out;
		/* set the driver data in platform device */
		platform_set_drvdata(pdev, disp_dev);
		video_set_drvdata(vpbe_display_layer->video_dev, disp_dev);
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			 "Unable to get VENC interrupt resource\n");
		err = -ENODEV;
		goto probe_out;
	}
	irq = res->start;
	if (request_irq(irq, venc_isr,  IRQF_DISABLED, VPBE_DISPLAY_DRIVER,
		disp_dev)) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Unable to request interrupt\n");
		err = -ENODEV;
		goto probe_out;
	}
	printk(KERN_DEBUG "Successfully completed the probing of vpbe v4l2 device\n");
	return 0;
probe_out:
	kfree(disp_dev);

	for (k = 0; k < j; k++) {
		/* Get the pointer to the layer object */
		vpbe_display_layer = disp_dev->dev[k];
		/* Unregister video device */
		video_unregister_device(vpbe_display_layer->video_dev);
		/* Release video device */
		video_device_release(vpbe_display_layer->video_dev);
		vpbe_display_layer->video_dev = NULL;
	}
	return err;
}

/*
 * vpbe_display_remove()
 * It un-register hardware layer from V4L2 driver
 */
static int vpbe_display_remove(struct platform_device *pdev)
{
	int i;
	struct vpbe_display_obj *vpbe_display_layer;
	struct vpbe_display *disp_dev = platform_get_drvdata(pdev);
	struct resource *res;

	v4l2_dbg(1, debug, &vpbe_dev->v4l2_dev, "vpbe_display_remove\n");

	/* unregister irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	free_irq(res->start, disp_dev);

	/* deinitialize the vpbe display controller */
	if (NULL != vpbe_dev->ops.deinitialize)
		vpbe_dev->ops.deinitialize(&pdev->dev, vpbe_dev);
	/* un-register device */
	for (i = 0; i < VPBE_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the layer object */
		vpbe_display_layer = disp_dev->dev[i];
		/* Unregister video device */
		video_unregister_device(vpbe_display_layer->video_dev);

		vpbe_display_layer->video_dev = NULL;
	}
	for (i = 0; i < VPBE_DISPLAY_MAX_DEVICES; i++) {
		kfree(disp_dev->dev[i]);
		disp_dev->dev[i] = NULL;
	}

	return 0;
}

static struct platform_driver vpbe_display_driver = {
	.driver = {
		.name = VPBE_DISPLAY_DRIVER,
		.owner = THIS_MODULE,
		.bus = &platform_bus_type,
	},
	.probe = vpbe_display_probe,
	.remove = vpbe_display_remove,
};

/*
 * vpbe_display_init()
 * This function registers device and driver to the kernel, requests irq
 * handler and allocates memory for layer objects
 */
static __init int vpbe_display_init(void)
{
	int err = 0;

	printk(KERN_DEBUG "vpbe_display_init\n");

	/* Register driver to the kernel */
	err = platform_driver_register(&vpbe_display_driver);
	if (0 != err)
		return err;

	printk(KERN_DEBUG "vpbe_display_init:"
			"VPBE V4L2 Display Driver V1.0 loaded\n");
	return 0;
}

/*
 * vpbe_display_cleanup()
 * This function un-registers device and driver to the kernel, frees requested
 * irq handler and de-allocates memory allocated for layer objects.
 */
static void vpbe_display_cleanup(void)
{
	printk(KERN_DEBUG "vpbe_display_cleanup\n");

	/* platform driver unregister */
	platform_driver_unregister(&vpbe_display_driver);
}

/* Function for module initialization and cleanup */
module_init(vpbe_display_init);
module_exit(vpbe_display_cleanup);

MODULE_DESCRIPTION("TI DMXXX VPBE Display controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments");
