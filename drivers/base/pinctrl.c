/*
 * Driver core interface to the pinctrl subsystem.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/device.h>
#include <linux/pinctrl/devinfo.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

/**
 * pinctrl_bind_pins() - called by the device core before probe
 * @dev: the device that is just about to probe
 */
int pinctrl_bind_pins(struct device *dev)
{
	struct dev_pin_info *dpi;
	int ret;

	/* Allocate a pin state container on-the-fly */
	if (!dev->pins) {
		dpi = devm_kzalloc(dev, sizeof(*dpi), GFP_KERNEL);
		if (!dpi)
			return -ENOMEM;
	} else
		dpi = dev->pins;

	/*
	 * Check if we already have a pinctrl handle, as we may arrive here
	 * after a deferral in the state selection below
	 */
	if (!dpi->p) {
		dpi->p = devm_pinctrl_get(dev);
		if (IS_ERR_OR_NULL(dpi->p)) {
			int ret = PTR_ERR(dpi->p);

			dev_dbg(dev, "no pinctrl handle\n");
			/* Only return deferrals */
			if (ret == -EPROBE_DEFER)
				return ret;
			return 0;
		}
	}

	/*
	 * For a newly allocated info struct, here is where we keep it,
	 * since at this point it actually contains something.
	 */
	dev->pins = dpi;

	/*
	 * We may have looked up the state earlier as well.
	 */
	if (!dpi->default_state) {
		dpi->default_state = pinctrl_lookup_state(dpi->p,
						PINCTRL_STATE_DEFAULT);
		if (IS_ERR(dpi->default_state)) {
			dev_dbg(dev, "no default pinctrl state\n");
			return 0;
		}
	}

	ret = pinctrl_select_state(dpi->p, dpi->default_state);
	if (ret) {
		dev_dbg(dev, "failed to activate default pinctrl state\n");

		/* Only return deferrals */
		if (ret == -EPROBE_DEFER)
			return ret;
		return 0;
	}

	return 0;
}
