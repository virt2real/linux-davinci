/*  Copyright (c) 2010  Christoph Mair <christoph.mair@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/types.h>


#define HMC5843_I2C_ADDRESS			0x1E

#define HMC5843_CONFIG_REG_A			0x00
#define HMC5843_CONFIG_REG_B			0x01
#define HMC5843_MODE_REG			0x02
#define HMC5843_DATA_OUT_X_MSB_REG		0x03
#define HMC5843_DATA_OUT_X_LSB_REG		0x04
#define HMC5843_DATA_OUT_Y_MSB_REG		0x05
#define HMC5843_DATA_OUT_Y_LSB_REG		0x06
#define HMC5843_DATA_OUT_Z_MSB_REG		0x07
#define HMC5843_DATA_OUT_Z_LSB_REG		0x08
#define HMC5843_STATUS_REG			0x09
#define HMC5843_ID_REG_A		0x0A

#define HMC5843_ID_REG_LENGTH	0x03	/* Registers A, B, C*/
#define HMC5843_ID_STRING		"H43"

/* Addresses to scan: 0x1E */
static const unsigned short normal_i2c[] = { HMC5843_I2C_ADDRESS,
							I2C_CLIENT_END };

enum HMC5843_RANGE {
	RANGE_0_7 = 0x00,
	RANGE_1_0 = (0x01 << 0x05),
	RANGE_1_5,
	RANGE_2_0,
	RANGE_3_2,
	RANGE_3_8,
	RANGE_4_5,
	RANGE_6_5
};

static const int range_to_counts_mG[] = {
	1620,	/* RANGE_0_7 */
	1300,
	970,
	780,
	530,
	460,
	390,
	280	/* RANGE_6_5 */
};

enum HMC5843_OUTPUT_RATE {
	RATE_0 =	(0x07 << 2),	/* unused within the chip */
	RATE_5 =	(0x00 << 2),
	RATE_10 =	(0x01 << 2),
	RATE_20 =	(0x02 << 2),
	RATE_50 =	(0x03 << 2),
	RATE_100 =	(0x04 << 2),
	RATE_200 =	(0x05 << 2),
	RATE_500 =	(0x06 << 2),
};

enum HMC5843_OPERATING_MODE {
	MODE_NORMAL = 0x00,
	MODE_POSITIVE_BIAS,
	MODE_NEGATIVE_BIAS,
	MODE_NOT_USED
};

enum HMC5843_CONVERSION_MODE {
	CONVERSION_CONTINUOUS,
	CONVERSION_SINGLE,
	CONVERSION_IDLE,
	CONVERSION_SLEEP
};

enum HMC5843_STATUS {
	DATA_READY = 			0x01,
	DATA_OUTPUT_LOCK = 		0x02,
	VOLTAGE_REGULATOR_ENABLED = 	0x04
};


/* Each client has this additional data */
struct hmc5843_data {
	enum HMC5843_OUTPUT_RATE	rate;
	enum HMC5843_OPERATING_MODE	mode;
	enum HMC5843_CONVERSION_MODE	conversion;
	enum HMC5843_RANGE		range;
	enum HMC5843_STATUS		status;
	s16				data[3];
};

static void hmc5843_init_client(struct i2c_client *client);


static s32 hmc5843_get_status(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, HMC5843_STATUS_REG);
}

static s32 hmc5843_get_conversion_mode(struct i2c_client *client)
{
	/* The lower two bits contain the current conversion mode */
	return i2c_smbus_read_byte_data(client, HMC5843_MODE_REG) & 0x03;
}

static s32 hmc5843_set_conversion_mode(struct i2c_client *client,
				       enum HMC5843_CONVERSION_MODE conversion)
{
	return i2c_smbus_write_byte_data(client, HMC5843_MODE_REG,
							conversion & 0x03);
}

static s32 hmc5843_get_operating_mode(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, HMC5843_CONFIG_REG_A) & 0x03;
}

static s32 hmc5843_set_operating_mode(struct i2c_client *client,
				      enum HMC5843_OPERATING_MODE mode)
{
	struct hmc5843_data *data = i2c_get_clientdata(client);
	return  i2c_smbus_write_byte_data(client, HMC5843_CONFIG_REG_A,
						(mode & 0x03) + data->rate);
}

static s32 hmc5843_get_rate(struct i2c_client *client)
{
	/* The output data rate is specified by bits 3 and 4 */
	return i2c_smbus_read_byte_data(client, HMC5843_CONFIG_REG_A) & 0x1C;
}

static s32 hmc5843_set_rate(struct i2c_client *client,
				enum HMC5843_OUTPUT_RATE rate)
{
	struct hmc5843_data *data = i2c_get_clientdata(client);
	if (rate == RATE_0)
		/* if the data rate is 0, switch to single conversion mode */
		hmc5843_set_conversion_mode(client, CONVERSION_SINGLE);
	else
		/* else switch to continuous conversion mode */
		hmc5843_set_conversion_mode(client, CONVERSION_CONTINUOUS);
		return i2c_smbus_write_byte_data(client, HMC5843_CONFIG_REG_A,
							data->mode + rate);
}

static s32 hmc5843_get_range(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, HMC5843_CONFIG_REG_B);
}

static s32 hmc5843_set_range(struct i2c_client *client, enum HMC5843_RANGE range)
{
	return i2c_smbus_write_byte_data(client, HMC5843_CONFIG_REG_B, range);
}

static s32 hmc5843_read_measurements(struct i2c_client *client, s16 *data)
{
	/* read all data output registers */
	return i2c_smbus_read_i2c_block_data(client, HMC5843_DATA_OUT_X_MSB_REG,
							0x06, (u8 *)data);
}


/* following are the sysfs callback functions */
static ssize_t show_value(struct device *dev, struct device_attribute *attr, char *buf)
{
	s16 data[3];
	struct i2c_client *client = to_i2c_client(dev);
	s32 result = hmc5843_read_measurements(client, data);
	if (result > 0) {
/*		printk("%x  %x  %x\n", (s16)swab16((u16)data[0]),
				(s16)swab16((u16)data[1]),
				(s16)swab16((u16)data[2]));*/
		return sprintf(buf, "%d %d %d\n",
				(s16)swab16((u16)data[0]),
				(s16)swab16((u16)data[1]),
				(s16)swab16((u16)data[2]));
	}
	return result;
}
static DEVICE_ATTR(value, S_IRUGO, show_value, NULL);


static ssize_t show_status(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 status = hmc5843_get_status(client);
	return sprintf(buf, "REN: %u, LOCK: %u, RDY: %u\n",
				(status & VOLTAGE_REGULATOR_ENABLED) == VOLTAGE_REGULATOR_ENABLED,
				(status & DATA_OUTPUT_LOCK) == DATA_OUTPUT_LOCK,
				(status & DATA_READY) == DATA_READY);
}
static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);


static ssize_t show_conversion_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	switch (hmc5843_get_conversion_mode(client)) {
	case CONVERSION_CONTINUOUS:
		return sprintf(buf, "continuous\n");
	case CONVERSION_SINGLE:
		return sprintf(buf, "single\n");
	case CONVERSION_IDLE:
		return sprintf(buf, "idle\n");
	case CONVERSION_SLEEP:
		return sprintf(buf, "sleep\n");
	default:
		return sprintf(buf, "Invalid conversion mode.\n");
	};
}

static DEVICE_ATTR(conversion, S_IRUGO, show_conversion_mode, NULL);


static ssize_t show_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	switch (hmc5843_get_operating_mode(client)) {
	case MODE_NORMAL:
		return sprintf(buf, "0: normal measurement\n");
	case MODE_POSITIVE_BIAS:
		return sprintf(buf, "1: positive bias\n");
	case MODE_NEGATIVE_BIAS:
		return sprintf(buf, "2: negative bias\n");
	default:
		return sprintf(buf, "Invalid mode. Valid values:\n"
		"0: normal measurement\n"
		"1: positive bias\n"
		"2: negative bias\n");
	};
}

static ssize_t set_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hmc5843_data *data = i2c_get_clientdata(client);
	unsigned long mode = 0;
	strict_strtoul(buf, 10, &mode);
	dev_dbg(dev, "set mode to %lu\n", mode);
	if (hmc5843_set_operating_mode(client, mode) == -EINVAL)
		return -EINVAL;
	data->mode = mode;
	return count;
}

static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO, show_mode, set_mode);


static ssize_t show_rate(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	switch (hmc5843_get_rate(client)) {
	case RATE_0:	return sprintf(buf, "0 (single conversion)\n");
	case RATE_5:	return sprintf(buf, "5\n");
	case RATE_10:	return sprintf(buf, "10\n");
	case RATE_20:	return sprintf(buf, "20\n");
	case RATE_50:	return sprintf(buf, "50\n");
	case RATE_100:	return sprintf(buf, "100\n");
	case RATE_200:	return sprintf(buf, "200\n");
	case RATE_500:	return sprintf(buf, "500\n");
	default:	return sprintf(buf, "Invalid refresh rate.\n");
	};
}

static ssize_t set_rate(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hmc5843_data *data = i2c_get_clientdata(client);
	enum HMC5843_OUTPUT_RATE rate = RATE_0;
	unsigned long param = 0;
	strict_strtoul(buf, 10, &param);
	if (param < 1)
		rate = RATE_0;
	else if (param < 10)
		rate = RATE_5;
	else if (param < 20)
		rate = RATE_10;
	else if (param < 50)
		rate = RATE_20;
	else if (param < 100)
		rate = RATE_50;
	else if (param < 200)
		rate = RATE_100;
	else if (param < 500)
		rate = RATE_200;
	else
		rate = RATE_500;

	dev_dbg(dev, "set rate to %d\n", rate);
	if (hmc5843_set_rate(client, rate) == -EINVAL)
		return -EINVAL;
	data->rate = rate;
	return count;
}

static DEVICE_ATTR(rate, S_IWUSR | S_IRUGO, show_rate, set_rate);


static ssize_t show_range(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 range = hmc5843_get_range(client);
	switch (range) {
	case RANGE_0_7:	return sprintf(buf, "700\n");
	case RANGE_1_0:	return sprintf(buf, "1000\n");
	case RANGE_1_5:	return sprintf(buf, "1500\n");
	case RANGE_2_0:	return sprintf(buf, "2000\n");
	case RANGE_3_2:	return sprintf(buf, "3200\n");
	case RANGE_3_8:	return sprintf(buf, "3800\n");
	case RANGE_4_5:	return sprintf(buf, "4500\n");
	case RANGE_6_5:	return sprintf(buf, "6500)\n");
	default:	return sprintf(buf, "Invalid range value: %d\n", range);
	};
}

static ssize_t set_range(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hmc5843_data *data = i2c_get_clientdata(client);
	enum HMC5843_RANGE range = RANGE_1_0;
	unsigned long param = 0;
	strict_strtoul(buf, 10, &param);
	dev_dbg(dev, "set range to %lu\n", param);
	if (param < 1000)
		range = RANGE_0_7;
	else if (param < 1500)
		range = RANGE_1_0;
	else if (param < 2000)
		range = RANGE_1_5;
	else if (param < 3200)
		range = RANGE_2_0;
	else if (param < 3800)
		range = RANGE_3_2;
	else if (param < 4500)
		range = RANGE_3_8;
	else if (param < 6500)
		range = RANGE_4_5;
	else
		range = RANGE_6_5;

	if (hmc5843_set_range(client, range) == -EINVAL)
		return -EINVAL;
	data->range = range;
	return count;
}

static DEVICE_ATTR(range, S_IWUSR | S_IRUGO, show_range, set_range);


static ssize_t show_counts_per_mgauss(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 range = hmc5843_get_range(client);
	return sprintf(buf, "%d\n", range_to_counts_mG[range>>0x05]);
}

static DEVICE_ATTR(counts_per_mgauss, S_IRUGO, show_counts_per_mgauss, NULL);


static struct attribute *hmc5843_attributes[] = {
	&dev_attr_value.attr,
	&dev_attr_conversion.attr,
	&dev_attr_mode.attr,
	&dev_attr_rate.attr,
	&dev_attr_range.attr,
	&dev_attr_counts_per_mgauss.attr,
	&dev_attr_status.attr,
	NULL
};

static const struct attribute_group hmc5843_attr_group = {
	.attrs = hmc5843_attributes,
};


static int hmc5843_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	unsigned char id_str[HMC5843_ID_REG_LENGTH];

	if (client->addr != HMC5843_I2C_ADDRESS)
		return -ENODEV;

	if (i2c_smbus_read_i2c_block_data(client, HMC5843_ID_REG_A,
				HMC5843_ID_REG_LENGTH, id_str)
			!= HMC5843_ID_REG_LENGTH)
		return -ENODEV;

	if (0 != strncmp(id_str, HMC5843_ID_STRING, HMC5843_ID_REG_LENGTH))
		return -ENODEV;

	strlcpy(info->type, "hmc5843", I2C_NAME_SIZE);
	return 0;
}


static int hmc5843_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct hmc5843_data *data;
	int err = 0;

	data = kzalloc(sizeof(struct hmc5843_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	/* default settings after POR */
	data->rate = RATE_0;
	data->mode = MODE_NORMAL;
	data->range = RANGE_1_0;
	data->conversion = CONVERSION_SLEEP;

	i2c_set_clientdata(client, data);

	/* Initialize the HMC5843 chip */
	hmc5843_init_client(client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &hmc5843_attr_group);
	if (err)
		goto exit_free;

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

static int hmc5843_remove(struct i2c_client *client)
{
	hmc5843_set_conversion_mode(client, CONVERSION_SLEEP); /* set chip to sleep mode */
	sysfs_remove_group(&client->dev.kobj, &hmc5843_attr_group);
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* Called when we have found a new HMC5843. */
static void hmc5843_init_client(struct i2c_client *client)
{
	struct hmc5843_data *data = i2c_get_clientdata(client);
	hmc5843_set_operating_mode(client, data->mode);
	hmc5843_set_range(client, data->range);
	hmc5843_set_rate(client, data->rate);
	hmc5843_set_conversion_mode(client, data->conversion);
	printk(KERN_INFO "HMC5843 initialized\n");
}

static int hmc5843_suspend(struct i2c_client *client, pm_message_t mesg)
{
	hmc5843_set_conversion_mode(client, CONVERSION_SLEEP);
	printk(KERN_INFO "HMC5843 suspended\n");
	return 0;
}

static int hmc5843_resume(struct i2c_client *client)
{
	struct hmc5843_data *data = i2c_get_clientdata(client);
	hmc5843_set_conversion_mode(client, data->conversion);
	printk(KERN_INFO "HMC5843 resumed\n");
	return 0;
}


static const struct i2c_device_id hmc5843_id[] = {
	{ "hmc5843", 0 },
	{ }
};

static struct i2c_driver hmc5843_driver = {
	.driver = {
		.name	= "hmc5843",
	},
	.id_table	= hmc5843_id,
	.probe		= hmc5843_probe,
	.remove		= hmc5843_remove,

	.class		= I2C_CLASS_HWMON,
	.detect		= hmc5843_detect,
	//.address_list	= normal_i2c,

	.suspend	= hmc5843_suspend,
	.resume         = hmc5843_resume,
};

static int __init hmc5843_init(void)
{
	printk(KERN_INFO "init!\n");
	return i2c_add_driver(&hmc5843_driver);
}

static void __exit hmc5843_exit(void)
{
	printk(KERN_INFO "exit!\n");
	i2c_del_driver(&hmc5843_driver);
}


MODULE_AUTHOR("Christoph Mair <christoph.mair@gmail.com");
MODULE_DESCRIPTION("HMC5843 driver");
MODULE_LICENSE("GPL");

module_init(hmc5843_init);
module_exit(hmc5843_exit);
