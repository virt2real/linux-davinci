/***************************************************************************
 *   Copyright (C) 2009 by Shlomo Kut,,,   *
 *   shl...@shlomo-desktop   *
 *
 *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or *
 *   (at your option) any later version.
 *
 *
 *
 *   This program is distributed in the hope that it will be useful,
 *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *   GNU General Public License for more details.
 *
 *
 *
 *   You should have received a copy of the GNU General Public License
 *
 *   along with this program; if not, write to the
 *
 *   Free Software Foundation, Inc.,
 *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *

***************************************************************************/
#ifndef _ADC_h
#define _ADC_H

#define ADC_MAX_CHANNELS 6

int adc_single(unsigned int channel);

#define DM365_ADC_BASE      (0x01C23C00)
#define DM365_ADC_ADCTL     0x0
#define DM365_ADC_ADCTL_BIT_BUSY    (1 << 7)
#define DM365_ADC_ADCTL_BIT_CMPFLG  (1 << 6)
#define DM365_ADC_ADCTL_BIT_CMPIEN  (1 << 5)
#define DM365_ADC_ADCTL_BIT_CMPMD   (1 << 4)
#define DM365_ADC_ADCTL_BIT_SCNFLG  (1 << 3)
#define DM365_ADC_ADCTL_BIT_SCNIEN  (1 << 2)
#define DM365_ADC_ADCTL_BIT_SCNMD   (1 << 1)
#define DM365_ADC_ADCTL_BIT_START   (1 << 0)

#define DM365_ADC_CMPTGT    0x4
#define DM365_ADC_CMPLDAT   0x8
#define DM365_ADC_CMPHDAT   0xC
#define DM365_ADC_SETDIV    0x10
#define DM365_ADC_CHSEL     0x14

#define DM365_ADC_AD0DAT    0x18
#define DM365_ADC_AD1DAT    0x1C
#define DM365_ADC_AD2DAT    0x20
#define DM365_ADC_AD3DAT    0x24
#define DM365_ADC_AD4DAT    0x28
#define DM365_ADC_AD5DAT    0x2C

#define DM365_ADC_EMUCTRL   0x30

#endif //_ADC_H
