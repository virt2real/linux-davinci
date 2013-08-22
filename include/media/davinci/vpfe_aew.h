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
#ifndef _VPFE_AEW_H
#define _VPFE_AEW_H

#define AEW_PAD_SINK      0
#define AEW_PADS_NUM      1

#define DAVINCI_AEW_NEVENTS 1

#define AEW_INPUT_NONE		0
#define AEW_INPUT_CCDC		1

#define DAVINCI_EVENT_AEWB	1

struct vpfe_aew_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[AEW_PADS_NUM];
	unsigned int			input;
	unsigned long			event_type;
};

int dm365_aew_init(struct platform_device *pdev);
void dm365_aew_cleanup(void);
int dm365_aew_set_stream(struct v4l2_subdev *sd, int enable);
int dm365_aew_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
int dm365_aew_open(void);
int dm365_aew_release(void);

int dm355_aew_init(struct platform_device *pdev);
void dm355_aew_cleanup(void);
int dm355_aew_set_stream(struct v4l2_subdev *sd, int enable);
int dm355_aew_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
int dm355_aew_open(void);
int dm355_aew_release(void);

int vpfe_aew_register_entities(struct vpfe_aew_device *aew,
			       struct v4l2_device *v4l2_dev);
void vpfe_aew_unregister_entities(struct vpfe_aew_device *aew);
void vpfe_aew_cleanup(struct platform_device *pdev);
int vpfe_aew_init(struct vpfe_aew_device *vpfe_aew,
		  struct platform_device *pdev);
void aew_queue_event(struct v4l2_subdev *sd);
#endif
