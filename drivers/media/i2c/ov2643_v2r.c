/*
 * Driver for MT9P031 CMOS Image Sensor from Micron, for TI Davinci platform
 *
 * Copyright (C) 2008, Guennadi Liakhovetski,
 * DENX Software Engineering <lg@denx.de>
 *
 * Heavily based on MT9T031 driver from Guennadi Liakhovetski
 * made changes to support TI Davinci platform
 * Copyright (C) 2010, Leopard Imaging, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/davinci/videohd.h>

/* mt9p031 i2c address 0x5d
 * The platform has to define i2c_board_info
 * and call i2c_register_board_info() */

/* mt9p031 selected register addresses */
#define MT9P031_CHIP_VERSION		0x00
#define MT9P031_ROW_START		0x01
#define MT9P031_COLUMN_START		0x02
#define MT9P031_WINDOW_HEIGHT		0x03
#define MT9P031_WINDOW_WIDTH		0x04
#define MT9P031_HORIZONTAL_BLANKING	0x05
#define MT9P031_VERTICAL_BLANKING	0x06
#define MT9P031_OUTPUT_CONTROL		0x07
#define MT9P031_SHUTTER_WIDTH_UPPER	0x08
#define MT9P031_SHUTTER_WIDTH		0x09
#define MT9P031_PIXEL_CLOCK_CONTROL	0x0a
#define MT9P031_FRAME_RESTART		0x0b
#define MT9P031_SHUTTER_DELAY		0x0c
#define MT9P031_RESET			0x0d
#define MT9P031_READ_MODE_1		0x1e
#define MT9P031_READ_MODE_2		0x20
#define MT9P031_READ_MODE_3		0x21
#define MT9P031_ROW_ADDRESS_MODE	0x22
#define MT9P031_COLUMN_ADDRESS_MODE	0x23
#define MT9P031_GLOBAL_GAIN		0x35
#define MT9P031_CHIP_ENABLE		0xF8

#define MT9P031_MAX_HEIGHT		1536
#define MT9P031_MAX_WIDTH		2048
#define MT9P031_MIN_HEIGHT		2
#define MT9P031_MIN_WIDTH		2
#define MT9P031_HORIZONTAL_BLANK	0
#define MT9P031_VERTICAL_BLANK	0
#define MT9P031_COLUMN_SKIP		32
#define MT9P031_ROW_SKIP		20
#define MT9P031_DEFAULT_WIDTH		1920
#define MT9P031_DEFAULT_HEIGHT		1080

#define V4L2_STD_MT9P031_STD_ALL  (V4L2_STD_720P_30)

#define MT9P031_BUS_PARAM	(SOCAM_PCLK_SAMPLE_RISING |	\
	SOCAM_PCLK_SAMPLE_FALLING | SOCAM_HSYNC_ACTIVE_HIGH |	\
	SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_DATA_ACTIVE_HIGH |	\
	SOCAM_MASTER | SOCAM_DATAWIDTH_10)

v4l2_std_id mt9p031_cur_std = V4L2_STD_720P_30;
/* Debug functions */
static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static const struct v4l2_fmtdesc mt9p031_formats[] = {
	{
		.index = 0,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.description = "Bayer (sRGB) 10 bit",
		.pixelformat = V4L2_PIX_FMT_SGRBG10,
	},
};
static const unsigned int mt9p031_num_formats = ARRAY_SIZE(mt9p031_formats);

static const struct v4l2_queryctrl mt9p031_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Vertically",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Horizontally",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}
};
static const unsigned int mt9p031_num_controls = ARRAY_SIZE(mt9p031_controls);

struct mt9p031 {
	struct v4l2_subdev sd;
	int model;	/* V4L2_IDENT_MT9P031* codes from v4l2-chip-ident.h */
	unsigned char autoexposure;
	u16 xskip;
	u16 yskip;
	u32 width;
	u32 height;
	unsigned short x_min;           /* Camera capabilities */
	unsigned short y_min;
	unsigned short x_current;       /* Current window location */
	unsigned short y_current;
	unsigned short width_min;
	unsigned short width_max;
	unsigned short height_min;
	unsigned short height_max;
	unsigned short y_skip_top;      /* Lines to skip at the top */
	unsigned short gain;
	unsigned short exposure;
};

static inline struct mt9p031 *to_mt9p031(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9p031, sd);
}
static int reg_read(struct i2c_client *client, const u8 reg)
{
	s32 data;
	data = i2c_smbus_read_byte_data(client, reg);
#ifdef CONFIG_V2R_DEBUG
	printk("\nREAD CAMERA I2C address=0x%x, register=0x%x, res=0x%x\n", client->addr, reg, data);
#endif
	return data;
}

static int reg_write(struct i2c_client *client, const u8 reg,
	const u8 data)
{
	int ret;
	//ret = reg_read(client, reg);
#ifdef CONFIG_V2R_DEBUG
	printk("\n***Register:0x%x actualvalue:0x%x, Value to be write:0x%x",reg,ret,data);
	printk("\nWRITE CAMERA I2C address=0x%x, register=0x%x, res=0x%x\n", client->addr, reg, data);
#endif
	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (reg == 0x12) msleep(5);
	return ret;
}


struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};
static struct regval_list ov2643_default_regs[] = 
{
		//;pclk=72mhz,30fps/pclk=36mhz,15fps
		//
		{0x12,0x80},
		{0xc3,0xff},
		{0xc4,0xff},
		//{0x3d,0x48},
		//{0xdd,0xa5},
		//;widws setup
		{0x20,0x01},//0x01,//
		{0x21,0x25},//0x60,//
		{0x22,0x00},
		{0x23,0x0c},
		{0x24,0x50},//;0x500=1280
		{0x25,0x04},
		{0x26,0x2d},//;0x2d0=720
		{0x27,0x04},
		{0x28,0x42},
		{0x12,0x40},
		//{0x29,0x06},//;dummy pixels //24.75M 0x29,0x06//24M 0x29,0x06,//
		//{0x2a,0x40},                //24.75M 0x2a,0x72//24M 0x2a,0x40,//
		//{0x2b,0x02},//;dummy lines  //24.75M 0x2b,0x02//24M 0x2b,0x02,//
		//{0x2c,0xee},                //24.75M 0x2c,0xee//24M 0x2c,0xee,//
		//for25fps 0x2ee*1.2=0x384
		//{0x1c,0x25},//vsync width
		//{0x1d,0x02},//0x04,//0x02,
		//{0x1e,0x00},
		//{0x1f,0xe1},
		{ 0xff, 0xff },	/* END MARKER */
};
static int ov2643_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	while (vals->reg_num != 0xff || vals->value != 0xff) {
		int ret = reg_write(client, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}
static int mt9p031_init(struct v4l2_subdev *sd, u32 val)
{
	return ov2643_write_array(sd, ov2643_default_regs);
}


static int mt9p031_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}


const struct v4l2_queryctrl *mt9p031_find_qctrl(u32 id)
{
	int i;

	for (i = 0; i < mt9p031_num_controls; i++) {
		if (mt9p031_controls[i].id == id)
			return &mt9p031_controls[i];
	}
	return NULL;
}

static int mt9p031_set_params(struct v4l2_subdev *sd,
			      struct v4l2_rect *rect, u16 xskip, u16 yskip)
{
	return 0;
}

static int mt9p031_get_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width		= 1280;
	pix->height		= 720;
	pix->pixelformat	= V4L2_PIX_FMT_SGRBG10;
	pix->field		= V4L2_FIELD_NONE;
	pix->colorspace		= V4L2_COLORSPACE_SRGB;
	return 0;
}
static int mt9p031_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_format *f)
{
	return 0;
}

static int mt9p031_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->height < 0)
		pix->height = 0;
	if (pix->height > 720)
		pix->height = 720;
	if (pix->width < 0)
		pix->width = 0;
	if (pix->width > 1280)
		pix->width = 1280;
	pix->width &= ~0x01; /* has to be even */
	pix->height &= ~0x01; /* has to be even */
	return 0;
}

static int mt9p031_get_chip_id(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct mt9p031 *mt9p031 = to_mt9p031(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);;

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != client->addr)
		return -ENODEV;

	id->ident	= mt9p031->model;
	id->revision	= 0;

	return 0;
}



static int mt9p031_get_control(struct v4l2_subdev *, struct v4l2_control *);
static int mt9p031_set_control(struct v4l2_subdev *, struct v4l2_control *);
static int mt9p031_queryctrl(struct v4l2_subdev *, struct v4l2_queryctrl *);
static int mt9p031_querystd(struct v4l2_subdev *sd, v4l2_std_id *id);
static int mt9p031_set_standard(struct v4l2_subdev *sd, v4l2_std_id id);

static const struct v4l2_subdev_core_ops mt9p031_core_ops = {
	.g_chip_ident = mt9p031_get_chip_id,
	.init = mt9p031_init,
	.queryctrl = mt9p031_queryctrl,
	.g_ctrl	= mt9p031_get_control,
	.s_ctrl	= mt9p031_set_control,
    .s_std     =  mt9p031_set_standard,
};

static const struct v4l2_subdev_video_ops mt9p031_video_ops = {
	.s_fmt = mt9p031_set_fmt,
    .g_fmt = mt9p031_get_fmt,
	.try_fmt = mt9p031_try_fmt,
	.querystd = mt9p031_querystd,
	.s_stream = mt9p031_s_stream,
};

static const struct v4l2_subdev_ops mt9p031_ops = {
	.core = &mt9p031_core_ops,
	.video = &mt9p031_video_ops,
};

static int mt9p031_queryctrl(struct v4l2_subdev *sd,
			    struct v4l2_queryctrl *qctrl)
{
	const struct v4l2_queryctrl *temp_qctrl;

	temp_qctrl = mt9p031_find_qctrl(qctrl->id);
	if (!temp_qctrl) {
		v4l2_err(sd, "control id %d not supported", qctrl->id);
		return -EINVAL;
	}
	memcpy(qctrl, temp_qctrl, sizeof(*qctrl));
	return 0;
}

static int mt9p031_get_control(struct v4l2_subdev *sd,
			       struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9p031 *mt9p031 = to_mt9p031(sd);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		ctrl->value = false;
		break;
	case V4L2_CID_HFLIP:
		ctrl->value = false;
		break;
	};
	return 0;
}

static int mt9p031_set_control(struct v4l2_subdev *sd,
			       struct v4l2_control *ctrl)
{

	return 0;
}

/* Function querystd not supported by mt9p031 */
static int mt9p031_querystd(struct v4l2_subdev *sd, v4l2_std_id *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9p031 *mt9p031 = to_mt9p031(sd);

	*id = V4L2_STD_MT9P031_STD_ALL;

	return 0;
}

/* Function set not supported by mt9p031 */
static int mt9p031_set_standard(struct v4l2_subdev *sd, v4l2_std_id id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9p031 *mt9p031 = to_mt9p031(sd);

    mt9p031_cur_std = id;

	return 0;
}
#define REG_MIDH			0x1c	
#define REG_MIDL			0x1d
#define REG_PIDH			0x0a	
#define REG_PIDL			0x0b	
/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int mt9p031_detect(struct i2c_client *client, int *model)
{
	unsigned char v;
	/*
	 * OK, we know we have an OmniVision chip...but which one?
	 */
	v = reg_read(client, REG_PIDH);
	printk("\n***Detect:0x%x\n",v);
	if (v != 0x26) return -ENODEV;;
	v = reg_read(client, REG_PIDL);
	printk("\n***Detect:0x%x\n",v);
	if (v != 0x43) return -ENODEV;
	printk("\nOV2643 successfully detected\n");
	*model = V4L2_IDENT_MT9P031;
	dev_info(&client->dev, "Detected a OV2643 chip ID\n");
    return 0;
    
}

static int mt9p031_probe(struct i2c_client *client,
	const struct i2c_device_id *did)
{
	struct mt9p031 *mt9p031;
	struct v4l2_subdev *sd;
	int pclk_pol;
	int ret;

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "No platform data!!\n");
		return -ENODEV;
	}

	pclk_pol = (int)client->dev.platform_data;

	mt9p031 = kzalloc(sizeof(struct mt9p031), GFP_KERNEL);
	if (!mt9p031)
		return -ENOMEM;

	ret = mt9p031_detect(client, &mt9p031->model);
	if (ret)
		goto clean;

	mt9p031->x_min      = 0;
	mt9p031->y_min      = 0;
	mt9p031->width      = 1280;
	mt9p031->height     = 720;
	mt9p031->x_current  = 0;
	mt9p031->y_current  = 0;
	mt9p031->width_min  = 1280;
	mt9p031->width_max  = 1280;
	mt9p031->height_min = 720;
	mt9p031->height_max = 720;
	mt9p031->y_skip_top = 10; //Originally it had 10, minimun value(6)which works
	mt9p031->autoexposure = 1;
	mt9p031->xskip = 1;
	mt9p031->yskip = 1;

	/* Register with V4L2 layer as slave device */
	sd = &mt9p031->sd;
	v4l2_i2c_subdev_init(sd, client, &mt9p031_ops);

	ret = mt9p031_init(sd,1);
	v4l2_info(sd, "%s decoder driver registered !!\n", sd->name);
	return 0;

clean:
	kfree(mt9p031);
	return ret;
}
static int mt9p031_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9p031 *mt9p031 = to_mt9p031(sd);

	v4l2_device_unregister_subdev(sd);

	kfree(mt9p031);
	return 0;
}

static const struct i2c_device_id mt9p031_id[] = {
	{ "mt9p031", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9p031_id);

static struct i2c_driver mt9p031_i2c_driver = {
	.driver = {
		.name = "mt9p031",
	},
	.probe		= mt9p031_probe,
	.remove		= mt9p031_remove,
	.id_table	= mt9p031_id,
};

static int __init mt9p031_mod_init(void)
{
	return i2c_add_driver(&mt9p031_i2c_driver);
}

static void __exit mt9p031_mod_exit(void)
{
	i2c_del_driver(&mt9p031_i2c_driver);
}

module_init(mt9p031_mod_init);
module_exit(mt9p031_mod_exit);

MODULE_DESCRIPTION("Micron MT9P031 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <lg@denx.de>");
MODULE_LICENSE("GPL v2");
