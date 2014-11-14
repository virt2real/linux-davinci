#define DEBUG
/*
 * Copyright (C) 2007-2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
/*#include <linux/i2c-id.h>*/
#include <linux/device.h>
#include <linux/workqueue.h>

#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/davinci/videohd.h> // For HD std (V4L2_STD_1080I, etc)
#include <asm/uaccess.h>

#include "adv7611.h"

/* Debug functions */
static int debug = 2;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");


/* Function Prototypes */
static int adv7611_i2c_read_reg(struct i2c_client *client, u8 addr, u8 reg, u8 * val);
static int adv7611_i2c_write_reg(struct i2c_client *client, u8 addr, u8 reg, u8 val);
static int adv7611_querystd(struct v4l2_subdev *sd, v4l2_std_id *id);
static int adv7611_s_std(struct v4l2_subdev *sd, v4l2_std_id std);

static int adv7611_s_stream(struct v4l2_subdev *sd, int enable);

static int adv7611_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int adv7611_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int adv7611_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc);

static int adv7611_initialize(struct v4l2_subdev *sd);
static int adv7611_deinitialize(struct v4l2_subdev *sd);

#if 0
static struct v4l2_standard adv7611_standards[ADV7611_MAX_NO_STANDARDS] = {
	{
		.index = 0,
		.id = V4L2_STD_720P_60,
		.name = "720P-60",
		.frameperiod = {1, 60},
		.framelines = 750
	},
	{
		.index = 1,
		.id = V4L2_STD_1080I_60,
		.name = "1080I-30",
		.frameperiod = {1, 30},
		.framelines = 1125
	},
	{
		.index = 2,
		.id = V4L2_STD_1080I_50,
		.name = "1080I-25",
		.frameperiod = {1, 25},
		.framelines = 1125
	},
	{
		.index = 3,
		.id = V4L2_STD_720P_50,
		.name = "720P-50",
		.frameperiod = {1, 50},
		.framelines = 750
	},
	{
		.index = 4,
		.id = V4L2_STD_1080P_25,
		.name = "1080P-25",
		.frameperiod = {1, 25},
		.framelines = 1125
	},
	{
		.index = 5,
		.id = V4L2_STD_1080P_30,
		.name = "1080P-30",
		.frameperiod = {1, 30},
		.framelines = 1125
	},
	{
		.index = 6,
		.id = V4L2_STD_1080P_24,
		.name = "1080P-24",
		.frameperiod = {1, 24},
		.framelines = 1125
	},
	{
		.index = 7,
		.id = V4L2_STD_525P_60,
		.name = "480P-60",
		.frameperiod = {1, 60},
		.framelines = 525
	},
	{
		.index = 8,
		.id = V4L2_STD_625P_50,
		.name = "576P-50",
		.frameperiod = {1, 50},
		.framelines = 625
	},
	{
		.index = 9,
		.id = V4L2_STD_525_60,
		.name = "NTSC",
		.frameperiod = {1001, 30000},
		.framelines = 525
	},
	{
		.index = 10,
		.id = V4L2_STD_625_50,
		.name = "PAL",
		.frameperiod = {1, 25},
		.framelines = 625
	},
	{
		.index = 11,
		.id = V4L2_STD_1080P_50,
		.name = "1080P-50",
		.frameperiod = {1, 50},
		.framelines = 1125
	},
	{
		.index = 12,
		.id = V4L2_STD_1080P_60,
		.name = "1080P-60",
		.frameperiod = {1, 60},
		.framelines = 1125
	},

};
#endif

struct adv7611_channel {
	struct v4l2_subdev    sd;
        struct work_struct    work;
        int                   ch_id;

        int                   streaming;

        s32                   output_format; // see ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_*

        unsigned char IO_ADDR;
        unsigned char DPLL_ADDR;
        unsigned char CEC_ADDR;
        unsigned char INFOFRAME_ADDR;
        unsigned char KSV_ADDR;
        unsigned char EDID_ADDR;
        unsigned char HDMI_ADDR;
        unsigned char CP_ADDR;
};


static const struct v4l2_subdev_video_ops adv7611_video_ops = {
	.querystd = adv7611_querystd,
	.s_stream = adv7611_s_stream,
//	.g_input_status = adv7611_g_input_status,
};

static const struct v4l2_subdev_core_ops adv7611_core_ops = {
        .g_chip_ident = NULL,
	.g_ctrl = adv7611_g_ctrl,
	.s_ctrl = adv7611_s_ctrl,
	.queryctrl = adv7611_queryctrl,
	.s_std = adv7611_s_std,
};

static const struct v4l2_subdev_ops adv7611_ops = {
	.core = &adv7611_core_ops,
	.video = &adv7611_video_ops,
};


static inline struct adv7611_channel *to_adv7611(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7611_channel, sd);
}



/* adv7611_initialize :
 * This function will set the video format standard
 */
static int write_edid(struct v4l2_subdev *sd){
	int err = 0;
	int i = 0;
    /*u8 edid_data[] = {
    		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x58, 0x12,
    		0x34, 0x02, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x18, 0x01, 0x03,
    		0x80, 0xA0, 0x5A, 0x78, 0x0A, 0xAE, 0xA5, 0xA6, 0x54, 0x4C,
    		0x99, 0x26, 0x14, 0x50, 0x54, 0x20, 0x00, 0x00, 0x01, 0x01,
    		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    		0x01, 0x01, 0x01, 0x01, 0x02, 0x3A, 0x00, 0x18, 0x51, 0xD0,
    		0x2D, 0x20, 0x58, 0x2C, 0x45, 0x00, 0xA0, 0x5A, 0x00, 0x00,
    		0x00, 0x1E, 0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
    		0x6E, 0x28, 0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E,
    		0x00, 0x00, 0x00, 0xFC, 0x00, 0x48, 0x44, 0x4D, 0x49, 0x20,
    		0x41, 0x44, 0x41, 0x50, 0x54, 0x45, 0x52, 0x0A, 0x00, 0x00,
    		0x00, 0xFD, 0x00, 0x31, 0x3D, 0x0F, 0x44, 0x17, 0x00, 0x0A,
    		0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x63, 0x02, 0x03,
    		0x27, 0x51, 0x4B, 0x93, 0x84, 0x01, 0x02, 0x00,
    		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
    		0x00, 0x00, 0x00, 0x00, 0x62, 0x00, 0x0F, 0xE3, 0x00, 0x00,
    		0x01, 0x67, 0x03, 0x0C, 0x00, 0x10, 0x00, 0xB8, 0x2D, 0x01,
    		0x03, 0x00, 0xBC, 0x52, 0xD0, 0x1E, 0x20, 0xB8, 0x28, 0x55,
    		0x40, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 0x01, 0x1D, 0x00,
    		0x18, 0x51, 0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25, 0x00, 0xA0,
    		0x5A, 0x00, 0x00, 0x00, 0x9E, 0x01, 0x1D, 0x00, 0xD0, 0x52,
    		0x1C, 0x16, 0x20, 0x10, 0x2C, 0x25, 0x80, 0x20, 0xE0, 0x2D,
    		0x00, 0x00, 0x9E, 0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D,
    		0x10, 0x10, 0x3E, 0x96, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00,
    		0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x27
    };*/
	///*
    u8 edid_data[] = {
    	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x58, 0x12,
    	0x34, 0x02, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x18, 0x01, 0x03,
    	0x80, 0xA0, 0x5A, 0x78, 0x0A, 0xAE, 0xA5, 0xA6, 0x54, 0x4C,
    	0x99, 0x26, 0x14, 0x50, 0x54, 0x20, 0x00, 0x00, 0x01, 0x01,
    	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    	0x01, 0x01, 0x01, 0x01, 0x4F, 0x17, 0x00, 0x18, 0x51, 0xD0,
    	0x2D, 0x20, 0x58, 0x2C, 0x45, 0x00, 0xA0, 0x5A, 0x00, 0x00,
    	0x00, 0x1E, 0x4F, 0x17, 0x00, 0x18, 0x51, 0xD0, 0x2D, 0x20,
    	0x58, 0x2C, 0x45, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E,
    	0x00, 0x00, 0x00, 0xFC, 0x00, 0x48, 0x44, 0x4D, 0x49, 0x20,
    	0x41, 0x44, 0x41, 0x50, 0x54, 0x45, 0x52, 0x0A, 0x00, 0x00,
    	0x00, 0xFD, 0x00, 0x31, 0x3D, 0x0F, 0x44, 0x17, 0x00, 0x0A,
    	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x5E,
    	0x02, 0x03, 0x27, 0x71, 0x41, 0x93, 0x2F, 0x00, 0x00, 0x00,
    	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    	0x2F, 0x00, 0x00, 0x00, 0x62, 0x00, 0x0F, 0xE3, 0x00, 0x00,
    	0x01, 0x67, 0x03, 0x0C, 0x00, 0x10, 0x00, 0xB8, 0x2D, 0x37,
    	0x14, 0x00, 0x64, 0x50, 0xD0, 0x1E, 0x20, 0x64, 0x28, 0x55,
    	0x40, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 0x01, 0x1D, 0x00,
    	0x18, 0x51, 0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25, 0x00, 0xA0,
    	0x5A, 0x00, 0x00, 0x00, 0x9E, 0x01, 0x1D, 0x00, 0xD0, 0x52,
    	0x1C, 0x16, 0x20, 0x10, 0x2C, 0x25, 0x80, 0x20, 0xE0, 0x2D,
    	0x00, 0x00, 0x9E, 0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D,
    	0x10, 0x10, 0x3E, 0x96, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00,
    	0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0
    };
    //*/
	/*
    u8 edid_data[] = {
    	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x34, 0xA9,
    	0x65, 0xA0, 0x1C, 0x07, 0x00, 0x00, 0x25, 0x11, 0x01, 0x03,
    	0x80, 0x00, 0x00, 0x78, 0x0A, 0xDA, 0xFF, 0xA3, 0x58, 0x4A,
    	0xA2, 0x29, 0x17, 0x49, 0x4B, 0x00, 0x00, 0x00, 0x01, 0x01,
    	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    	0x01, 0x01, 0x01, 0x01, 0x01, 0x1D, 0x00, 0xBC, 0x52, 0xD0,
    	0x1E, 0x20, 0xB8, 0x28, 0x55, 0x40, 0x9A, 0x26, 0x53, 0x00,
    	0x00, 0x1E, 0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
    	0x6E, 0x28, 0x55, 0x00, 0x9A, 0x26, 0x53, 0x00, 0x00, 0x1E,
    	0x00, 0x00, 0x00, 0xFC, 0x00, 0x50, 0x41, 0x4E, 0x41, 0x53,
    	0x4F, 0x4E, 0x49, 0x43, 0x2D, 0x54, 0x56, 0x0A, 0x00, 0x00,
    	0x00, 0xFD, 0x00, 0x31, 0x3D, 0x0F, 0x44, 0x0F, 0x00, 0x0A,
    	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x25, 0x02, 0x03,
    	0x1F, 0x72, 0x4F, 0x93, 0x84, 0x14, 0x05, 0x1F, 0x10, 0x12,
    	0x03, 0x11, 0x02, 0x16, 0x07, 0x15, 0x06, 0x01, 0x23, 0x09,
    	0x07, 0x01, 0x66, 0x03, 0x0C, 0x00, 0x10, 0x00, 0x80, 0x01,
    	0x1D, 0x80, 0xD0, 0x72, 0x1C, 0x16, 0x20, 0x10, 0x2C, 0x25,
    	0x80, 0x9A, 0x26, 0x53, 0x00, 0x00, 0x9E, 0x8C, 0x0A, 0xD0,
    	0x90, 0x20, 0x40, 0x31, 0x20, 0x0C, 0x40, 0x55, 0x00, 0x9A,
    	0x26, 0x53, 0x00, 0x00, 0x18, 0x01, 0x1D, 0x80, 0x18, 0x71,
    	0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25, 0x00, 0x9A, 0x26, 0x53,
    	0x00, 0x00, 0x9E, 0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D,
    	0x10, 0x10, 0x3E, 0x96, 0x00, 0x9A, 0x26, 0x53, 0x00, 0x00,
    	0x18, 0x02, 0x3A, 0x80, 0xD0, 0x72, 0x38, 0x2D, 0x40, 0x10,
    	0x2C, 0x45, 0x80, 0x9A, 0x26, 0x53, 0x00, 0x00, 0x18, 0x00,
    	0x00, 0x00, 0x00, 0x00, 0x00, 0x0D
    };
*/
	struct i2c_client *ch_client = NULL;
    struct adv7611_channel *channel = (to_adv7611(sd));
    u8 ksv_i2caddr;
    u8 edid_i2caddr;
	ch_client = v4l2_get_subdevdata(sd);
	ksv_i2caddr = channel->KSV_ADDR;
	edid_i2caddr = channel->EDID_ADDR;
	v4l2_dbg(1, debug, sd, "Write edid controller\n");
	printk("Write edid controller\n");
	/* Disable I2C access to internal EDID ram from DDC port */
	/* Disable HDCP 1.1 features */
	/* Disable the Internal EDID */
	/* for all ports */
    err = adv7611_i2c_write_reg(ch_client,
    		ksv_i2caddr, 0x40, 0x81);
    if (err < 0) {
    	v4l2_dbg(1, debug, sd, "failed to disable HDCP 1.1 features\n");
    	return err;
    }
	v4l2_dbg(1, debug, sd, "HDCP 1.1 features disabled\n");
    err = adv7611_i2c_write_reg(ch_client,
    		ksv_i2caddr, 0x74, 0x00);
    if (err < 0) {
    	v4l2_dbg(1, debug, sd, "fail to reset ksv controller\n");
    	return err;
    }
	v4l2_dbg(1, debug, sd, "KSV controller is in reset\n");
	v4l2_dbg(1, debug, sd, "Write edid data %d\n", sizeof(edid_data));
	//Write EDID block
	for (i = 0; i < sizeof(edid_data); i++){
		printk("EDID %x, %x\n", i, edid_data[i]);
	    err = adv7611_i2c_write_reg(ch_client,
	    		edid_i2caddr, i, edid_data[i]);
	    if (err < 0) {
	    	v4l2_dbg(1, debug, sd, "fail to write edid data\n");
	    	return err;
	    	/* ADV761x calculates the checksums and enables I2C access
	    	 * to internal EDID ram from DDC port.
	    	 */
	    }
	}
	v4l2_dbg(1, debug, sd, "KSV controller is out of reset\n");
    err = adv7611_i2c_write_reg(ch_client,
    		ksv_i2caddr, 0x74, 0x01);
    if (err < 0) {
    	v4l2_dbg(1, debug, sd, "fail to set ksv controller\n");
    	return err;
    }
    return err;
}
static int adv7611_initialize(struct v4l2_subdev *sd)
{
	int err = 0;
	struct i2c_client *ch_client = NULL;
        struct adv7611_channel *channel = (to_adv7611(sd));
        struct {
                adv7611_i2caddr_t i2caddr;
                u8                reg;
                u8                val;
        } *init_cur, init_sequence[] = {
                { ADV7611_I2CADDR_IO,   0x01, 0x05 }, // TV Frameformat
                //{ ADV7611_I2CADDR_IO,   0x00, 0x19 }, // 720p with 2x1 decimation
                { ADV7611_I2CADDR_IO,   0x00, 0x13 }, // 720p withot 2x1 decimation
                { ADV7611_I2CADDR_IO,   0x02, 0xf5 }, // YUV out
                //{ ADV7611_I2CADDR_IO,   0x03, 0x00 },
                { ADV7611_I2CADDR_IO,   0x03, 0x80 },
                { ADV7611_I2CADDR_IO,   0x05, 0x2c },
                { ADV7611_I2CADDR_IO,   0x06, 0xa6 }, // Invert HS, VS pins

                /* Bring chip out of powerdown and disable tristate */
                { ADV7611_I2CADDR_IO,   0x0b, 0x44 },
                { ADV7611_I2CADDR_IO,   0x0c, 0x42 },
                { ADV7611_I2CADDR_IO,   0x14, 0x3f },
                { ADV7611_I2CADDR_IO,   0x15, 0xBE },

                /* LLC DLL enable */
                { ADV7611_I2CADDR_IO,   0x19, 0x83 },
               // { ADV7611_I2CADDR_IO,   0x19, 0xC0 },
                { ADV7611_I2CADDR_IO,   0x33, 0x40 },

                { ADV7611_I2CADDR_IO,   0xfd, channel->CP_ADDR << 1 },                
                { ADV7611_I2CADDR_IO,   0xf9, channel->KSV_ADDR << 1 },  
                { ADV7611_I2CADDR_IO,   0xfb, channel->HDMI_ADDR << 1 },  
                { ADV7611_I2CADDR_IO,   0xfa, channel->EDID_ADDR << 1 },
                
                /* Force HDMI free run */
                { ADV7611_I2CADDR_CP,   0xba, 0x01 },

                /* Disable HDCP 1.1*/
                { ADV7611_I2CADDR_KSV,  0x40, 0x81 },

                /* ADI recommended writes */
                { ADV7611_I2CADDR_HDMI, 0x9B, 0x03 },
                { ADV7611_I2CADDR_HDMI, 0xC1, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC2, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC3, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC4, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC5, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC6, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC7, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC8, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC9, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCA, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCB, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCC, 0x01 },

                { ADV7611_I2CADDR_HDMI, 0x00, 0x00 }, // Set HDMI port A
                { ADV7611_I2CADDR_HDMI, 0x83, 0xFE }, // Enable clock terminator for port A
                { ADV7611_I2CADDR_HDMI, 0x6F, 0x08 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x85, 0x1F }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x87, 0x70 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x8D, 0x04 }, // LFG
                { ADV7611_I2CADDR_HDMI, 0x8E, 0x1E }, // HFG
                { ADV7611_I2CADDR_HDMI, 0x1A, 0x8A }, // unmute audio
                { ADV7611_I2CADDR_HDMI, 0x57, 0xDA }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x58, 0x01 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x75, 0x10 }, // DDC drive strength

                { ADV7611_I2CADDR_NONE, 0, 0 },
        };

        u8 i2caddr;

	ch_client = v4l2_get_subdevdata(sd);

	v4l2_dbg(1, debug, sd, "Adv7611 driver registered\n");

	/*Configure the ADV7611 in default 720p 60 Hz standard for normal
	   power up mode */

        for ( init_cur=&init_sequence[0];
              err >=0 && init_cur->i2caddr != ADV7611_I2CADDR_NONE;
              init_cur++ ) {

                switch ( init_cur->i2caddr ) {
                case ADV7611_I2CADDR_IO:
                        i2caddr = channel->IO_ADDR;
                        break;
                case ADV7611_I2CADDR_DPLL:
                        i2caddr = channel->DPLL_ADDR;
                        break;
                case ADV7611_I2CADDR_CEC:
                        i2caddr = channel->CEC_ADDR;
                        break;
                case ADV7611_I2CADDR_INFOFRAME:
                        i2caddr = channel->INFOFRAME_ADDR;
                        break;
                case ADV7611_I2CADDR_KSV:
                        i2caddr = channel->KSV_ADDR;
                        break;
                case ADV7611_I2CADDR_EDID:
                        i2caddr = channel->EDID_ADDR;
                        break;
                case ADV7611_I2CADDR_HDMI:
                        i2caddr = channel->HDMI_ADDR;
                        break;
                case ADV7611_I2CADDR_CP:
                        i2caddr = channel->CP_ADDR;
                        break;
                default:
                        err = -EINVAL;
                        adv7611_deinitialize(sd);
                        return err;
                        break;
                }
                err = adv7611_i2c_write_reg(ch_client,
                                             i2caddr,
                                             init_cur->reg,
                                             init_cur->val);
        }


	if (err < 0) {
		err = -EINVAL;
		adv7611_deinitialize(sd);
		return err;
	}
	err = write_edid(sd);
	if (err < 0) {
		err = -EINVAL;
		adv7611_deinitialize(sd);
		return err;
	}
	v4l2_dbg(1, debug, sd, "End of adv7611_init.\n");
	return err;
}

static int adv7611_deinitialize(struct v4l2_subdev *sd)
{
        struct adv7611_channel *channel = (to_adv7611(sd));
	struct i2c_client      *ch_client = NULL;
        int                     err;

	ch_client = v4l2_get_subdevdata(sd);

	v4l2_dbg(1, debug, sd, "adv7611_deinitialize.\n");

        err = adv7611_i2c_write_reg(ch_client,
                                    channel->IO_ADDR,
                                    0x15,
                                    0xBE);

        return err;
}

static int adv7611_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT:
		return v4l2_ctrl_query_fill(qc,
                                            0,
                                            ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_MAX,
                                            1,
                                            ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_YUV422 );
	default:
                return -EINVAL;
	}

	return 0;
}

static int adv7611_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret = 0;
	struct i2c_client      *ch_client = NULL;
        struct adv7611_channel *ch;


        ch = to_adv7611(sd);

	ch_client = v4l2_get_subdevdata(sd);


	switch (ctrl->id) {
	case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT:
                ctrl->value = ch->output_format;
                break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int adv7611_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	u8 reg_io_02;
	u8 reg_io_03;
	u8 reg_io_05;
	int ret = 0;

	struct i2c_client      *ch_client = NULL;
        struct adv7611_channel *ch;


        ch = to_adv7611(sd);

	ch_client = v4l2_get_subdevdata(sd);


	switch (ctrl->id) {
	case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT:
                switch ( ctrl->value ) {
                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_YUV422:
                        reg_io_02 = 0xf5; // YUV colorspace
                        reg_io_03 = 0x80; // 16-bit SDR
                        reg_io_05 = 0x2c; // embedded syncs
                        break;
                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_RGB24:
                        reg_io_02 = 0xf7; // RGB colorspace
                        reg_io_03 = 0x40; // 24-bit SDR embedded syncs
                        reg_io_05 = 0x2c; // embedded syncs
                        break;

                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_RGB24_DISCRETE:
                        reg_io_02 = 0xf7; // RGB colorspace
                        reg_io_03 = 0x40; // 24-bit SDR embedded syncs
                        reg_io_05 = 0x28; // embedded syncs
                        break;

                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_YUV444:
                        reg_io_02 = 0xf5; // YUV colorspace
                        reg_io_03 = 0x40; // 24-bit SDR embedded syncs
                        reg_io_05 = 0x2c; // embedded syncs
                        break;

                default:
                        ret = -ERANGE;
                        break;
                }

                if ( 0 == ret ) {
                        ret = adv7611_i2c_write_reg(ch_client,
                                                    ch->IO_ADDR,
                                                    0x02,
                                                    reg_io_02);
                        ret |= adv7611_i2c_write_reg(ch_client,
                                                     ch->IO_ADDR,
                                                     0x03,
                                                     reg_io_03);
                        ret |= adv7611_i2c_write_reg(ch_client,
                                                     ch->IO_ADDR,
                                                     0x05,
                                                     reg_io_05);
                }

                ch->output_format = ctrl->value;

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}



/* adv7611_setstd :
 * Function to set the video standard
 */
static int adv7611_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	int err = 0;
        struct adv7611_channel *channel = to_adv7611(sd);
        struct i2c_client *ch_client = v4l2_get_subdevdata(sd);

        /* No support for forcing standard */

        (void) channel;
        (void) ch_client;

	v4l2_dbg(1, debug, sd, "Start of adv7611_setstd..\n");

	v4l2_dbg(1, debug, sd, "End of adv7611 set standard...\n");
	return err;
}


/* adv7611_querystd :
 * Function to return standard detected by decoder
 */
static int adv7611_querystd(struct v4l2_subdev *sd, v4l2_std_id *id)
{
	int err = 0;
	unsigned char val;
	unsigned short stdi_cp8l       = 0;      /* Block length - clocks per 8 lines */
        unsigned char  stdi_lcvs       = 0;      /* Line count in vertical sync */
        unsigned short stdi_lpf        = 0;       /* Lines per field */
        unsigned short stdi_cpfdiv256  = 0; /* Clocks per field div 256 */
        unsigned char  stdi_interlaced = 0;

        int detected      = 0;
        int gotformat     = 0;
        int formatretries = 4;
        struct i2c_client *ch_client    = v4l2_get_subdevdata(sd);
        struct adv7611_channel *channel = to_adv7611(sd);
        unsigned int fps_1000 = 0;

        struct {
                v4l2_std_id id;
                unsigned short       lpf_low;
                unsigned short       lpf_high;
                unsigned int         fps1000_low;
                unsigned int         fps1000_high;

        } *queryStdEntry, queryStdTable[ ] = {
                {V4L2_STD_1080P_25, 0x462, 0x465, 24800, 25200},
                {V4L2_STD_1080P_30, 0x462, 0x465, 29700, 30300},
                {V4L2_STD_1080P_50, 0x462, 0x465, 49500, 50500},
                {V4L2_STD_1080P_60|V4L2_STD_HD_DIV_1001, 0x462, 0x465, 59900, 59979},
                {V4L2_STD_1080P_60, 0x462, 0x465, 59400, 60600},
                {V4L2_STD_1080P_24, 0x462, 0x465, 23700, 24300},

                {V4L2_STD_1080I_60|V4L2_STD_HD_DIV_1001, 0x230, 0x233, 59900, 59979},
                {V4L2_STD_1080I_60, 0x230, 0x233, 59400, 60600},
                {V4L2_STD_1080I_50, 0x230, 0x233, 49500, 50500},

                {V4L2_STD_720P_60|V4L2_STD_HD_DIV_1001,  0x2ea, 0x2ee, 59900, 59979},
                {V4L2_STD_720P_60,  0x2ea, 0x2ee, 59400, 60600},
                {V4L2_STD_720P_50,  0x2ea, 0x2ee, 49500, 50500},


                {V4L2_STD_800x600_60,  627,   629, 59000, 61000 },
                {V4L2_STD_800x600_72,  665,   667, 71000, 73000 },
                {V4L2_STD_800x600_75,  624,   626, 74000, 76000 },
                {V4L2_STD_800x600_85,  630,   632, 84000, 86000 },

                {V4L2_STD_525P_60,     523,   525, 59000,  61000 },
                {V4L2_STD_625P_50,     623,   625, 49000,  51000 },
                {V4L2_STD_525_60,      260,   263, 59000,  61000 },
                {V4L2_STD_625_50,      310,   313, 49000,  51000 },

                {V4L2_STD_1024x768_60, 805,   807, 59000, 61000 },
                {V4L2_STD_1024x768_70, 805,   807, 69000, 71000 },
                {V4L2_STD_1024x768_75, 799,   801, 74000, 76000 },
                {V4L2_STD_1024x768_85, 807,   809, 84000, 86000 },

                {V4L2_STD_1280x600_60, 621,   623, 59000, 61000 },

                {V4L2_STD_1280x720_60, 745,   747, 59000, 61000 },
                {V4L2_STD_1280x720_75, 751,   753, 74000, 76000 },
                {V4L2_STD_1280x720_85, 755,   757, 84000, 86000 },

                {V4L2_STD_1280x768_60, 794,   796, 59000, 61000 },
                {V4L2_STD_1280x768_75, 801,   803, 74000, 76000 },
                {V4L2_STD_1280x768_85, 806,   808, 84000, 86000 },

                {V4L2_STD_1280x800_60, 822,   824, 59000, 61000 },

                {V4L2_STD_1280x1024_60, 1065, 1067, 59000, 61000 },
                {V4L2_STD_1280x1024_75, 1065, 1067, 74000, 76000 },
                {V4L2_STD_1280x1024_85, 1071, 1073, 84000, 86000 },

                {               0,      0,     0,     0,     0},
        };

	v4l2_dbg(1, debug, sd, "Starting querystd function...\n");
	if (id == NULL) {
		dev_err(&ch_client->dev, "NULL Pointer.\n");
		return -EINVAL;
	}


        err = adv7611_i2c_read_reg(ch_client,
                                   channel->CP_ADDR,
                                   0xb1,
                                   &val);

        if (err < 0) {
                dev_err(&ch_client->dev,
                        "I2C read fails...sync detect\n");
                return err;
        }

        detected = (val & 0x80);
        stdi_interlaced = (val & 0x40) ? 1 : 0;
        stdi_cp8l = (val&0x3f);
        stdi_cp8l <<= 8;

        if ( !detected ) {
                v4l2_dbg( 1, debug, sd, "No sync detected\n");
                return -EIO;
        }


        do {
                /* Query clock cycles per 8 lines */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb2, &val);
                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...Lines per frame high\n");
                        return err;
                }

                stdi_cp8l |= (val&0xff);


                /* Query line count in vertical sync */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb3, &stdi_lcvs);
                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...Lines per frame low\n");
                        return err;
                }
                stdi_lcvs >>= 3;
                stdi_lcvs &= 0x1f;


                /* Query lines per field */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xa3, &val);
                stdi_lpf = (val& 0xf);
                stdi_lpf <<= 8;

                err |= adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xa4, &val);
                stdi_lpf |= val;

                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...Lines per field\n");
                        return err;
                }

                /* Query clocks per field div 256 */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb8, &val);
                stdi_cpfdiv256 = (val& 0x1f);
                stdi_cpfdiv256 <<= 8;

                err |= adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb9, &val);
                stdi_cpfdiv256 |= val;

                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...CPF div 256\n");
                        return err;
                }


                fps_1000 = (28636360/256)*1000;
                if ( stdi_cpfdiv256 != 0 ) {
                        fps_1000 /= stdi_cpfdiv256;
                }

                for ( queryStdEntry = &queryStdTable[0];
                      queryStdEntry->id != 0 ;
                      queryStdEntry++ ) {

                        dev_dbg(&ch_client->dev,
                                "entry %d %u-%u %u-%u\n",
                                queryStdEntry - &queryStdTable[0],
                                queryStdEntry->lpf_low,
                                queryStdEntry->lpf_high,
                                queryStdEntry->fps1000_low,
                                queryStdEntry->fps1000_high );

                        if ( (queryStdEntry->lpf_low <= stdi_lpf)
                             && (queryStdEntry->lpf_high >= stdi_lpf )
                             && (queryStdEntry->fps1000_low <= fps_1000)
                             && (queryStdEntry->fps1000_high >= fps_1000) ) {

                                *id = queryStdEntry->id;
                                gotformat = 1;
                                break;
                        }
                }


                if ( !gotformat ) {
                        /* VSYNC ctr may take some time to converge */
                        msleep(50);
                }

        } while ( gotformat == 0 && --formatretries > 0 ) ;

	dev_notice(&ch_client->dev,
		   "ADV7611 - interlaced=%d lines per field=%d clocks per 8 lines=%d fps=%u.%03u\n",
                   (int)stdi_interlaced,
                   (int)stdi_lpf, (int)stdi_cp8l, fps_1000/1000, fps_1000%1000);

        if ( !gotformat ) {
		dev_notice(&ch_client->dev, "querystd: No std detected\n" );
                return -EINVAL;
	}

        err = 0;

	v4l2_dbg(1, debug, sd, "End of querystd function.\n");
	return err;
}


/* adv7611_s_stream:
 *
 *     Enable streaming.
 *
 *     For video decoder, this means driving the output bus,
 *     which may be shared with other video decoders.
 */
static int adv7611_s_stream(struct v4l2_subdev *sd, int enable)
{
        struct i2c_client *ch_client = v4l2_get_subdevdata(sd);
        struct adv7611_channel *channel = to_adv7611(sd);
        int err = 0;

        v4l2_dbg(1, debug, sd, "s_stream %d\n", enable);

//        if (channel->streaming == enable)
//                return 0;

        if ( enable ) {
                err = adv7611_i2c_write_reg(ch_client,
                                            channel->IO_ADDR,
                                            0x15,
                                            0xA0);
        } else {
                err = adv7611_i2c_write_reg(ch_client,
                                            channel->IO_ADDR,
                                            0x15,
                                            0xBE);
        }

        if (err) {
                v4l2_err(sd, "s_stream: err %d\n", err);
        } else {
                channel->streaming = enable;
        }

        return err;
}


/* adv7611_i2c_read_reg :This function is used to read value from register
 * for i2c client.
 */
static int adv7611_i2c_read_reg(struct i2c_client *client, u8 addr, u8 reg, u8 * val)
{
	int err = 0;
        int retries = 5;

	struct i2c_msg msg[2];
	unsigned char writedata[1];
	unsigned char readdata[1];

	if (!client->adapter) {
		err = -ENODEV;
	} else {

             do {
		msg[0].addr = addr;
		msg[0].flags = 0;
		msg[0].len = 1;
		msg[0].buf = writedata;
		writedata[0] = reg;

		msg[1].addr = addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = 1;
		msg[1].buf = readdata;

		err = i2c_transfer(client->adapter, msg, 2);
                if (err >= 2) {
                        *val = readdata[0];
		} else {
                        msleep(10);
                        v4l2_warn(client, "Read: retry ... %d\n", retries);
                }
             } while ( err < 2 && --retries > 0);
	}
        if ( err < 0 ) {
                dev_err( &client->adapter->dev, "ADV7611: read addr %02x reg x%02x failed\n", (int) addr, (int) reg );
        } else {
                v4l_dbg( 2, debug, client, "ADV7611: read addr %02x reg x%02x val %02x\n", (int) addr, (int)reg, (int)(*val) );
        }

	return ((err < 0) ? err : 0);
}

/* adv7611_i2c_write_reg :This function is used to write value into register
 * for i2c client.
 */
static int adv7611_i2c_write_reg(struct i2c_client *client, u8 addr, u8 reg, u8 val)
{
	int err = 0;

        int retries = 3;

	struct i2c_msg msg[1];
	unsigned char data[2];
	if (!client->adapter) {
		err = -ENODEV;
	} else {
             do {
		msg->addr = addr;
		msg->flags = 0;
		msg->len = 2;
		msg->buf = data;
		data[0] = reg;
		data[1] = val;
		err = i2c_transfer(client->adapter, msg, 1);
                if ( err < 0 ) {
                     msleep(10);
                     v4l_warn(client, "Write: retry ... %d\n", retries);
                }
             } while ( err < 0 && --retries > 0);
	}

        if ( err < 0
             && client->adapter != NULL ) {
             dev_err( &client->adapter->dev,
                      "adv7611 i2c write failed: addr %02x reg x%02x value x%02x\n",
                      (unsigned int)addr,
                      (unsigned int)reg,
                      (unsigned int)val
                  );
        }

        v4l_dbg( 2, debug, client, "ADV7611: write addr %02x reg x%02x val %02x\n", (int) addr, (int)reg, (int)val );

	return ((err < 0) ? err : 0);
}


/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/

static int adv7611_probe(struct i2c_client *c,
			 const struct i2c_device_id *id)
{
	struct adv7611_channel *core;
	struct v4l2_subdev *sd;
        int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(c->adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	core = kzalloc(sizeof(struct adv7611_channel), GFP_KERNEL);
	if (!core) {
		return -ENOMEM;
	}


	sd = &core->sd;
	v4l2_i2c_subdev_init(sd, c, &adv7611_ops);
	v4l_info(c, "chip found @ 0x%02x (%s)\n",
		 c->addr << 1, c->adapter->name);

        core->IO_ADDR = c->addr;
        if ( core->IO_ADDR == 0x4c ) {
                core->DPLL_ADDR = 0x3F;
		core->CEC_ADDR  = 0x40;
		core->INFOFRAME_ADDR = 0x3E;
		core->KSV_ADDR  = 0x32;
		core->EDID_ADDR = 0x36;
		core->HDMI_ADDR = 0x34;
		core->CP_ADDR   = 0x22;
        }
        else if ( core->IO_ADDR == 0x4d ) {
                core->DPLL_ADDR = 0x45;
		core->CEC_ADDR  = 0x46;
		core->INFOFRAME_ADDR = 0x47;
		core->KSV_ADDR  = 0x48;
		core->EDID_ADDR = 0x49;
		core->HDMI_ADDR = 0x4a;
		core->CP_ADDR   = 0x4b;
        }
        else {
                ret = -EINVAL;
                v4l_err(c, "adv7611 invalid I2C address %02x\n", c->addr );
                kfree(core);
                return ret;
        }


        ret = adv7611_initialize(sd);
        if ( ret != 0 ) {
             v4l_err(c, "adv7611 init failed, code %d\n", ret );
             kfree(core);
             return ret;
        }

	return ret;
}

static int adv7611_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);

#ifdef DEBUG
 	v4l_info(c,
		"adv7611.c: removing adv7611 adapter on address 0x%x\n",
		c->addr << 1);
#endif

	v4l2_device_unregister_subdev(sd);
//	kfree(to_adv7611(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id adv7611_id[] = {
	{ "adv7611", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7611_id);

static struct i2c_driver adv7611_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "adv7611",
	},
	.probe		= adv7611_probe,
	.remove		= adv7611_remove,
	.id_table	= adv7611_id,
};

static __init int init_adv7611(void)
{
	return i2c_add_driver(&adv7611_driver);
}

static __exit void exit_adv7611(void)
{
	i2c_del_driver(&adv7611_driver);
}

module_init(init_adv7611);
module_exit(exit_adv7611);

MODULE_DESCRIPTION("Analog Devices ADV7611 video decoder driver");
MODULE_AUTHOR("John Whittington");
MODULE_LICENSE("GPL");
