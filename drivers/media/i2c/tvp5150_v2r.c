/***************************************************************************
 *
 * OV TVP5150 CameraCube module driver
 *
 * Copyright (C) VIRT@REAL <info@virt2real.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
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


#ifdef CONFIG_V2R_DEBUG
    #define TVP5150_DEBUG
#endif

/* TVP5150 has 8 I2C registers */
#define I2C_8BIT			1


#define TVP5150_TERM_REG 		0xFF
#define TVP5150_TERM_VAL 		0xFF



//Camera ID
#define TVP5150_PIDH_MAGIC		0x26
#define TVP5150_PIDL_MAGIC		0x43

//Camear ID registers
#define TVP5150_REG_PIDH				0x0a
#define TVP5150_REG_PIDL				0x0b
//Camera functional registers

//The rest values will be declared later
#define TVP5150_IMAGE_WIDTH	720
#define TVP5150_IMAGE_HEIGHT	576

#define SENSOR_DETECTED		1
#define SENSOR_NOT_DETECTED	0

/**
 * struct tvp5150_reg - tvp5150 register format
 * @reg: 8bit offset to register
 * @val: 8bit register value
 *
 * Define a structure for TVP5150 register initialization values
 */
struct tvp5150_reg {
	u8 	reg;
	u8 	val;
};

/**
 * struct capture_size - image capture size information
 * @width: image width in pixels
 * @height: image height in pixels
 */
struct capture_size {
	unsigned long width;
	unsigned long height;
};

/*
 * Array of image sizes supported by TVP5150.  These must be ordered from
 * smallest image size to largest.
 */
const static struct capture_size tvp5150_sizes[] = {
	{  TVP5150_IMAGE_WIDTH, TVP5150_IMAGE_HEIGHT },  /* VGA */
};

#define NUM_IMAGE_SIZES ARRAY_SIZE(tvp5150_sizes)
#define NUM_FORMAT_SIZES 1


/**
 * struct struct frame_settings - struct for storage of sensor
 * frame settings
 */
struct tvp5150_frame_settings {
	u16	frame_len_lines_min;
	u16	frame_len_lines;
	u16	line_len_pck;
	u16	x_addr_start;
	u16	x_addr_end;
	u16	y_addr_start;
	u16	y_addr_end;
	u16	x_output_size;
	u16	y_output_size;
	u16	x_even_inc;
	u16	x_odd_inc;
	u16	y_even_inc;
	u16	y_odd_inc;
	u16	v_mode_add;
	u16	h_mode_add;
	u16	h_add_ave;
};

/**
 * struct struct tvp5150_sensor_settings - struct for storage of
 * sensor settings.
 */
struct tvp5150_sensor_settings {
	/* struct tvp5150_clk_settings clk; */
	struct tvp5150_frame_settings frame;
};

/**
 * struct struct tvp5150_clock_freq - struct for storage of sensor
 * clock frequencies
 */
struct tvp5150_clock_freq {
	u32 vco_clk;
	u32 mipi_clk;
	u32 ddr_clk;
	u32 vt_pix_clk;
};

#define TVP5150_DRIVER_NAME  "tvp5150"
#define TVP5150_MOD_NAME "tvp5150: "

/*
 * Our nominal (default) frame rate.
 */
#define TVP5150_FRAME_RATE 30

#define COM12_RESET	(1 << 7)

//Some image formats will be added
enum image_size { VGA };
enum pixel_format { YUV };

static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

const static struct tvp5150_reg initial_list[] = {
		{ 0x30, 0x00 },
		{ 0xff, 0x10 },
		{ 0x00, 0x00 },
		{ 0x02, 0x00 },
		{ 0x03, 0x01 },
		{ 0x01, 0x15 },
		{ 0x06, 0x10 },
		{ 0x07, 0x60 },
		{ 0x08, 0x00 },
		{ 0x09, 0x80 },
		{ 0x0a, 0x80 },
		{ 0x0b, 0x00 },
		{ 0x0c, 0x80 },
		{ 0x0d, 0x47 },
		{ 0x0e, 0x00 },
		{ 0x0f, 0x08 },
		{ 0x11, 0x00 },
		{ 0x12, 0x00 },
		{ 0x13, 0x00 },
		{ 0x14, 0x00 },
		{ 0x15, 0x01 },
		{ 0x16, 0x80 },
		{ 0x18, 0x00 },
		{ 0x19, 0x00 },
		{ 0x1a, 0x0c },
		{ 0x1b, 0x14 },
		{ 0x1c, 0x00 },
		{ 0x1d, 0x00 },
		{ 0x1e, 0x00 },
		{ 0x28, 0x00 },
		{ 0x2e, 0x0f },
		{ 0x2f, 0x01 },
		{ 0xbb, 0x00 },
		{ 0xc0, 0x00 },
		{ 0xc1, 0x00 },
		{ 0xc2, 0x04 },
		{ 0xc8, 0x80 },
		{ 0xc9, 0x00 },
		{ 0xca, 0x00 },
		{ 0xcb, 0x4e },
		{ 0xcc, 0x00 },
		{ 0xcd, 0x01 },
		{ 0xcf, 0x00 },
		{ 0xd0, 0x00 },
		{ 0xfc, 0x7f },
		{ 0xcf, 0x00 },
		{ 0x02, 0x30 },
		{ 0x00, 0x00 },
		{ 0x03, 0x41 },
		{ 0x0F, 0x02 },
		{ 0x01, 0x15 },
		{ 0x03, 0x6f },
		{ 0x04, 0x00 },
		{ 0x0d, 0x47 },
		{ 0x1a, 0x0c },
		{ 0x1b, 0x54 },
		{ 0x27, 0x20 },
		{ 0x09, 0x80 },
		{ 0x0c, 0x80 },
		{ 0x0a, 0x80 },
		{ 0x0b, 0x00 },
		{ 0x28, 0x00 },
		{ 0x16, 0x5a },
		{0xff, 250},//delay 255ms
		{0xff, 0xff}	/* END MARKER */
};

//For any sake
static const struct tvp5150_reg tvp5150_YYUV_regs[] = {
	{ TVP5150_TERM_REG, TVP5150_TERM_VAL },
};
static const struct tvp5150_reg tvp5150_vga_regs[] = {
		{ TVP5150_TERM_REG, TVP5150_TERM_VAL },
};

//Forward declaration of driver operations
static int tvp5150_get_control(struct v4l2_subdev *, struct v4l2_control *);
static int tvp5150_set_control(struct v4l2_subdev *, struct v4l2_control *);
static int tvp5150_query_control(struct v4l2_subdev *, struct v4l2_queryctrl *);

static enum image_size tvp5150_find_size(unsigned int width, unsigned int height){
	enum image_size isize;
	unsigned long pixels = width * height;
#ifdef TVP5150_DEBUG
	printk("tvp5150 find size\r\n");
#endif
	//for (isize = QVGA; isize < VGA; isize++)
	isize = VGA;
	{
		if (tvp5150_sizes[isize + 1].height *
			tvp5150_sizes[isize + 1].width > pixels)
			return isize;
	}
	return VGA;
}

struct tvp5150 {
	struct v4l2_subdev sd;
	struct v4l2_pix_format pix;
	struct v4l2_fract timeperframe;
};
static inline struct tvp5150 *to_tvp5150(struct v4l2_subdev *sd){
	return container_of(sd, struct tvp5150, sd);//Funny thing works
}

static struct i2c_driver tvp5150_i2c_driver;

/* list of image formats supported by tvp5150 sensor */
const static struct v4l2_fmtdesc tvp5150_formats[] = {
	{
		.description	= "YUYV 4:2:2",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
	}
};

#define NUM_CAPTURE_FORMATS ARRAY_SIZE(tvp5150_formats)

static int tvp5150_read_reg(struct i2c_client *client, u8 reg, u8 *val){
	int ret;
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret >= 0) {
		*val = (unsigned char) ret;
                ret = 0;
        }
#ifdef TVP5150_DEBUG
	printk("tvp5150 read reg: client: %x, addr: %x, reg: %x, val: %x, ret: %x\r\n", (unsigned int)client->adapter, client->addr, reg, *val, ret);
#endif
	return ret;
}

static int tvp5150_write_reg(struct i2c_client *client, u8 reg, u8 val){
	int ret = 0;
	//!if (!client->adapter) return -ENODEV;
#ifdef TVP5150_DEBUG
	printk("tvp5150 write reg: client: %x, addr: %x, reg: %x, val: %x\r\n", (unsigned int)client->adapter, client->addr, reg, val);
#endif
	if (reg == TVP5150_TERM_REG){
		if (val == TVP5150_TERM_VAL) return 0;
	}
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (reg == 0x05 && (val & 0x01)) msleep(5); /* Wait for reset to run */
	if (reg == TVP5150_TERM_REG){
		msleep(val);
	}
	return 0;//!ret;
}

static int tvp5150_write_regs(struct v4l2_subdev *sd, struct tvp5150_reg *vals){
	int err = 0;
	const struct tvp5150_reg *list = vals;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	while (!((list->reg == TVP5150_TERM_REG) && (list->val == TVP5150_TERM_VAL))) {
		err = tvp5150_write_reg(client, list->reg,list->val);
		//!if (err) return err;
		msleep(1);
		list++;
	}
	return 0;
}


static int tvp5150_configure(struct v4l2_subdev *sd){
	struct tvp5150 *tvp5150 = to_tvp5150(sd);
	//struct v4l2_pix_format *pix = &tvp5150->pix;
	//enum pixel_format pfmt = YUV;
#ifdef TVP5150_DEBUG
	printk("Configuring tvp5150 camera chip\n");
#endif
#if 0 //Just for initial driver version
	switch (pix->pixelformat) {
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			pfmt = RGB565;
			break;
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
		default:
			pfmt = YUV;
			break;
	}
	xclk = tvp5150sensor_calc_xclk();
	isize = tvp5150_find_size(pix->width, pix->height);
	/* configure pixel format */
	err = tvp5150_write_regs(sd, (struct tvp5150_reg *)(tvp5150_reg_format_init[pfmt]) );
	if (err){
		printk("Configure made error1 %d\r\n", err);
		return err;
	}
	/* configure size */
	err = tvp5150_write_regs(sd, (struct tvp5150_reg *)(tvp5150_reg_size_init[isize]) );
	if (err){
		printk("Configure made error2 %d\r\n", err);
		return err;
	}
#endif
	return 0;
}

static int tvp5150_init(struct v4l2_subdev *sd, u32 val){
	/* Reset and wait two milliseconds */
	//struct i2c_client *client = v4l2_get_subdevdata(sd);
#ifdef TVP5150_DEBUG
	printk("tvp5150 initialization function\r\n");
#endif
    msleep(5);
	return tvp5150_write_regs(sd, (struct tvp5150_reg *)initial_list );
}

//All that clear about controls
static int tvp5150_query_control(struct v4l2_subdev *sd, struct v4l2_queryctrl *qctr){
#if 0
	printk("tvp5150 ioctl_queryctrl dummy method\r\n");
	printk("Def val %x\r\n",qctr->default_value);
	printk("Name %s\r\n", qctr->name);
	printk("Type %d\r\n", qctr->type);
	printk("ID 0x%08x\r\n", qctr->id);
	//printk("", gctr->)
#endif
	return -EINVAL;
}

static int tvp5150_get_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl){
#ifdef TVP5150_DEBUG
	printk("tvp5150 ioctl_g_ctrl dummy method\r\n");
#endif
	return -EINVAL;
}

static int tvp5150_set_control(struct v4l2_subdev *sd,struct v4l2_control *ctrl){
	int retval = -EINVAL;
#ifdef TVP5150_DEBUG
	printk("tvp5150 ioctl_s_ctrl dummy method\r\n");
#endif
	return retval;
}

static int tvp5150_enum_format(struct v4l2_subdev *sd, struct v4l2_fmtdesc *fmt){
	int index = fmt->index;
	enum v4l2_buf_type type = fmt->type;
	//
	memset(fmt, 0, sizeof(*fmt));
	fmt->index = index;
	fmt->type = type;
	//
#ifdef TVP5150_DEBUG
	printk("%s: Enum format capability\n", __func__);
	printk("tvp5150 ioctl_enum_fmt_cap\r\n");
#endif
	//
	switch (fmt->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (index >= NUM_CAPTURE_FORMATS) return -EINVAL;
			break;
		default:
			return -EINVAL;
	}
	fmt->flags = tvp5150_formats[index].flags;
	strlcpy(fmt->description, tvp5150_formats[index].description, sizeof(fmt->description));
	fmt->pixelformat = tvp5150_formats[index].pixelformat;
	return 0;
}

static int tvp5150_set_stream(struct v4l2_subdev *sd, int enable){
#ifdef TVP5150_DEBUG
	printk("TVP5150 set stream - dummy method\r\n");
#endif
	return 0;
}

static int tvp5150_try_format(struct v4l2_subdev *sd, struct v4l2_format *f){
	enum image_size isize;
	int ifmt;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	char* colorspace = 0;
	switch(f->fmt.pix.colorspace){
		case V4L2_COLORSPACE_SMPTE170M 		: colorspace = "V4L2_COLORSPACE_SMPTE170M"; break;
		case V4L2_COLORSPACE_SMPTE240M 		: colorspace = "V4L2_COLORSPACE_SMPTE240M"; break;
		case V4L2_COLORSPACE_REC709    		: colorspace = "V4L2_COLORSPACE_REC709"; break;
		case V4L2_COLORSPACE_BT878     		: colorspace = "V4L2_COLORSPACE_BT878"; break;
		case V4L2_COLORSPACE_470_SYSTEM_M 	: colorspace = "V4L2_COLORSPACE_470_SYSTEM_M"; break;
		case V4L2_COLORSPACE_470_SYSTEM_BG 	: colorspace = "V4L2_COLORSPACE_470_SYSTEM_BG"; break;
		case V4L2_COLORSPACE_JPEG         	: colorspace = "V4L2_COLORSPACE_JPEG"; break;
		case V4L2_COLORSPACE_SRGB          	: colorspace = "V4L2_COLORSPACE_SRGB"; break;
		default								: colorspace = "NOT SET"; break;
	}
#if 0
  	printk("tvp5150 try format:\r\n bytes per line: %d\r\npixelformat %c%c%c%c\r\n, colorspace %s\r\n, field %d\r\n, height %d, width %d, sizeimage %d\r\n",
  			f->fmt.pix.bytesperline,
  			((f->fmt.pix.pixelformat)&0xff),(((f->fmt.pix.pixelformat)>>8)&0xff),(((f->fmt.pix.pixelformat)>>16)&0xff),(((f->fmt.pix.pixelformat)>>24)&0xff),
  			colorspace,
  			f->fmt.pix.field,
  			f->fmt.pix.height,
  			f->fmt.pix.width,
  			f->fmt.pix.sizeimage
  	);
#endif
	isize = tvp5150_find_size(pix->width, pix->height);
	pix->width = tvp5150_sizes[isize].width;
	pix->height = tvp5150_sizes[isize].height;
#ifdef TVP5150_DEBUG
	printk("%s: Trying format\n", __func__);
#endif
	for (ifmt = 0; ifmt < NUM_CAPTURE_FORMATS; ifmt++) {
		if (pix->pixelformat == tvp5150_formats[ifmt].pixelformat) break;
	}

	if (ifmt == NUM_CAPTURE_FORMATS) ifmt = 0;

	pix->pixelformat = tvp5150_formats[ifmt].pixelformat;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->priv = 0;
	switch (pix->pixelformat) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
		default:
			pix->colorspace = V4L2_COLORSPACE_JPEG;
			break;
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			pix->colorspace = V4L2_COLORSPACE_SRGB;
			break;
	}
	return 0;
}

static int tvp5150_set_format(struct v4l2_subdev *sd, struct v4l2_format *f){
	struct tvp5150 *tvp5150 = to_tvp5150(sd);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int rval;
#ifdef TVP5150_DEBUG
	printk("tvp5150 set format\r\n");
#endif
	rval = tvp5150_try_format(sd, f);
	if (rval) {
		printk("%s: Error trying format\n", __func__);
		return rval;
	}
	rval = tvp5150_configure(sd);
	if (!rval) {
		tvp5150->pix = *pix;
	} else {
		printk("%s: Error configure format %d\n", __func__, rval);
	}
	return rval;
}

static int tvp5150_get_format(struct v4l2_subdev *sd, struct v4l2_format *f){
	struct tvp5150 *tvp5150 = to_tvp5150(sd);
	f->fmt.pix = tvp5150->pix;
#ifdef TVP5150_DEBUG
	printk("tvp5150 ioctl_g_fmt_cap\r\n");
#endif
	return 0;
}

static int tvp5150_get_param(struct v4l2_subdev *sd, struct v4l2_streamparm *a){
	struct tvp5150 *tvp5150 = to_tvp5150(sd);
	struct v4l2_captureparm *cparm = &a->parm.capture;
#ifdef TVP5150_DEBUG
	printk("tvp5150 ioctl_g_parm\r\n");
#endif
	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)	return -EINVAL;
	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	cparm->timeperframe = tvp5150->timeperframe;
	return 0;
}

static int tvp5150_get_chip_id(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id){
	struct i2c_client *client = v4l2_get_subdevdata(sd);;
#ifdef TVP5150_DEBUG
	printk("tvp5150 get chipid\r\n");
#endif
	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR){
		printk("match type fails\r\n");
		return -EINVAL;
	}
	if (id->match.addr != client->addr){
		printk("match addr fails\r\n");
		return -ENODEV;
	}
	id->ident	= V4L2_IDENT_TVP5150;
	id->revision	= 0;
#ifdef TVP5150_DEBUG
	printk("tvp5150 chip id ok\r\n");
#endif
	return 0;
}

static const struct v4l2_subdev_core_ops tvp5150_core_ops = {
	.g_chip_ident = tvp5150_get_chip_id,
	.init         = tvp5150_init,
	.queryctrl    = tvp5150_query_control,
	.g_ctrl	      = tvp5150_get_control,
	.s_ctrl	      = tvp5150_set_control,
};

static const struct v4l2_subdev_video_ops tvp5150_video_ops = {
	.s_fmt    = tvp5150_set_format,
	.g_fmt    = tvp5150_get_format,//Check it correct
	.try_fmt  = tvp5150_try_format,
	.s_stream = tvp5150_set_stream,
	.enum_fmt = tvp5150_enum_format,//Check it correct
	.g_parm   = tvp5150_get_param//Check it correct
};

static const struct v4l2_subdev_ops tvp5150_ops = {
	.core = &tvp5150_core_ops,
	.video = &tvp5150_video_ops,
};

static int tvp5150_detect(struct i2c_client *client)
{
//Shadrin todo improve: may check model ID also
	u8 pidh, pidl;
	printk("Detect tvp5150\r\n");
	if (!client) return -ENODEV;

	if (tvp5150_read_reg(client, 0x80, &pidh)) return -ENODEV;
	if (tvp5150_read_reg(client, 0x81, &pidl)) return -ENODEV;

	v4l_info(client, "model id detected 0x%02x%02x\n", pidh, pidl);
	printk("model id detected 0x%02x%02x\n", pidh, pidl);
	if ((pidh != 0x51)|| (pidl != 0x50)) {
		return -ENODEV;
	}
	return 0;
}


static int tvp5150_probe(struct i2c_client *client, const struct i2c_device_id *id){
	struct tvp5150 *tvp5150;
	struct v4l2_subdev *sd;
	int ret;
#ifdef TVP5150_DEBUG
	printk("tvp5150 probe enter\n");
#endif
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->adapter->dev,"I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "No platform data!!\n");
		return -ENODEV;
	}
	tvp5150 = kzalloc(sizeof(struct tvp5150), GFP_KERNEL);
	if (!tvp5150) return -ENOMEM;
	ret = tvp5150_detect(client);
	if (ret){
#ifdef TVP5150_DEBUG
		printk("tvp5150 detection failed\r\n");
#endif
		goto clean;
	}
	//Filling  tvp5150 data stucture
	tvp5150->pix.width = TVP5150_IMAGE_WIDTH;
	tvp5150->pix.height = TVP5150_IMAGE_HEIGHT;
	tvp5150->pix.pixelformat = V4L2_PIX_FMT_YUYV;
	tvp5150->timeperframe.numerator = 1;
	tvp5150->timeperframe.denominator = TVP5150_FRAME_RATE;

	/* Register with V4L2 layer as slave device */
	sd = &tvp5150->sd;
	v4l2_i2c_subdev_init(sd, client, &tvp5150_ops);

	ret = tvp5150_init(sd,0);
	v4l2_info(sd, "%s decoder driver registered !!\n", sd->name);
	return 0;

clean:
	kfree(tvp5150);
	return ret;
}
static int tvp5150_remove(struct i2c_client *client){
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tvp5150 *tvp5150 = to_tvp5150(sd);
	v4l2_device_unregister_subdev(sd);
	kfree(tvp5150);
	return 0;
}

static const struct i2c_device_id tvp5150_id[] = {
	{ "tvp5150", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tvp5150_id);

static struct i2c_driver tvp5150_i2c_driver = {
	.driver = {
		.name = "tvp5150",
	},
	.probe		= tvp5150_probe,
	.remove		= tvp5150_remove,
	.id_table	= tvp5150_id,
};

static int __init tvp5150_driver_init(void){
	int err;
#ifdef TVP5150_DEBUG
	printk("TVP5150 camera sensor init\r\n");
#endif
	err = i2c_add_driver(&tvp5150_i2c_driver);
	if (err) {
		printk("Failed to register" TVP5150_DRIVER_NAME ".\n");
		return err;
	}
	return 0;
}

static void __exit tvp5150_driver_cleanup(void){
#ifdef TVP5150_DEBUG
	printk("TVP5150 camera driver cleanup\r\n");
#endif
	i2c_del_driver(&tvp5150_i2c_driver);
}

module_init(tvp5150_driver_init);
module_exit(tvp5150_driver_cleanup);

MODULE_LICENSE("GPL V2");
MODULE_AUTHOR("Alexander V. Shadrin, alex.virt2real@gmail.com");
MODULE_DESCRIPTION("tvp5150 primitive camera sensor driver");
