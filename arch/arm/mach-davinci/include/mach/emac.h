/*
 * TI DaVinci EMAC platform support
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef _MACH_DAVINCI_EMAC_H
#define _MACH_DAVINCI_EMAC_H

struct emac_platform_data {
	char mac_addr[6];
	u32 ctrl_reg_offset;
	u32 ctrl_mod_reg_offset;
	u32 ctrl_ram_offset;
	u32 mdio_reg_offset;
	u32 ctrl_ram_size;
	u32 phy_mask;
	u32 mdio_max_freq;
};

void davinci_init_emac(struct emac_platform_data *pdata);
#endif


