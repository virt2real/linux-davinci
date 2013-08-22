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
#include <media/davinci/imp_hw_if.h>
#include <media/davinci/imp_common.h>
#include <media/davinci/imp_previewer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>

#define MIN_OUT_WIDTH	32
#define MIN_OUT_HEIGHT	32

/* previewer format descriptions */
static const unsigned int prev_input_fmts[] = {
	V4L2_MBUS_FMT_Y8_1X8,
	V4L2_MBUS_FMT_YUYV8_2X8,
	V4L2_MBUS_FMT_YUYV8_1X16,
	V4L2_MBUS_FMT_YUYV10_1X20,
	V4L2_MBUS_FMT_SBGGR10_1X10,
};

static const unsigned int prev_output_fmts[] = {
	V4L2_MBUS_FMT_Y8_1X8,
	V4L2_MBUS_FMT_YUYV8_2X8,
	V4L2_MBUS_FMT_YUYV8_1X16,
	V4L2_MBUS_FMT_YUYV10_1X20,
};

static int set_channel_prv_cont_mode(struct vpfe_previewer_device *previewer)
{
	struct imp_logical_channel *channel = &previewer->channel;
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;

	channel->type = IMP_PREVIEWER;
	channel->config_state = STATE_NOT_CONFIGURED;

	return imp_hw_if->set_oper_mode(IMP_MODE_CONTINUOUS);
}

static int set_channel_prv_ss_mode(struct vpfe_previewer_device *previewer)
{
	struct imp_logical_channel *channel = &previewer->channel;
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;

	channel->type = IMP_PREVIEWER;
	channel->config_state = STATE_NOT_CONFIGURED;

	return imp_hw_if->set_oper_mode(IMP_MODE_SINGLE_SHOT);
}

static void reset_channel_prv_mode(struct vpfe_previewer_device *previewer)
{
	struct imp_logical_channel *channel = &previewer->channel;
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;

	imp_hw_if->reset_oper_mode();

	if (channel->config_state == STATE_CONFIGURED) {
		kfree(channel->user_config);
		memset(channel, 0, sizeof(struct imp_logical_channel));
	}
}

static void prv_video_in_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_previewer_device *previewer = &vpfe_dev->vpfe_previewer;
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;
	struct imp_logical_channel *chan = &previewer->channel;

	if (previewer->input == PREVIEWER_INPUT_MEMORY)
		imp_hw_if->update_inbuf_address(chan->config, addr);
	else
		imp_hw_if->update_inbuf_address(NULL, addr);
}

static void prv_video_out_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_previewer_device *previewer = &vpfe_dev->vpfe_previewer;
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;
	struct imp_logical_channel *chan = &previewer->channel;

	if (previewer->input == PREVIEWER_INPUT_MEMORY)
		imp_hw_if->update_outbuf1_address(chan->config, addr);
	else
		imp_hw_if->update_outbuf1_address(NULL, addr);
}

static const struct vpfe_video_operations video_in_ops = {
	.queue = prv_video_in_queue,
};

static const struct vpfe_video_operations video_out_ops = {
	.queue = prv_video_out_queue,
};

static void prv_ss_buffer_isr(struct vpfe_previewer_device *previewer)
{
	struct vpfe_video_device *video_out = &previewer->video_out;
	struct vpfe_video_device *video_in = &previewer->video_in;
	u32 val;

	if (!video_in->started)
		return;

	if (previewer->output == PREVIEWER_OUTPUT_MEMORY) {
		val = vpss_dma_complete_interrupt();/*TODO we need this?*/
		if ((val != 0) && (val != 2))
			return;
	}

	if (previewer->input == PREVIEWER_INPUT_MEMORY) {
			vpfe_process_buffer_complete(video_in);
			video_in->state &= ~VPFE_VIDEO_BUFFER_QUEUED;
	}

	if (previewer->output == PREVIEWER_OUTPUT_MEMORY) {
			vpfe_process_buffer_complete(video_out);
			video_out->state &= ~VPFE_VIDEO_BUFFER_QUEUED;
	}
}

void prv_buffer_isr(struct vpfe_previewer_device *previewer)
{
	struct vpfe_video_device *video_out = &previewer->video_out;
	struct vpfe_device *vpfe_dev = to_vpfe_device(previewer);
	enum v4l2_field field;

	if (!video_out->started)
		return;

	if (previewer->input != PREVIEWER_INPUT_CCDC)
		return;

	field = video_out->fmt.fmt.pix.field;

	if (field == V4L2_FIELD_NONE) {
		/* handle progressive frame capture */
		if (video_out->cur_frm != video_out->next_frm)
			vpfe_process_buffer_complete(video_out);
		/* TODO: I need to switch resizer on? and skip frames */
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

void prv_dma_isr(struct vpfe_previewer_device *previewer)
{
	int fid, schedule_capture = 0;
	enum v4l2_field field;
	struct vpfe_video_device *video_out = &previewer->video_out;
	struct vpfe_device *vpfe_dev = to_vpfe_device(previewer);

	if (previewer->input == PREVIEWER_INPUT_MEMORY) {
		/* single shot..TODO handle well*/
		prv_ss_buffer_isr(previewer);
		return;
	}

	if (!video_out->started)
		return;

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
	return;
}

/*
 * previewer_ioctl - PREVIEWER module private ioctl's
 * @sd: VPFE PREVIEWER V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long previewer_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct vpfe_previewer_device *previewer = v4l2_get_subdevdata(sd);
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;
	struct imp_logical_channel *chan = &previewer->channel;
	struct device *dev = previewer->subdev.v4l2_dev->dev;
	int ret = 0;

	if (ISNULL((void *)arg))
		return -EFAULT;

	switch (cmd) {
	case PREV_ENUM_CAP:{

		struct prev_cap *cap = (struct prev_cap *)arg;
		struct prev_module_if *module_if = NULL;

		module_if = imp_hw_if->prev_enum_modules(dev, cap->index);

		if (ISNULL(module_if)) {
				dev_dbg(dev, "PREV_ENUM_CAP - Last module\n");
				return -EINVAL;
			} else {
				strcpy(cap->version, module_if->version);
				cap->module_id = module_if->module_id;
				cap->control = module_if->control;
				cap->path = module_if->path;
				strcpy(cap->module_name,
				       module_if->module_name);
			}
	}
	break;

	case PREV_S_PARAM:
	{
		struct prev_module_param *module_param =
			    (struct prev_module_param *)arg;
		struct prev_module_if *module_if;

		if (chan->config_state != STATE_CONFIGURED) {
				dev_err(dev, "Channel not configured\n");
				return -EINVAL;

		}

		module_if =
			    imp_get_module_interface(dev,
						     module_param->module_id);
			if (ISNULL(module_if)) {
				dev_err(dev, "Invalid module id\n");
				return -EINVAL;
			} else {
				if (strcmp
				    (module_if->version,
				     module_param->version)) {
					dev_err(dev,
						"Invalid module version\n");
					return -EINVAL;
				}
				/* we have a valid */
				ret = module_if->set(dev,
						     module_param->
						     param, module_param->len);
				if (ret < 0) {
					dev_err(dev,
						"error in PREV_S_PARAM\n");
					return -EINVAL;
				}
			}
	}
	break;

	case PREV_G_PARAM:
		{
			struct prev_module_param *module_param =
			    (struct prev_module_param *)arg;
			struct prev_module_if *module_if;

			dev_dbg(dev, "PREV_G_PARAM:\n");

			if (ISNULL(module_param)) {
				ret = -EINVAL;
				goto ERROR;
			}
			module_if =
			    imp_get_module_interface(dev,
						     module_param->module_id);
			if (ISNULL(module_if)) {
				dev_err(dev, "Invalid module id\n");
				ret = -EINVAL;
				goto ERROR;
			} else {
				if (strcmp
				    (module_if->version,
				     module_param->version)) {
					dev_err(dev,
						"Invalid module version\n");
					ret = -EINVAL;
					goto ERROR;
				}

				ret = module_if->get(dev,
						     module_param->param,
						     module_param->len);
				if (ret < 0) {
					dev_err(dev,
						"error in PREV_G_PARAM\n");
					goto ERROR;
				}
			}
		}
		break;

	case PREV_S_CONFIG:
		{
			dev_dbg(dev, "PREV_S_CONFIG:\n");
			ret =
			    imp_set_preview_config(dev, chan,
						   (struct prev_channel_config
						    *)arg);
		}
		break;

	case PREV_G_CONFIG:
		{
			struct prev_channel_config *user_config =
			    (struct prev_channel_config *)arg;

			dev_dbg(dev, "PREV_G_CONFIG:\n");
			if (ISNULL(user_config->config)) {
				ret = -EINVAL;
				dev_err(dev, "error in PREV_GET_CONFIG\n");
				goto ERROR;
			}

			ret =
			    imp_get_preview_config(dev, chan, user_config);
		}
		break;


	default:
		ret = -ENOIOCTLCMD;
	}

ERROR:
	return ret;
}

/*
 * previewer_set_stream - Enable/Disable streaming on the PREVIEWER module
 * @sd: VPFE PREVIEWER V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int previewer_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_previewer_device *previewer = v4l2_get_subdevdata(sd);
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;
	struct device *dev = previewer->subdev.v4l2_dev->dev;
	struct imp_logical_channel *chan = &previewer->channel;
	/* in case of single shot, send config to ipipe */
	void *config = (previewer->input == PREVIEWER_INPUT_MEMORY) ?
				chan->config : NULL;

	if (previewer->output != PREVIEWER_OUTPUT_MEMORY)
		return 0;

	switch (enable) {
	case 1:
		if ((previewer->input == PREVIEWER_INPUT_MEMORY) &&
		(imp_hw_if->serialize())) {
			if (imp_hw_if->hw_setup(dev, config) < 0)
				return -1;
		} else if (previewer->input == PREVIEWER_INPUT_CCDC) {
			imp_hw_if->lock_chain();
			if (imp_hw_if->hw_setup(dev, NULL))
				return -1;
		}
		imp_hw_if->enable(1, config);
		break;
	case 0:
		imp_hw_if->enable(0, config);
		if (previewer->input == PREVIEWER_INPUT_CCDC)
			imp_hw_if->unlock_chain();
		break;
	}

	return 0;
}

static struct v4l2_mbus_framefmt *
__previewer_get_format(struct vpfe_previewer_device *prev,
		       struct v4l2_subdev_fh *fh,
		       unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &prev->formats[pad];
}

/*
 * preview_try_format - Handle try format by pad subdev method
 * @prev: VPFE preview device
 * @fh : V4L2 subdev file handle
 * @pad: pad num
 * @fmt: pointer to v4l2 format structure
 */
static void preview_try_format(struct vpfe_previewer_device *prev,
			       struct v4l2_subdev_fh *fh, unsigned int pad,
			       struct v4l2_mbus_framefmt *fmt,
			       enum v4l2_subdev_format_whence which)
{
	unsigned int max_out_width, max_out_height;
	struct imp_hw_interface *imp_hw_if;
	unsigned int i;

	imp_hw_if = prev->imp_hw_if;
	max_out_width = imp_hw_if->get_max_output_width(0);
	max_out_height = imp_hw_if->get_max_output_height(0);

	if (pad == PREVIEWER_PAD_SINK ||
		pad == PREVIEWER_PAD_SOURCE) {
		for (i = 0; i < ARRAY_SIZE(prev_input_fmts); i++) {
			if (fmt->code == prev_input_fmts[i])
				break;
		}

		/* If not found, use SBGGR10 as default */
		if (i >= ARRAY_SIZE(prev_input_fmts))
			fmt->code = V4L2_MBUS_FMT_SBGGR10_1X10;

		fmt->width = clamp_t(u32, fmt->width, MIN_OUT_HEIGHT,
				     max_out_width);
		fmt->height = clamp_t(u32, fmt->height, MIN_OUT_WIDTH,
				      max_out_height);
	}
}

static int previewer_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct vpfe_previewer_device *previewer = v4l2_get_subdevdata(sd);
	struct imp_hw_interface *imp_hw_if = previewer->imp_hw_if;
	struct v4l2_mbus_framefmt *format;
	enum imp_pix_formats imp_pix;
	struct imp_window imp_win;
	int ret = 0;

	format = __previewer_get_format(previewer, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	preview_try_format(previewer, fh, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if ((fmt->pad == PREVIEWER_PAD_SINK) &&
		((previewer->input == PREVIEWER_INPUT_CCDC) ||
		(previewer->input == PREVIEWER_INPUT_MEMORY))) {
		/*
		 *In continous mode,set IPEPE input format here.
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

		previewer->formats[fmt->pad] = fmt->format;

	} else if ((fmt->pad == PREVIEWER_PAD_SOURCE) &&
		(previewer->output == PREVIEWER_OUTPUT_RESIZER)) {

		previewer->formats[fmt->pad] = fmt->format;
	} else if ((fmt->pad == PREVIEWER_PAD_SOURCE) &&
		(previewer->output == PREVIEWER_OUTPUT_MEMORY)) {
		/*
		 *FIXME:format verification code to come here.
		 */

		if (fmt->format.code == V4L2_MBUS_FMT_SBGGR10_1X10)
			imp_pix = IMP_BAYER;
		else if ((fmt->format.code == V4L2_MBUS_FMT_YUYV8_2X8) ||
			(fmt->format.code == V4L2_MBUS_FMT_YUYV10_1X20))
			imp_pix = IMP_UYVY;
		else if (fmt->format.code == V4L2_MBUS_FMT_UYVY8_2X8)
			/* TODO:fix */
			imp_pix = IMP_YUV420SP;
		else
			return -EINVAL;

		if (imp_hw_if->set_out_pixel_format(imp_pix) < 0)
			return -EINVAL;

		imp_win.width = fmt->format.width;
		imp_win.height = fmt->format.height;
		imp_win.hst = 0;
		imp_win.vst = 0;
		if (imp_hw_if->set_output_win(&imp_win) < 0)
			return -EINVAL;

		previewer->formats[fmt->pad] = fmt->format;
	} else
		return -EINVAL;

	return 0;
}

/*
 * previewer_get_format - Retrieve the video format on a pad
 * @sd : VPFE PREVIEWER V4L2 subdevice
 * @fh : V4L2 subdev file handle
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int previewer_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct vpfe_previewer_device *previewer = v4l2_get_subdevdata(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		fmt->format = previewer->formats[fmt->pad];
	else
		fmt->format = *(v4l2_subdev_get_try_format(fh, fmt->pad));

	return 0;
}

static int previewer_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct vpfe_previewer_device *prev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	preview_try_format(prev, fh, fse->pad, &format,
			   V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	preview_try_format(prev, fh, fse->pad, &format,
			   V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

static int previewer_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->pad) {
	case PREVIEWER_PAD_SINK:
		if (code->index >= ARRAY_SIZE(prev_input_fmts))
			return -EINVAL;

		code->code = prev_input_fmts[code->index];
		break;
	case PREVIEWER_PAD_SOURCE:
		if (code->index >= ARRAY_SIZE(prev_output_fmts))
			return -EINVAL;

		code->code = prev_output_fmts[code->index];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * previewer_init_formats - Initialize formats on all pads
 * @sd: VPFE resizer V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int previewer_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct vpfe_previewer_device *prv = v4l2_get_subdevdata(sd);
	struct imp_hw_interface *imp_hw_if = prv->imp_hw_if;

	memset(&format, 0, sizeof(format));
	format.pad = PREVIEWER_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_SBGGR10_1X10;
	format.format.width = imp_hw_if->get_max_output_width(0);
	format.format.height = imp_hw_if->get_max_output_height(0);
	previewer_set_format(sd, fh, &format);

	memset(&format, 0, sizeof(format));
	format.pad = PREVIEWER_PAD_SOURCE;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_YUYV8_2X8;
	format.format.width = imp_hw_if->get_max_output_width(0);
	format.format.height = imp_hw_if->get_max_output_height(0);
	previewer_set_format(sd, fh, &format);

	return 0;
}

static const struct v4l2_subdev_core_ops previewer_v4l2_core_ops = {
	.ioctl = previewer_ioctl,
};

/* subdev file operations */
static const struct v4l2_subdev_file_ops previewer_v4l2_file_ops = {
	.open = previewer_init_formats,
};

static const struct v4l2_subdev_video_ops previewer_v4l2_video_ops = {
	.s_stream = previewer_set_stream,
};

static const struct v4l2_subdev_pad_ops previewer_v4l2_pad_ops = {
	.enum_mbus_code = previewer_enum_mbus_code,
	.enum_frame_size = previewer_enum_frame_size,
	.get_fmt = previewer_get_format,
	.set_fmt = previewer_set_format,
};

static const struct v4l2_subdev_ops previewer_v4l2_ops = {
	.core = &previewer_v4l2_core_ops,
	.file = &previewer_v4l2_file_ops,
	.video = &previewer_v4l2_video_ops,
	.pad = &previewer_v4l2_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * previewer_link_setup - Setup PREVIEWER connections
 * @entity: PREVIEWER media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int previewer_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{

	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_previewer_device *previewer = v4l2_get_subdevdata(sd);

	switch (local->index | media_entity_type(remote->entity)) {
	case PREVIEWER_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* Read from ccdc - continous mode */
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			previewer->input = PREVIEWER_INPUT_NONE;

			reset_channel_prv_mode(previewer);
			break;
		}

		if (previewer->input != PREVIEWER_INPUT_NONE)
			return -EBUSY;

		if (set_channel_prv_cont_mode(previewer))
			return -EINVAL;

		previewer->input = PREVIEWER_INPUT_CCDC;
		break;
	case PREVIEWER_PAD_SINK | MEDIA_ENT_T_DEVNODE:
		/* Read from memory - single shot mode*/
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			previewer->input = PREVIEWER_INPUT_NONE;
			reset_channel_prv_mode(previewer);
			break;
		}
		if (set_channel_prv_ss_mode(previewer))
			return -EINVAL;

		previewer->input = PREVIEWER_INPUT_MEMORY;
		break;
	case PREVIEWER_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
		/* out to RESIZER */
		if (flags & MEDIA_LNK_FL_ENABLED)
			previewer->output = PREVIEWER_OUTPUT_RESIZER;
		else
			previewer->output = PREVIEWER_OUTPUT_NONE;

		break;
	case PREVIEWER_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		/* Write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED)
			previewer->output = PREVIEWER_OUTPUT_MEMORY;
		else
			previewer->output = PREVIEWER_OUTPUT_NONE;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations previewer_media_ops = {
	.link_setup = previewer_link_setup,
};

void vpfe_previewer_unregister_entities(struct vpfe_previewer_device *vpfe_prev)
{
	/* unregister video devices */
	vpfe_video_unregister(&vpfe_prev->video_in);
	vpfe_video_unregister(&vpfe_prev->video_out);

	/* cleanup entity */
	media_entity_cleanup(&vpfe_prev->subdev.entity);
	/* unregister subdev */
	v4l2_device_unregister_subdev(&vpfe_prev->subdev);
}

int vpfe_previewer_register_entities(struct vpfe_previewer_device *previewer,
				     struct v4l2_device *vdev)
{
	int ret;
	unsigned int flags = 0;
	struct vpfe_device *vpfe_dev = to_vpfe_device(previewer);

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &previewer->subdev);
	if (ret) {
		printk(KERN_ERR "failed to register previewer as v4l2 subdevice\n");
		return ret;
	}

	ret = vpfe_video_register(&previewer->video_in, vdev);
	if (ret) {
		printk(KERN_ERR "failed to register previewer video-in device\n");
		goto out_video_in_register;
	}

	previewer->video_in.vpfe_dev = vpfe_dev;

	ret = vpfe_video_register(&previewer->video_out, vdev);
	if (ret) {
		printk(KERN_ERR "failed to register previewer video-out device\n");
		goto out_video_out_register;
	}

	previewer->video_out.vpfe_dev = vpfe_dev;

	ret = media_entity_create_link(&previewer->video_in.video_dev.entity,
				       0,
				       &previewer->subdev.entity,
				       0, flags);
	if (ret < 0)
		goto out_create_link;

	ret = media_entity_create_link(&previewer->subdev.entity,
				       1,
				       &previewer->video_out.video_dev.entity,
				       0, flags);
	if (ret < 0)
		goto out_create_link;

	return 0;

out_create_link:
	vpfe_video_unregister(&previewer->video_out);
out_video_out_register:
	vpfe_video_unregister(&previewer->video_in);
out_video_in_register:
	media_entity_cleanup(&previewer->subdev.entity);
	v4l2_device_unregister_subdev(&previewer->subdev);
	return ret;
}

int vpfe_previewer_init(struct vpfe_previewer_device *vpfe_prev,
			struct platform_device *pdev)
{
	struct v4l2_subdev *previewer = &vpfe_prev->subdev;
	struct media_pad *pads = &vpfe_prev->pads[0];
	struct media_entity *me = &previewer->entity;
	struct imp_logical_channel *channel = &vpfe_prev->channel;
	int ret;

	if (cpu_is_davinci_dm365() || cpu_is_davinci_dm355()) {
		vpfe_prev->imp_hw_if = imp_get_hw_if();

		if (ISNULL(vpfe_prev->imp_hw_if))
			return -1;
	} else
		return -1;

	vpfe_prev->video_in.ops = &video_in_ops;
	vpfe_prev->video_out.ops = &video_out_ops;

	v4l2_subdev_init(previewer, &previewer_v4l2_ops);
	strlcpy(previewer->name, "DAVINCI PREVIEWER", sizeof(previewer->name));
	previewer->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(previewer, vpfe_prev);
	previewer->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[PREVIEWER_PAD_SINK].flags = MEDIA_PAD_FL_INPUT;
	pads[PREVIEWER_PAD_SOURCE].flags = MEDIA_PAD_FL_OUTPUT;

	vpfe_prev->input = PREVIEWER_INPUT_NONE;
	vpfe_prev->output = PREVIEWER_OUTPUT_NONE;

	channel->type = IMP_PREVIEWER;
	channel->config_state = STATE_NOT_CONFIGURED;

	me->ops = &previewer_media_ops;

	ret = media_entity_init(me, PREVIEWER_PADS_NUM, pads, 0);
	if (ret)
		return ret;

	vpfe_prev->video_in.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = vpfe_video_init(&vpfe_prev->video_in, "PRV");
	if (ret) {
		printk(KERN_ERR "failed to init PRV video-in device\n");
		return ret;
	}

	vpfe_prev->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = vpfe_video_init(&vpfe_prev->video_out, "PRV");
	if (ret) {
		printk(KERN_ERR "failed to init PRV video-out device\n");
		return ret;
	}

	imp_init_serializer(); /* TODO we need it?*/

	return 0;
}

/*
 * vpfe_previewer_cleanup - PREVIEWER module cleanup.
 * @dev: Device pointer specific to the VPFE.
 */
void vpfe_previewer_cleanup(struct platform_device *pdev)
{
	/* do nothing */
}
