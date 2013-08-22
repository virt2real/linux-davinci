/*
 * Driver for MT9P031 CMOS Image Sensor from Aptina
 *
 * Copyright (C) 2011, Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Copyright (C) 2011, Javier Martin <javier.martin@vista-silicon.com>
 * Copyright (C) 2011, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * Based on the MT9V032 driver and Bastian Hecht's code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-chip-ident.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>


#define OV2643_PIXEL_ARRAY_WIDTH			1280
#define OV2643_PIXEL_ARRAY_HEIGHT			720

#define MEDIA_PAD_FL_SINK		(1 << 0)
#define MEDIA_PAD_FL_SOURCE		(1 << 1)

struct ov2643_reg {
     unsigned char address;
     unsigned char value;
};
static struct ov2643_reg ov2643_init_regs[] =
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
struct ov2643_platform_data {
	int (*set_xclk)(struct v4l2_subdev *subdev, int hz);
	int reset;
	int ext_freq;
	int target_freq;
};

enum ov2643_model {
	OV2643_MODEL_COLOR,
};

struct ov2643 {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_rect crop;  /* Sensor window */
	struct v4l2_mbus_framefmt format;
	struct ov2643_platform_data *pdata;
	struct mutex power_lock; /* lock to protect power_count */
	int power_count;
	int reset;
	struct v4l2_ctrl_handler ctrls;

};
struct ov2643_framesize {
	u16 width;
	u16 height;
	const u8 *regs;
};
/* Supported resolutions */
#define UXGA_WIDTH		1600
#define UXGA_HEIGHT		1200
#define HD720_WIDTH 	1280
#define HD720_HEIGHT	720
#define VGA_WIDTH		640
#define VGA_HEIGHT	480
#define QVGA_WIDTH	320
#define QVGA_HEIGHT	240
#define CIF_WIDTH		352
#define CIF_HEIGHT	288
#define QCIF_WIDTH	176
#define	QCIF_HEIGHT	144
static const struct ov2643_framesize ov2643_framesizes[] = {
//	{
//		.width		= UXGA_WIDTH,
//		.height		= UXGA_HEIGHT,
//		.regs		= ov2643_uxga_regs,
//	},
	{
		.width		= HD720_WIDTH,
		.height		= HD720_HEIGHT,
		//.regs		= ov2643_hd720_regs,
	},
//	{
//		.width		= VGA_WIDTH,
//		.height		= VGA_HEIGHT,
//		.regs		= ov2643_vga_regs,
//	},
//	{
//		.width		= QVGA_WIDTH,
//		.height		= QVGA_HEIGHT,
//		.regs		= ov2643_qvga_regs,
//	},
};

static struct ov2643 *to_ov2643(struct v4l2_subdev *sd){
	printk("to_ov2543\r\n");
	return container_of(sd, struct ov2643, subdev);
}

static u8 ov2643_read(struct i2c_client *client, u8 reg){
	u8 ret = i2c_smbus_read_byte_data(client, reg);
	printk("OV2643 i2c read: address=%x, value=%x\r\n", reg, ret);
	return ret;
}

static int ov2643_write(struct i2c_client *client, u8 reg, u8 data){
	printk("OV2643 i2c write: address=%x, value=%x\r\n", reg, data);
	return i2c_smbus_write_byte_data(client, reg, data);
}

static void ov2643_write_array(struct i2c_client *client, struct ov2643_reg *reg){
	u32 error = 0;
	printk("OV2643 write array\r\n");
	if (!reg) return;
	while (1){
	   if (reg->address != 0xFF && reg->value != 0xFF) break;
	   if (reg->address == 0x12 && reg->value == 0x80) msleep(50);
	   if ((error = ov2643_write(client, reg->address, reg->value)) != 0){
		   printk("OV2643: Failed to write %x %x, error = %x\r\n",  reg->address, reg->value, error);
	   }
	}
}

static int ov2643_reset(struct ov2643 *ov2643){
	printk("OV2643 reset\r\n");
	return 0;
}


static int ov2643_power_on(struct ov2643 *ov2643){
	printk("OV2643 power on\r\n");
	return 0;
}

static void ov2643_power_off(struct ov2643 *ov2643){
	printk("OV2643 power off\r\n");
}

static int __ov2643_set_power(struct ov2643 *ov2643, bool on){
	int ret;

	struct i2c_client *client = v4l2_get_subdevdata(&ov2643->subdev);
	printk("OV2643 set power %s\r\n", on ? "on" : "off");
	if (!on) {
		ov2643_power_off(ov2643);
		return 0;
	}

	ret = ov2643_power_on(ov2643);
	if (ret < 0)
		return ret;

	ret = ov2643_reset(ov2643);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to reset the camera\n");
		return ret;
	}

	return v4l2_ctrl_handler_setup(&ov2643->ctrls);;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static int ov2643_set_params(struct ov2643 *ov2643){
	//Set params for working mode
	struct i2c_client *client = v4l2_get_subdevdata(&ov2643->subdev);
	printk("OV2643 set params\r\n");
	ov2643_write_array(client, ov2643_init_regs);
	return 0;
}

static int ov2643_s_stream(struct v4l2_subdev *subdev, int enable){
	struct ov2643 *ov2643 = to_ov2643(subdev);
	int ret = 0;
	printk("OV2643 s_stream\r\n");
	if (!enable) {
		/* Stop sensor readout
		 * Here we should set default state and stop camera operating
		 * reset or something
		 */
		if (ret < 0) return ret;
		return ret;
	}

	ret = ov2643_set_params(ov2643);
	if (ret < 0) return ret;
	/* Switch to master "normal" mode */
	return ret;
}

static int ov2643_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code){
	struct ov2643 *ov2643 = to_ov2643(subdev);
	printk("OV2643 enum bus code\r\n");
	if (code->pad || code->index)
		return -EINVAL;

	code->code = ov2643->format.code;
	printk("OV2643 enum bus code returned %x\r\n", code->code);
	return 0;
}

static int ov2643_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse){
	struct ov2643 *ov2643 = to_ov2643(subdev);
	printk("OV2643 enum frame size\r\n");
	if (fse->index > ARRAY_SIZE(ov2643_framesizes)
			|| fse->code != ov2643->format.code) return -EINVAL;

	fse->min_width  = ov2643_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = ov2643_framesizes[fse->index].height;
	fse->min_height = fse->max_height;
	printk("enum frame size index=%d, %d, %d, %d, %d\r\n",
			fse->index, fse->min_width, fse->max_width, fse->max_height, fse->min_height);
	return 0;
}

static struct v4l2_mbus_framefmt *
__ov2643_get_pad_format(struct ov2643 *ov2643, struct v4l2_subdev_fh *fh,
			 unsigned int pad, u32 which)
{
	printk("OV2643 __ov2643_get_pad_format\r\n");
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov2643->format;
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__ov2643_get_pad_crop(struct ov2643 *ov2643, struct v4l2_subdev_fh *fh,
		     unsigned int pad, u32 which)
{
	printk("OV2643 __ov2643_get_pad_crop\r\n");
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov2643->crop;
	default:
		return NULL;
	}
}

static int ov2643_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct ov2643 *ov2643 = to_ov2643(subdev);
	printk("OV2643 ov2643_get_format\r\n");
	fmt->format = *__ov2643_get_pad_format(ov2643, fh, fmt->pad,
						fmt->which);
	return 0;
}

static int ov2643_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *format)
{
	struct ov2643 *ov2643 = to_ov2643(subdev);
	struct v4l2_mbus_framefmt *__format;
	printk("OV2643 ov2643_set_format\r\n");
    //Review this code to support different window sizes
	__format = __ov2643_get_pad_format(ov2643, fh, format->pad,
					    format->which);
	__format->width = 1280;
	__format->height = 720;

	format->format = *__format;

	return 0;
}

static int ov2643_get_crop(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh,
			    struct v4l2_subdev_crop *crop)
{
	struct ov2643 *ov2643 = to_ov2643(subdev);
	printk("OV2643 ov2643_get_crop\r\n");
	crop->rect = *__ov2643_get_pad_crop(ov2643, fh, crop->pad,
					     crop->which);
	return 0;
}

static int ov2643_set_crop(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh,
			    struct v4l2_subdev_crop *crop)
{
	struct ov2643 *ov2643 = to_ov2643(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	struct v4l2_rect rect;
	printk("OV2643 ov2643_set_crop\r\n");
	/* Clamp the crop rectangle boundaries and align them to a multiple of 2
	 * pixels to ensure a GRBG Bayer pattern.
	 */
	rect.left = clamp(ALIGN(crop->rect.left, 2), 2,
			  1600);
	rect.top = clamp(ALIGN(crop->rect.top, 2), 2,
			 1200);
	rect.width = clamp(ALIGN(crop->rect.width, 2),
			   2,
			   1600);
	rect.height = clamp(ALIGN(crop->rect.height, 2),
			    2,
			    1200);

	rect.width = min(rect.width, 1600 - rect.left);
	rect.height = min(rect.height, 1200 - rect.top);

	__crop = __ov2643_get_pad_crop(ov2643, fh, crop->pad, crop->which);

	if (rect.width != __crop->width || rect.height != __crop->height) {
		/* Reset the output image size if the crop rectangle size has
		 * been modified.
		 */
		__format = __ov2643_get_pad_format(ov2643, fh, crop->pad,
						    crop->which);
		__format->width = rect.width;
		__format->height = rect.height;
	}

	*__crop = rect;
	crop->rect = rect;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev control operations
 */

static int ov2643_s_ctrl(struct v4l2_ctrl *ctrl)
{
	printk("OV2643 ov2643_s_ctrl\r\n");
	return 0;
}

//static struct v4l2_ctrl_ops ov2643_ctrl_ops = {
//	.s_ctrl = ov2643_s_ctrl,
//};
static const struct v4l2_ctrl_config ov2643_ctrls[] = {
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int ov2643_set_power(struct v4l2_subdev *subdev, int on)
{
	struct ov2643 *ov2643 = to_ov2643(subdev);
	int ret = 0;
	printk("OV2643 ov2643_set_power\r\n");
	mutex_lock(&ov2643->power_lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (ov2643->power_count == !on) {
		ret = __ov2643_set_power(ov2643, !!on);
		if (ret < 0)
			goto out;
	}

	/* Update the power count. */
	ov2643->power_count += on ? 1 : -1;
	WARN_ON(ov2643->power_count < 0);

out:
	mutex_unlock(&ov2643->power_lock);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

static int ov2643_registered(struct v4l2_subdev *subdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	//struct ov2643 *ov2643 = to_ov2643(subdev);
	s32 data;
	int ret = 0;
	printk("OV2643 ov2643_registered\r\n");
	/* Read out the chip version register */
	data = ov2643_read(client, 0x0a);
	if (data != 0x26) {
		dev_err(&client->dev, "OV2643 not detected, wrong version "
			"0x%04x\n", data);
		return -ENODEV;
	}
	data = ov2643_read(client, 0x0b);
	if (data != 0x43) {
		dev_err(&client->dev, "OV2643 not detected, wrong version "
			"0x%04x\n", data);
		return -ENODEV;
	}
	printk("OV2643 detected at address 0x%02x\n", client->addr);

	return ret;
}

static int ov2643_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	//struct ov2643 *ov2643 = to_ov2643(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	printk("OV2643 ov2643_open\r\n");
	crop = v4l2_subdev_get_try_crop(fh, 0);
	crop->left = 0;
	crop->top = 0;
	crop->width = 1600;
	crop->height = 1200;

	format = v4l2_subdev_get_try_format(fh, 0);

	format->code = V4L2_MBUS_FMT_SGRBG12_1X12;

	format->width = 1280;
	format->height = 720;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int ov2643_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	printk("OV2643 ov2643_close\r\n");
	return 0;//mt9p031_set_power(subdev, 0);
}

static struct v4l2_subdev_core_ops ov2643_subdev_core_ops = {
	.s_power        = ov2643_set_power,
};

static struct v4l2_subdev_video_ops ov2643_subdev_video_ops = {
	.s_stream       = ov2643_s_stream,
};

static struct v4l2_subdev_pad_ops ov2643_subdev_pad_ops = {
	.enum_mbus_code = ov2643_enum_mbus_code,
	.enum_frame_size = ov2643_enum_frame_size,
	.get_fmt = ov2643_get_format,
	.set_fmt = ov2643_set_format,
	.get_crop = ov2643_get_crop,
	.set_crop = ov2643_set_crop,
};

static struct v4l2_subdev_ops ov2643_subdev_ops = {
	.core   = &ov2643_subdev_core_ops,
	.video  = &ov2643_subdev_video_ops,
	.pad    = &ov2643_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov2643_subdev_internal_ops = {
	.registered = ov2643_registered,
	.open = ov2643_open,
	.close = ov2643_close,
};

/* -----------------------------------------------------------------------------
 * Driver initialization and probing
 */

static int ov2643_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct ov2643_platform_data *pdata = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct ov2643 *ov2643;
//	unsigned int i;
	int ret;
	printk("OV2643 ov2643_probe\r\n");
	if (pdata == NULL) {
		dev_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&client->dev,
			"I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	ov2643 = kzalloc(sizeof(*ov2643), GFP_KERNEL);
	if (ov2643 == NULL)
		return -ENOMEM;

	ov2643->pdata = pdata;
	ov2643->reset = -1;

	v4l2_ctrl_handler_init(&ov2643->ctrls, ARRAY_SIZE(ov2643_ctrls));


	ov2643->subdev.ctrl_handler = &ov2643->ctrls;

	if (ov2643->ctrls.error) {
		printk("%s: control initialization error %d\n",
		       __func__, ov2643->ctrls.error);
		ret = ov2643->ctrls.error;
		goto done;
	}

	mutex_init(&ov2643->power_lock);
	v4l2_i2c_subdev_init(&ov2643->subdev, client, &ov2643_subdev_ops);
	ov2643->subdev.internal_ops = &ov2643_subdev_internal_ops;

	ov2643->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&ov2643->subdev.entity, 1, &ov2643->pad, 0);
	if (ret < 0)
		goto done;

	ov2643->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	ov2643->crop.width = 1280;
	ov2643->crop.height = 720;
	ov2643->crop.left = 0;
	ov2643->crop.top = 0;
	ov2643->format.code = V4L2_MBUS_FMT_SGRBG12_1X12;

	ov2643->format.width = 1280;
	ov2643->format.height = 720;
	ov2643->format.field = V4L2_FIELD_NONE;
	ov2643->format.colorspace = V4L2_COLORSPACE_SRGB;

done:
	if (ret < 0) {
		v4l2_ctrl_handler_free(&ov2643->ctrls);
		media_entity_cleanup(&ov2643->subdev.entity);
		kfree(ov2643);
	}

	return ret;
}

static int ov2643_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov2643 *ov2643 = to_ov2643(subdev);
	printk("OV2643 ov2643_remove\r\n");
	v4l2_ctrl_handler_free(&ov2643->ctrls);
	v4l2_device_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	if (ov2643->reset != -1)
		gpio_free(ov2643->reset);
	kfree(ov2643);

	return 0;
}

static const struct i2c_device_id ov2643_id[] = {
	{ "ov2643", OV2643_MODEL_COLOR }
};



MODULE_DEVICE_TABLE(i2c, ov2643_id);

static struct i2c_driver ov2643_i2c_driver = {
	.driver = {
		.name = "ov2643",
	},
	.probe          = ov2643_probe,
	.remove         = ov2643_remove,
	.id_table       = ov2643_id,
};

module_i2c_driver(ov2643_i2c_driver);

MODULE_DESCRIPTION("Omnivision OV2643 Camera driver");
MODULE_AUTHOR("Bastian Hecht <hechtb@gmail.com>");
MODULE_LICENSE("GPL v2");
