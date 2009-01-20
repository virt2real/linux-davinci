/*
 *  Information structures for board-specific data
 *
 *  Derived from OMAP board.h:
 *  	Copyright (C) 2004	Nokia Corporation
 *  	Written by Juha Yrjölä <juha.yrjola@nokia.com>
 */

#ifndef _DAVINCI_BOARD_H
#define _DAVINCI_BOARD_H

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
