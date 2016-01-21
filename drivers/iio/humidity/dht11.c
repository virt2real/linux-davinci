/*
 * DHT11/DHT22 bit banging GPIO driver
 *
 * Copyright (c) Harald Geyer <harald@ccbib.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/iio/iio.h>
#include <linux/platform_data/dht11.h>

#define DRIVER_NAME	"dht11"

#define DHT11_DATA_VALID_TIME	2000000000  /* 2s in ns */

#define DHT11_BITS_PER_READ 40
/* We care only about falling edges */
#define DHT11_EDGES_PER_READ (DHT11_BITS_PER_READ + 2)

/* Data transmission timing */
#define DHT11_START_TRANSMISSION	30  /* ms */

/* Each bit on wire consists of 'start' part - 50 us low state,
 * followed by high state staying for 26-28 us for '0' and 70 us for '1'.
 * It's good to have a big tolerance for 0/1 distinguishing threshold.
 * 60 us works fine.
 */
#define DHT11_BIT_START		50000  /* us */
#define DHT11_BIT_THRESHOLD	50000  /* us */

struct dht11 {
	struct device			*dev;

	int				gpio;
	int				irq;

	struct completion		completion;
	struct mutex			lock;

	s64				timestamp;
	int				error;
	int				temperature;
	int				humidity;

	/* num_edges: -1 means "no transmission in progress" */
	int				num_edges;
	s64				edges[DHT11_EDGES_PER_READ];
};

static unsigned char dht11_decode_byte(s64 *edges, int offset)
{
	unsigned char ret = 0;
	int i;

	for (i = 0; i < 8; ++i) {
		ret <<= 1;
		if (edges[offset+i+1] - edges[offset+i] > DHT11_BIT_START + DHT11_BIT_THRESHOLD)
			ret |= 1;
	}

	return ret;
}

static int dht11_decode(struct dht11 *dht11)
{
	unsigned char temp_int, temp_dec, hum_int, hum_dec, checksum;
	s64 *edges = dht11->edges + 1;

	hum_int = dht11_decode_byte(edges, 0);
	hum_dec = dht11_decode_byte(edges, 8);
	temp_int = dht11_decode_byte(edges, 16);
	temp_dec = dht11_decode_byte(edges, 24);
	checksum = dht11_decode_byte(edges, 32);

	if (((hum_int + hum_dec + temp_int + temp_dec) & 0xff) != checksum)
		return -EIO;

	if (hum_int < 20) {  /* DHT22 */
		dht11->temperature = (((temp_int & 0x7f) << 8) + temp_dec) *
					((temp_int & 0x80) ? -100 : 100);
		dht11->humidity = ((hum_int << 8) + hum_dec) * 100;
	} else if (temp_dec == 0 && hum_dec == 0) {  /* DHT11 */
		dht11->temperature = temp_int * 1000;
		dht11->humidity = hum_int * 1000;
	} else {
		dev_err(dht11->dev,
			"Don't know how to decode data: %d %d %d %d\n",
			hum_int, hum_dec, temp_int, temp_dec);
		return -EIO;
	}

	return 0;
}

/*
 * IRQ handler called on GPIO edges
 */
static irqreturn_t dht11_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct dht11 *dht11 = iio_priv(iio);

	/* TODO: Consider making the handler safe for IRQ sharing */
	if (dht11->num_edges < DHT11_EDGES_PER_READ && dht11->num_edges >= 0) {
		dht11->edges[dht11->num_edges++] = iio_get_time_ns();

		if (dht11->num_edges >= DHT11_EDGES_PER_READ)
			complete(&dht11->completion);
	}

	return IRQ_HANDLED;
}

static int dht11_read_raw(struct iio_dev *iio_dev,
			const struct iio_chan_spec *chan,
			int *val, int *val2, long m)
{
	struct dht11 *dht11 = iio_priv(iio_dev);
	int ret;

	mutex_lock(&dht11->lock);
	if (dht11->timestamp + DHT11_DATA_VALID_TIME < iio_get_time_ns()) {
		dht11->timestamp = iio_get_time_ns();

		reinit_completion(&dht11->completion);

		ret = gpio_direction_output(dht11->gpio, 0);
		if (ret)
			goto err;
		msleep(DHT11_START_TRANSMISSION);

		dht11->num_edges = 0;

		ret = gpio_direction_input(dht11->gpio);
		if (ret) {
			free_irq(dht11->irq, iio_dev);
			goto err;
		}

		ret = wait_for_completion_killable_timeout(&dht11->completion,
								 HZ);
		if (ret == 0 && dht11->num_edges < DHT11_EDGES_PER_READ - 1) {
			dev_err(&iio_dev->dev,
					"Only %d signal edges detected\n",
					dht11->num_edges);
			ret = -ETIMEDOUT;
		}
		if (ret < 0)
			goto err;

		ret = dht11_decode(dht11);
		if (ret)
			goto err;

		dht11->error = 0;
	}

	if (dht11->error) {
		ret = dht11->error;
		goto err;
	}

	ret = IIO_VAL_INT;
	if (chan->type == IIO_TEMP)
		*val = dht11->temperature;
	else if (chan->type == IIO_HUMIDITYRELATIVE)
		*val = dht11->humidity;
	else
		ret = -EINVAL;
err:
	if (ret < 0)
		dht11->error = ret;
	dht11->num_edges = -1;
	mutex_unlock(&dht11->lock);
	return ret;
}

static const struct iio_info dht11_iio_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= dht11_read_raw,
};

static const struct iio_chan_spec dht11_chan_spec[] = {
	{ .type = IIO_TEMP,
		.info_mask = BIT(IIO_CHAN_INFO_PROCESSED), },
	{ .type = IIO_HUMIDITYRELATIVE,
		.info_mask = BIT(IIO_CHAN_INFO_PROCESSED), }
};

static const struct of_device_id dht11_dt_ids[] = {
	{ .compatible = "dht11", },
	{ }
};
MODULE_DEVICE_TABLE(of, dht11_dt_ids);

static int dht11_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dht11_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *node = dev->of_node;
	struct dht11 *dht11;
	struct iio_dev *iio;
	int ret;

	iio = devm_iio_device_alloc(dev, sizeof(*dht11));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	dht11 = iio_priv(iio);
	dht11->dev = dev;
	
	if (pdata) {
		dht11->gpio = pdata->gpio;
	}
	else if (node) {
		dht11->gpio = ret = of_get_gpio(node, 0);
		if (ret < 0)
			return ret;
	}
	else {
		dev_err(&pdev->dev, "neither platform data nor Device Tree node available\n");
		return -EINVAL;
	}

	ret = devm_gpio_request_one(dev, dht11->gpio, GPIOF_IN, pdev->name);
	if (ret)
		return ret;

	dht11->irq = gpio_to_irq(dht11->gpio);
	if (dht11->irq < 0) {
		dev_err(dev, "GPIO %d has no interrupt\n", dht11->gpio);
		return -EINVAL;
	}
	ret = devm_request_irq(dev, dht11->irq, dht11_handle_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				pdev->name, iio);
	if (ret)
		return ret;

	dht11->timestamp = iio_get_time_ns() - DHT11_DATA_VALID_TIME - 1;
	dht11->num_edges = -1;

	platform_set_drvdata(pdev, iio);

	init_completion(&dht11->completion);
	mutex_init(&dht11->lock);
	iio->name = pdev->name;
	iio->dev.parent = &pdev->dev;
	iio->info = &dht11_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->channels = dht11_chan_spec;
	iio->num_channels = ARRAY_SIZE(dht11_chan_spec);

	return devm_iio_device_register(dev, iio);
}

static struct platform_driver dht11_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = dht11_dt_ids,
	},
	.probe  = dht11_probe,
};

module_platform_driver(dht11_driver);

MODULE_AUTHOR("Harald Geyer <harald@ccbib.org>");
MODULE_DESCRIPTION("DHT11 humidity/temperature sensor driver");
MODULE_LICENSE("GPL v2");
