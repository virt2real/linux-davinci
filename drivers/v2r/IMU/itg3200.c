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


#define ITG3200_REG_ID				0x00 /* 110100 */
#define ITG3200_REG_SAMPLE_RATE_DIV		0x15 /* 0x00 */
#define ITG3200_REG_LP_FULL_SCALE		0x16 /* 0x00 */
#define ITG3200_REG_IRQ				0x17 /* 0x00 */
#define ITG3200_REG_IRQ_STATUS			0x1A /* 0x00 */
#define ITG3200_REG_TEMP_OUT_H			0x1B
#define ITG3200_REG_TEMP_OUT_L			0x1C
#define ITG3200_REG_GYRO_XOUT_H			0x1D
#define ITG3200_REG_GYRO_XOUT_L			0x1E
#define ITG3200_REG_GYRO_YOUT_H			0x1F
#define ITG3200_REG_GYRO_YOUT_L			0x20
#define ITG3200_REG_GYRO_ZOUT_H			0x21
#define ITG3200_REG_GYRO_ZOUT_L			0x22
#define ITG3200_REG_POWER_MGMT			0x3E	/* 0x00 */

#define ITG3200_ID_MAGIC			0x69	/* after poweron; can be changed at runtime */

#define ITG3200_FULL_SCALE_2000			(0x03 << 3)

#define ITG3200_LP_256				0x00
#define ITG3200_LP_188				0x01
#define ITG3200_LP_98				0x02
#define ITG3200_LP_42				0x03
#define ITG3200_LP_20				0x04
#define ITG3200_LP_10				0x05
#define ITG3200_LP_5				0x06

#define ITG3200_IRQ_LOGIC_LEVEL			7
#define ITG3200_IRQ_DRIVE_TYPE			6
#define ITG3200_IRQ_LATCH_MODE			5
#define ITG3200_IRQ_LATCH_CLEAR_MODE		4
#define ITG3200_IRQ_DEVICE_READY		2
#define ITG3200_IRC_DATA_AVAILABLE		0

#define ITG3200_IRQ_ACTIVE_LOW			0x01
#define ITG3200_IRQ_ACTIVE_HIGH			0x00
#define ITG3200_IRQ_OPEN_DRAIN			0x01
#define ITG3200_IRQ_PUSH_PULL			0x00
#define ITG3200_IRQ_LATCH_UNTIL_CLEARED		0x01
#define ITG3200_IRQ_LATCH_PULSE			0x00
#define ITG3200_IRQ_ENABLE_DEVICE_READY		0x01
#define ITG3200_IRQ_ENABLE_DATA_AVAILABLE	0x01

#define ITG3200_OSC_INTERNAL			0x00
#define ITG3200_OSC_GYRO_X			0x01
#define ITG3200_OSC_GYRO_Y			0x02
#define ITG3200_OSC_GYRO_Z			0x03
#define ITG3200_OSC_32K				0x04
#define ITG3200_OSC_19M				0x05

#define ITG3200_GYRO_X				(0x01 << 3)
#define ITG3200_GYRO_Y				(0x01 << 4)
#define ITG3200_GYRO_Z				(0x01 << 5)

#define ITG3200_STANDBY_Z			(0x01 << 3)
#define ITG3200_STANDBY_Y			(0x01 << 4)
#define ITG3200_STANDBY_X			(0x01 << 5)
#define ITG3200_SLEEP				(0x01 << 6)
#define ITG3200_RESET				(0x01 << 7)

/* Addresses to scan: 0x68, 0x69 */
static const unsigned short normal_i2c[] = { 0x68, 0x69, I2C_CLIENT_END };

struct itg3200_data {
	unsigned char low_pass;
	unsigned char fullscale;
	unsigned char powernamagement;
};

struct itg3200_sensordata {
	s16 temperature;
	s16 gyro_x;
	s16 gyro_y;
	s16 gyro_z;
};

static void itg3200_init_client(struct i2c_client *client);


static s32 itg3200_get_id(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, ITG3200_REG_ID);
}

static s32 itg3200_get_samplerate_divider(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, ITG3200_REG_SAMPLE_RATE_DIV);
}

static s32 itg3200_set_samplerate_divider(struct i2c_client *client, unsigned char samplerate_divider)
{
	return i2c_smbus_write_byte_data(client, ITG3200_REG_SAMPLE_RATE_DIV, samplerate_divider);
}

static s32 itg3200_get_low_pass(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	s32 result = i2c_smbus_read_byte_data(client, ITG3200_REG_LP_FULL_SCALE);
	if (result >= 0) {
		data->low_pass = result & 0x07;
		return data->low_pass;
	}
	return result;
}

static s32 itg3200_set_low_pass(struct i2c_client *client, unsigned char low_pass)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	data->low_pass = low_pass;
	return i2c_smbus_write_byte_data(client, ITG3200_REG_LP_FULL_SCALE, data->low_pass | data->fullscale);
}

static s32 itg3200_get_fullscale(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	s32 result = i2c_smbus_read_byte_data(client, ITG3200_REG_LP_FULL_SCALE);
	if (result >= 0) {
		data->fullscale = result & 0x18;
		return data->fullscale;
	}
	return result;
}

static s32 itg3200_set_fullscale(struct i2c_client *client, unsigned char fullscale)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	data->fullscale = fullscale;
	return i2c_smbus_write_byte_data(client, ITG3200_REG_LP_FULL_SCALE, data->low_pass | data->fullscale);
}

static s32 itg3200_get_irq_config(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, ITG3200_REG_IRQ);
}

static s32 itg3200_set_irq_config(struct i2c_client *client, unsigned char irq_config)
{
	return i2c_smbus_write_byte_data(client, ITG3200_REG_IRQ, irq_config);
}

static s32 itg3200_get_irq_status(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, ITG3200_REG_IRQ_STATUS);
}

static s32 itg3200_get_clocksource(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	s32 result = i2c_smbus_read_byte_data(client, ITG3200_REG_POWER_MGMT);
	if (result >= 0) {
		data->powernamagement = result;
		return data->powernamagement & 0x03;
	}
	return result;
}

static s32 itg3200_get_axis(struct i2c_client *client, unsigned char axis)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	return (data->powernamagement & axis) == axis;
}

static s32 itg3200_set_axis_disable(struct i2c_client *client, unsigned char axis)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	data->powernamagement = data->powernamagement | axis;
	return i2c_smbus_write_byte_data(client, ITG3200_REG_POWER_MGMT, data->powernamagement);
}

static s32 itg3200_set_axis_enable(struct i2c_client *client, unsigned char axis)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	data->powernamagement = data->powernamagement & ~axis;
	return i2c_smbus_write_byte_data(client, ITG3200_REG_POWER_MGMT, data->powernamagement);
}

static s32 itg3200_set_suspend(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	data->powernamagement = data->powernamagement | ITG3200_SLEEP;
	return i2c_smbus_write_byte_data(client, ITG3200_REG_POWER_MGMT, data->powernamagement);
}

static s32 itg3200_get_suspended(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	s32 result = i2c_smbus_read_byte_data(client, ITG3200_REG_POWER_MGMT);
	if (result >= 0) {
		data->powernamagement = result;
		return (data->powernamagement & ITG3200_SLEEP) == ITG3200_SLEEP;
	}
	return result;
}

static s32 itg3200_set_resume(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	data->powernamagement = data->powernamagement & ~ITG3200_SLEEP;
	return i2c_smbus_write_byte_data(client, ITG3200_REG_POWER_MGMT, data->powernamagement);
}

static s32 itg3200_reset(struct i2c_client *client)
{
	return i2c_smbus_write_byte_data(client, ITG3200_REG_POWER_MGMT, ITG3200_RESET);
}

static s32 itg3200_read_measurements(struct i2c_client *client, struct itg3200_sensordata *data)
{
	u8 *d;
	d = (u8*) data;
	i2c_smbus_read_i2c_block_data(client, ITG3200_REG_TEMP_OUT_H, 8, d);
	data->temperature = be16_to_cpu(data->temperature);
	data->gyro_x = be16_to_cpu(data->gyro_x);
	data->gyro_y = be16_to_cpu(data->gyro_y);
	data->gyro_z = be16_to_cpu(data->gyro_z);
	return 1;
}


/* following are the sysfs callback functions */
static ssize_t show_values(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct itg3200_sensordata data;
	struct i2c_client *client = to_i2c_client(dev);
	s32 result;
	data.temperature = 0;
	data.gyro_x = 0;
	data.gyro_y = 0;
	data.gyro_z = 0;
	result = itg3200_read_measurements(client, &data);
	if (result > 0) {
		return sprintf(buf, "%d %d %d %d\n",
				data.temperature,
				data.gyro_x,
				data.gyro_y,
			        data.gyro_z
				);
	}
	return result;
}
static DEVICE_ATTR(values, S_IRUGO, show_values, NULL);


static ssize_t show_samplerate(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned int internal_samplerate = (itg3200_get_low_pass(client) == 0 ? 8000 : 1000);
	unsigned int samplerate = internal_samplerate / (itg3200_get_samplerate_divider(client)+1);
	return sprintf(buf, "%d\n", samplerate);
}

static ssize_t set_samplerate(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long desired_samplerate;
	unsigned int divider;
	unsigned int internal_samplerate;

	strict_strtoul(buf, 10, &desired_samplerate);
	internal_samplerate = (itg3200_get_low_pass(client) == 0 ? 8000 : 1000);

	divider = internal_samplerate / desired_samplerate - 1;

	itg3200_set_samplerate_divider(client, divider);
	return count;
}
static DEVICE_ATTR(samplerate, S_IWUSR | S_IRUGO, show_samplerate, set_samplerate);

static ssize_t show_low_pass(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 low_pass = itg3200_get_low_pass(client);
	switch (low_pass) {
	case ITG3200_LP_256: return sprintf(buf, "256\n");
	case ITG3200_LP_188: return sprintf(buf, "188\n");
	case ITG3200_LP_98:  return sprintf(buf, "98\n");
	case ITG3200_LP_42:  return sprintf(buf, "42\n");
	case ITG3200_LP_20:  return sprintf(buf, "20\n");
	case ITG3200_LP_10:  return sprintf(buf, "10\n");
	case ITG3200_LP_5:   return sprintf(buf, "5\n");
	default:	     return sprintf(buf, "invalid low pass filter setting: %d\n", low_pass);
	}
}

static ssize_t set_low_pass(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long param;
	unsigned int low_pass;
	strict_strtoul(buf, 10, &param);
	if (param < 10)
		low_pass = ITG3200_LP_5;
	else if (param < 20)
		low_pass = ITG3200_LP_10;
	else if (param < 42)
		low_pass = ITG3200_LP_20;
	else if (param < 98)
		low_pass = ITG3200_LP_42;
	else if (param < 188)
		low_pass = ITG3200_LP_98;
	else if (param < 256)
		low_pass = ITG3200_LP_188;
	else
		low_pass = ITG3200_LP_256;
	itg3200_set_low_pass(client, low_pass);
	return count;
}
static DEVICE_ATTR(low_pass, S_IWUSR | S_IRUGO, show_low_pass, set_low_pass);

static ssize_t get_enable_axis(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	return sprintf(buf, "%s%s%s\n",
		(itg3200_get_axis(client, ITG3200_GYRO_X) ? "X":""),
		(itg3200_get_axis(client, ITG3200_GYRO_Y) ? "Y":""),
		(itg3200_get_axis(client, ITG3200_GYRO_Z) ? "Z":"")
		);
}

static ssize_t set_enable_axis(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;
	struct i2c_client *client = to_i2c_client(dev);
	if (count < 1)
		return -EINVAL;

	for(i=0; i<count; i++) {
		if (strncasecmp(buf+i, "X", 1) == 0)
			itg3200_set_axis_enable(client, ITG3200_GYRO_X);
		else if (strncasecmp(buf+i, "Y", 1) == 0)
			itg3200_set_axis_enable(client, ITG3200_GYRO_Y);
		else if (strncasecmp(buf+i, "Z", 1) == 0)
			itg3200_set_axis_enable(client, ITG3200_GYRO_Z);
	}
	return count;
}

static DEVICE_ATTR(enable_axis, S_IWUSR | S_IRUGO, get_enable_axis, set_enable_axis);

static ssize_t set_disable_axis(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int i;
	if (count < 1)
		return -EINVAL;

	for(i=0; i<count; i++) {
		if (strncasecmp(buf+1, "X", 1) == 0)
			itg3200_set_axis_disable(client, ITG3200_GYRO_X);
		else if (strncasecmp(buf+1, "Y", 1) == 0)
			itg3200_set_axis_disable(client, ITG3200_GYRO_X);
		else if (strncasecmp(buf+1, "Z", 1) == 0)
			itg3200_set_axis_disable(client, ITG3200_GYRO_X);
	}
	return count;
}
static DEVICE_ATTR(disable_axis, S_IWUSR, NULL, set_disable_axis);

static ssize_t get_clocksource(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned int chars = 0;
	switch (itg3200_get_clocksource(client)) {
	  case 0: chars += sprintf(buf, "internal oscillator"); break;
	  case 1: chars += sprintf(buf, "PLL with X Gyro reference"); break;
	  case 2: chars += sprintf(buf, "PLL with Y Gyro reference"); break;
	  case 3: chars += sprintf(buf, "PLL with Z Gyro reference"); break;
	  case 4: chars += sprintf(buf, "PLL with external 32.768 kHz reference"); break;
	  case 5: chars += sprintf(buf, "PLL with external 19.2 MHz reference"); break;
	  default: chars += sprintf(buf, "unknown (reserved)"); break;
	}
	chars += sprintf(buf+chars, "\n");
	return chars;
}

static ssize_t set_clocksource(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}
static DEVICE_ATTR(clocksource, S_IWUSR | S_IRUGO, get_clocksource, set_clocksource);




static ssize_t get_suspend(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%d\n", itg3200_get_suspended(client));
}

static ssize_t set_suspend(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long suspend;
	strict_strtoul(buf, 10, &suspend);
	if (suspend == 0)
		itg3200_set_resume(client);
	else if (suspend == 1)
		itg3200_set_suspend(client);
	else
	  return -EINVAL;
	return count;
}
static DEVICE_ATTR(suspend, S_IWUSR | S_IRUGO, get_suspend, set_suspend);


static struct attribute *itg3200_attributes[] = {
	&dev_attr_values.attr,
	&dev_attr_samplerate.attr,
	&dev_attr_low_pass.attr,
	&dev_attr_enable_axis.attr,
	&dev_attr_disable_axis.attr,
	&dev_attr_clocksource.attr,
	&dev_attr_suspend.attr,
	NULL
};

static const struct attribute_group itg3200_attr_group = {
	.attrs = itg3200_attributes,
};


static int itg3200_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
//	if (client->addr != ITG3200_I2C_ADDRESS)
//		return -ENODEV;

	if (itg3200_get_id(client) != ITG3200_ID_MAGIC)
		return -ENODEV;

	strlcpy(info->type, "itg3200", I2C_NAME_SIZE);

	return 0;
}


static int itg3200_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct itg3200_data *data;
	int err = 0;

	data = kzalloc(sizeof(struct itg3200_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);

	itg3200_init_client(client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &itg3200_attr_group);
	if (err) {
		dev_err(&client->dev, "probe failed!\n");
		goto exit;
	}

	dev_info(&client->dev, "probe succeeded!\n");

exit:
	return err;
}

static int itg3200_remove(struct i2c_client *client)
{
	struct itg3200_data *data = i2c_get_clientdata(client);
	itg3200_set_suspend(client);
	sysfs_remove_group(&client->dev.kobj, &itg3200_attr_group);
	kfree(data);
	return 0;
}

static void itg3200_init_client(struct i2c_client *client)
{
	itg3200_reset(client);
//	usleep(20000);	/* TODO: Figure out actual time */
	itg3200_set_fullscale(client, ITG3200_FULL_SCALE_2000);
	/* read default register values */
	itg3200_get_suspended(client);
	itg3200_get_samplerate_divider(client);
	/* save power */
	itg3200_set_suspend(client);
}

static int itg3200_suspend(struct i2c_client *client, pm_message_t mesg)
{
	itg3200_set_suspend(client);
	dev_info(&client->dev, "suspended\n");
	return 0;
}

static int itg3200_resume(struct i2c_client *client)
{
	itg3200_set_resume(client);
	dev_info(&client->dev, "resumed\n");
	return 0;
}


static const struct i2c_device_id itg3200_id[] = {
	{ "itg3200", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, itg3200_id);

static struct i2c_driver itg3200_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name	= "itg3200",
	},
	.id_table	= itg3200_id,
	.probe		= itg3200_probe,
	.remove		= itg3200_remove,

	.class		= I2C_CLASS_HWMON,
	.detect		= itg3200_detect,
	//.address_list	= normal_i2c,

	.suspend	= itg3200_suspend,
	.resume         = itg3200_resume,
};

static int __init itg3200_init(void)
{
	return i2c_add_driver(&itg3200_driver);
}

static void __exit itg3200_exit(void)
{
	i2c_del_driver(&itg3200_driver);
}


MODULE_AUTHOR("Christoph Mair <christoph.mair@gmail.com");
MODULE_DESCRIPTION("ITG3200 driver");
MODULE_LICENSE("GPL");

module_init(itg3200_init);
module_exit(itg3200_exit);
