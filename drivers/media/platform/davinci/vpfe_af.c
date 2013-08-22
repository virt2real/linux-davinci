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
#include <media/davinci/vpss.h>
#include <mach/cputype.h>

#include <media/v4l2-event.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <media/davinci/vpfe_capture.h>
#include <linux/export.h>
#include <linux/module.h>
/*
 * af_link_setup - Setup AF connections
 * @entity: AF media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int af_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{

	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_af_device *af = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev;

	vpfe_dev = to_vpfe_device(af);

	if ((flags & MEDIA_LNK_FL_ENABLED)) {
		af->input = AF_INPUT_CCDC;
		if (cpu_is_davinci_dm365())
			dm365_af_open();
		else if (cpu_is_davinci_dm355())
			dm355_af_open();
	} else {
		af->input = AF_INPUT_NONE;
		if (cpu_is_davinci_dm365())
			dm365_af_release();
		else if (cpu_is_davinci_dm355())
			dm355_af_release();
	}
	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static int af_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	struct vpfe_af_device *af = v4l2_get_subdevdata(sd);

	if (sub->type != af->event_type)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, 0);

}
static int af_unsubscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	struct vpfe_af_device *af = v4l2_get_subdevdata(sd);

	if (sub->type != af->event_type)
		return -EINVAL;

	return v4l2_event_unsubscribe(fh, sub);
}

void af_queue_event(struct v4l2_subdev *sd)
{
	struct video_device *vdev = &sd->devnode;
	struct vpfe_af_device *af = v4l2_get_subdevdata(sd);
	struct v4l2_event event;

	memset(&event, 0, sizeof(event));

	event.type = af->event_type;
	v4l2_event_queue(vdev, &event);
}
EXPORT_SYMBOL(af_queue_event);

/*
 * af_set_stream - Enable/Disable streaming on the AF module
 * @sd: VPFE AF V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int af_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_af_device *af = v4l2_get_subdevdata(sd);
	if (af->input == AF_INPUT_CCDC) {
		if (cpu_is_davinci_dm365())
			return dm365_af_set_stream(sd, enable);
		else if (cpu_is_davinci_dm355())
			return dm355_af_set_stream(sd, enable);
	}

	return 0;
}

/*
 * af_ioctl - AF module private ioctl's
 * @sd: VPFE AF V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long af_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	if (cpu_is_davinci_dm365())
		return dm365_af_ioctl(sd, cmd, arg);
	else if (cpu_is_davinci_dm355())
		return dm355_af_ioctl(sd, cmd, arg);
	else
		return -EINVAL;
}

static const struct media_entity_operations af_media_ops = {
	.link_setup = af_link_setup,
};

/* V4L2 subdev core operations */
static const struct v4l2_subdev_core_ops af_v4l2_core_ops = {
	.ioctl = af_ioctl,
	.subscribe_event = af_subscribe_event,
	.unsubscribe_event = af_unsubscribe_event,
};

/* V4L2 subdev video operations */
static const struct v4l2_subdev_video_ops af_v4l2_video_ops = {
	.s_stream = af_set_stream,
};

/* V4L2 subdev operations */
static const struct v4l2_subdev_ops af_v4l2_ops = {
	.core = &af_v4l2_core_ops,
	.video = &af_v4l2_video_ops,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

int vpfe_af_register_entities(struct vpfe_af_device *af,
				struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &af->subdev);
	if (ret < 0)
		return ret;

	return 0;
}

void vpfe_af_unregister_entities(struct vpfe_af_device *af)
{
	/* cleanup entity */
	media_entity_cleanup(&af->subdev.entity);
	/* unregister subdev */
	v4l2_device_unregister_subdev(&af->subdev);
}

int vpfe_af_init(struct vpfe_af_device *vpfe_af, struct platform_device *pdev)
{
	struct v4l2_subdev *af = &vpfe_af->subdev;
	struct media_pad *pads = &vpfe_af->pads[0];
	struct media_entity *me = &af->entity;

	int ret;

	if (cpu_is_davinci_dm644x()) {
		return -1;
	} else if (cpu_is_davinci_dm365()) {
		if (dm365_af_init(pdev))
			return -1;

	} else if (cpu_is_davinci_dm355()) {
		printk(KERN_NOTICE "dm355 init af\n");
		if (dm355_af_init(pdev)) {
			printk(KERN_ERR "error initing af\n");
			return -1;
		}
	} else {
		return -1;
	}

	v4l2_subdev_init(af, &af_v4l2_ops);
	strlcpy(af->name, "DAVINCI AF", sizeof(af->name));
	af->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(af, vpfe_af);
	af->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	af->nevents = DAVINCI_AF_NEVENTS;

	pads[AF_PAD_SINK].flags = MEDIA_PAD_FL_INPUT;

	vpfe_af->input = AF_INPUT_NONE;
	vpfe_af->event_type = DAVINCI_EVENT_AF;

	me->ops = &af_media_ops;

	ret = media_entity_init(me, AF_PADS_NUM, pads, 0);
	if (ret)
		goto out_davanci_init;

	return 0;

out_davanci_init:
	if (cpu_is_davinci_dm365())
		dm365_af_cleanup();
	else if (cpu_is_davinci_dm355())
		dm355_af_cleanup();

	return ret;
}

/*
 * vpfe_af_cleanup - AF module cleanup.
 * @dev: Device pointer specific to the VPFE.
 */
void vpfe_af_cleanup(struct platform_device *pdev)
{

	if (cpu_is_davinci_dm365())
		dm365_af_cleanup();
	else if (cpu_is_davinci_dm355())
		dm355_af_cleanup();

}
