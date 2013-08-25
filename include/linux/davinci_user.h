/*
 * Copyright (C) 2011 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _DAVINCI_USER_H
#define _DAVINCI_USER_H

#ifdef CONFIG_ARCH_DAVINCI_DM365
#include <../include/media/davinci/dm365_ccdc.h>
#endif

#ifdef CONFIG_ARCH_DAVINCI_DM355
#include <../include/media/davinci/dm355_ccdc.h>
#endif

#ifdef CONFIG_ARCH_DAVINCI_DM644x
#include <../include/media/davinci/dm644x_ccdc.h>
#endif

#define VPFE_CMD_S_CCDC_RAW_PARAMS _IOW('V', 1, \
					struct ccdc_config_params_raw)
#define VPFE_CMD_G_CCDC_RAW_PARAMS _IOR('V', 2, \
					struct ccdc_config_params_raw)
#endif

/*BASE_VIDIOC_PRIVATE + */
/*BASE_VIDIOC_PRIVATE + */
