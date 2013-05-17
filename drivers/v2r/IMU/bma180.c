/*  Copyright (c) 2011  Christoph Mair <christoph.mair@gmail.com>

    This driver supports the BMA180 digital accelerometer from Bosch Sensortec.
    
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
#include <linux/delay.h>

#define BMA180_I2C_ADDRESS			0x41

struct bma180_meas_data {
	s16 accel_x;
	s16 accel_y;
	s16 accel_z;
	s8 temp;
};


static const unsigned short normal_i2c[] = { BMA180_I2C_ADDRESS, I2C_CLIENT_END };


static s32 bma180_read_data(struct i2c_client *client, struct bma180_meas_data *data)
{
	s32 result = i2c_smbus_read_i2c_block_data(client, 0x02, 7, (u8*)data);

	return result;
}


static ssize_t show_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 result;
	struct bma180_meas_data data;

	result = bma180_read_data(client, &data);
	if (result > 0) {
//	  return sprintf(buf, "Lis302DLXSource %d\nLis302DLYSource %d\nLis302DLYSource %d\n", data.accel_x>>2, data.accel_y>>2, data.accel_z>>2);
	  return sprintf(buf, "%d,%d,%d\n", data.accel_x>>2, data.accel_y>>2, data.accel_z>>2);	}
	return result;
}
static DEVICE_ATTR(coord, S_IRUGO, show_data, NULL);


static struct attribute *bma180_attributes[] = {
	&dev_attr_coord.attr,
	NULL
};

static const struct attribute_group bma180_attr_group = {
	.attrs = bma180_attributes,
};


static int bma180_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	/* Don't know how to identify this chip. Just assume it's there */
	strlcpy(info->type, "bma180", I2C_NAME_SIZE);
	return 0;
}


static int bma180_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err = 0;
	u8 serial[8];

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &bma180_attr_group);
	if (err) {
		dev_err(&client->dev, "registering with sysfs failed!\n");
		goto exit;
	}

	dev_info(&client->dev, "probe succeeded!\n");

  exit:
	return err;
}

static int bma180_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &bma180_attr_group);
	return 0;
}


static const struct i2c_device_id bma180_id[] = {
	{ "bma180", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma180_id);

static struct i2c_driver bma180_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name	= "bma180",
	},
	.id_table	= bma180_id,
	.probe		= bma180_probe,
	.remove		= bma180_remove,

	.class		= I2C_CLASS_HWMON,
	.detect		= bma180_detect,
	//.address_list	= normal_i2c,
};

static int __init bma180_init(void)
{
	return i2c_add_driver(&bma180_driver);
}

static void __exit bma180_exit(void)
{
	i2c_del_driver(&bma180_driver);
}


MODULE_AUTHOR("Christoph Mair <christoph.mair@gmail.com");
MODULE_DESCRIPTION("BMA180 driver");
MODULE_LICENSE("GPL");

module_init(bma180_init);
module_exit(bma180_exit);
