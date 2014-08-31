/* 
    Virt2real External PWM driver v0.1
*/


#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>

#define PCA9685_MODE1 0x0
#define PCA9685_MODE2 0x1
#define PCA9685_PRESCALE 0xFE

/*
  output modes
  0 - text mode (default)
  1 - binary mode
*/

int output_mode = 0;

static ssize_t set_init(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	s32 mode1 = 0, mode2 = 0;
	struct i2c_client *client = to_i2c_client(dev);

	if (!count)  return count;

	if (buf[0] != '1' && buf[0] != 1) return count;

	// read current mode1
	mode1 = i2c_smbus_read_byte_data(client, PCA9685_MODE1);

	// set bit 5 (auto_increment ON)
	mode1 = (mode1 & 0xFF) | 0x20;

	// write new mode1	
	i2c_smbus_write_byte_data(client, PCA9685_MODE1, mode1);

	// read current mode2
	mode2 = i2c_smbus_read_byte_data(client, PCA9685_MODE2);

	// set bit 2 (turn on totem pole structure)
	mode2 = (mode2 & 0xFF) | 0x04;

	// write new mode2
	i2c_smbus_write_byte_data(client, PCA9685_MODE2, mode2);

	return count;
}

static ssize_t set_mode1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	s32 result = 0;
	s32 value = 0;
	struct i2c_client *client = to_i2c_client(dev);

	if (!count)  return count;

	if (buf[0] > 1)
		strict_strtoul(buf, 10, &value);
	else
		value = buf[1];

	i2c_smbus_write_byte_data(client, PCA9685_MODE1, value);

	return count;
}

static ssize_t get_mode1(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 result;
	struct i2c_client *client = to_i2c_client(dev);

	result = i2c_smbus_read_byte_data(client, PCA9685_MODE1);

	/* if error */
	if (result < 0) {

		dev_err(&client->dev, "error reading PCA9685_MODE1\n");

		return 0;
	}

	return sprintf(buf, "%d\n", result);
}


static ssize_t set_mode2(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	s32 result = 0;
	s32 value = 0;
	struct i2c_client *client = to_i2c_client(dev);

	if (!count)  return count;

	if (buf[0] > 1)
		strict_strtoul(buf, 10, &value);
	else
		value = buf[1];

	result = i2c_smbus_write_byte_data(client, PCA9685_MODE2, value);

	return count;
}

static ssize_t get_mode2(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 result = i2c_smbus_read_byte_data(client, PCA9685_MODE2);

	/* if error */
	if (result < 0) {

		dev_err(&client->dev, "error reading PCA9685_MODE2\n");

		return 0;
	}

	return sprintf(buf, "%d\n", result);
}


static ssize_t set_freq(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	s32 result;
	struct i2c_client *client = to_i2c_client(dev);
	s32 current_mode = 0;
	s32 sleep_mode = 0;
	s32 value = 0;

	if (!count)  return count;

	if (buf[0] > 1)
		strict_strtoul(buf, 10, &value);
	else
		value = buf[1];

	/* turn on sleep mode */
	current_mode = i2c_smbus_read_byte_data(client, PCA9685_MODE1);

	/* if error */
	if (current_mode < 0) {

		dev_err(&client->dev, "error reading PCA9685_MODE1\n");

		return count;
	}

	sleep_mode = (current_mode & 0x7F) | 0x10;

        i2c_smbus_write_byte_data(client, PCA9685_MODE1, sleep_mode);
	
	/* write new frequency*/
	result = i2c_smbus_write_byte_data(client, PCA9685_PRESCALE, value);
	udelay(500);

	/* restore current mode */
	//i2c_smbus_write_byte_data(client, PCA9685_MODE1, current_mode);
	i2c_smbus_write_byte_data(client, PCA9685_MODE1, 0x80);
	udelay(500);
	i2c_smbus_write_byte_data(client, PCA9685_MODE1, 0x20);
	udelay(500);
	i2c_smbus_write_byte_data(client, PCA9685_MODE2, 0x04);

	return count;
}

static ssize_t get_freq(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 result = i2c_smbus_read_byte_data(client, PCA9685_PRESCALE);

	/* if error */
	if (result < 0) {

		dev_err(&client->dev, "error reading PCA9685_PRESCALE\n");

		return 0;
	}

	return sprintf(buf, "%d\n", result);
}

static ssize_t set_cmdmode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	if (!count) return count;
	output_mode = buf[0] & 1;
	return count;
}

static ssize_t get_cmdmode(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", output_mode);
}

static ssize_t set_sleep(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	s32 result;
	s8 sleep;
	s32 current_mode;
	struct i2c_client *client = to_i2c_client(dev);

	if (!count)  return count;

	if (buf[0] == 0 || buf[0] == '0') sleep = 0;
	if (buf[0] == 1 || buf[0] == '1') sleep = 1;

	current_mode = i2c_smbus_read_byte_data(client, PCA9685_MODE1);

	if (!sleep)
		current_mode = (current_mode & 0xFF) & 0xEF;
	else
		current_mode = (current_mode & 0xFF) | 0x10;

	i2c_smbus_write_byte_data(client, PCA9685_MODE1, current_mode);

	printk("set sleep %d %lu\n", sleep, current_mode);

	return count;
}

static ssize_t get_sleep(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 result;
	result = i2c_smbus_read_byte_data(client, PCA9685_MODE1);

	/* if error */
	if (result < 0) {

		dev_err(&client->dev, "error reading PCA9685_MODE1\n");

		return 0;
	}

	result = (result >> 4 ) & 1;
	return sprintf(buf, "%d\n", result);
}


static DEVICE_ATTR(cmdmode, S_IWUSR | S_IRUGO, get_cmdmode, set_cmdmode);
static DEVICE_ATTR(init, S_IWUSR, NULL, set_init);
static DEVICE_ATTR(mode1, S_IWUSR | S_IRUGO, get_mode1, set_mode1);
static DEVICE_ATTR(mode2, S_IWUSR | S_IRUGO, get_mode2, set_mode2);
static DEVICE_ATTR(freq, S_IWUSR  | S_IRUGO, get_freq,  set_freq);
static DEVICE_ATTR(sleep, S_IWUSR  | S_IRUGO, get_sleep,  set_sleep);


static ssize_t setchannelcommon(int id, struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long value0 = 0, value1 = 0, value;
	int i;
	char *part;
	char *temp_string;
	s8 address; 

	// make address for all channels
	if (id == 0xFF) 
		address = 0xFA;
	else 
		address = 0x06 + id * 4;

	if (buf[0] > 1) {
			// split cmd string via spaces (" ")
			temp_string = kstrdup(buf, GFP_KERNEL);
			i = 0;
			do {
				part = strsep(&temp_string, " ");
				if (part) {
					/* not good but it works */
					if (i == 0) strict_strtoul(part, 10, &value0);
					if (i == 1) strict_strtoul(part, 10, &value1);
					i++;
				}
			
			} while (part);

			value = value0 | (value1 << 16);

			i2c_smbus_write_i2c_block_data(client, address, 4, &value);
	} else {
			if (count < 4) {
				dev_err(&client->dev, "wrong command length\n");
				return count;
			}

			if (buf[2] > 0x0F || buf[4] > 0x0F) {
				dev_err(&client->dev, "wrong command bytes\n");
				return count;
			}

			i2c_smbus_write_i2c_block_data(client, address, 4, buf+1);

	}

	return count;
}

static ssize_t getchannelcommon(int id, struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 result  = 0;
	s32 bigword = 0;

	struct i2c_client *client = to_i2c_client(dev);

	s8 address; 

	// make address for all channels
	if (id == 0xFF) 
		address = 0xFA;
	else 
		address = 0x06 + id * 4;


	result = i2c_smbus_read_i2c_block_data(client, address, 4, &bigword);

	if (result > 0) {

		switch (output_mode) {
			case 0:
				return sprintf(buf, "%lu %lu\n", bigword & 0xFFFF, (bigword >> 16 ) & 0xFFFF);
				break;
			case 1:
				/* not implemented yet */
				return sprintf(buf, "%lu %lu\n", bigword & 0xFFFF, (bigword >> 16 ) & 0xFFFF);
				break;
		}
	}

	return result;
}


static ssize_t setanychannel(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { 
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long id = 0, value0 = 0, value1 = 0, value;
	int i;
	char *part;
	char *temp_string;
	s8 address; 

	if (buf[0] > 1) {
			// split cmd string via spaces (" ")
			temp_string = kstrdup(buf, GFP_KERNEL);
			i = 0;
			do {
				part = strsep(&temp_string, " ");
				if (part) {
					/* not good but it works */
					if (i == 0) strict_strtoul(part, 10, &id);
					if (i == 1) strict_strtoul(part, 10, &value0);
					if (i == 2) strict_strtoul(part, 10, &value1);
					i++;
				}
			
			} while (part);

			value = value0 | (value1 << 16);
			address = 0x06 + id * 4;

			i2c_smbus_write_i2c_block_data(client, address, 4, &value);
	} else {
			if (count < 6) {
				dev_err(&client->dev, "wrong command length\n");
				return count;
			}

			if (buf[3] > 0x0F || buf[5] > 0x0F) {
				dev_err(&client->dev, "wrong command bytes\n");
				return count;
			}

			id = buf[1];
			address = 0x06 + id * 4;

			i2c_smbus_write_i2c_block_data(client, address, 4, buf+2);

	}

	return count;
}


static ssize_t setallchannels(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(0xff, dev, attr, buf, count); }
static ssize_t getallchannels(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(0xff, dev, attr, buf); }

static ssize_t setchannel0(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(0, dev, attr, buf, count); }
static ssize_t getchannel0(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(0, dev, attr, buf); }
static ssize_t setchannel1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(1, dev, attr, buf, count); }
static ssize_t getchannel1(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(1, dev, attr, buf); }
static ssize_t setchannel2(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(2, dev, attr, buf, count); }
static ssize_t getchannel2(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(2, dev, attr, buf); }
static ssize_t setchannel3(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(3, dev, attr, buf, count); }
static ssize_t getchannel3(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(3, dev, attr, buf); }
static ssize_t setchannel4(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(4, dev, attr, buf, count); }
static ssize_t getchannel4(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(4, dev, attr, buf); }
static ssize_t setchannel5(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(5, dev, attr, buf, count); }
static ssize_t getchannel5(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(5, dev, attr, buf); }
static ssize_t setchannel6(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(6, dev, attr, buf, count); }
static ssize_t getchannel6(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(6, dev, attr, buf); }
static ssize_t setchannel7(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(7, dev, attr, buf, count); }
static ssize_t getchannel7(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(7, dev, attr, buf); }
static ssize_t setchannel8(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(8, dev, attr, buf, count); }
static ssize_t getchannel8(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(8, dev, attr, buf); }
static ssize_t setchannel9(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(9, dev, attr, buf, count); }
static ssize_t getchannel9(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(9, dev, attr, buf); }
static ssize_t setchannel10(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(10, dev, attr, buf, count); }
static ssize_t getchannel10(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(10, dev, attr, buf); }
static ssize_t setchannel11(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(11, dev, attr, buf, count); }
static ssize_t getchannel11(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(11, dev, attr, buf); }
static ssize_t setchannel12(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(12, dev, attr, buf, count); }
static ssize_t getchannel12(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(12, dev, attr, buf); }
static ssize_t setchannel13(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(13, dev, attr, buf, count); }
static ssize_t getchannel13(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(13, dev, attr, buf); }
static ssize_t setchannel14(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(14, dev, attr, buf, count); }
static ssize_t getchannel14(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(14, dev, attr, buf); }
static ssize_t setchannel15(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) { return setchannelcommon(15, dev, attr, buf, count); }
static ssize_t getchannel15(struct device *dev, struct device_attribute *attr, char *buf) { return getchannelcommon(15, dev, attr, buf); }


static DEVICE_ATTR(all, S_IWUSR | S_IRUGO, getallchannels, setallchannels);
static DEVICE_ATTR(any, S_IWUSR, NULL, setanychannel);

static DEVICE_ATTR(ch0, S_IWUSR | S_IRUGO, getchannel0, setchannel0);
static DEVICE_ATTR(ch1, S_IWUSR | S_IRUGO, getchannel1, setchannel1);
static DEVICE_ATTR(ch2, S_IWUSR | S_IRUGO, getchannel2, setchannel2);
static DEVICE_ATTR(ch3, S_IWUSR | S_IRUGO, getchannel3, setchannel3);
static DEVICE_ATTR(ch4, S_IWUSR | S_IRUGO, getchannel4, setchannel4);
static DEVICE_ATTR(ch5, S_IWUSR | S_IRUGO, getchannel5, setchannel5);
static DEVICE_ATTR(ch6, S_IWUSR | S_IRUGO, getchannel6, setchannel6);
static DEVICE_ATTR(ch7, S_IWUSR | S_IRUGO, getchannel7, setchannel7);
static DEVICE_ATTR(ch8, S_IWUSR | S_IRUGO, getchannel8, setchannel8);
static DEVICE_ATTR(ch9, S_IWUSR | S_IRUGO, getchannel9, setchannel9);
static DEVICE_ATTR(ch10, S_IWUSR | S_IRUGO, getchannel10, setchannel10);
static DEVICE_ATTR(ch11, S_IWUSR | S_IRUGO, getchannel11, setchannel11);
static DEVICE_ATTR(ch12, S_IWUSR | S_IRUGO, getchannel12, setchannel12);
static DEVICE_ATTR(ch13, S_IWUSR | S_IRUGO, getchannel13, setchannel13);
static DEVICE_ATTR(ch14, S_IWUSR | S_IRUGO, getchannel14, setchannel14);
static DEVICE_ATTR(ch15, S_IWUSR | S_IRUGO, getchannel15, setchannel15);

static struct attribute *extpwm_attributes[] = {
	&dev_attr_sleep.attr,
	&dev_attr_cmdmode.attr,
	&dev_attr_init.attr,
	&dev_attr_mode1.attr,
	&dev_attr_mode2.attr,
	&dev_attr_freq.attr,
	&dev_attr_ch0.attr,
	&dev_attr_ch1.attr,
	&dev_attr_ch2.attr,
	&dev_attr_ch3.attr,
	&dev_attr_ch4.attr,
	&dev_attr_ch5.attr,
	&dev_attr_ch6.attr,
	&dev_attr_ch7.attr,
	&dev_attr_ch8.attr,
	&dev_attr_ch9.attr,
	&dev_attr_ch10.attr,
	&dev_attr_ch11.attr,
	&dev_attr_ch12.attr,
	&dev_attr_ch13.attr,
	&dev_attr_ch14.attr,
	&dev_attr_ch15.attr,
	&dev_attr_all.attr,
	&dev_attr_any.attr,
	NULL
};

static const struct attribute_group extpwm_attr_group = {
	.attrs = extpwm_attributes,
};


static int extpwm_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	/* Don't know how to identify this chip. Just assume it's there */
	strlcpy(info->type, "v2r_extpwm", I2C_NAME_SIZE);
	return 0;
}


static int extpwm_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err = 0;
	u8 serial[8];

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &extpwm_attr_group);
	if (err) {
		dev_err(&client->dev, "registering with sysfs failed!\n");
		goto exit;
	}

	dev_info(&client->dev, "probe succeeded!\n");

  exit:
	return err;
}

static int extpwm_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &extpwm_attr_group);
	return 0;
}


static const struct i2c_device_id extpwm_id[] = {
	{ "v2r_extpwm", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, extpwm_id);

static struct i2c_driver extpwm_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name	= "v2r_extpwm",
	},
	.id_table	= extpwm_id,
	.probe		= extpwm_probe,
	.remove		= extpwm_remove,

	.class		= I2C_CLASS_HWMON,
	.detect		= extpwm_detect,
};

static int __init extpwm_init(void)
{
	return i2c_add_driver(&extpwm_driver);
}

static void __exit extpwm_exit(void)
{
	i2c_del_driver(&extpwm_driver);
}


MODULE_AUTHOR("Gol gol@g0l.ru");
MODULE_DESCRIPTION("External PWM driver v0.1");
MODULE_LICENSE("GPL v2");

module_init(extpwm_init);
module_exit(extpwm_exit);
