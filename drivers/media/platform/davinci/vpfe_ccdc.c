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
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <media/davinci/vpfe_capture.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <media/davinci/vpss.h>

#include <mach/cputype.h>

#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/media-entity.h>

#include "ccdc_hw_device.h"

#define MAX_WIDTH	4096
#define MAX_HEIGHT	4096

static const unsigned int ccdc_fmts[] = {
	V4L2_MBUS_FMT_Y8_1X8,
	V4L2_MBUS_FMT_YUYV8_2X8,
	V4L2_MBUS_FMT_YUYV8_1X16,
	V4L2_MBUS_FMT_YUYV10_1X20,
	V4L2_MBUS_FMT_SBGGR10_1X10,
};

/* -----------------------------------------------------------------------------
 * CCDC helper functions
 */

enum v4l2_field ccdc_get_fid(struct vpfe_device *vpfe_dev)
{
	struct vpfe_ccdc_device *ccdc = &vpfe_dev->vpfe_ccdc;
	struct ccdc_hw_device *ccdc_dev = ccdc->ccdc_dev;

	return ccdc_dev->hw_ops.getfid();
}


/* function to reset sbl */
void vpfe_sbl_reset(struct vpfe_ccdc_device *vpfe_ccdc)
{
	struct ccdc_hw_device *ccdc_dev = vpfe_ccdc->ccdc_dev;

	if (ccdc_dev->hw_ops.reset != NULL)
		ccdc_dev->hw_ops.reset();
}

static struct v4l2_mbus_framefmt *
__ccdc_get_format(struct vpfe_ccdc_device *ccdc, struct v4l2_subdev_fh *fh,
		  unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_subdev_format fmt;

		fmt.pad = pad;
		fmt.which = which;

		return v4l2_subdev_get_try_format(fh, pad);
	} else
		return &ccdc->formats[pad];
}

/*
 * ccdc_try_format - Try video format on a pad
 * @ccdc: VPFE CCDC device
 * @fh : V4L2 subdev file handle
 * @pad: Pad number
 * @fmt: Format
 */
static void
ccdc_try_format(struct vpfe_ccdc_device *vpfe_ccdc, struct v4l2_subdev_fh *fh,
		struct v4l2_subdev_format *fmt)
{
	unsigned int width = fmt->format.width;
	unsigned int height = fmt->format.height;
	unsigned int i;

	switch (fmt->pad) {
	case CCDC_PAD_SINK:
		for (i = 0; i < ARRAY_SIZE(ccdc_fmts); i++) {
			if (fmt->format.code == ccdc_fmts[i])
				break;
		}

		/* If not found, use YUYV8_2x8 as default */
		if (i >= ARRAY_SIZE(ccdc_fmts))
			fmt->format.code = V4L2_MBUS_FMT_YUYV8_2X8;

		/* Clamp the input size. */
		fmt->format.width = clamp_t(u32, width, 32, MAX_WIDTH);
		fmt->format.height = clamp_t(u32, height, 32, MAX_HEIGHT);
		break;

	case CCDC_PAD_SOURCE:
		for (i = 0; i < ARRAY_SIZE(ccdc_fmts); i++) {
			if (fmt->format.code == ccdc_fmts[i])
				break;
		}

		/* If not found, use YUYV8_2x8 as default */
		if (i >= ARRAY_SIZE(ccdc_fmts))
			fmt->format.code = V4L2_MBUS_FMT_YUYV8_2X8;

		/* The data formatter truncates the number of horizontal output
		 * pixels to a multiple of 16. To avoid clipping data, allow
		 * callers to request an output size bigger than the input size
		 * up to the nearest multiple of 16.
		 */
		fmt->format.width = clamp_t(u32, width, 32, MAX_WIDTH);
		fmt->format.width &= ~15;
		fmt->format.height = clamp_t(u32, height, 32, MAX_HEIGHT);
		break;
	}

}

static int vpfe_config_ccdc_format(struct vpfe_device *vpfe_dev, unsigned int pad)
{
	struct ccdc_hw_device *ccdc_dev = vpfe_dev->vpfe_ccdc.ccdc_dev;
	struct vpfe_ccdc_device *vpfe_ccdc = &vpfe_dev->vpfe_ccdc;
	enum ccdc_frmfmt frm_fmt = CCDC_FRMFMT_INTERLACED;
	struct v4l2_pix_format format;
	int ret = 0;

	v4l2_fill_pix_format(&format, &vpfe_dev->vpfe_ccdc.formats[pad]);
	mbus_to_pix(&vpfe_dev->vpfe_ccdc.formats[pad], &format);

	if (ccdc_dev->hw_ops.set_pixel_format(
			format.pixelformat) < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"couldn't set pix format in ccdc\n");
		return -EINVAL;
	}

	/* call for s_crop will override these values */
	vpfe_ccdc->crop.left = 0;
	vpfe_ccdc->crop.top = 0;
	vpfe_ccdc->crop.width = format.width;
	vpfe_ccdc->crop.height = format.height;

	/* configure the image window */
	ccdc_dev->hw_ops.set_image_window(&vpfe_ccdc->crop);

	switch (vpfe_dev->vpfe_ccdc.formats[pad].field) {
	case V4L2_FIELD_INTERLACED:
		/* do nothing, since it is default */
		ret = ccdc_dev->hw_ops.set_buftype(
				CCDC_BUFTYPE_FLD_INTERLEAVED);
		break;
	case V4L2_FIELD_NONE:
		frm_fmt = CCDC_FRMFMT_PROGRESSIVE;
		/* buffer type only applicable for interlaced scan */
		break;
	case V4L2_FIELD_SEQ_TB:
		ret = ccdc_dev->hw_ops.set_buftype(
				CCDC_BUFTYPE_FLD_SEPARATED);
		break;
	default:
		return -EINVAL;
	}

	/* set the frame format */
	if (!ret)
		ret = ccdc_dev->hw_ops.set_frame_format(frm_fmt);
	return ret;
}

void ccdc_buffer_isr(struct vpfe_ccdc_device *ccdc)
{
	struct vpfe_video_device *video = &ccdc->video_out;
	struct ccdc_hw_device *ccdc_dev = ccdc->ccdc_dev;
	enum v4l2_field field;

	if (!video->started)
		return;

	field = video->fmt.fmt.pix.field;

	/* reset sbl overblow bit */
	if (ccdc_dev->hw_ops.reset != NULL)
		ccdc_dev->hw_ops.reset();

	if (field == V4L2_FIELD_NONE) {
		/* handle progressive frame capture */
		if (video->cur_frm != video->next_frm)
			vpfe_process_buffer_complete(video);
	} else {
		int fid;
		/* interlaced or TB capture check which field we
		 * are in hardware
		 */
		fid = ccdc_dev->hw_ops.getfid();

		/* switch the software maintained field id */
		video->field_id ^= 1;
		if (fid == video->field_id) {
			/* we are in-sync here,continue */
			if (fid == 0) {
				/*
				 * One frame is just being captured. If the
				 * next frame is available, release the current
				 * frame and move on
				 */
				if (video->cur_frm != video->next_frm)
					vpfe_process_buffer_complete(video);
				/*
				 * based on whether the two fields are stored
				 * interleavely or separately in memory,
				 * reconfigure the CCDC memory address
				 */
				if (field == V4L2_FIELD_SEQ_TB)
					vpfe_schedule_bottom_field(video);

				return;
			} else {
				/*
				 * if one field is just being captured configure
				 * the next frame get the next frame from the
				 * empty queue if no frame is available hold on
				 * to the current buffer
				 */
				spin_lock(&video->dma_queue_lock);
				if (!list_empty(&video->dma_queue) &&
				video->cur_frm == video->next_frm)
					vpfe_schedule_next_buffer(video);
				spin_unlock(&video->dma_queue_lock);
			}
		} else if (fid == 0) {
			/*
			 * out of sync. Recover from any hardware out-of-sync.
			 * May loose one frame
			 */
			video->field_id = fid;
		}
	}
}

void ccdc_vidint1_isr(struct vpfe_ccdc_device *ccdc)
{
	struct vpfe_video_device *video = &ccdc->video_out;

	if (!video->started)
		return;

	spin_lock(&video->dma_queue_lock);
	if ((video->fmt.fmt.pix.field == V4L2_FIELD_NONE) &&
	    !list_empty(&video->dma_queue) &&
	    video->cur_frm == video->next_frm)
		vpfe_schedule_next_buffer(video);
	spin_unlock(&video->dma_queue_lock);
}

/* -----------------------------------------------------------------------------
 * VPFE video operations
 */

static void ccdc_video_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_ccdc_device *vpfe_ccdc = &vpfe_dev->vpfe_ccdc;
	struct ccdc_hw_device *ccdc_dev = vpfe_ccdc->ccdc_dev;
	ccdc_dev->hw_ops.setfbaddr(addr);
}

static const struct vpfe_video_operations ccdc_video_ops = {
	.queue = ccdc_video_queue,
};


/*
 * V4L2 subdev operations
 */

/*
 * ccdc_ioctl - CCDC module private ioctl's
 * @sd: VPFE CCDC V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long ccdc_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct vpfe_ccdc_device *ccdc = v4l2_get_subdevdata(sd);
	struct ccdc_hw_device *ccdc_dev = ccdc->ccdc_dev;
	int ret;

	switch (cmd) {
	case VPFE_CMD_S_CCDC_RAW_PARAMS:
		ret = ccdc_dev->hw_ops.set_params(arg);
		break;
	case VPFE_CMD_G_CCDC_RAW_PARAMS:
		if (!ccdc_dev->hw_ops.get_params) {
			ret = -EINVAL;
			break;
		}
		ret = ccdc_dev->hw_ops.get_params(arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

/*
 * ccdc_set_stream - Enable/Disable streaming on the CCDC module
 * @sd: VPFE CCDC V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int ccdc_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_ccdc_device *ccdc = v4l2_get_subdevdata(sd);
	struct ccdc_hw_device *ccdc_dev = ccdc->ccdc_dev;
	int ret;

	if (enable) {
		ret = ccdc_dev->hw_ops.configure(
		(ccdc->output == CCDC_OUTPUT_MEMORY) ? 0 : 1);
		if (ret)
			return ret;

		if ((ccdc_dev->hw_ops.enable_out_to_sdram) &&
			(ccdc->output == CCDC_OUTPUT_MEMORY))
			ccdc_dev->hw_ops.enable_out_to_sdram(1);

		ccdc_dev->hw_ops.enable(1);
	} else {

		ccdc_dev->hw_ops.enable(0);

		if (ccdc_dev->hw_ops.enable_out_to_sdram)
			ccdc_dev->hw_ops.enable_out_to_sdram(0);
	}

	return 0;
}

static int ccdc_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct vpfe_ccdc_device *ccdc = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev = to_vpfe_device(ccdc);
	struct v4l2_mbus_framefmt *format;

	format = __ccdc_get_format(ccdc, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	ccdc_try_format(ccdc, fh, fmt);
	memcpy(format, &fmt->format, sizeof(*format));

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (fmt->pad == CCDC_PAD_SINK)
		return vpfe_config_ccdc_format(vpfe_dev, fmt->pad);

	return 0;
}

/*
 * ccdc_get_format - Retrieve the video format on a pad
 * @sd : VPFE CCDC V4L2 subdevice
 * @fh : V4L2 subdev file handle
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int ccdc_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{

	struct vpfe_ccdc_device *vpfe_ccdc = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __ccdc_get_format(vpfe_ccdc, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	memcpy(&fmt->format, format, sizeof(fmt->format));

	return 0;
}

static int ccdc_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct vpfe_ccdc_device *ccdc = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_format format;

	if (fse->index != 0)
		return -EINVAL;

	format.pad = fse->pad;
	format.format.code = fse->code;
	format.format.width = 1;
	format.format.height = 1;
	format.which = V4L2_SUBDEV_FORMAT_TRY;
	ccdc_try_format(ccdc, fh, &format);
	fse->min_width = format.format.width;
	fse->min_height = format.format.height;

	if (format.format.code != fse->code)
		return -EINVAL;

	format.pad = fse->pad;
	format.format.code = fse->code;
	format.format.width = -1;
	format.format.height = -1;
	format.which = V4L2_SUBDEV_FORMAT_TRY;
	ccdc_try_format(ccdc, fh, &format);
	fse->max_width = format.format.width;
	fse->max_height = format.format.height;

	return 0;
}

static int ccdc_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->pad) {
	case CCDC_PAD_SINK:
	case CCDC_PAD_SOURCE:
		if (code->index >= ARRAY_SIZE(ccdc_fmts))
			return -EINVAL;

		code->code = ccdc_fmts[code->index];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ccdc_pad_set_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_crop *crop)
{
	struct vpfe_ccdc_device *vpfe_ccdc = v4l2_get_subdevdata(sd);
	struct ccdc_hw_device *ccdc_dev = vpfe_ccdc->ccdc_dev;
	struct v4l2_mbus_framefmt *format;

	/* check wether its a valid pad */
	if (crop->pad != CCDC_PAD_SINK)
		return -EINVAL;

	format = __ccdc_get_format(vpfe_ccdc, fh, crop->pad, crop->which);
	if (format == NULL)
		return -EINVAL;

	/* check wether crop rect is within limits */
	if (crop->rect.top < 0 || crop->rect.left < 0 ||
	(crop->rect.left + crop->rect.width >
	vpfe_ccdc->formats[CCDC_PAD_SINK].width) ||
	(crop->rect.top + crop->rect.height >
	vpfe_ccdc->formats[CCDC_PAD_SINK].height)) {
		crop->rect.left = 0;
		crop->rect.top = 0;
		crop->rect.width = format->width;
		crop->rect.height = format->height;
	}

	/* adjust the width to 16 pixel boundry */
	crop->rect.width = ((crop->rect.width + 15) & ~0xf);

	vpfe_ccdc->crop = crop->rect;

	if (crop->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		ccdc_dev->hw_ops.set_image_window(&vpfe_ccdc->crop);
	else {
		struct v4l2_rect *rect;

		rect = v4l2_subdev_get_try_crop(fh, CCDC_PAD_SINK);
		memcpy(rect, &vpfe_ccdc->crop, sizeof(*rect));
	}

	return 0;
}

static int ccdc_pad_get_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_crop *crop)
{
	struct vpfe_ccdc_device *vpfe_ccdc = v4l2_get_subdevdata(sd);

	/* check wether its a valid pad */
	if (crop->pad != CCDC_PAD_SINK)
		return -EINVAL;

	if (crop->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_rect *rect;
		rect = v4l2_subdev_get_try_crop(fh, CCDC_PAD_SINK);
		memcpy(&crop->rect, rect, sizeof(*rect));
	} else
		crop->rect = vpfe_ccdc->crop;

	return 0;
}

/*
 * ccdc_init_formats - Initialize formats on all pads
 * @sd: VPFE ccdc V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int ccdc_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct v4l2_subdev_crop crop;

	memset(&format, 0, sizeof(format));
	format.pad = CCDC_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_SBGGR10_1X10;
	format.format.width = MAX_WIDTH;
	format.format.height = MAX_HEIGHT;
	ccdc_set_format(sd, fh, &format);

	memset(&format, 0, sizeof(format));
	format.pad = CCDC_PAD_SOURCE;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_SBGGR10_1X10;
	format.format.width = MAX_WIDTH;
	format.format.height = MAX_HEIGHT;
	ccdc_set_format(sd, fh, &format);

	memset(&crop, 0, sizeof(crop));
	crop.pad = CCDC_PAD_SINK;
	crop.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	crop.rect.width = MAX_WIDTH;
	crop.rect.height = MAX_HEIGHT;
	ccdc_pad_set_crop(sd, fh, &crop);

	return 0;
}

/* subdev core operations */
static const struct v4l2_subdev_core_ops ccdc_v4l2_core_ops = {
	.ioctl = ccdc_ioctl,
};

/* subdev file operations */
static const struct v4l2_subdev_file_ops ccdc_v4l2_file_ops = {
	.open = ccdc_init_formats,
};

/* subdev video operations */
static const struct v4l2_subdev_video_ops ccdc_v4l2_video_ops = {
	.s_stream = ccdc_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops ccdc_v4l2_pad_ops = {
	.enum_mbus_code = ccdc_enum_mbus_code,
	.enum_frame_size = ccdc_enum_frame_size,
	.get_fmt = ccdc_get_format,
	.set_fmt = ccdc_set_format,
	.set_crop = ccdc_pad_set_crop,
	.get_crop = ccdc_pad_get_crop,
};

/* v4l2 subdev operations */
static const struct v4l2_subdev_ops ccdc_v4l2_ops = {
	.core = &ccdc_v4l2_core_ops,
	.file = &ccdc_v4l2_file_ops,
	.video = &ccdc_v4l2_video_ops,
	.pad = &ccdc_v4l2_pad_ops,
};

/*
 * Media entity operations
 */

/*
 * ccdc_link_setup - Setup CCDC connections
 * @entity: CCDC media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int ccdc_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{

	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_ccdc_device *ccdc = v4l2_get_subdevdata(sd);

	switch (local->index | media_entity_type(remote->entity)) {
	case CCDC_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* read from decoder/sensor */
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			ccdc->input = CCDC_INPUT_NONE;
			break;
		}

		if (ccdc->input != CCDC_INPUT_NONE)
			return -EBUSY;

		ccdc->input = CCDC_INPUT_PARALLEL;

		break;

	case CCDC_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		/* write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED)
			ccdc->output = CCDC_OUTPUT_MEMORY;
		else
			ccdc->output = CCDC_OUTPUT_NONE;
		break;

	case CCDC_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
		if (flags & MEDIA_LNK_FL_ENABLED)
			ccdc->output = CCDC_OUTPUT_PREVIEWER;
		else
			ccdc->output = CCDC_OUTPUT_NONE;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static const struct media_entity_operations ccdc_media_ops = {
	.link_setup = ccdc_link_setup,
};

void vpfe_ccdc_unregister_entities(struct vpfe_ccdc_device *ccdc)
{
	struct ccdc_hw_device *ccdc_dev = ccdc->ccdc_dev;
	struct device *dev = ccdc->subdev.v4l2_dev->dev;

	vpfe_video_unregister(&ccdc->video_out);

	if (ccdc_dev->hw_ops.close)
		ccdc_dev->hw_ops.close(dev);

	/* cleanup entity */
	media_entity_cleanup(&ccdc->subdev.entity);
	/* unregister subdev */
	v4l2_device_unregister_subdev(&ccdc->subdev);
}

int vpfe_ccdc_register_entities(struct vpfe_ccdc_device *ccdc,
				struct v4l2_device *vdev)
{
	struct ccdc_hw_device *ccdc_dev = NULL;
	struct device *dev = vdev->dev;
	struct vpfe_device *vpfe_dev = to_vpfe_device(ccdc);
	int ret;
	unsigned int flags = 0;

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &ccdc->subdev);
	if (ret < 0)
		return ret;

	ccdc_dev = ccdc->ccdc_dev;

	ret = ccdc_dev->hw_ops.open(dev);
	if (ret)
		goto out_ccdc_open;

	ret = vpfe_video_register(&ccdc->video_out, vdev);
	if (ret) {
		printk(KERN_ERR "failed to register ccdc video out device\n");
		goto out_video_register;
	}

	ccdc->video_out.vpfe_dev = vpfe_dev;

	/* connect ccdc to video node */
	ret = media_entity_create_link(&ccdc->subdev.entity,
				       1,
				       &ccdc->video_out.video_dev.entity,
				       0, flags);
	if (ret < 0)
		goto out_create_link;

	return 0;
out_create_link:
	vpfe_video_unregister(&ccdc->video_out);
out_video_register:
	if (ccdc_dev->hw_ops.close)
		ccdc_dev->hw_ops.close(dev);

out_ccdc_open:
	v4l2_device_unregister_subdev(&ccdc->subdev);

	return ret;
}

/*
 * vpfe_ccdc_init_entities - Initialize V4L2 subdev and media entity
 * @ccdc: VPFE CCDC module
 *
 * Return 0 on success and a negative error code on failure.
 */
int vpfe_ccdc_init(struct vpfe_ccdc_device *ccdc, struct platform_device *pdev)
{
	struct v4l2_subdev *sd = &ccdc->subdev;
	struct media_pad *pads = &ccdc->pads[0];
	struct media_entity *me = &sd->entity;
	int ret;

	if (cpu_is_davinci_dm644x()) {
		if (dm644x_ccdc_init(pdev))
			return -1;
	} else if (cpu_is_davinci_dm365()) {
		if (dm365_ccdc_init(pdev))
			return -1;
	} else if (cpu_is_davinci_dm355()) {
		if (dm355_ccdc_init(pdev))
			return -1;
	} else {
		printk(KERN_ERR "vpfe_ccdc_init-not supported\n");
		return -1;
	}

	/* queue ops */
	ccdc->video_out.ops = &ccdc_video_ops;

	v4l2_subdev_init(sd, &ccdc_v4l2_ops);
	strlcpy(sd->name, "DAVINCI CCDC", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(sd, ccdc);
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->nevents = DAVINCI_CCDC_NEVENTS;
	pads[CCDC_PAD_SINK].flags = MEDIA_PAD_FL_INPUT;
	pads[CCDC_PAD_SOURCE].flags = MEDIA_PAD_FL_OUTPUT;

	ccdc->input = CCDC_INPUT_NONE;
	ccdc->output = CCDC_OUTPUT_NONE;

	me->ops = &ccdc_media_ops;

	ret = media_entity_init(me, CCDC_PADS_NUM, pads, 0);
	if (ret)
		goto out_davanci_init;
	ccdc->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = vpfe_video_init(&ccdc->video_out, "CCDC");
	if (ret) {
		printk(KERN_ERR "failed to init ccdc-out video device\n");
		goto out_davanci_init;
	}

	ccdc->ccdc_dev = get_ccdc_dev();

	return 0;

out_davanci_init:
	if (cpu_is_davinci_dm644x())
		dm644x_ccdc_remove(pdev);
	else if (cpu_is_davinci_dm365())
		dm365_ccdc_remove(pdev);
	return ret;
}

/*
 * vpfe_ccdc_cleanup - CCDC module cleanup.
 * @dev: Device pointer specific to the VPFE.
 */
void vpfe_ccdc_cleanup(struct platform_device *pdev)
{
	if (cpu_is_davinci_dm644x())
		dm644x_ccdc_remove(pdev);
	else if (cpu_is_davinci_dm365())
		dm365_ccdc_remove(pdev);
}
