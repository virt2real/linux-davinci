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

/* All video device related structures will go here */
#ifndef _VPFE_VIDEO_H
#define _VPFE_VIDEO_H

#include <media/media-entity.h>
#include <media/videobuf-dma-contig.h>

struct vpfe_device;

/*
 * struct vpfe_video_operations - VPFE video operations
 * @queue:	Resume streaming when a buffer is queued. Called on VIDIOC_QBUF
 *		if there was no buffer previously queued.
 */
struct vpfe_video_operations {
	void(*queue)(struct vpfe_device *vpfe_dev, unsigned long addr);
};

enum vpfe_pipeline_stream_state {
	VPFE_PIPELINE_STREAM_STOPPED,
	VPFE_PIPELINE_STREAM_CONTINUOUS,
	VPFE_PIPELINE_STREAM_SINGLESHOT
};

enum vpfe_video_state {
	/* indicates that buffer is queued */
	VPFE_VIDEO_BUFFER_QUEUED = 1,
};

struct vpfe_pipeline {
	struct media_pipeline		*pipe;
	enum vpfe_pipeline_stream_state	state;
	struct v4l2_subdev		*input_sd;
	struct vpfe_video_device	*input;
	struct vpfe_video_device	*output;
};

#define to_vpfe_pipeline(__e) \
	container_of((__e)->pipe, struct vpfe_pipeline, pipe)

#define to_vpfe_video(vdev) \
	container_of(vdev, struct vpfe_video_device, video_dev)

/* moved in from vpfe_capture.h */
struct vpfe_std_info {
	int active_pixels;
	int active_lines;
	/* current frame format */
	int frame_format;
	struct v4l2_fract fps;
};

/* TODO - revisit for MC moved in from vpfe_capture.h */
enum output_src {
	VPFE_CCDC_OUT,
	VPFE_IMP_PREV_OUT,
	VPFE_IMP_RSZ_OUT
};

struct vpfe_video_device {
	struct vpfe_device			*vpfe_dev;
	struct video_device			video_dev;
	struct media_pad			pad;
	const struct vpfe_video_operations	*ops;

	/* below are not yet used */
	enum v4l2_buf_type			type;
	/* Indicates id of the field which is being captured */
	u32					field_id;

	struct vpfe_pipeline			pipe;
	/* Indicates whether streaming started */
	u8					started;
	/* Indicates state of the stream */
	unsigned int				state;
	/* current input at the sub device */
	int					current_input;
	/*
	 * This field keeps track of type of buffer exchange mechanism
	 * user has selected
	 */
	enum v4l2_memory			memory;
	/* Used to keep track of state of the priority */
	struct v4l2_prio_state			prio;
	/* number of open instances of the channel */
	u32					usrs;
	/* flag to indicate whether decoder is initialized */
	u8					initialized;
	/* skip frame count */
	u8					skip_frame_count;
	/* skip frame count init value */
	u8					skip_frame_count_init;
	/* time per frame for skipping */
	struct v4l2_fract			timeperframe;
	/* ptr to currently selected sub device */
	struct vpfe_subdev_info			*current_subdev;
	/* Pointer pointing to current v4l2_buffer */
	struct videobuf_buffer			*cur_frm;
	/* Pointer pointing to next v4l2_buffer */
	struct videobuf_buffer			*next_frm;
	/* Used to store pixel format */
	struct v4l2_format			fmt;
	/* Buffer queue used in video-buf */
	struct videobuf_queue			buffer_queue;
	/* Queue of filled frames */
	struct list_head			dma_queue;
	/* Used in video-buf */
	spinlock_t				irqlock;
	/* IRQ lock for DMA queue */
	spinlock_t				dma_queue_lock;
	/* lock used to access this structure */
	struct mutex				lock;
	/* number of users performing IO */
	u32					io_usrs;
	/*
	 * offset where second field starts from the starting of the
	 * buffer for field seperated YCbCr formats
	 */
	u32					field_off;
};

void vpfe_video_unregister(struct vpfe_video_device *video);
int vpfe_video_register(struct vpfe_video_device *video,
			struct v4l2_device *vdev);
int vpfe_video_init(struct vpfe_video_device *video, const char *name);

void vpfe_process_buffer_complete(struct vpfe_video_device *video);
void vpfe_schedule_bottom_field(struct vpfe_video_device *video);
void vpfe_schedule_next_buffer(struct vpfe_video_device *video);
unsigned long vpfe_get_next_buffer(struct vpfe_video_device *video);
int vpfe_pipeline_set_stream(struct vpfe_video_device *video,
			     struct vpfe_pipeline *pipe,
			    enum vpfe_pipeline_stream_state state);
#endif
