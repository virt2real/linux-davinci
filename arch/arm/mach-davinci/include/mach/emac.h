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
};

void davinci_init_emac(char *mac_addr);
#endif


