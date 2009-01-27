/*
 *  Board-specific MMC configuration
 */

#ifndef _DAVINCI_MMC_H
#define _DAVINCI_MMC_H

#include <linux/types.h>

struct davinci_mmc_config {
	/* get_cd()/get_wp() may sleep */
	int	(*get_cd)(int module);
	int	(*get_ro)(int module);
	/* wires == 0 is equivalent to wires == 4 (4-bit parallel) */
	u8	wires;
};
void davinci_setup_mmc(int module, struct davinci_mmc_config *config);

#endif
