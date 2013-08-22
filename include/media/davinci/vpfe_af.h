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
#ifndef _VPFE_AF_H
#define _VPFE_AF_H

#define AF_PAD_SINK      0
#define AF_PADS_NUM      1

#define DAVINCI_AF_NEVENTS 1

#define AF_INPUT_NONE		0
#define AF_INPUT_CCDC		1

#define DAVINCI_EVENT_AF	1

struct vpfe_af_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[AF_PADS_NUM];
	unsigned int			input;
	unsigned long			event_type;
};

int dm365_af_init(struct platform_device *pdev);
void dm365_af_cleanup(void);
int dm365_af_set_stream(struct v4l2_subdev *sd, int enable);
int dm365_af_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
int dm365_af_open(void);
int dm365_af_release(void);

int dm355_af_init(struct platform_device *pdev);
void dm355_af_cleanup(void);
int dm355_af_set_stream(struct v4l2_subdev *sd, int enable);
int dm355_af_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
int dm355_af_open(void);
int dm355_af_release(void);

int vpfe_af_register_entities(struct vpfe_af_device *af,
			      struct v4l2_device *v4l2_dev);
void vpfe_af_unregister_entities(struct vpfe_af_device *af);
void vpfe_af_cleanup(struct platform_device *pdev);
int vpfe_af_init(struct vpfe_af_device *vpfe_af, struct platform_device *pdev);
void af_queue_event(struct v4l2_subdev *sd);
#endif
