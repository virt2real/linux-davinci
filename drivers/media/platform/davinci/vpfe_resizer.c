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
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <media/davinci/vpfe_capture.h>
#include <mach/cputype.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <media/davinci/imp_hw_if.h>
#include <media/davinci/imp_resizer.h>

#define MIN_IN_WIDTH	32
#define MIN_IN_HEIGHT	32
#define MAX_IN_WIDTH	4095
#define MAX_IN_HEIGHT	4095
#define MIN_OUT_WIDTH	16
#define MIN_OUT_HEIGHT	2

/* resizer pixel formats */
static const unsigned int resz_fmts[] = {
	V4L2_MBUS_FMT_Y8_1X8,
	V4L2_MBUS_FMT_UYVY8_2X8,
	V4L2_MBUS_FMT_YUYV8_2X8,
	V4L2_MBUS_FMT_YUYV8_1X16,
	V4L2_MBUS_FMT_YUYV10_1X20,
};

static void set_resizer_channel_cont_mode(struct vpfe_resizer_device *resizer)
{
	struct imp_logical_channel *channel = &resizer->channel;

	channel->config_state = STATE_NOT_CONFIGURED;
	channel->user_config = NULL;
}

static int set_resizer_channel_ss_mode(struct vpfe_resizer_device *resizer)
{
	struct imp_logical_channel *channel = &resizer->channel;
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;

	channel->config_state = STATE_NOT_CONFIGURED;
	channel->user_config = NULL;

	return imp_hw_if->set_oper_mode(IMP_MODE_SINGLE_SHOT);

	return 0;
}

static void reset_resizer_channel_mode(struct vpfe_resizer_device *resizer)
{
	struct imp_logical_channel *channel = &resizer->channel;
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;

	if (resizer->input == RESIZER_INPUT_MEMORY)
		imp_hw_if->reset_oper_mode();

	if (channel->config_state == STATE_CONFIGURED) {
		kfree(channel->user_config);
		channel->user_config = NULL;
	}
}

/* -----------------------------------------------------------------------------
 * VPFE video operations
 */

static void rsz_video_in_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_resizer_device *resizer = &vpfe_dev->vpfe_resizer;
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;
	struct imp_logical_channel *chan = &resizer->channel;

	if (resizer->input == RESIZER_INPUT_MEMORY)
		imp_hw_if->update_inbuf_address(chan->config, addr);
	else
		imp_hw_if->update_inbuf_address(NULL, addr);
}

static void rsz_video_out1_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_resizer_device *resizer = &vpfe_dev->vpfe_resizer;
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;
	struct imp_logical_channel *chan = &resizer->channel;
	struct vpfe_pipeline *pipe = &resizer->video_out.pipe;

	if (pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT)
		imp_hw_if->update_outbuf1_address(chan->config, addr);
	else
		imp_hw_if->update_outbuf1_address(NULL, addr);

}

static void rsz_video_out2_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_resizer_device *resizer = &vpfe_dev->vpfe_resizer;
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;
	struct imp_logical_channel *chan = &resizer->channel;
	struct vpfe_pipeline *pipe = &resizer->video_out.pipe;

	if (pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT)
		imp_hw_if->update_outbuf2_address(chan->config, addr);
	else
		imp_hw_if->update_outbuf2_address(NULL, addr);
}

static const struct vpfe_video_operations video_in_ops = {
	.queue = rsz_video_in_queue,
};

static const struct vpfe_video_operations video_out1_ops = {
	.queue = rsz_video_out1_queue,
};

static const struct vpfe_video_operations video_out2_ops = {
	.queue = rsz_video_out2_queue,
};

static void rsz_ss_buffer_isr(struct vpfe_resizer_device *resizer)
{
	struct vpfe_video_device *video_out = &resizer->video_out;
	struct vpfe_video_device *video_in = &resizer->video_in;
	u32 val;

	if (resizer->output == RESIZER_OUPUT_MEMORY) {
		val = vpss_dma_complete_interrupt();/* TODO we need this?*/
		if ((val != 0) && (val != 2))
			return;
	}

	if (resizer->input == RESIZER_INPUT_MEMORY) {
		vpfe_process_buffer_complete(video_in);
		video_in->state &= ~VPFE_VIDEO_BUFFER_QUEUED;
	}

	if (resizer->output == RESIZER_OUPUT_MEMORY) {
		vpfe_process_buffer_complete(video_out);
		video_out->state &= ~VPFE_VIDEO_BUFFER_QUEUED;
	}
}

void rsz_buffer_isr(struct vpfe_resizer_device *resizer)
{
	struct vpfe_video_device *video_out = &resizer->video_out;
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	enum v4l2_field field;


	if (!video_out->started)
		return;

	if (resizer->input != RESIZER_INPUT_PREVIEWER)
		return;

	field = video_out->fmt.fmt.pix.field;

	if (field == V4L2_FIELD_NONE) {
		struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;

		/* handle progressive frame capture */
		if (video_out->cur_frm != video_out->next_frm)
			vpfe_process_buffer_complete(video_out);

		video_out->skip_frame_count--;
		if (!video_out->skip_frame_count) {
			video_out->skip_frame_count =
				video_out->skip_frame_count_init;
			if (imp_hw_if->enable_resize)
				imp_hw_if->enable_resize(1);
		} else {
			if (imp_hw_if->enable_resize)
				imp_hw_if->enable_resize(0);
		}

	} else {
		/* handle interlaced frame capture */
		int fid;

		fid = ccdc_get_fid(vpfe_dev);

		/* switch the software maintained field id */
		video_out->field_id ^= 1;
		if (fid == video_out->field_id) {
			/* we are in-sync here,continue */
			if (fid == 0) {
				/*
				 * One frame is just being captured. If the
				 * next frame is available, release the current
				 * frame and move on
				 */
				if (video_out->cur_frm != video_out->next_frm)
					vpfe_process_buffer_complete(video_out);
			}
		} else if (fid == 0) {
			/*
			* out of sync. Recover from any hardware out-of-sync.
			* May loose one frame
			*/
			video_out->field_id = fid;
		}
	}
}

void rsz_dma_isr(struct vpfe_resizer_device *resizer)
{
	int fid, schedule_capture = 0;
	enum v4l2_field field;
	struct vpfe_video_device *video_out = &resizer->video_out;
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	struct vpfe_pipeline *pipe = &video_out->pipe;

	if (!video_out->started)
		return;

	if (pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT) {
		/* single shot ..TODO handle well*/
		rsz_ss_buffer_isr(resizer);
		return;
	}

	field = video_out->fmt.fmt.pix.field;

	if (field == V4L2_FIELD_NONE) {
		if (!list_empty(&video_out->dma_queue) &&
			video_out->cur_frm == video_out->next_frm)
			schedule_capture = 1;
	} else {
		fid = ccdc_get_fid(vpfe_dev);
		if (fid == video_out->field_id) {
			/* we are in-sync here,continue */
			if (fid == 1 && !list_empty(&video_out->dma_queue) &&
			    video_out->cur_frm == video_out->next_frm)
				schedule_capture = 1;
		}
	}
	if (schedule_capture) {
		spin_lock(&video_out->dma_queue_lock);
		vpfe_schedule_next_buffer(video_out);
		spin_unlock(&video_out->dma_queue_lock);
	}
}

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
static long resizer_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct imp_logical_channel *rsz_conf_chan = &resizer->channel;
	struct device *dev = resizer->subdev.v4l2_dev->dev;
	int ret = 0;

	switch (cmd) {

	case RSZ_S_CONFIG:
		{
			struct rsz_channel_config *user_config =
			    (struct rsz_channel_config *)arg;

			dev_dbg(dev, "RSZ_S_CONFIG:\n");

			if (!ret) {
				ret = imp_set_resizer_config(dev,
						     rsz_conf_chan,
							user_config);

			}
		}
		break;

	case RSZ_G_CONFIG:
		{
			struct rsz_channel_config *user_config =
			    (struct rsz_channel_config *)arg;

			dev_err(dev, "RSZ_G_CONFIG:%d:%d\n",
				user_config->chain,
				user_config->len);
			if (ISNULL(user_config->config)) {
				ret = -EINVAL;
				dev_err(dev,
					"error in PREV_GET_CONFIG\n");
				goto ERROR;
			}

			ret =
			    imp_get_resize_config(dev, rsz_conf_chan,
						  user_config);

		}
		break;
	default:
		printk(KERN_ERR "invalid command\n");
	}

ERROR:
	return ret;
}

/*
 * ccdc_set_stream - Enable/Disable streaming on the RESIZER module
 * @sd: VPFE RESIZER V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int resizer_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct imp_logical_channel *chan = &resizer->channel;
	struct device *dev = resizer->subdev.v4l2_dev->dev;
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;
	struct vpfe_pipeline *pipe = &resizer->video_out.pipe;
	void *config = (pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT) ?
				chan->config : NULL;

	if (resizer->output != RESIZER_OUPUT_MEMORY)
		return 0;

	switch (enable) {
	case 1:
		if ((pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT) &&
		(imp_hw_if->serialize())) {
			if (imp_hw_if->hw_setup(dev, config) < 0)
				return -1;
		} else if (pipe->state == VPFE_PIPELINE_STREAM_CONTINUOUS) {
			imp_hw_if->lock_chain();
			if (imp_hw_if->hw_setup(dev, NULL))
				return -1;
		}
		imp_hw_if->enable(1, config);
		break;
	case 0:
		imp_hw_if->enable(0, config);
		if ((pipe->state == VPFE_PIPELINE_STREAM_CONTINUOUS))
			imp_hw_if->unlock_chain();
		break;
	}

	return 0;
}

/*
* __resizer_get_format - helper function for getting resizer format
* @res   : pointer to resizer private structure
* @pad   : pad number
* @fh    : V4L2 subdev file handle
* @which : wanted subdev format
* Retun wanted mbus frame format
*/
static struct v4l2_mbus_framefmt *
__resizer_get_format(struct vpfe_resizer_device *res, struct v4l2_subdev_fh *fh,
		     unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &res->formats[pad];
}

/*
* resizer_try_format - Handle try format by pad subdev method
* @res   : ISP resizer device
* @fh    : V4L2 subdev file handle
* @pad   : pad num
* @fmt   : pointer to v4l2 format structure
* @which : wanted subdev format
*/
static void resizer_try_format(struct vpfe_resizer_device *res,
			struct v4l2_subdev_fh *fh, unsigned int pad,
			struct v4l2_mbus_framefmt *fmt,
			enum v4l2_subdev_format_whence which)
{
	unsigned int max_out_width, max_out_height;
	struct imp_hw_interface *imp_hw_if;

	imp_hw_if = res->imp_hw_if;
	max_out_width = imp_hw_if->get_max_output_width(0);
	max_out_height = imp_hw_if->get_max_output_height(0);

	if (fmt->code != V4L2_MBUS_FMT_UYVY8_2X8 &&
		fmt->code != V4L2_MBUS_FMT_YUYV8_2X8 &&
		fmt->code != V4L2_MBUS_FMT_YUYV8_1X16 &&
		fmt->code != V4L2_MBUS_FMT_YUYV10_1X20 &&
		fmt->code != V4L2_MBUS_FMT_Y8_1X8)
		fmt->code = V4L2_MBUS_FMT_YUYV8_2X8;

	if (pad == RESIZER_PAD_SINK) {
		fmt->width = clamp_t(u32, fmt->width, MIN_IN_WIDTH,
					MAX_IN_WIDTH);
		fmt->height = clamp_t(u32, fmt->height, MIN_IN_HEIGHT,
				MAX_IN_HEIGHT);
	} else if (pad == RESIZER_PAD_SOURCE) {
		fmt->width = clamp_t(u32, fmt->width, MIN_OUT_WIDTH,
					max_out_width);
		fmt->width &= ~15;
		fmt->height = clamp_t(u32, fmt->height, MIN_OUT_HEIGHT,
				max_out_height);
	}
}

static int resizer_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct imp_hw_interface *imp_hw_if = resizer->imp_hw_if;
	struct v4l2_mbus_framefmt *format;
	enum imp_pix_formats imp_pix;
	struct imp_window imp_win;
	int ret;

	format = __resizer_get_format(resizer, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	resizer_try_format(resizer, fh, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if ((fmt->pad == RESIZER_PAD_SINK) &&
		(resizer->input == RESIZER_INPUT_PREVIEWER)) {
		/*
		 *This is continous mode, input format is already set in
		 *PREVIEWER sink set format, no need to do here.
		 */
		resizer->formats[fmt->pad] = fmt->format;
	} else if ((fmt->pad == RESIZER_PAD_SOURCE) &&
		(resizer->output == RESIZER_OUPUT_MEMORY)) {

		if (fmt->format.code == V4L2_MBUS_FMT_SBGGR10_1X10)
			imp_pix = IMP_BAYER;
		else if ((fmt->format.code == V4L2_MBUS_FMT_YUYV8_2X8) ||
			(fmt->format.code == V4L2_MBUS_FMT_YUYV10_1X20))
			imp_pix = IMP_UYVY;
		/* FIXME:Replace V4L2_MBUS_FMT_UYVY8_2X8 with one for NV12 */
		else if (fmt->format.code == V4L2_MBUS_FMT_UYVY8_2X8)
			imp_pix = IMP_YUV420SP;
		else
			return -EINVAL;

		ret = imp_hw_if->set_out_pixel_format(imp_pix);
		if (ret)
			return ret;

		imp_win.width = fmt->format.width;
		imp_win.height = fmt->format.height;
		imp_win.hst = 0;
		imp_win.vst = 0;

		ret = imp_hw_if->set_output_win(&imp_win);
		if (ret)
			return ret;

		resizer->formats[fmt->pad] = fmt->format;

	} else if ((fmt->pad == RESIZER_PAD_SINK) &&
		(resizer->input == RESIZER_INPUT_MEMORY)) {
		/*
		 * single shot mode
		 */
		if (fmt->format.code == V4L2_MBUS_FMT_SBGGR10_1X10)
			imp_pix = IMP_BAYER;
		else
			imp_pix = IMP_UYVY;

		ret = imp_hw_if->set_in_pixel_format(imp_pix);
		if (ret)
			return ret;

		if (fmt->format.field == V4L2_FIELD_INTERLACED) {
			imp_hw_if->set_buftype(0);
			imp_hw_if->set_frame_format(0);
		} else if (fmt->format.field == V4L2_FIELD_NONE)
			imp_hw_if->set_frame_format(1);
		else
			return ret;

		imp_win.width = fmt->format.width;
		imp_win.height = fmt->format.height;
		imp_win.hst = 0;
		imp_win.vst = 1;

		ret = imp_hw_if->set_input_win(&imp_win);
		if (ret)
			return ret;

		resizer->formats[fmt->pad] = fmt->format;

	} else
		return -EINVAL;

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
 * TODO:not tested
 */
static int resizer_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct vpfe_resizer_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __resizer_get_format(res, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;

}

static int resizer_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct vpfe_resizer_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	resizer_try_format(res, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	resizer_try_format(res, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;
	return 0;
}

static int resizer_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad == RESIZER_PAD_SINK ||
		code->pad == RESIZER_PAD_SOURCE) {
		if (code->index >= ARRAY_SIZE(resz_fmts))
			return -EINVAL;

		code->code = resz_fmts[code->index];
	}

	return 0;
}

/*
 * resizer_init_formats - Initialize formats on all pads
 * @sd: VPFE resizer V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int resizer_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct vpfe_resizer_device *res = v4l2_get_subdevdata(sd);
	struct imp_hw_interface *imp_hw_if = res->imp_hw_if;

	memset(&format, 0, sizeof(format));
	format.pad = RESIZER_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_YUYV8_2X8;
	format.format.width = MAX_IN_WIDTH;
	format.format.height = MAX_IN_HEIGHT;
	resizer_set_format(sd, fh, &format);

	memset(&format, 0, sizeof(format));
	format.pad = RESIZER_PAD_SOURCE;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_YUYV8_2X8;
	format.format.width = imp_hw_if->get_max_output_width(0);
	format.format.height = imp_hw_if->get_max_output_height(0);
	resizer_set_format(sd, fh, &format);

	return 0;
}

/* V4L2 subdev core operations */
static const struct v4l2_subdev_core_ops resizer_v4l2_core_ops = {
	.ioctl = resizer_ioctl,
};

/* subdev file operations */
static const struct v4l2_subdev_file_ops resizer_v4l2_file_ops = {
	.open = resizer_init_formats,
};

/* V4L2 subdev video operations */
static const struct v4l2_subdev_video_ops resizer_v4l2_video_ops = {
	.s_stream = resizer_set_stream,
};

/* V4L2 subdev pad operations */
static const struct v4l2_subdev_pad_ops resizer_v4l2_pad_ops = {
	.enum_mbus_code = resizer_enum_mbus_code,
	.enum_frame_size = resizer_enum_frame_size,
	.get_fmt = resizer_get_format,
	.set_fmt = resizer_set_format,
};

/* V4L2 subdev operations */
static const struct v4l2_subdev_ops resizer_v4l2_ops = {
	.core = &resizer_v4l2_core_ops,
	.file = &resizer_v4l2_file_ops,
	.video = &resizer_v4l2_video_ops,
	.pad = &resizer_v4l2_pad_ops,
};

/* -----------------------------------------------------------------------------
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
static int resizer_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{

	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);

	switch (local->index | media_entity_type(remote->entity)) {
	case RESIZER_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* Read from previewer - continous mode */
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			reset_resizer_channel_mode(resizer);
			resizer->input = RESIZER_INPUT_NONE;
			break;
		}

		if (resizer->input != RESIZER_INPUT_NONE)
			return -EBUSY;

		resizer->input = RESIZER_INPUT_PREVIEWER;

		/* TODO: need 2 handle this */
		set_resizer_channel_cont_mode(resizer);

		break;

	case RESIZER_PAD_SINK | MEDIA_ENT_T_DEVNODE:
		/* Read from memory - single shot mode*/
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			reset_resizer_channel_mode(resizer);
			resizer->input = RESIZER_INPUT_NONE;
			break;
		}

		if (set_resizer_channel_ss_mode(resizer))
			return -EINVAL;

		resizer->input = RESIZER_INPUT_MEMORY;
		break;
	case RESIZER_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		/* Write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED)
			resizer->output = RESIZER_OUPUT_MEMORY;
		else
			resizer->output = RESIZER_OUTPUT_NONE;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static const struct media_entity_operations resizer_media_ops = {
	.link_setup = resizer_link_setup,
};

void vpfe_resizer_unregister_entities(struct vpfe_resizer_device *vpfe_rsz)
{
	/* unregister video devices */
	vpfe_video_unregister(&vpfe_rsz->video_in);
	vpfe_video_unregister(&vpfe_rsz->video_out);

	/* cleanup entity */
	media_entity_cleanup(&vpfe_rsz->subdev.entity);
	/* unregister subdev */
	v4l2_device_unregister_subdev(&vpfe_rsz->subdev);
}

int vpfe_resizer_register_entities(struct vpfe_resizer_device *resizer,
				   struct v4l2_device *vdev)
{
	int ret;
	unsigned int flags = 0;
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &resizer->subdev);
	if (ret < 0) {
		printk(KERN_ERR "failed to register resizer as v4l2-subdev\n");
		return ret;
	}

	ret = vpfe_video_register(&resizer->video_in, vdev);
	if (ret) {
		printk(KERN_ERR "failed to register RSZ video-in device\n");
		goto out_video_in_register;
	}

	resizer->video_in.vpfe_dev = vpfe_dev;

	ret = vpfe_video_register(&resizer->video_out, vdev);
	if (ret) {
		printk(KERN_ERR "failed to register RSZ video-out device\n");
		goto out_video_out_register;
	}

	resizer->video_out.vpfe_dev = vpfe_dev;

	ret = media_entity_create_link(&resizer->video_in.video_dev.entity,
				       0,
				       &resizer->subdev.entity,
				       0, flags);
	if (ret < 0)
		goto out_create_link;

	ret = media_entity_create_link(&resizer->subdev.entity,
				       1,
				       &resizer->video_out.video_dev.entity,
				       0, flags);
	if (ret < 0)
		goto out_create_link;

	return 0;

out_create_link:
	vpfe_video_unregister(&resizer->video_out);
out_video_out_register:
	vpfe_video_unregister(&resizer->video_in);
out_video_in_register:
	media_entity_cleanup(&resizer->subdev.entity);
	v4l2_device_unregister_subdev(&resizer->subdev);
	return ret;
}

int vpfe_resizer_init(struct vpfe_resizer_device *vpfe_rsz,
		      struct platform_device *pdev)
{
	struct v4l2_subdev *resizer = &vpfe_rsz->subdev;
	struct media_pad *pads = &vpfe_rsz->pads[0];
	struct media_entity *me = &resizer->entity;
	struct imp_logical_channel *channel = &vpfe_rsz->channel;

	int ret;

	if (cpu_is_davinci_dm365() || cpu_is_davinci_dm355()) {
		vpfe_rsz->imp_hw_if = imp_get_hw_if();

		if (ISNULL(vpfe_rsz->imp_hw_if))
			return -1;
	} else
		return -1;

	vpfe_rsz->video_in.ops = &video_in_ops;
	vpfe_rsz->video_out.ops = &video_out1_ops;
	/*TODO:enable with rsz-b*/

	v4l2_subdev_init(resizer, &resizer_v4l2_ops);
	strlcpy(resizer->name, "DAVINCI RESIZER", sizeof(resizer->name));
	resizer->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(resizer, vpfe_rsz);
	resizer->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[RESIZER_PAD_SINK].flags = MEDIA_PAD_FL_INPUT;
	pads[RESIZER_PAD_SOURCE].flags = MEDIA_PAD_FL_OUTPUT;

	vpfe_rsz->input = RESIZER_INPUT_NONE;
	vpfe_rsz->output = RESIZER_OUTPUT_NONE;

	channel->type = IMP_RESIZER;
	channel->config_state = STATE_NOT_CONFIGURED;

	me->ops = &resizer_media_ops;

	ret = media_entity_init(me, RESIZER_PADS_NUM, pads, 0);
	if (ret)
		return ret;

	vpfe_rsz->video_in.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = vpfe_video_init(&vpfe_rsz->video_in, "RSZ");
	if (ret) {
		printk(KERN_ERR "failed to init RSZ video-in device\n");
		return ret;
	}

	vpfe_rsz->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = vpfe_video_init(&vpfe_rsz->video_out, "RSZ");
	if (ret) {
		printk(KERN_ERR "failed to init RSZ video-out device\n");
		return ret;
	}

	return 0;
}

/*
 * vpfe_resizer_cleanup - RESIZER module cleanup.
 * @dev: Device pointer specific to the VPFE.
 */
void vpfe_resizer_cleanup(struct platform_device *pdev)
{
  /* do nothing */
}
