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
#include <media/v4l2-dev.h>
#include <media/v4l2-subdev.h>
#include <media/media-entity.h>
#include <media/davinci/vpfe_capture.h>
#include <linux/export.h>
#include <linux/module.h>
/*
 * aew_link_setup - Setup AEW connections
 * @entity: AEW media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int aew_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{

	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_aew_device *aew = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev;

	vpfe_dev = to_vpfe_device(aew);

	if ((flags & MEDIA_LNK_FL_ENABLED)) {
		aew->input = AEW_INPUT_CCDC;
		if (cpu_is_davinci_dm365())
			dm365_aew_open();
		else if (cpu_is_davinci_dm355())
			dm355_aew_open();
	} else {
		aew->input = AEW_INPUT_NONE;
		if (cpu_is_davinci_dm365())
			dm365_aew_release();
		if (cpu_is_davinci_dm355())
			dm355_aew_release();
	}
	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static int aew_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	struct vpfe_aew_device *aew = v4l2_get_subdevdata(sd);


	if (sub->type != aew->event_type)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, 0);

}
static int aew_unsubscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	struct vpfe_aew_device *aew = v4l2_get_subdevdata(sd);


	if (sub->type != aew->event_type)
		return -EINVAL;

	return v4l2_event_unsubscribe(fh, sub);
}

void aew_queue_event(struct v4l2_subdev *sd)
{
	struct video_device *vdev = &sd->devnode;
	struct vpfe_aew_device *aew = v4l2_get_subdevdata(sd);
	struct v4l2_event event;

	memset(&event, 0, sizeof(event));

	event.type = aew->event_type;
	v4l2_event_queue(vdev, &event);
}
EXPORT_SYMBOL(aew_queue_event);

/*
 * aew_set_stream - Enable/Disable streaming on the AEW module
 * @sd: VPFE AEW V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int aew_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_aew_device *aew = v4l2_get_subdevdata(sd);

	if (aew->input == AEW_INPUT_CCDC) {
		if (cpu_is_davinci_dm365())
			return dm365_aew_set_stream(sd, enable);
		else if (cpu_is_davinci_dm355())
			return dm355_aew_set_stream(sd, enable);
	}

	return 0;
}

/*
 * aew_ioctl - AEW module private ioctl's
 * @sd: VPFE AEW V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long aew_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	if (cpu_is_davinci_dm365())
		return dm365_aew_ioctl(sd, cmd, arg);
	else if (cpu_is_davinci_dm355())
		return dm355_aew_ioctl(sd, cmd, arg);
	else
		return -EINVAL;
}

static const struct media_entity_operations aew_media_ops = {
	.link_setup = aew_link_setup,
};

/* V4L2 subdev core operations */
static const struct v4l2_subdev_core_ops aew_v4l2_core_ops = {
	.ioctl = aew_ioctl,
	.subscribe_event = aew_subscribe_event,
	.unsubscribe_event = aew_unsubscribe_event,
};

/* V4L2 subdev video operations */
static const struct v4l2_subdev_video_ops aew_v4l2_video_ops = {
	.s_stream = aew_set_stream,
};

/* V4L2 subdev operations */
static const struct v4l2_subdev_ops aew_v4l2_ops = {
	.core = &aew_v4l2_core_ops,
	.video = &aew_v4l2_video_ops,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

int vpfe_aew_register_entities(struct vpfe_aew_device *aew,
				struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &aew->subdev);
	if (ret < 0)
		return ret;


	return 0;
}

void vpfe_aew_unregister_entities(struct vpfe_aew_device *aew)
{
	/* cleanup entity */
	media_entity_cleanup(&aew->subdev.entity);
	/* unregister subdev */
	v4l2_device_unregister_subdev(&aew->subdev);
}

int vpfe_aew_init(struct vpfe_aew_device *vpfe_aew,
		  struct platform_device *pdev)
{
	struct v4l2_subdev *aew = &vpfe_aew->subdev;
	struct media_pad *pads = &vpfe_aew->pads[0];
	struct media_entity *me = &aew->entity;

	int ret;

	if (cpu_is_davinci_dm644x()) {
		return -1;
	} else if (cpu_is_davinci_dm365()) {
		if (dm365_aew_init(pdev))
			return -1;
	} else if (cpu_is_davinci_dm355()) {
		if (dm355_aew_init(pdev))
			return -1;
	} else {
		return -1;
	}

	v4l2_subdev_init(aew, &aew_v4l2_ops);
	strlcpy(aew->name, "DAVINCI AEW", sizeof(aew->name));
	aew->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(aew, vpfe_aew);
	aew->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	aew->nevents = DAVINCI_AEW_NEVENTS;

	pads[AEW_PAD_SINK].flags = MEDIA_PAD_FL_INPUT;

	vpfe_aew->input = AEW_INPUT_NONE;
	vpfe_aew->event_type = DAVINCI_EVENT_AEWB;

	me->ops = &aew_media_ops;

	ret = media_entity_init(me, AEW_PADS_NUM, pads, 0);
	if (ret)
		goto out_davanci_init;

	return 0;

out_davanci_init:
	if (cpu_is_davinci_dm365())
		dm365_aew_cleanup();
	else if (cpu_is_davinci_dm355())
		dm355_aew_cleanup();
	return ret;
}

/*
 * vpfe_aew_cleanup - AEW module cleanup.
 * @dev: Device pointer specific to the VPFE.
 */
void vpfe_aew_cleanup(struct platform_device *pdev)
{
	if (cpu_is_davinci_dm365())
		dm365_aew_cleanup();
	else if (cpu_is_davinci_dm355())
		dm355_aew_cleanup();

}
