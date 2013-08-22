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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-mediabus.h>
#include <media/media-entity.h>
#include <media/media-device.h>
#include <media/davinci/videohd.h>
#include <media/davinci/vpfe_capture.h>
#include <mach/cputype.h>
#include "ccdc_hw_device.h"

static int debug;

/* lock for accessing ccdc information */
static DEFINE_MUTEX(ccdc_lock);

static struct v4l2_subdev *vpfe_get_input_sd(struct vpfe_video_device *video)
{
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct media_pad *remote;

	remote = media_entity_remote_source(&vpfe_dev->vpfe_ccdc.pads[0]);
	if (remote == NULL) {
		printk(KERN_ERR "invalid media connection to ccdc\n");
		return NULL;
	}

	return media_entity_to_v4l2_subdev(remote->entity);
}

/* updates external subdev(sensor/decoder) which is active */
static int vpfe_update_current_ext_subdev(struct vpfe_video_device *video)
{
	struct media_pad *remote;
	struct v4l2_subdev *subdev;
	struct vpfe_config *vpfe_cfg;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	int i;

	remote = media_entity_remote_source(&vpfe_dev->vpfe_ccdc.pads[0]);
	if (remote == NULL) {
		printk(KERN_ERR "invalid media connection to video node\n");
		return -1;
	}

	subdev = media_entity_to_v4l2_subdev(remote->entity);

	vpfe_cfg = vpfe_dev->pdev->platform_data;

	for (i = 0; i < vpfe_cfg->num_subdevs; i++) {
		if (!strcmp(vpfe_cfg->sub_devs[i].module_name, subdev->name)) {
			video->current_subdev = &vpfe_cfg->sub_devs[i];
			break;
		}
	}

	/* if user faied to link decoder/sensor to ccdc */
	if (i == vpfe_cfg->num_subdevs) {
		printk(KERN_ERR "media chain input should be sensor or decoder\n");
		return -1;
	}

	/* find the v4l2 subdev pointer */
	for (i = 0; i < vpfe_dev->num_subdevs; i++) {
		if (!strcmp(video->current_subdev->module_name,
			vpfe_dev->sd[i]->name))
			video->current_subdev->subdev = vpfe_dev->sd[i];
	}

	return 0;
}

static struct v4l2_subdev *
vpfe_video_remote_subdev(struct vpfe_video_device *video, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_source(&video->pad);

	if (remote == NULL ||
		remote->entity->type != MEDIA_ENT_T_V4L2_SUBDEV)
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int
__vpfe_video_get_format(struct vpfe_video_device *video,
			struct v4l2_format *format)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	struct media_pad *remote;
	u32 pad;
	int ret;

	subdev = vpfe_video_remote_subdev(video, &pad);
	if (subdev == NULL)
		return -EINVAL;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	remote = media_entity_remote_source(&video->pad);
	fmt.pad = remote->index;

	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret == -ENOIOCTLCMD)
		return -EINVAL;

	format->type = video->type;
	/* convert mbus_format to v4l2_format */
	v4l2_fill_pix_format(&format->fmt.pix, &fmt.format);
	mbus_to_pix(&fmt.format, &format->fmt.pix);
	return 0;
}

/* Return a pointer to the VPFE video instance at the far end of the pipeline.*/
static struct vpfe_video_device *
vpfe_video_far_end(struct vpfe_video_device *video)
{
	struct media_entity_graph graph;
	struct media_entity *entity = &video->video_dev.entity;
	struct media_device *mdev = entity->parent;
	struct vpfe_video_device *far_end = NULL;

	mutex_lock(&mdev->graph_mutex);
	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		if (entity == &video->video_dev.entity)
			continue;

		if (media_entity_type(entity) != MEDIA_ENT_T_DEVNODE)
			continue;

		far_end = to_vpfe_video(media_entity_to_video_device(entity));
		if (far_end->type != video->type)
			break;

		far_end = NULL;
	}

	mutex_unlock(&mdev->graph_mutex);
	return far_end;
}

static int vpfe_update_pipe_state(struct vpfe_video_device *video)
{
	int ret = 0;
	struct vpfe_pipeline *pipe = &video->pipe;
	struct vpfe_video_device *far_end = NULL;

	far_end = vpfe_video_far_end(video);
	if (far_end == NULL) {
		/* possible that it is continous mode */
		ret = vpfe_update_current_ext_subdev(video);
		if (ret) {
			printk(KERN_ERR "invalid external subdev\n");
			return ret;
		}
		pipe->state = VPFE_PIPELINE_STREAM_CONTINUOUS;
		pipe->output = video;
		pipe->input_sd = vpfe_get_input_sd(video);
	} else if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		pipe->state = VPFE_PIPELINE_STREAM_SINGLESHOT;
		pipe->input = far_end;
		pipe->output = video;
	} else {
		pipe->state = VPFE_PIPELINE_STREAM_SINGLESHOT;
		pipe->output = far_end;
		pipe->input = video;
	}

	video->initialized = 1;
	video->skip_frame_count = 1;
	video->skip_frame_count_init = 1;

	return ret;
}

static const int
	vpfe_check_format(struct vpfe_video_device *video,
			  struct v4l2_pix_format *pixfmt)

{
	struct v4l2_format format;
	int result = 0;

	video->fmt.fmt.pix = *pixfmt;

	/* get adjacent subdev's output pad format */
	result = __vpfe_video_get_format(video, &format);
	if (result)
		return result;

	/* copy subdev's output format onto video format */
	memcpy(pixfmt, &format.fmt, sizeof(format.fmt));

	return 0;
}

/*
* Validate a pipeline by checking both ends of all links for format
 * discrepancies.
 *
 * Return 0 if all formats match, or -EPIPE if at least one link is found with
 * different formats on its two ends.
 */

static int vpfe_video_validate_pipeline(struct vpfe_pipeline *pipe)
{
	struct v4l2_subdev_format fmt_source;
	struct v4l2_subdev_format fmt_sink;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int ret;

	subdev = vpfe_video_remote_subdev(pipe->output, NULL);
	if (subdev == NULL)
		return -EPIPE;

	while (1) {

		/* Retrieve the sink format */
		pad = &subdev->entity.pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_INPUT))
			break;

		fmt_sink.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt_sink.pad = pad->index;
		ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL,
				       &fmt_sink);

		if (ret < 0 && ret != -ENOIOCTLCMD)
			return -EPIPE;

		/* Retrieve the source format */
		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
			pad->entity->type != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		subdev = media_entity_to_v4l2_subdev(pad->entity);

		fmt_source.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt_source.pad = pad->index;
		ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt_source);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return -EPIPE;

		/* Check if the two ends match */
		if (fmt_source.format.code != fmt_sink.format.code ||
		    fmt_source.format.width != fmt_sink.format.width ||
		    fmt_source.format.height != fmt_sink.format.height)
			return -EPIPE;
	}

	return 0;
}

static int is_pipe_ready(struct vpfe_pipeline *pipe)
{
	if (pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT) {
		if (!pipe->input->started ||
			!(pipe->input->state & VPFE_VIDEO_BUFFER_QUEUED) ||
			!pipe->output->started ||
			!(pipe->output->state & VPFE_VIDEO_BUFFER_QUEUED))
			return 0;
	} else
		return 1;

	return 1;
}

/*
 * vpfe_pipeline_enable - Enable streaming on a pipeline
 * @vpfe_dev: vpfe device
 * @pipe: vpfe pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and start
 * all modules in the chain in the given mode.
 *
 * Return 0 if successfull, or the return value of the failed video::s_stream
 * operation otherwise.
 */

static int vpfe_pipeline_enable(struct vpfe_video_device *video,
				struct vpfe_pipeline *pipe)
{
	struct media_entity_graph graph;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_device *mdev;
	int ret = 0;

	if (pipe->state == VPFE_PIPELINE_STREAM_CONTINUOUS)
		entity = &pipe->input_sd->entity;
	else
		entity = &pipe->input->video_dev.entity;

	mdev = entity->parent;

	mutex_lock(&mdev->graph_mutex);
	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {

		if (media_entity_type(entity) == MEDIA_ENT_T_DEVNODE)
			continue;

		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			break;

	}

	mutex_unlock(&mdev->graph_mutex);

	return ret;
}

/*
 * vpfe_pipeline_disable - Disable streaming on a pipeline
 * @vpfe_dev: vpfe device
 * @pipe: VPFE pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and stop
 * all modules in the chain. Wait synchronously for the modules to be stopped if
 * necessary.
 *
 * Return 0 if all modules have been properly stopped, or -ETIMEDOUT if a module
 * can't be stopped.
 */
static int vpfe_pipeline_disable(struct vpfe_video_device *video,
				 struct vpfe_pipeline *pipe)
{
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_entity_graph graph;
	struct media_device *mdev;
	int ret = 0;

	if (pipe->state == VPFE_PIPELINE_STREAM_CONTINUOUS)
		entity = &pipe->input_sd->entity;
	else
		entity = &pipe->input->video_dev.entity;

	mdev = entity->parent;

	mutex_lock(&mdev->graph_mutex);
	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {

		if (media_entity_type(entity) == MEDIA_ENT_T_DEVNODE)
			continue;

		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			break;

	}

	mutex_unlock(&mdev->graph_mutex);

	return (ret == 0) ? ret : -ETIMEDOUT ;
}
/*
 * vpfe_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @vpfe_dev: VPFE device
 * @pipe: VPFE pipeline
 * @state: Stream state (stopped or continuous)
 *
 * Set the pipeline to the given stream state. Pipelines can be started in
 * continuous mode only.
 *
 * Return 0 if successfull, or the return value of the failed video::s_stream
 * operation otherwise.
 */
int vpfe_pipeline_set_stream(struct vpfe_video_device *video,
			     struct vpfe_pipeline *pipe,
			    enum vpfe_pipeline_stream_state state)
{
	if (state == VPFE_PIPELINE_STREAM_STOPPED)
		return vpfe_pipeline_disable(video, pipe);
	else
		return vpfe_pipeline_enable(video, pipe);
}

/*
 * vpfe_open : It creates object of file handle structure and
 * stores it in private_data  member of filepointer
 */
static int vpfe_open(struct file *file)
{
	struct vpfe_video_device *video = video_drvdata(file);

	struct vpfe_fh *fh;


	/* Allocate memory for the file handle object */
	fh = kzalloc(sizeof(struct vpfe_fh), GFP_KERNEL);

	if (NULL == fh)
		return -ENOMEM;
	/* store pointer to fh in private_data member of file */
	file->private_data = fh;
	fh->video = video;
	mutex_lock(&video->lock);
	/* If decoder is not initialized. initialize it */
	if (!video->initialized) {
		if (vpfe_update_pipe_state(video)) {
			mutex_unlock(&video->lock);
			return -ENODEV;
		}
	}
	/* Increment device usrs counter */
	video->usrs++;
	/* Set io_allowed member to false */
	fh->io_allowed = 0;
	/* Initialize priority of this instance to default priority */
	fh->prio = V4L2_PRIORITY_UNSET;
	v4l2_prio_open(&video->prio, &fh->prio);
	mutex_unlock(&video->lock);
	return 0;
}

unsigned long vpfe_get_next_buffer(struct vpfe_video_device *video)
{
	if (list_empty(&video->dma_queue))
		return -1;

	/* mark next buffer as active */
	video->next_frm = list_entry(video->dma_queue.next,
					struct videobuf_buffer, queue);

	/* in single shot mode both curr_frm
	   and next_frm point to same buffer */
	video->cur_frm = video->next_frm;
	list_del(&video->next_frm->queue);
	video->next_frm->state = VIDEOBUF_ACTIVE;
	return videobuf_to_dma_contig(video->next_frm);
}

void vpfe_schedule_next_buffer(struct vpfe_video_device *video)
{
	unsigned long addr;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	video->next_frm = list_entry(video->dma_queue.next,
					struct videobuf_buffer, queue);
	list_del(&video->next_frm->queue);
	video->next_frm->state = VIDEOBUF_ACTIVE;
	addr = videobuf_to_dma_contig(video->next_frm);

	video->ops->queue(vpfe_dev, addr);
}

void vpfe_schedule_bottom_field(struct vpfe_video_device *video)
{
	unsigned long addr;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	addr = videobuf_to_dma_contig(video->cur_frm);
	addr += video->field_off;

	video->ops->queue(vpfe_dev, addr);
}

void vpfe_process_buffer_complete(struct vpfe_video_device *video)
{
	struct timeval timevalue;
	struct vpfe_pipeline *pipe = &video->pipe;

	do_gettimeofday(&timevalue);

	video->cur_frm->ts = timevalue;
	video->cur_frm->state = VIDEOBUF_DONE;
	video->cur_frm->size = video->fmt.fmt.pix.sizeimage;
	wake_up_interruptible(&video->cur_frm->done);
	/* for continous mode, proceed with next buffer */
	if (pipe->state == VPFE_PIPELINE_STREAM_CONTINUOUS)
		video->cur_frm = video->next_frm;
}

/* vpfe_stop_capture: stop streaming in ccdc/isif */
static void vpfe_stop_capture(struct vpfe_video_device *video)
{
	struct vpfe_pipeline *pipe = &video->pipe;

	video->started = 0;

	if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return;

	vpfe_pipeline_set_stream(video, pipe, VPFE_PIPELINE_STREAM_STOPPED);
}

/*
 * vpfe_release : This function deletes buffer queue, frees the
 * buffers and the vpfe file  handle
 */
static int vpfe_release(struct file *file)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct ccdc_hw_device *ccdc_dev = vpfe_dev->vpfe_ccdc.ccdc_dev;

	struct vpfe_fh *fh = file->private_data;
	struct vpfe_subdev_info *sdinfo;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_release\n");

	/* Get the device lock */
	mutex_lock(&video->lock);
	/* if this instance is doing IO */
	if (fh->io_allowed) {
		if (video->started) {
			sdinfo = video->current_subdev;

			vpfe_stop_capture(video);

			videobuf_streamoff(&video->buffer_queue);
		}
		video->io_usrs = 0;
	}

	/* Decrement device usrs counter */
	video->usrs--;
	/* Close the priority */
	v4l2_prio_close(&video->prio, fh->prio);

	/* If this is the last file handle */
	if (!video->usrs) {
		video->initialized = 0;
		module_put(ccdc_dev->owner);
	}
	mutex_unlock(&video->lock);
	file->private_data = NULL;
	/* Free memory allocated to file handle object */
	kzfree(fh);
	return 0;
}

/*
 * vpfe_mmap : It is used to map kernel space buffers
 * into user spaces
 */
static int vpfe_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_mmap\n");

	return videobuf_mmap_mapper(&video->buffer_queue, vma);
}

/*
 * vpfe_poll: It is used for select/poll system call
 */
static unsigned int vpfe_poll(struct file *file, poll_table *wait)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_poll\n");

	if (video->started)
		return videobuf_poll_stream(file,
					    &video->buffer_queue, wait);
	return 0;
}

/* vpfe capture driver file operations */
static const struct v4l2_file_operations vpfe_fops = {
	.owner = THIS_MODULE,
	.open = vpfe_open,
	.release = vpfe_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vpfe_mmap,
	.poll = vpfe_poll
};

static int vpfe_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_querycap\n");

	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	else
		cap->capabilities = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;

	cap->version = VPFE_CAPTURE_VERSION_CODE;
	strlcpy(cap->driver, CAPTURE_DRV_NAME, sizeof(cap->driver));
	strlcpy(cap->bus_info, "VPFE", sizeof(cap->bus_info));
	strlcpy(cap->card, vpfe_dev->cfg->card_name, sizeof(cap->card));
	return 0;
}

static int vpfe_g_fmt(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_fmt_vid_cap\n");
	/* Fill in the information about format */
	*fmt = video->fmt;

	return ret;
}

static int vpfe_enum_fmt(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *fmt)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	int ret;
	struct v4l2_subdev *subdev;
	struct media_pad *remote;
	struct v4l2_subdev_format sd_fmt;
	struct v4l2_mbus_framefmt mbus;
	struct v4l2_format format;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_enum_fmt_vid_cap\n");

	/* since already subdev pad format is set,
	only one pixel format is available */
	if (fmt->index > 0) {
		printk(KERN_ERR "invalid index\n");
		return -EINVAL;
	}

	/* get the remote pad */
	remote = media_entity_remote_source(&video->pad);
	if (remote == NULL) {
		printk(KERN_ERR "invalid remote pad for video node\n");
		return -EINVAL;
	}

	/* get the remote subdev */
	subdev = vpfe_video_remote_subdev(video, NULL);
	if (subdev == NULL) {
		printk(KERN_ERR "invalid remote subdev for video node\n");
		return -EINVAL;
	}

	sd_fmt.pad = remote->index;
	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	/* get output format of remote subdev */
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL,
				       &sd_fmt);
	if (ret) {
		printk(KERN_ERR "failed to get output format from subdev\n");
		return ret;
	}
	/* convert to pix format */
	mbus.code = sd_fmt.format.code;
	mbus_to_pix(&mbus, &format.fmt.pix);

	/* copy the result */
	fmt->pixelformat = format.fmt.pix.pixelformat;

	return 0;
}

static int vpfe_s_fmt(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_fmt_vid_cap\n");

	/* If streaming is started, return error */
	if (video->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Streaming is started\n");
		return -EBUSY;
	}

	/* Check for valid frame format */
	ret = vpfe_check_format(video, &fmt->fmt.pix);
	if (ret)
		return -EINVAL;

	video->fmt = *fmt;

	return 0;
}

static int vpfe_try_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_try_fmt_vid_cap\n");

	ret = vpfe_check_format(video, &f->fmt.pix);
	if (ret)
		return -EINVAL;
	return 0;
}

static int vpfe_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_subdev_info *sdinfo = video->current_subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_enum_input\n");

	/* enumerate from the subdev user has choosen through mc */
	if (inp->index < sdinfo->num_inputs) {
		memcpy(inp, &sdinfo->inputs[inp->index],
		       sizeof(struct v4l2_input));
		return 0;
	}

	return -EINVAL;
}

static int vpfe_g_input(struct file *file, void *priv, unsigned int *index)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_input\n");

	*index = video->current_input;

	return 0;
}

static int vpfe_s_input(struct file *file, void *priv, unsigned int index)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct ccdc_hw_device *ccdc_dev = vpfe_dev->vpfe_ccdc.ccdc_dev;
	struct imp_hw_interface *imp_hw_if = vpfe_dev->vpfe_previewer.imp_hw_if;
	int ret, i;
	struct vpfe_subdev_info *sdinfo;
	struct vpfe_route *route;
	u32 input = 0, output = 0;
	struct v4l2_input *inps;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_input\n");

	ret = mutex_lock_interruptible(&video->lock);
	if (ret)
		return ret;

	/*
	 * If streaming is started return device busy
	 * error
	 */
	if (video->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Streaming is on\n");
		ret = -EBUSY;
		goto unlock_out;
	}

	sdinfo = video->current_subdev;

	if (!sdinfo->registered) {
		ret = -EINVAL;
		goto unlock_out;
	}

	if (vpfe_dev->cfg->setup_input) {
		if (vpfe_dev->cfg->setup_input(sdinfo->grp_id) < 0) {
			ret = -EFAULT;
			v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
				 "couldn't setup input for %s\n",
				 sdinfo->module_name);
			goto unlock_out;
		}
	}

	route = &sdinfo->routes[index];
	if (route && sdinfo->can_route) {
		input = route->input;
		output = route->output;
		ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev,
						 sdinfo->grp_id, video,
						 s_routing, input, output, 0);

		if (ret) {
			v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
				"s_input:error in setting input in decoder\n");
			ret = -EINVAL;
			goto unlock_out;
		}
	}

	/* set standards set by subdev in video device */
	for (i = 0; i < sdinfo->num_inputs; i++) {
		inps = &sdinfo->inputs[i];
		video->video_dev.tvnorms |= inps->std;
	}

	/* set the bus/interface parameter for the sub device in ccdc */
	ret = ccdc_dev->hw_ops.set_hw_if_params(&sdinfo->ccdc_if_params);
	if (ret)
		goto unlock_out;

	/* update the if parameters to imp hw interface */
	if (imp_hw_if && imp_hw_if->set_hw_if_param)
		ret = imp_hw_if->set_hw_if_param(&sdinfo->ccdc_if_params);
	if (ret)
		goto unlock_out;

	video->current_input = index;
unlock_out:
	mutex_unlock(&video->lock);
	return ret;
}

static int vpfe_querystd(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_subdev_info *sdinfo;
	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_querystd\n");

	ret = mutex_lock_interruptible(&video->lock);
	sdinfo = video->current_subdev;
	if (ret)
		return ret;
	/* Call querystd function of decoder device */
	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sdinfo->grp_id,
					 video, querystd, std_id);
	mutex_unlock(&video->lock);
	return ret;
}

static int vpfe_s_std(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_subdev_info *sdinfo;
	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_std\n");

	/* Call decoder driver function to set the standard */
	ret = mutex_lock_interruptible(&video->lock);
	if (ret)
		return ret;

	sdinfo = video->current_subdev;
	/* If streaming is started, return device busy error */
	if (video->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "streaming is started\n");
		ret = -EBUSY;
		goto unlock_out;
	}

	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sdinfo->grp_id,
					 core, s_std, *std_id);
	if (ret < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Failed to set standard\n");
		goto unlock_out;
	}

unlock_out:
	mutex_unlock(&video->lock);
	return ret;
}



static int vpfe_enum_preset(struct file *file, void *fh,
			    struct v4l2_dv_enum_preset *preset)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct v4l2_subdev *subdev = video->current_subdev->subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_enum_preset\n");

	return v4l2_subdev_call(subdev, video, enum_dv_presets, preset);
}

static int vpfe_query_preset(struct file *file, void *fh,
			     struct v4l2_dv_preset *preset)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct v4l2_subdev *subdev = video->current_subdev->subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_query_preset\n");

	return v4l2_subdev_call(subdev, video, query_dv_preset, preset);
}

static int vpfe_s_preset(struct file *file, void *fh,
			 struct v4l2_dv_preset *preset)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_preset\n");

	return v4l2_device_call_until_err(&vpfe_dev->v4l2_dev,
					  video->current_subdev->grp_id,
					  video, s_dv_preset, preset);
}

static int vpfe_g_preset(struct file *file, void *fh,
			 struct v4l2_dv_preset *preset)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct v4l2_subdev *subdev = video->current_subdev->subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_preset\n");

	return v4l2_subdev_call(subdev, video, query_dv_preset, preset);
}

/*
 *  Videobuf operations
 */
static int vpfe_videobuf_setup(struct videobuf_queue *vq,
				unsigned int *count,
				unsigned int *size)
{
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_video_device *video = fh->video;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_pipeline *pipe = &video->pipe;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_buffer_setup\n");

	/*
	 * if we are using mmap, check the size of the allocated buffer is less
	 * than or equal to the maximum specified in the driver. Assume here the
	 * user has called S_FMT and sizeimage has been calculated.
	 */
	*size = video->fmt.fmt.pix.sizeimage;

	if (video->memory == V4L2_MEMORY_MMAP) {
		/* Limit maximum to what is configured */
		if (*size > vpfe_dev->config_params.device_bufsize)
			*size = vpfe_dev->config_params.device_bufsize;
	}

	if (vpfe_dev->config_params.video_limit) {
		while (*size * *count > vpfe_dev->config_params.video_limit)
			(*count)--;
	}

	if (pipe->state == VPFE_PIPELINE_STREAM_CONTINUOUS) {
		if (*count < vpfe_dev->config_params.min_numbuffers)
			*count = vpfe_dev->config_params.min_numbuffers;
	} else
		/* for single-shot it should be 1 buffer */
		*count = 1;


	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
		"count=%d, size=%d\n", *count, *size);
	return 0;
}

static int vpfe_videobuf_prepare(struct videobuf_queue *vq,
				struct videobuf_buffer *vb,
				enum v4l2_field field)
{
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_video_device *video = fh->video;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	unsigned long addr;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_videobuf_prepare\n");

	/* If buffer is not initialized, initialize it */
	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		vb->width = video->fmt.fmt.pix.width;
		vb->height = video->fmt.fmt.pix.height;
		vb->size = video->fmt.fmt.pix.sizeimage;
		vb->field = field;

		ret = videobuf_iolock(vq, vb, NULL);
		if (ret < 0)
			return ret;

		addr = videobuf_to_dma_contig(vb);
		/* Make sure user addresses are aligned to 32 bytes */
		if (!ALIGN(addr, 32))
			return -EINVAL;

		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;
}

static void vpfe_videobuf_queue(struct videobuf_queue *vq,
				struct videobuf_buffer *vb)
{
	/* Get the file handle object and device object */
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_video_device *video = fh->video;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	unsigned long flags, empty, addr;
	struct vpfe_pipeline *pipe = &video->pipe;

	empty = list_empty(&video->dma_queue);

	/* add the buffer to the DMA queue */
	spin_lock_irqsave(&video->dma_queue_lock, flags);
	list_add_tail(&vb->queue, &video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);

	/* Change state of the buffer */
	vb->state = VIDEOBUF_QUEUED;

	/* this case happens in case of single shot */
	if (empty && video->started && (pipe->state ==
		VPFE_PIPELINE_STREAM_SINGLESHOT)){
		spin_lock(&video->dma_queue_lock);
		addr = vpfe_get_next_buffer(video);
		video->ops->queue(vpfe_dev, addr);
		spin_unlock(&video->dma_queue_lock);

		video->state |= VPFE_VIDEO_BUFFER_QUEUED;
		/* we need to enable h/w each time in single shot */
		if (is_pipe_ready(pipe))
			vpfe_pipeline_set_stream(video,
					pipe,
					VPFE_PIPELINE_STREAM_SINGLESHOT);
	}
}

static void vpfe_videobuf_release(struct videobuf_queue *vq,
				  struct videobuf_buffer *vb)
{
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_video_device *video = fh->video;
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_videobuf_release\n");

	if (video->memory == V4L2_MEMORY_MMAP)
		videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops vpfe_videobuf_qops = {
	.buf_setup      = vpfe_videobuf_setup,
	.buf_prepare    = vpfe_videobuf_prepare,
	.buf_queue      = vpfe_videobuf_queue,
	.buf_release    = vpfe_videobuf_release,
};

/*
 * vpfe_reqbufs. currently support REQBUF only once opening
 * the device.
 */
static int vpfe_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *req_buf)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_fh *fh = file->private_data;
	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_reqbufs\n");

	if ((V4L2_BUF_TYPE_VIDEO_CAPTURE != req_buf->type) &&
		(V4L2_BUF_TYPE_VIDEO_OUTPUT != req_buf->type)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&video->lock);
	if (ret)
		return ret;

	if (video->io_usrs != 0) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Only one IO user allowed\n");
		ret = -EBUSY;
		goto unlock_out;
	}

	video->memory = req_buf->memory;
	videobuf_queue_dma_contig_init(&video->buffer_queue,
				&vpfe_videobuf_qops,
				vpfe_dev->pdev,
				&video->irqlock,
				req_buf->type,
				video->fmt.fmt.pix.field,
				sizeof(struct videobuf_buffer),
				fh,
				NULL);

	fh->io_allowed = 1;
	video->io_usrs = 1;
	INIT_LIST_HEAD(&video->dma_queue);
	ret = videobuf_reqbufs(&video->buffer_queue, req_buf);

unlock_out:
	mutex_unlock(&video->lock);
	return ret;
}

static int vpfe_querybuf(struct file *file, void *priv,
			 struct v4l2_buffer *buf)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_querybuf\n");

	if ((V4L2_BUF_TYPE_VIDEO_CAPTURE != buf->type) &&
		(V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return  -EINVAL;
	}

	if (video->memory != V4L2_MEMORY_MMAP) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid memory\n");
		return -EINVAL;
	}
	/* Call videobuf_querybuf to get information */
	return videobuf_querybuf(&video->buffer_queue, buf);
}

static int vpfe_qbuf(struct file *file, void *priv,
		     struct v4l2_buffer *p)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_fh *fh = file->private_data;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_qbuf\n");

	if ((V4L2_BUF_TYPE_VIDEO_CAPTURE != p->type) &&
		(V4L2_BUF_TYPE_VIDEO_OUTPUT != p->type)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}

	/*
	 * If this file handle is not allowed to do IO,
	 * return error
	 */
	if (!fh->io_allowed) {
		v4l2_err(&vpfe_dev->v4l2_dev, "fh->io_allowed\n");
		return -EACCES;
	}
	return videobuf_qbuf(&video->buffer_queue, p);
}

static int vpfe_dqbuf(struct file *file, void *priv,
		      struct v4l2_buffer *buf)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_dqbuf\n");

	if ((V4L2_BUF_TYPE_VIDEO_CAPTURE != buf->type) &&
		(V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}
	return videobuf_dqbuf(&video->buffer_queue,
				      buf, file->f_flags & O_NONBLOCK);
}

/* vpfe_start_capture: start streaming on all the subdevs */
static int vpfe_start_capture(struct vpfe_video_device *video)
{
	struct vpfe_pipeline *pipe = &video->pipe;
	int ret = 0;

	video->started = 1;

	if (is_pipe_ready(pipe))
		ret = vpfe_pipeline_set_stream(video, pipe,
				       VPFE_PIPELINE_STREAM_CONTINUOUS);

	return ret;
}

/*
 * vpfe_streamon. Assume the DMA queue is not empty.
 * application is expected to call QBUF before calling
 * this ioctl. If not, driver returns error
 */
static int vpfe_streamon(struct file *file, void *priv,
			 enum v4l2_buf_type buf_type)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_fh *fh = file->private_data;
	struct vpfe_subdev_info *sdinfo;
	unsigned long addr;

	struct vpfe_pipeline *pipe = &video->pipe;
	int ret = -EINVAL;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_streamon\n");

	if ((V4L2_BUF_TYPE_VIDEO_CAPTURE != buf_type) &&
		(V4L2_BUF_TYPE_VIDEO_OUTPUT != buf_type)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return ret;
	}

	/* If file handle is not allowed IO, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpfe_dev->v4l2_dev, "fh->io_allowed\n");
		return -EACCES;
	}

	sdinfo = video->current_subdev;

	/* If buffer queue is empty, return error */
	if (list_empty(&video->buffer_queue.stream)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "buffer queue is empty\n");
		return -EIO;
	}

	/* Validate the pipeline */
	if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type) {
		ret = vpfe_video_validate_pipeline(pipe);
		if (ret < 0)
			return ret;
	}

	/* Call videobuf_streamon to start streaming * in videobuf */
	ret = videobuf_streamon(&video->buffer_queue);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&video->lock);
	if (ret)
		goto streamoff;
	/* Get the next frame from the buffer queue */
	video->next_frm = list_entry(video->dma_queue.next,
					struct videobuf_buffer, queue);
	video->cur_frm = video->next_frm;
	/* Remove buffer from the buffer queue */
	list_del(&video->cur_frm->queue);
	/* Mark state of the current frame to active */
	video->cur_frm->state = VIDEOBUF_ACTIVE;
	/* Initialize field_id and started member */
	video->field_id = 0;
	addr = videobuf_to_dma_contig(video->cur_frm);

	video->ops->queue(vpfe_dev, addr);

	video->state |= VPFE_VIDEO_BUFFER_QUEUED;

	/* Image processor chained in the path */
	if (!cpu_is_davinci_dm365() &&
	    !video->current_subdev->is_camera) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Doesn't support chaining\n");
		goto unlock_out;
	}

	ret = 0;

	ret = vpfe_start_capture(video);
	if (ret)
		goto unlock_out;

	mutex_unlock(&video->lock);
	return ret;
unlock_out:
	mutex_unlock(&video->lock);
streamoff:
	ret = videobuf_streamoff(&video->buffer_queue);
	return ret;
}

static int vpfe_streamoff(struct file *file, void *priv,
			  enum v4l2_buf_type buf_type)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_fh *fh = file->private_data;
	int ret = 0;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_streamoff\n");

	if ((V4L2_BUF_TYPE_VIDEO_CAPTURE != buf_type) &&
		(V4L2_BUF_TYPE_VIDEO_OUTPUT != buf_type)) {
		v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}

	/* If io is allowed for this file handle, return error */
	if (!fh->io_allowed) {
		v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "fh->io_allowed\n");
		return -EACCES;
	}

	/* If streaming is not started, return error */
	if (!video->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "device is not started\n");
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&video->lock);
	if (ret)
		return ret;

	vpfe_stop_capture(video);

	video->pipe.state = VPFE_PIPELINE_STREAM_STOPPED;

	ret = videobuf_streamoff(&video->buffer_queue);
	mutex_unlock(&video->lock);
	return ret;
}

static int vpfe_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_subdev_info *sub_dev = video->current_subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_queryctrl\n");

	/* pass it to sub device */
	return v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sub_dev->grp_id,
					  core, queryctrl, qc);
}

static int vpfe_g_ctrl(struct file *file, void *priv,
			struct v4l2_control *ctrl)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_subdev_info *sub_dev = video->current_subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_ctrl\n");

	return v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sub_dev->grp_id,
					  core, g_ctrl, ctrl);
}

static int vpfe_s_ctrl(struct file *file, void *priv,
			     struct v4l2_control *ctrl)
{
	struct vpfe_video_device *video = video_drvdata(file);
	struct vpfe_device *vpfe_dev = video->vpfe_dev;
	struct vpfe_subdev_info *sub_dev = video->current_subdev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_ctrl\n");

	return v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sub_dev->grp_id,
					  core, s_ctrl, ctrl);
}

/* vpfe capture ioctl operations */
static const struct v4l2_ioctl_ops vpfe_ioctl_ops = {
	.vidioc_querycap	 = vpfe_querycap,
	.vidioc_g_fmt_vid_cap    = vpfe_g_fmt,
	.vidioc_s_fmt_vid_cap    = vpfe_s_fmt,
	.vidioc_try_fmt_vid_cap  = vpfe_try_fmt,
	.vidioc_enum_fmt_vid_cap = vpfe_enum_fmt,
	.vidioc_g_fmt_vid_out    = vpfe_g_fmt,
	.vidioc_s_fmt_vid_out    = vpfe_s_fmt,
	.vidioc_try_fmt_vid_out  = vpfe_try_fmt,
	.vidioc_enum_fmt_vid_out = vpfe_enum_fmt,
	.vidioc_enum_input	 = vpfe_enum_input,
	.vidioc_g_input		 = vpfe_g_input,
	.vidioc_s_input		 = vpfe_s_input,
	.vidioc_querystd	 = vpfe_querystd,
	.vidioc_s_std		 = vpfe_s_std,
	.vidioc_enum_dv_presets	 = vpfe_enum_preset,
	.vidioc_query_dv_preset	 = vpfe_query_preset,
	.vidioc_s_dv_preset	 = vpfe_s_preset,
	.vidioc_g_dv_preset	 = vpfe_g_preset,
	.vidioc_reqbufs		 = vpfe_reqbufs,
	.vidioc_querybuf	 = vpfe_querybuf,
	.vidioc_qbuf		 = vpfe_qbuf,
	.vidioc_dqbuf		 = vpfe_dqbuf,
	.vidioc_streamon	 = vpfe_streamon,
	.vidioc_streamoff	 = vpfe_streamoff,
	.vidioc_queryctrl	 = vpfe_queryctrl,
	.vidioc_g_ctrl		 = vpfe_g_ctrl,
	.vidioc_s_ctrl		 = vpfe_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * VPFE video core
 */

static const struct vpfe_video_operations vpfe_video_dummy_ops = {
};

int vpfe_video_init(struct vpfe_video_device *video, const char *name)
{
	const char *direction;
	int ret = 0;

	switch (video->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		direction = "output";
		video->pad.flags = MEDIA_PAD_FL_INPUT;
		video->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		direction = "input";
		video->pad.flags = MEDIA_PAD_FL_OUTPUT;
		video->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		break;

	default:
		return -EINVAL;
	}

	/* Initialize field of video device */
	video->video_dev.release = video_device_release;
	video->video_dev.fops = &vpfe_fops;
	video->video_dev.ioctl_ops = &vpfe_ioctl_ops;
	video->video_dev.minor = -1;
	video->video_dev.tvnorms = 0;
	video->video_dev.current_norm = V4L2_STD_NTSC;

	snprintf(video->video_dev.name, sizeof(video->video_dev.name),
		 "DAVINCI VIDEO %s %s", name, direction);

	/* Initialize prio member of device object */
	v4l2_prio_init(&video->prio);

	spin_lock_init(&video->irqlock);
	spin_lock_init(&video->dma_queue_lock);
	mutex_init(&video->lock);

	ret = media_entity_init(&video->video_dev.entity,
				1, &video->pad, 0);
	if (ret < 0)
		return ret;

	video_set_drvdata(&video->video_dev, video);

	return 0;
}
EXPORT_SYMBOL_GPL(vpfe_video_init);

int vpfe_video_register(struct vpfe_video_device *video,
			struct v4l2_device *vdev)
{
	int ret;

	video->video_dev.v4l2_dev = vdev;

	ret = video_register_device(&video->video_dev, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		printk(KERN_ERR "%s: could not register video device (%d)\n",
			__func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(vpfe_video_register);

void vpfe_video_unregister(struct vpfe_video_device *video)
{
	if (video_is_registered(&video->video_dev)) {
		media_entity_cleanup(&video->video_dev.entity);
		video_unregister_device(&video->video_dev);
	}
}
EXPORT_SYMBOL_GPL(vpfe_video_unregister);
