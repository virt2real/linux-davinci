/*
 * Copyright (C) 2007-2010 Texas Instruments Inc
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Andy Lowe (alowe@mvista.com), MontaVista Software
 * - Initial version
 * Murali Karicheri (mkaricheri@gmail.com), Texas Instruments Ltd.
 * - ported to sub device interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>

#include <mach/cputype.h>
#include <mach/hardware.h>

#include <media/davinci/vpss.h>
#include <media/v4l2-device.h>
#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe_osd.h>

#include <linux/io.h>
#include "vpbe_osd_regs.h"

#define MODULE_NAME	VPBE_OSD_SUBDEV_NAME
#define DAVINCI_DDR_BASE 0x80000000
/* register access routines */
static inline u32 osd_read(struct osd_state *sd, u32 offset)
{
	struct osd_state *osd = sd;

	return readl(osd->osd_base + offset);
}

static inline u32 osd_write(struct osd_state *sd, u32 val, u32 offset)
{
	struct osd_state *osd = sd;

	writel(val, osd->osd_base + offset);

	return val;
}

static inline u32 osd_set(struct osd_state *sd, u32 mask, u32 offset)
{
	struct osd_state *osd = sd;

	void __iomem *addr = osd->osd_base + offset;
	u32 val = readl(addr) | mask;

	writel(val, addr);

	return val;
}

static inline u32 osd_clear(struct osd_state *sd, u32 mask, u32 offset)
{
	struct osd_state *osd = sd;

	void __iomem *addr = osd->osd_base + offset;
	u32 val = readl(addr) & ~mask;

	writel(val, addr);

	return val;
}

static inline u32 osd_modify(struct osd_state *sd, u32 mask, u32 val,
				 u32 offset)
{
	struct osd_state *osd = sd;

	void __iomem *addr = osd->osd_base + offset;
	u32 new_val = (readl(addr) & ~mask) | (val & mask);

	writel(new_val, addr);

	return new_val;
}

/* define some macros for layer and pixfmt classification */
#define is_osd_win(layer) (((layer) == WIN_OSD0) || ((layer) == WIN_OSD1))
#define is_vid_win(layer) (((layer) == WIN_VID0) || ((layer) == WIN_VID1))
#define is_rgb_pixfmt(pixfmt) \
	(((pixfmt) == PIXFMT_RGB565) || ((pixfmt) == PIXFMT_RGB888))
#define is_yc_pixfmt(pixfmt) \
	(((pixfmt) == PIXFMT_YCbCrI) || ((pixfmt) == PIXFMT_YCrCbI) || \
	((pixfmt) == PIXFMT_NV12))
#define MAX_WIN_SIZE OSD_VIDWIN0XP_V0X
#define MAX_LINE_LENGTH (OSD_VIDWIN0OFST_V0LO << 5)

/**
 * _osd_dm6446_vid0_pingpong() - field inversion fix for DM6446
 * @sd - ptr to struct osd_state
 * @field_inversion - inversion flag
 * @fb_base_phys - frame buffer address
 * @lconfig - ptr to layer config
 *
 * This routine implements a workaround for the field signal inversion silicon
 * erratum described in Advisory 1.3.8 for the DM6446.  The fb_base_phys and
 * lconfig parameters apply to the vid0 window.  This routine should be called
 * whenever the vid0 layer configuration or start address is modified, or when
 * the OSD field inversion setting is modified.
 * Returns: 1 if the ping-pong buffers need to be toggled in the vsync isr, or
 *          0 otherwise
 */
static int _osd_dm6446_vid0_pingpong(struct osd_state *sd,
				     int field_inversion,
				     unsigned long fb_base_phys,
				     const struct osd_layer_config *lconfig)
{
	struct osd_platform_data *pdata;
	pdata = (struct osd_platform_data *)sd->dev->platform_data;
	if (pdata->field_inv_wa_enable) {

		if (!field_inversion || !lconfig->interlaced) {
			osd_write(sd, fb_base_phys & ~0x1F, OSD_VIDWIN0ADR);
			osd_write(sd, fb_base_phys & ~0x1F, OSD_PPVWIN0ADR);
			osd_modify(sd, OSD_MISCCTL_PPSW | OSD_MISCCTL_PPRV, 0,
				   OSD_MISCCTL);
			return 0;
		} else {
			unsigned miscctl = OSD_MISCCTL_PPRV;

			osd_write(sd,
				(fb_base_phys & ~0x1F) - lconfig->line_length,
				OSD_VIDWIN0ADR);
			osd_write(sd,
				(fb_base_phys & ~0x1F) + lconfig->line_length,
				OSD_PPVWIN0ADR);
			osd_modify(sd,
				OSD_MISCCTL_PPSW | OSD_MISCCTL_PPRV, miscctl,
				OSD_MISCCTL);

			return 1;
		}
	}
	return 0;
}

static int osd_get_field_inversion(struct osd_state *sd)
{
	struct osd_state *osd = sd;

	return osd->field_inversion;
}

static void _osd_set_field_inversion(struct osd_state *sd, int enable)
{
	unsigned fsinv = 0;

	if (enable)
		fsinv = OSD_MODE_FSINV;

	osd_modify(sd, OSD_MODE_FSINV, fsinv, OSD_MODE);
}

static void osd_set_field_inversion(struct osd_state *sd, int enable)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->field_inversion = (enable != 0);
	_osd_set_field_inversion(sd, enable);

	osd->pingpong =
	    _osd_dm6446_vid0_pingpong(sd, osd->field_inversion,
					       osd->win[WIN_VID0].fb_base_phys,
					       &osd->win[WIN_VID0].lconfig);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_get_background(struct osd_state *sd, enum osd_clut *clut,
				 unsigned char *clut_index)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*clut = osd->backg_clut;
	*clut_index = osd->backg_clut_index;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_background(struct osd_state *sd, enum osd_clut clut,
				unsigned char clut_index)
{
	u32 mode = 0;

	if (clut == RAM_CLUT)
		mode |= OSD_MODE_BCLUT;
	mode |= clut_index;
	osd_modify(sd, OSD_MODE_BCLUT | OSD_MODE_CABG, mode, OSD_MODE);
}

static void osd_set_background(struct osd_state *sd, enum osd_clut clut,
			       unsigned char clut_index)
{
	struct osd_state *osd = sd;
	unsigned long flags;
	spin_lock_irqsave(&osd->lock, flags);

	osd->backg_clut = clut;
	osd->backg_clut_index = clut_index;
	_osd_set_background(sd, clut, clut_index);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static int osd_get_interpolation_filter(struct osd_state *sd)
{
	struct osd_state *osd = sd;

	return osd->interpolation_filter;
}

static void _osd_set_interpolation_filter(struct osd_state *sd, int filter)
{
	struct osd_state *osd = sd;

	if (osd->vpbe_type == DM355_VPBE || osd->vpbe_type == DM365_VPBE)
		osd_clear(sd, OSD_EXTMODE_EXPMDSEL, OSD_EXTMODE);
	osd_modify(sd, OSD_MODE_EF, filter ? OSD_MODE_EF : 0, OSD_MODE);
}

static void osd_set_interpolation_filter(struct osd_state *sd, int filter)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->interpolation_filter = (filter != 0);
	_osd_set_interpolation_filter(sd, filter);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_get_cursor_config(struct osd_state *sd,
				  struct osd_cursor_config *cursor)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*cursor = osd->cursor.config;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_cursor_config(struct osd_state *sd,
				   const struct osd_cursor_config *cursor)
{
	struct osd_state *osd = sd;
	unsigned rectcur = 0;

	osd_write(sd, cursor->xsize, OSD_CURXL);
	osd_write(sd, cursor->xpos, OSD_CURXP);

	if (cursor->interlaced) {
		osd_write(sd, cursor->ypos >> 1, OSD_CURYP);
		if (osd->vpbe_type == DM644X_VPBE) {
			/* Must add 1 to ysize due to device erratum. */
			osd_write(sd, (cursor->ysize >> 1) + 1, OSD_CURYL);
		} else
			osd_write(sd, cursor->ysize >> 1, OSD_CURYL);
	} else {
		osd_write(sd, cursor->ypos, OSD_CURYP);
		if (osd->vpbe_type == DM644X_VPBE) {
			/* Must add 1 to ysize due to device erratum. */
			osd_write(sd, cursor->ysize + 1, OSD_CURYL);
		} else
			osd_write(sd, cursor->ysize, OSD_CURYL);
	}

	if (cursor->clut == RAM_CLUT)
		rectcur |= OSD_RECTCUR_CLUTSR;
	rectcur |= (cursor->clut_index << OSD_RECTCUR_RCAD_SHIFT);
	rectcur |= (cursor->h_width << OSD_RECTCUR_RCHW_SHIFT);
	rectcur |= (cursor->v_width << OSD_RECTCUR_RCVW_SHIFT);
	osd_modify(sd, OSD_RECTCUR_RCAD | OSD_RECTCUR_CLUTSR |
		   OSD_RECTCUR_RCHW | OSD_RECTCUR_RCVW, rectcur, OSD_RECTCUR);
}

static void osd_set_cursor_config(struct osd_state *sd,
				  struct osd_cursor_config *cursor)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	cursor->xsize = min(cursor->xsize, (unsigned)OSD_CURXL_RCSW);
	cursor->ysize = min(cursor->ysize, (unsigned)OSD_CURYL_RCSH);
	cursor->xpos = min(cursor->xpos, (unsigned)OSD_CURXP_RCSX);
	cursor->ypos = min(cursor->ypos, (unsigned)OSD_CURYP_RCSY);
	cursor->interlaced = (cursor->interlaced != 0);
	if (cursor->interlaced) {
		cursor->ysize &= ~1;
		cursor->ypos &= ~1;
	}
	cursor->h_width &= (OSD_RECTCUR_RCHW >> OSD_RECTCUR_RCHW_SHIFT);
	cursor->v_width &= (OSD_RECTCUR_RCVW >> OSD_RECTCUR_RCVW_SHIFT);
	cursor->clut = (cursor->clut == RAM_CLUT) ? RAM_CLUT : ROM_CLUT;

	osd->cursor.config = *cursor;
	_osd_set_cursor_config(sd, cursor);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static int osd_cursor_is_enabled(struct osd_state *sd)
{
	struct osd_state *osd = sd;

	return osd->cursor.is_enabled;
}

static void _osd_cursor_disable(struct osd_state *sd)
{
	osd_clear(sd, OSD_RECTCUR_RCACT, OSD_RECTCUR);
}

static void osd_cursor_disable(struct osd_state *sd)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->cursor.is_enabled = 0;
	_osd_cursor_disable(sd);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_cursor_enable(struct osd_state *sd)
{
	osd_set(sd, OSD_RECTCUR_RCACT, OSD_RECTCUR);
}

static void osd_cursor_enable(struct osd_state *sd)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->cursor.is_enabled = 1;
	_osd_cursor_enable(sd);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_get_vid_expansion(struct osd_state *sd,
				  enum osd_h_exp_ratio *h_exp,
				  enum osd_v_exp_ratio *v_exp)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*h_exp = osd->vid_h_exp;
	*v_exp = osd->vid_v_exp;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_vid_expansion(struct osd_state *sd,
				   enum osd_h_exp_ratio h_exp,
				   enum osd_v_exp_ratio v_exp)
{
	struct osd_state *osd = sd;
	u32 mode = 0, extmode = 0;

	switch (h_exp) {
	case H_EXP_OFF:
		break;
	case H_EXP_9_OVER_8:
		mode |= OSD_MODE_VHRSZ;
		break;
	case H_EXP_3_OVER_2:
		extmode |= OSD_EXTMODE_VIDHRSZ15;
		break;
	}

	switch (v_exp) {
	case V_EXP_OFF:
		break;
	case V_EXP_6_OVER_5:
		mode |= OSD_MODE_VVRSZ;
		break;
	}

	if ((osd->vpbe_type == DM355_VPBE) || (osd->vpbe_type == DM365_VPBE))
		osd_modify(sd, OSD_EXTMODE_VIDHRSZ15, extmode, OSD_EXTMODE);
	osd_modify(sd, OSD_MODE_VHRSZ | OSD_MODE_VVRSZ, mode, OSD_MODE);
}

static int osd_set_vid_expansion(struct osd_state *sd,
				 enum osd_h_exp_ratio h_exp,
				 enum osd_v_exp_ratio v_exp)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	if (h_exp == H_EXP_3_OVER_2 && (osd->vpbe_type == DM644X_VPBE))
		return -1;

	spin_lock_irqsave(&osd->lock, flags);

	osd->vid_h_exp = h_exp;
	osd->vid_v_exp = v_exp;
	_osd_set_vid_expansion(sd, h_exp, v_exp);

	spin_unlock_irqrestore(&osd->lock, flags);
	return 0;
}

static void osd_get_osd_expansion(struct osd_state *sd,
				  enum osd_h_exp_ratio *h_exp,
				  enum osd_v_exp_ratio *v_exp)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*h_exp = osd->osd_h_exp;
	*v_exp = osd->osd_v_exp;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_osd_expansion(struct osd_state *sd,
				   enum osd_h_exp_ratio h_exp,
				   enum osd_v_exp_ratio v_exp)
{
	struct osd_state *osd = sd;
	u32 mode = 0, extmode = 0;

	switch (h_exp) {
	case H_EXP_OFF:
		break;
	case H_EXP_9_OVER_8:
		mode |= OSD_MODE_OHRSZ;
		break;
	case H_EXP_3_OVER_2:
		extmode |= OSD_EXTMODE_OSDHRSZ15;
		break;
	}

	switch (v_exp) {
	case V_EXP_OFF:
		break;
	case V_EXP_6_OVER_5:
		mode |= OSD_MODE_OVRSZ;
		break;
	}

	if ((osd->vpbe_type == DM355_VPBE) || (osd->vpbe_type == DM365_VPBE))
		osd_modify(sd, OSD_EXTMODE_OSDHRSZ15, extmode, OSD_EXTMODE);
	osd_modify(sd, OSD_MODE_OHRSZ | OSD_MODE_OVRSZ, mode, OSD_MODE);
}

static int osd_set_osd_expansion(struct osd_state *sd,
				 enum osd_h_exp_ratio h_exp,
				 enum osd_v_exp_ratio v_exp)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	if (h_exp == H_EXP_3_OVER_2 && (osd->vpbe_type == DM644X_VPBE))
		return -1;

	spin_lock_irqsave(&osd->lock, flags);

	osd->osd_h_exp = h_exp;
	osd->osd_v_exp = v_exp;
	_osd_set_osd_expansion(sd, h_exp, v_exp);

	spin_unlock_irqrestore(&osd->lock, flags);
	return 0;
}

static void osd_get_blink_attribute(struct osd_state *sd, int *enable,
				    enum osd_blink_interval *blink)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*enable = osd->is_blinking;
	*blink = osd->blink;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_blink_attribute(struct osd_state *sd, int enable,
				     enum osd_blink_interval blink)
{
	u32 osdatrmd = 0;

	if (enable) {
		osdatrmd |= OSD_OSDATRMD_BLNK;
		osdatrmd |= blink << OSD_OSDATRMD_BLNKINT_SHIFT;
	}
	/* caller must ensure that OSD1 is configured in attribute mode */
	osd_modify(sd, OSD_OSDATRMD_BLNKINT | OSD_OSDATRMD_BLNK, osdatrmd,
		  OSD_OSDATRMD);
}

static void osd_set_blink_attribute(struct osd_state *sd, int enable,
				    enum osd_blink_interval blink)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->is_blinking = (enable != 0);
	osd->blink = blink;
	if (osd->win[WIN_OSD1].lconfig.pixfmt == PIXFMT_OSD_ATTR)
		_osd_set_blink_attribute(sd, enable, blink);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static enum osd_rom_clut osd_get_rom_clut(struct osd_state *sd)
{
	struct osd_state *osd = sd;

	return osd->rom_clut;
}

static void _osd_set_rom_clut(struct osd_state *sd,
			      enum osd_rom_clut rom_clut)
{
	if (rom_clut == ROM_CLUT0)
		osd_clear(sd, OSD_MISCCTL_RSEL, OSD_MISCCTL);
	else
		osd_set(sd, OSD_MISCCTL_RSEL, OSD_MISCCTL);
}

static void osd_set_rom_clut(struct osd_state *sd,
			     enum osd_rom_clut rom_clut)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->rom_clut = rom_clut;
	_osd_set_rom_clut(sd, rom_clut);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_clut_ycbcr(struct osd_state *sd,
				unsigned char clut_index,
				unsigned char y, unsigned char cb,
				unsigned char cr)
{
	/* wait until any previous writes to the CLUT RAM have completed */
	while (osd_read(sd, OSD_MISCCTL) & OSD_MISCCTL_CPBSY)
		cpu_relax();

	osd_write(sd, (y << OSD_CLUTRAMYCB_Y_SHIFT) | cb, OSD_CLUTRAMYCB);
	osd_write(sd, (cr << OSD_CLUTRAMCR_CR_SHIFT) | clut_index,
		  OSD_CLUTRAMCR);
}

static void osd_set_clut_ycbcr(struct osd_state *sd,
			       unsigned char clut_index, unsigned char y,
			       unsigned char cb, unsigned char cr)
{
	struct osd_state *osd = sd;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osd->clut_ram[clut_index][0] = y;
	osd->clut_ram[clut_index][1] = cb;
	osd->clut_ram[clut_index][2] = cr;
	_osd_set_clut_ycbcr(sd, clut_index, y, cb, cr);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_rgb_to_ycbcr(const unsigned char rgb[3],
			      unsigned char ycbcr[3])
{
	int y, cb, cr;
	int r = rgb[0];
	int g = rgb[1];
	int b = rgb[2];
	/*
	 * This conversion matrix corresponds to the conversion matrix used
	 * by the OSD to convert RGB values to YCbCr values.  All coefficients
	 * have been scaled by a factor of 2^22.
	 */
	static const int rgb_to_ycbcr[3][3] = {
		{1250330, 2453618, 490352},
		{-726093, -1424868, 2150957},
		{2099836, -1750086, -349759}
};

	y = rgb_to_ycbcr[0][0] * r + rgb_to_ycbcr[0][1] * g +
	    rgb_to_ycbcr[0][2] * b;
	cb = rgb_to_ycbcr[1][0] * r + rgb_to_ycbcr[1][1] * g +
	    rgb_to_ycbcr[1][2] * b;
	cr = rgb_to_ycbcr[2][0] * r + rgb_to_ycbcr[2][1] * g +
		rgb_to_ycbcr[2][2] * b;

	/* round and scale */
	y = ((y + (1 << 21)) >> 22);
	cb = ((cb + (1 << 21)) >> 22) + 128;
	cr = ((cr + (1 << 21)) >> 22) + 128;

	/* clip */
	y = (y < 0) ? 0 : y;
	y = (y > 255) ? 255 : y;
	cb = (cb < 0) ? 0 : cb;
	cb = (cb > 255) ? 255 : cb;
	cr = (cr < 0) ? 0 : cr;
	cr = (cr > 255) ? 255 : cr;

	ycbcr[0] = y;
	ycbcr[1] = cb;
	ycbcr[2] = cr;
}

static void osd_set_clut_rgb(struct osd_state *sd, unsigned char clut_index,
			     unsigned char r, unsigned char g, unsigned char b)
{
	struct osd_state *osd = sd;
	unsigned char rgb[3], ycbcr[3];
	unsigned long flags;

	rgb[0] = r;
	rgb[1] = g;
	rgb[2] = b;
	_osd_rgb_to_ycbcr(rgb, ycbcr);

	spin_lock_irqsave(&osd->lock, flags);

	osd->clut_ram[clut_index][0] = ycbcr[0];
	osd->clut_ram[clut_index][1] = ycbcr[1];
	osd->clut_ram[clut_index][2] = ycbcr[2];
	_osd_set_clut_ycbcr(sd, clut_index, ycbcr[0], ycbcr[1], ycbcr[2]);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static unsigned char osd_get_palette_map(struct osd_state *sd,
					 enum osd_win_layer osdwin,
					 unsigned char pixel_value)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned char clut_index;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	switch (win->lconfig.pixfmt) {
	case PIXFMT_1BPP:
		clut_index = osdwin_state->palette_map[pixel_value & 0x1];
		break;
	case PIXFMT_2BPP:
		clut_index = osdwin_state->palette_map[pixel_value & 0x3];
		break;
	case PIXFMT_4BPP:
		clut_index = osdwin_state->palette_map[pixel_value & 0xf];
		break;
	default:
		clut_index = 0;
		break;
	}

	spin_unlock_irqrestore(&osd->lock, flags);

	return clut_index;
}

static void _osd_set_palette_map(struct osd_state *sd,
				 enum osd_win_layer osdwin,
				 unsigned char pixel_value,
				 unsigned char clut_index,
				 enum osd_pix_format pixfmt)
{
	int bmp_reg, bmp_offset, bmp_mask, bmp_shift;
	static const int map_1bpp[] = { 0, 15 };
	static const int map_2bpp[] = { 0, 5, 10, 15 };

	switch (pixfmt) {
	case PIXFMT_1BPP:
		bmp_reg = map_1bpp[pixel_value & 0x1];
		break;
	case PIXFMT_2BPP:
		bmp_reg = map_2bpp[pixel_value & 0x3];
		break;
	case PIXFMT_4BPP:
		bmp_reg = pixel_value & 0xf;
		break;
	default:
		return;
	}

	switch (osdwin) {
	case OSDWIN_OSD0:
		bmp_offset = OSD_W0BMP01 + (bmp_reg >> 1) * sizeof(u32);
		break;
	case OSDWIN_OSD1:
		bmp_offset = OSD_W1BMP01 + (bmp_reg >> 1) * sizeof(u32);
		break;
	default:
		return;
	}

	if (bmp_reg & 1) {
		bmp_shift = 8;
		bmp_mask = 0xff << 8;
	} else {
		bmp_shift = 0;
		bmp_mask = 0xff;
	}

	osd_modify(sd, bmp_mask, clut_index << bmp_shift, bmp_offset);
}

static void osd_set_palette_map(struct osd_state *sd,
				enum osd_win_layer osdwin,
				unsigned char pixel_value,
				unsigned char clut_index)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	switch (win->lconfig.pixfmt) {
	case PIXFMT_1BPP:
		osdwin_state->palette_map[pixel_value & 0x1] = clut_index;
		break;
	case PIXFMT_2BPP:
		osdwin_state->palette_map[pixel_value & 0x3] = clut_index;
		break;
	case PIXFMT_4BPP:
		osdwin_state->palette_map[pixel_value & 0xf] = clut_index;
		break;
	default:
		spin_unlock_irqrestore(&osd->lock, flags);
		return;
	}

	_osd_set_palette_map(sd, osdwin, pixel_value, clut_index,
			      win->lconfig.pixfmt);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static int osd_get_rec601_attenuation(struct osd_state *sd,
				      enum osd_win_layer osdwin)
{
	struct osd_state *osd = sd;
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];

	return osdwin_state->rec601_attenuation;
}

static void _osd_set_rec601_attenuation(struct osd_state *sd,
					enum osd_win_layer osdwin, int enable)
{
	struct osd_state *osd = sd;
	switch (osdwin) {
	case OSDWIN_OSD0:
		if (osd->vpbe_type == DM644X_VPBE) {
			osd_modify(sd, OSD_OSDWIN0MD_ATN0E,
				  enable ? OSD_OSDWIN0MD_ATN0E : 0,
				  OSD_OSDWIN0MD);
		} else if ((osd->vpbe_type == DM355_VPBE) ||
			   (osd->vpbe_type == DM365_VPBE)) {
			osd_modify(sd, OSD_EXTMODE_ATNOSD0EN,
				  enable ? OSD_EXTMODE_ATNOSD0EN : 0,
				  OSD_EXTMODE);
		}
		break;
	case OSDWIN_OSD1:
		if (osd->vpbe_type == DM644X_VPBE) {
			osd_modify(sd, OSD_OSDWIN1MD_ATN1E,
				  enable ? OSD_OSDWIN1MD_ATN1E : 0,
				  OSD_OSDWIN1MD);
		} else if ((osd->vpbe_type == DM355_VPBE) ||
			   (osd->vpbe_type == DM365_VPBE)) {
			osd_modify(sd, OSD_EXTMODE_ATNOSD1EN,
				  enable ? OSD_EXTMODE_ATNOSD1EN : 0,
				  OSD_EXTMODE);
		}
		break;
	}
}

static void osd_set_rec601_attenuation(struct osd_state *sd,
				       enum osd_win_layer osdwin,
				       int enable)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osdwin_state->rec601_attenuation = (enable != 0);
	if (win->lconfig.pixfmt != PIXFMT_OSD_ATTR)
		_osd_set_rec601_attenuation(sd, osdwin, enable);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static enum osd_blending_factor
osd_get_blending_factor(struct osd_state *sd, enum osd_win_layer osdwin)
{
	struct osd_state *osd = sd;
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];

	return osdwin_state->blend;
}

static void _osd_set_blending_factor(struct osd_state *sd,
				     enum osd_win_layer osdwin,
				     enum osd_blending_factor blend)
{
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_modify(sd, OSD_OSDWIN0MD_BLND0,
			  blend << OSD_OSDWIN0MD_BLND0_SHIFT, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		osd_modify(sd, OSD_OSDWIN1MD_BLND1,
			  blend << OSD_OSDWIN1MD_BLND1_SHIFT, OSD_OSDWIN1MD);
		break;
	}
}

static void osd_set_blending_factor(struct osd_state *sd,
				    enum osd_win_layer osdwin,
				    enum osd_blending_factor blend)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osdwin_state->blend = blend;
	if (win->lconfig.pixfmt != PIXFMT_OSD_ATTR)
		_osd_set_blending_factor(sd, osdwin, blend);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_enable_rgb888_pixblend(struct osd_state *sd,
					enum osd_win_layer osdwin)
{

	osd_modify(sd, OSD_MISCCTL_BLDSEL, 0, OSD_MISCCTL);
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_modify(sd, OSD_EXTMODE_OSD0BLDCHR,
			  OSD_EXTMODE_OSD0BLDCHR, OSD_EXTMODE);
		break;
	case OSDWIN_OSD1:
		osd_modify(sd, OSD_EXTMODE_OSD1BLDCHR,
			  OSD_EXTMODE_OSD1BLDCHR, OSD_EXTMODE);
		break;
	}
}

static void _osd_enable_color_key(struct osd_state *sd,
				  enum osd_win_layer osdwin,
				  unsigned colorkey,
				  enum osd_pix_format pixfmt)
{
	struct osd_state *osd = sd;
	switch (pixfmt) {
	case PIXFMT_1BPP:
	case PIXFMT_2BPP:
	case PIXFMT_4BPP:
	case PIXFMT_8BPP:
		if (osd->vpbe_type == DM355_VPBE) {
			switch (osdwin) {
			case OSDWIN_OSD0:
				osd_modify(sd, OSD_TRANSPBMPIDX_BMP0,
					  colorkey <<
					  OSD_TRANSPBMPIDX_BMP0_SHIFT,
					  OSD_TRANSPBMPIDX);
				break;
			case OSDWIN_OSD1:
				osd_modify(sd, OSD_TRANSPBMPIDX_BMP1,
					  colorkey <<
					  OSD_TRANSPBMPIDX_BMP1_SHIFT,
					  OSD_TRANSPBMPIDX);
				break;
			}
		}
		break;
	case PIXFMT_RGB565:
		if (osd->vpbe_type == DM644X_VPBE) {
			osd_write(sd, colorkey & OSD_TRANSPVAL_RGBTRANS,
				  OSD_TRANSPVAL);
		} else if (osd->vpbe_type == DM355_VPBE) {
			osd_write(sd, colorkey & OSD_TRANSPVALL_RGBL,
				  OSD_TRANSPVALL);
		}
		break;
	case PIXFMT_YCbCrI:
	case PIXFMT_YCrCbI:
		if (osd->vpbe_type == DM355_VPBE)
			osd_modify(sd, OSD_TRANSPVALU_Y, colorkey,
				   OSD_TRANSPVALU);
		break;
	case PIXFMT_RGB888:
		if (osd->vpbe_type == DM355_VPBE) {
			osd_write(sd, colorkey & OSD_TRANSPVALL_RGBL,
				  OSD_TRANSPVALL);
			osd_modify(sd, OSD_TRANSPVALU_RGBU, colorkey >> 16,
				  OSD_TRANSPVALU);
		}
		break;
	default:
		break;
	}

	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_set(sd, OSD_OSDWIN0MD_TE0, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		osd_set(sd, OSD_OSDWIN1MD_TE1, OSD_OSDWIN1MD);
		break;
	}
}

static void osd_enable_color_key(struct osd_state *sd,
				 enum osd_win_layer osdwin,
				 unsigned colorkey)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osdwin_state->colorkey_blending = 1;
	osdwin_state->colorkey = colorkey;
	if (win->lconfig.pixfmt != PIXFMT_OSD_ATTR) {
		_osd_enable_color_key(sd, osdwin, colorkey,
					       win->lconfig.pixfmt);
	}

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_disable_color_key(struct osd_state *sd,
				   enum osd_win_layer osdwin)
{
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_clear(sd, OSD_OSDWIN0MD_TE0, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		osd_clear(sd, OSD_OSDWIN1MD_TE1, OSD_OSDWIN1MD);
		break;
	}
}

static void osd_disable_color_key(struct osd_state *sd,
				  enum osd_win_layer osdwin)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osdwin_state->colorkey_blending = 0;
	if (win->lconfig.pixfmt != PIXFMT_OSD_ATTR)
		_osd_disable_color_key(sd, osdwin);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_osd_clut(struct osd_state *sd,
			      enum osd_win_layer osdwin,
			      enum osd_clut clut)
{
	u32 winmd = 0;

	switch (osdwin) {
	case OSDWIN_OSD0:
		if (clut == RAM_CLUT)
			winmd |= OSD_OSDWIN0MD_CLUTS0;
		osd_modify(sd, OSD_OSDWIN0MD_CLUTS0, winmd, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		if (clut == RAM_CLUT)
			winmd |= OSD_OSDWIN1MD_CLUTS1;
		osd_modify(sd, OSD_OSDWIN1MD_CLUTS1, winmd, OSD_OSDWIN1MD);
		break;
	}
}

static void osd_set_osd_clut(struct osd_state *sd, enum osd_win_layer osdwin,
			     enum osd_clut clut)
{
	struct osd_state *osd = sd;
	enum osd_layer layer =
	    (osdwin == OSDWIN_OSD0) ? WIN_OSD0 : WIN_OSD1;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	osdwin_state->clut = clut;
	if (win->lconfig.pixfmt != PIXFMT_OSD_ATTR)
		_osd_set_osd_clut(sd, osdwin, clut);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static enum osd_clut osd_get_osd_clut(struct osd_state *sd,
				      enum osd_win_layer osdwin)
{
	struct osd_state *osd = sd;
	struct osd_osdwin_state *osdwin_state = &osd->osdwin[osdwin];

	return osdwin_state->clut;
}

static void osd_get_zoom(struct osd_state *sd, enum osd_layer layer,
			 enum osd_zoom_factor *h_zoom,
			 enum osd_zoom_factor *v_zoom)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*h_zoom = win->h_zoom;
	*v_zoom = win->v_zoom;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_set_zoom(struct osd_state *sd, enum osd_layer layer,
			  enum osd_zoom_factor h_zoom,
			  enum osd_zoom_factor v_zoom)
{
	u32 winmd = 0;

	switch (layer) {
	case WIN_OSD0:
		winmd |= (h_zoom << OSD_OSDWIN0MD_OHZ0_SHIFT);
		winmd |= (v_zoom << OSD_OSDWIN0MD_OVZ0_SHIFT);
		osd_modify(sd, OSD_OSDWIN0MD_OHZ0 | OSD_OSDWIN0MD_OVZ0, winmd,
			  OSD_OSDWIN0MD);
		break;
	case WIN_VID0:
		winmd |= (h_zoom << OSD_VIDWINMD_VHZ0_SHIFT);
		winmd |= (v_zoom << OSD_VIDWINMD_VVZ0_SHIFT);
		osd_modify(sd, OSD_VIDWINMD_VHZ0 | OSD_VIDWINMD_VVZ0, winmd,
			  OSD_VIDWINMD);
		break;
	case WIN_OSD1:
		winmd |= (h_zoom << OSD_OSDWIN1MD_OHZ1_SHIFT);
		winmd |= (v_zoom << OSD_OSDWIN1MD_OVZ1_SHIFT);
		osd_modify(sd, OSD_OSDWIN1MD_OHZ1 | OSD_OSDWIN1MD_OVZ1, winmd,
			  OSD_OSDWIN1MD);
		break;
	case WIN_VID1:
		winmd |= (h_zoom << OSD_VIDWINMD_VHZ1_SHIFT);
		winmd |= (v_zoom << OSD_VIDWINMD_VVZ1_SHIFT);
		osd_modify(sd, OSD_VIDWINMD_VHZ1 | OSD_VIDWINMD_VVZ1, winmd,
			  OSD_VIDWINMD);
		break;
	}
}

static void osd_set_zoom(struct osd_state *sd, enum osd_layer layer,
			 enum osd_zoom_factor h_zoom,
			 enum osd_zoom_factor v_zoom)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	win->h_zoom = h_zoom;
	win->v_zoom = v_zoom;
	_osd_set_zoom(sd, layer, h_zoom, v_zoom);

	spin_unlock_irqrestore(&osd->lock, flags);
}


static int osd_layer_is_enabled(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];

	return win->is_enabled;
}

static void _osd_disable_layer(struct osd_state *sd, enum osd_layer layer)
{
	switch (layer) {
	case WIN_OSD0:
		osd_clear(sd, OSD_OSDWIN0MD_OACT0, OSD_OSDWIN0MD);
		break;
	case WIN_VID0:
		osd_clear(sd, OSD_VIDWINMD_ACT0, OSD_VIDWINMD);
		break;
	case WIN_OSD1:
		/* disable attribute mode as well as disabling the window */
		osd_clear(sd, OSD_OSDWIN1MD_OASW | OSD_OSDWIN1MD_OACT1,
			  OSD_OSDWIN1MD);
		break;
	case WIN_VID1:
		osd_clear(sd, OSD_VIDWINMD_ACT1, OSD_VIDWINMD);
		break;
	}
}

static void osd_disable_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	if (!win->is_enabled) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return;
	}
	win->is_enabled = 0;

	_osd_disable_layer(sd, layer);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_enable_attribute_mode(struct osd_state *sd)
{
	/* enable attribute mode for OSD1 */
	osd_set(sd, OSD_OSDWIN1MD_OASW | OSD_OSDWIN1MD_OACT1, OSD_OSDWIN1MD);
}

static void _osd_enable_layer(struct osd_state *sd, enum osd_layer layer)
{
	switch (layer) {
	case WIN_OSD0:
		osd_set(sd, OSD_OSDWIN0MD_OACT0, OSD_OSDWIN0MD);
		break;
	case WIN_VID0:
		osd_set(sd, OSD_VIDWINMD_ACT0, OSD_VIDWINMD);
		break;
	case WIN_OSD1:
		/* enable OSD1 and disable attribute mode */
		osd_modify(sd, OSD_OSDWIN1MD_OASW | OSD_OSDWIN1MD_OACT1,
			  OSD_OSDWIN1MD_OACT1, OSD_OSDWIN1MD);
		break;
	case WIN_VID1:
		osd_set(sd, OSD_VIDWINMD_ACT1, OSD_VIDWINMD);
		break;
	}
}

static int osd_enable_layer(struct osd_state *sd, enum osd_layer layer,
			    int otherwin)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;
	struct osd_layer_config *cfg = &win->lconfig;

	spin_lock_irqsave(&osd->lock, flags);

	/*
	 * use otherwin flag to know this is the other vid window
	 * in YUV420 mode, if is, skip this check
	 */
	if (!otherwin && (!win->is_allocated ||
			!win->fb_base_phys ||
			!cfg->line_length ||
			!cfg->xsize ||
			!cfg->ysize)) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return -1;
	}

	if (win->is_enabled) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return 0;
	}
	win->is_enabled = 1;

	if (cfg->pixfmt != PIXFMT_OSD_ATTR)
		_osd_enable_layer(sd, layer);
	else {
		_osd_enable_attribute_mode(sd);
		_osd_set_blink_attribute(sd, osd->is_blinking, osd->blink);
	}

	spin_unlock_irqrestore(&osd->lock, flags);
	return 0;
}

static void _osd_start_layer(struct osd_state *sd, enum osd_layer layer,
			     unsigned long fb_base_phys,
			     unsigned long cbcr_ofst)
{
	struct osd_state *osd = sd;

	if (sd->vpbe_type == DM644X_VPBE) {
		switch (layer) {
		case WIN_OSD0:
			osd_write(osd, fb_base_phys & ~0x1F, OSD_OSDWIN0ADR);
			break;
		case WIN_VID0:
			osd_write(osd, fb_base_phys & ~0x1F, OSD_VIDWIN0ADR);
			break;
		case WIN_OSD1:
			osd_write(osd, fb_base_phys & ~0x1F, OSD_OSDWIN1ADR);
			break;
		case WIN_VID1:
			osd_write(osd, fb_base_phys & ~0x1F, OSD_VIDWIN1ADR);
			break;
	      }
	} else if (osd->vpbe_type == DM355_VPBE) {
		unsigned long fb_offset_32 =
		    (fb_base_phys - DAVINCI_DDR_BASE) >> 5;

		switch (layer) {
		case WIN_OSD0:
			osd_modify(osd, OSD_OSDWINADH_O0AH,
				  fb_offset_32 >> (16 -
						   OSD_OSDWINADH_O0AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(osd, fb_offset_32 & OSD_OSDWIN0ADL_O0AL,
				  OSD_OSDWIN0ADL);
			break;
		case WIN_VID0:
			osd_modify(osd, OSD_VIDWINADH_V0AH,
				  fb_offset_32 >> (16 -
						   OSD_VIDWINADH_V0AH_SHIFT),
				  OSD_VIDWINADH);
			osd_write(osd, fb_offset_32 & OSD_VIDWIN0ADL_V0AL,
				  OSD_VIDWIN0ADL);
			break;
		case WIN_OSD1:
			osd_modify(osd, OSD_OSDWINADH_O1AH,
				  fb_offset_32 >> (16 -
						   OSD_OSDWINADH_O1AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(osd, fb_offset_32 & OSD_OSDWIN1ADL_O1AL,
				  OSD_OSDWIN1ADL);
			break;
		case WIN_VID1:
			osd_modify(osd, OSD_VIDWINADH_V1AH,
				  fb_offset_32 >> (16 -
						   OSD_VIDWINADH_V1AH_SHIFT),
				  OSD_VIDWINADH);
			osd_write(osd, fb_offset_32 & OSD_VIDWIN1ADL_V1AL,
				  OSD_VIDWIN1ADL);
			break;
		}
	} else if (osd->vpbe_type == DM365_VPBE) {
		struct osd_window_state *win = &sd->win[layer];
		unsigned long fb_offset_32, cbcr_offset_32;

		fb_offset_32 = fb_base_phys - DAVINCI_DDR_BASE;
		if (cbcr_ofst)
			cbcr_offset_32 = cbcr_ofst;
		else
			cbcr_offset_32 = win->lconfig.line_length *
					 win->lconfig.ysize;
		cbcr_offset_32 += fb_offset_32;
		fb_offset_32 = fb_offset_32 >> 5;
		cbcr_offset_32 = cbcr_offset_32 >> 5;
		/*
		 * DM365: start address is 27-bit long address b26 - b23 are
		 * in offset register b12 - b9, and * bit 26 has to be '1'
		 */
		if (win->lconfig.pixfmt == PIXFMT_NV12) {
			switch (layer) {
			case WIN_VID0:
			case WIN_VID1:
				/* Y is in VID0 */
				osd_modify(osd, OSD_VIDWIN0OFST_V0AH,
					 ((fb_offset_32 & 0x7800000) >>
					 (23 - OSD_WINOFST_AH_SHIFT)) | 0x1000,
					  OSD_VIDWIN0OFST);
				osd_modify(osd, OSD_VIDWINADH_V0AH,
					  (fb_offset_32 & 0x7F0000) >>
					  (16 - OSD_VIDWINADH_V0AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(osd, fb_offset_32 & 0xFFFF,
					  OSD_VIDWIN0ADL);
				/* CbCr is in VID1 */
				osd_modify(osd, OSD_VIDWIN1OFST_V1AH,
					 ((cbcr_offset_32 & 0x7800000) >>
					 (23 - OSD_WINOFST_AH_SHIFT)) | 0x1000,
					  OSD_VIDWIN1OFST);
				osd_modify(osd, OSD_VIDWINADH_V1AH,
					  (cbcr_offset_32 & 0x7F0000) >>
					  (16 - OSD_VIDWINADH_V1AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(osd, cbcr_offset_32 & 0xFFFF,
					  OSD_VIDWIN1ADL);
				break;
			default:
				break;
			}
		}

		switch (layer) {
		case WIN_OSD0:
			osd_modify(osd, OSD_OSDWIN0OFST_O0AH,
				 ((fb_offset_32 & 0x7800000) >>
				 (23 - OSD_WINOFST_AH_SHIFT)) | 0x1000,
				  OSD_OSDWIN0OFST);
			osd_modify(osd, OSD_OSDWINADH_O0AH,
				 (fb_offset_32 & 0x7F0000) >>
				 (16 - OSD_OSDWINADH_O0AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(osd, fb_offset_32 & 0xFFFF, OSD_OSDWIN0ADL);
			break;
		case WIN_VID0:
			if (win->lconfig.pixfmt != PIXFMT_NV12) {
				osd_modify(osd, OSD_VIDWIN0OFST_V0AH,
					 ((fb_offset_32 & 0x7800000) >>
					 (23 - OSD_WINOFST_AH_SHIFT)) | 0x1000,
					  OSD_VIDWIN0OFST);
				osd_modify(osd, OSD_VIDWINADH_V0AH,
					  (fb_offset_32 & 0x7F0000) >>
					  (16 - OSD_VIDWINADH_V0AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(osd, fb_offset_32 & 0xFFFF,
					  OSD_VIDWIN0ADL);
			}
			break;
		case WIN_OSD1:
			osd_modify(osd, OSD_OSDWIN1OFST_O1AH,
				 ((fb_offset_32 & 0x7800000) >>
				 (23 - OSD_WINOFST_AH_SHIFT)) | 0x1000,
				  OSD_OSDWIN1OFST);
			osd_modify(osd, OSD_OSDWINADH_O1AH,
				  (fb_offset_32 & 0x7F0000) >>
				  (16 - OSD_OSDWINADH_O1AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(osd, fb_offset_32 & 0xFFFF, OSD_OSDWIN1ADL);
			break;
		case WIN_VID1:
			if (win->lconfig.pixfmt != PIXFMT_NV12) {
				osd_modify(osd, OSD_VIDWIN1OFST_V1AH,
					 ((fb_offset_32 & 0x7800000) >>
					 (23 - OSD_WINOFST_AH_SHIFT)) | 0x1000,
					  OSD_VIDWIN1OFST);
				osd_modify(osd, OSD_VIDWINADH_V1AH,
					  (fb_offset_32 & 0x7F0000) >>
					  (16 - OSD_VIDWINADH_V1AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(osd, fb_offset_32 & 0xFFFF,
					  OSD_VIDWIN1ADL);
			}
			break;
		}
	}
}

static void osd_start_layer(struct osd_state *sd, enum osd_layer layer,
			    unsigned long fb_base_phys,
			    unsigned long cbcr_ofst)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;
	struct osd_layer_config *cfg = &win->lconfig;

	spin_lock_irqsave(&osd->lock, flags);

	win->fb_base_phys = fb_base_phys & ~0x1F;
	_osd_start_layer(sd, layer, fb_base_phys, cbcr_ofst);

	if (layer == WIN_VID0) {
		osd->pingpong =
		    _osd_dm6446_vid0_pingpong(sd, osd->field_inversion,
						       win->fb_base_phys,
						       cfg);
	}

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_get_layer_config(struct osd_state *sd, enum osd_layer layer,
				 struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*lconfig = win->lconfig;

	spin_unlock_irqrestore(&osd->lock, flags);
}

/**
 * try_layer_config() - Try a specific configuration for the layer
 * @sd  - ptr to struct osd_state
 * @layer - layer to configure
 * @lconfig - layer configuration to try
 *
 * If the requested lconfig is completely rejected and the value of lconfig on
 * exit is the current lconfig, then try_layer_config() returns 1.  Otherwise,
 * try_layer_config() returns 0.  A return value of 0 does not necessarily mean
 * that the value of lconfig on exit is identical to the value of lconfig on
 * entry, but merely that it represents a change from the current lconfig.
 */
static int try_layer_config(struct osd_state *sd, enum osd_layer layer,
			    struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	int bad_config = 0;

	/* verify that the pixel format is compatible with the layer */
	switch (lconfig->pixfmt) {
	case PIXFMT_1BPP:
	case PIXFMT_2BPP:
	case PIXFMT_4BPP:
	case PIXFMT_8BPP:
	case PIXFMT_RGB565:
		if (osd->vpbe_type == DM644X_VPBE)
			bad_config = !is_osd_win(layer);
		break;
	case PIXFMT_YCbCrI:
	case PIXFMT_YCrCbI:
		bad_config = !is_vid_win(layer);
		break;
	case PIXFMT_RGB888:
		if (osd->vpbe_type == DM644X_VPBE)
			bad_config = !is_vid_win(layer);
		else if ((osd->vpbe_type == DM355_VPBE) ||
			 (osd->vpbe_type == DM365_VPBE))
			bad_config = !is_osd_win(layer);
		break;
	case PIXFMT_NV12:
		if (osd->vpbe_type != DM365_VPBE)
			bad_config = 1;
		else
			bad_config = is_osd_win(layer);
		break;
	case PIXFMT_OSD_ATTR:
		bad_config = (layer != WIN_OSD1);
		break;
	default:
		bad_config = 1;
		break;
	}
	if (bad_config) {
		/*
		 * The requested pixel format is incompatible with the layer,
		 * so keep the current layer configuration.
		 */
		*lconfig = win->lconfig;
		return bad_config;
	}

	/* DM6446: */
	/* only one OSD window at a time can use RGB pixel formats */
	  if ((osd->vpbe_type == DM644X_VPBE) &&
		  is_osd_win(layer) && is_rgb_pixfmt(lconfig->pixfmt)) {
		enum osd_pix_format pixfmt;
		if (layer == WIN_OSD0)
			pixfmt = osd->win[WIN_OSD1].lconfig.pixfmt;
		else
			pixfmt = osd->win[WIN_OSD0].lconfig.pixfmt;

		if (is_rgb_pixfmt(pixfmt)) {
			/*
			 * The other OSD window is already configured for an
			 * RGB, so keep the current layer configuration.
			 */
			*lconfig = win->lconfig;
			return 1;
		}
	}

	/* DM6446: only one video window at a time can use RGB888 */
	if ((osd->vpbe_type == DM644X_VPBE) && is_vid_win(layer)
		&& lconfig->pixfmt == PIXFMT_RGB888) {
		enum osd_pix_format pixfmt;

		if (layer == WIN_VID0)
			pixfmt = osd->win[WIN_VID1].lconfig.pixfmt;
		else
			pixfmt = osd->win[WIN_VID0].lconfig.pixfmt;

		if (pixfmt == PIXFMT_RGB888) {
			/*
			 * The other video window is already configured for
			 * RGB888, so keep the current layer configuration.
			 */
			*lconfig = win->lconfig;
			return 1;
		}
	}

	/* window dimensions must be non-zero */
	if (!lconfig->line_length || !lconfig->xsize || !lconfig->ysize) {
		*lconfig = win->lconfig;
		return 1;
	}

	/* round line_length up to a multiple of 32 */
	lconfig->line_length = ((lconfig->line_length + 31) / 32) * 32;
	lconfig->line_length =
	    min(lconfig->line_length, (unsigned)MAX_LINE_LENGTH);
	lconfig->xsize = min(lconfig->xsize, (unsigned)MAX_WIN_SIZE);
	lconfig->ysize = min(lconfig->ysize, (unsigned)MAX_WIN_SIZE);
	lconfig->xpos = min(lconfig->xpos, (unsigned)MAX_WIN_SIZE);
	lconfig->ypos = min(lconfig->ypos, (unsigned)MAX_WIN_SIZE);
	lconfig->interlaced = (lconfig->interlaced != 0);
	if (lconfig->interlaced) {
		/* ysize and ypos must be even for interlaced displays */
		lconfig->ysize &= ~1;
		lconfig->ypos &= ~1;
	}

	return 0;
}

static int osd_try_layer_config(struct osd_state *sd, enum osd_layer layer,
				struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	int reject_config;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	reject_config = try_layer_config(sd, layer, lconfig);

	spin_unlock_irqrestore(&osd->lock, flags);

	return reject_config;
}

static void _osd_disable_vid_rgb888(struct osd_state *sd)
{
	struct osd_state *osd = sd;
	/*
	 * The DM6446 supports RGB888 pixel format in a single video window.
	 * This routine disables RGB888 pixel format for both video windows.
	 * The caller must ensure that neither video window is currently
	 * configured for RGB888 pixel format.
	 */
	if (osd->vpbe_type == DM644X_VPBE)
		osd_clear(sd, OSD_MISCCTL_RGBEN, OSD_MISCCTL);
}

static void _osd_enable_vid_rgb888(struct osd_state *sd,
				   enum osd_layer layer)
{
	struct osd_state *osd = sd;
	/*
	 * The DM6446 supports RGB888 pixel format in a single video window.
	 * This routine enables RGB888 pixel format for the specified video
	 * window.  The caller must ensure that the other video window is not
	 * currently configured for RGB888 pixel format, as this routine will
	 * disable RGB888 pixel format for the other window.
	 */
	if (osd->vpbe_type == DM644X_VPBE) {
		if (layer == WIN_VID0) {
			osd_modify(sd, OSD_MISCCTL_RGBEN | OSD_MISCCTL_RGBWIN,
				  OSD_MISCCTL_RGBEN, OSD_MISCCTL);
		} else if (layer == WIN_VID1) {
			osd_modify(sd, OSD_MISCCTL_RGBEN | OSD_MISCCTL_RGBWIN,
				  OSD_MISCCTL_RGBEN | OSD_MISCCTL_RGBWIN,
				  OSD_MISCCTL);
		}
	}
}

static void _osd_set_cbcr_order(struct osd_state *sd,
				enum osd_pix_format pixfmt)
{
	/*
	 * The caller must ensure that all windows using YC pixfmt use the same
	 * Cb/Cr order.
	 */
	if (pixfmt == PIXFMT_YCbCrI)
		osd_clear(sd, OSD_MODE_CS, OSD_MODE);
	else if (pixfmt == PIXFMT_YCrCbI)
		osd_set(sd, OSD_MODE_CS, OSD_MODE);
}

static void _osd_set_layer_config(struct osd_state *sd, enum osd_layer layer,
				  const struct osd_layer_config *lconfig)
{
	u32 winmd = 0, winmd_mask = 0, bmw = 0;

	_osd_set_cbcr_order(sd, lconfig->pixfmt);

	switch (layer) {
	case WIN_OSD0:
		if (sd->vpbe_type == DM644X_VPBE) {
			winmd_mask |= OSD_OSDWIN0MD_RGB0E;
			if (lconfig->pixfmt == PIXFMT_RGB565)
				winmd |= OSD_OSDWIN0MD_RGB0E;
		} else if ((sd->vpbe_type == DM355_VPBE) ||
		  (sd->vpbe_type == DM365_VPBE)) {
			winmd_mask |= OSD_OSDWIN0MD_BMP0MD;
			switch (lconfig->pixfmt) {
			case PIXFMT_RGB565:
					winmd |= (1 <<
					OSD_OSDWIN0MD_BMP0MD_SHIFT);
					break;
			case PIXFMT_RGB888:
				winmd |= (2 << OSD_OSDWIN0MD_BMP0MD_SHIFT);
				_osd_enable_rgb888_pixblend(sd, OSDWIN_OSD0);
				break;
			case PIXFMT_YCbCrI:
			case PIXFMT_YCrCbI:
				winmd |= (3 << OSD_OSDWIN0MD_BMP0MD_SHIFT);
				break;
			default:
				break;
			}
		}

		winmd_mask |= OSD_OSDWIN0MD_BMW0 | OSD_OSDWIN0MD_OFF0;

			switch (lconfig->pixfmt) {
			case PIXFMT_1BPP:
				bmw = 0;
				break;
			case PIXFMT_2BPP:
				bmw = 1;
				break;
			case PIXFMT_4BPP:
				bmw = 2;
				break;
			case PIXFMT_8BPP:
				bmw = 3;
				break;
			default:
				break;
			}
		winmd |= (bmw << OSD_OSDWIN0MD_BMW0_SHIFT);

		if (lconfig->interlaced)
			winmd |= OSD_OSDWIN0MD_OFF0;

		osd_modify(sd, winmd_mask, winmd, OSD_OSDWIN0MD);
		osd_write(sd, lconfig->line_length >> 5, OSD_OSDWIN0OFST);
		osd_write(sd, lconfig->xpos, OSD_OSDWIN0XP);
		osd_write(sd, lconfig->xsize, OSD_OSDWIN0XL);
		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_OSDWIN0YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_OSDWIN0YL);
		} else {
			osd_write(sd, lconfig->ypos, OSD_OSDWIN0YP);
			osd_write(sd, lconfig->ysize, OSD_OSDWIN0YL);
		}
		break;
	case WIN_VID0:
		winmd_mask |= OSD_VIDWINMD_VFF0;
		if (lconfig->interlaced)
			winmd |= OSD_VIDWINMD_VFF0;

		osd_modify(sd, winmd_mask, winmd, OSD_VIDWINMD);
		osd_write(sd, lconfig->line_length >> 5, OSD_VIDWIN0OFST);
		osd_write(sd, lconfig->xpos, OSD_VIDWIN0XP);
		osd_write(sd, lconfig->xsize, OSD_VIDWIN0XL);
		/*
		 * For YUV420P format the register contents are
		 * duplicated in both VID registers
		 */
		if (sd->vpbe_type == DM365_VPBE) {
			if (lconfig->pixfmt == PIXFMT_NV12) {
				/* other window also */
				if (lconfig->interlaced) {
					winmd_mask |= OSD_VIDWINMD_VFF1;
					winmd |= OSD_VIDWINMD_VFF1;
					osd_modify(sd, winmd_mask, winmd,
						  OSD_VIDWINMD);
				}

				osd_modify(sd, OSD_MISCCTL_S420D,
					   OSD_MISCCTL_S420D, OSD_MISCCTL);
				osd_write(sd, lconfig->line_length >> 5,
					  OSD_VIDWIN1OFST);
				osd_write(sd, lconfig->xpos, OSD_VIDWIN1XP);
				osd_write(sd, lconfig->xsize, OSD_VIDWIN1XL);
				/*
				 * if NV21 pixfmt and line length not 32B
				 * aligned (e.g. NTSC), Need to set window
				 * X pixel size to be 32B aligned as well
				 */
				if (lconfig->xsize % 32) {
					osd_write(sd,
						 ((lconfig->xsize + 31) & ~31),
						 OSD_VIDWIN1XL);
					osd_write(sd,
						 ((lconfig->xsize + 31) & ~31),
						 OSD_VIDWIN0XL);
				}
			} else
				osd_modify(sd, OSD_MISCCTL_S420D,
					   ~OSD_MISCCTL_S420D, OSD_MISCCTL);
		}
		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_VIDWIN0YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_VIDWIN0YL);
			if ((sd->vpbe_type == DM365_VPBE)
			    && lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos >> 1,
					  OSD_VIDWIN1YP);
				osd_write(sd, lconfig->ysize >> 1,
					  OSD_VIDWIN1YL);
			}
		} else {
			osd_write(sd, lconfig->ypos, OSD_VIDWIN0YP);
			osd_write(sd, lconfig->ysize, OSD_VIDWIN0YL);
			if ((sd->vpbe_type == DM365_VPBE)
			    && lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos, OSD_VIDWIN1YP);
				osd_write(sd, lconfig->ysize, OSD_VIDWIN1YL);
			}
		}
		break;
	case WIN_OSD1:
		/*
		 * The caller must ensure that OSD1 is disabled prior to
		 * switching from a normal mode to attribute mode or from
		 * attribute mode to a normal mode.
		 */
		if (lconfig->pixfmt == PIXFMT_OSD_ATTR) {
			if (sd->vpbe_type == DM644X_VPBE) {
				winmd_mask |= OSD_OSDWIN1MD_ATN1E |
				OSD_OSDWIN1MD_RGB1E | OSD_OSDWIN1MD_CLUTS1 |
				OSD_OSDWIN1MD_BLND1 | OSD_OSDWIN1MD_TE1;
			} else {
				winmd_mask |=
				    OSD_OSDWIN1MD_BMP1MD | OSD_OSDWIN1MD_CLUTS1
				    | OSD_OSDWIN1MD_BLND1 | OSD_OSDWIN1MD_TE1;
			}
		} else {
			if (sd->vpbe_type == DM644X_VPBE) {
				winmd_mask |= OSD_OSDWIN1MD_RGB1E;
				if (lconfig->pixfmt == PIXFMT_RGB565)
					winmd |= OSD_OSDWIN1MD_RGB1E;
			} else if ((sd->vpbe_type == DM355_VPBE)
				   || (sd->vpbe_type == DM365_VPBE)) {
				winmd_mask |= OSD_OSDWIN1MD_BMP1MD;
				switch (lconfig->pixfmt) {
				case PIXFMT_RGB565:
					winmd |=
					    (1 << OSD_OSDWIN1MD_BMP1MD_SHIFT);
					break;
				case PIXFMT_RGB888:
					winmd |=
					    (2 << OSD_OSDWIN1MD_BMP1MD_SHIFT);
					_osd_enable_rgb888_pixblend(sd,
							OSDWIN_OSD1);
					break;
				case PIXFMT_YCbCrI:
				case PIXFMT_YCrCbI:
					winmd |=
					    (3 << OSD_OSDWIN1MD_BMP1MD_SHIFT);
					break;
				default:
					break;
				}
			}

			winmd_mask |= OSD_OSDWIN1MD_BMW1;
			switch (lconfig->pixfmt) {
			case PIXFMT_1BPP:
				bmw = 0;
				break;
			case PIXFMT_2BPP:
				bmw = 1;
				break;
			case PIXFMT_4BPP:
				bmw = 2;
				break;
			case PIXFMT_8BPP:
				bmw = 3;
				break;
			default:
				break;
			}
			winmd |= (bmw << OSD_OSDWIN1MD_BMW1_SHIFT);
		}

		winmd_mask |= OSD_OSDWIN1MD_OFF1;
		if (lconfig->interlaced)
			winmd |= OSD_OSDWIN1MD_OFF1;

		osd_modify(sd, winmd_mask, winmd, OSD_OSDWIN1MD);
		osd_write(sd, lconfig->line_length >> 5, OSD_OSDWIN1OFST);
		osd_write(sd, lconfig->xpos, OSD_OSDWIN1XP);
		osd_write(sd, lconfig->xsize, OSD_OSDWIN1XL);
		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_OSDWIN1YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_OSDWIN1YL);
		} else {
			osd_write(sd, lconfig->ypos, OSD_OSDWIN1YP);
			osd_write(sd, lconfig->ysize, OSD_OSDWIN1YL);
		}
		break;
	case WIN_VID1:
		winmd_mask |= OSD_VIDWINMD_VFF1;
		if (lconfig->interlaced)
			winmd |= OSD_VIDWINMD_VFF1;

		osd_modify(sd, winmd_mask, winmd, OSD_VIDWINMD);
		osd_write(sd, lconfig->line_length >> 5, OSD_VIDWIN1OFST);
		osd_write(sd, lconfig->xpos, OSD_VIDWIN1XP);
		osd_write(sd, lconfig->xsize, OSD_VIDWIN1XL);
		/*
		 * For YUV420P format the register contents are
		 * duplicated in both VID registers
		 */
		if (sd->vpbe_type == DM365_VPBE) {
			if (lconfig->pixfmt == PIXFMT_NV12) {
				/* other window also */
				if (lconfig->interlaced) {
					winmd_mask |= OSD_VIDWINMD_VFF0;
					winmd |= OSD_VIDWINMD_VFF0;
					osd_modify(sd, winmd_mask, winmd,
						  OSD_VIDWINMD);
				}
				osd_modify(sd, OSD_MISCCTL_S420D,
					   OSD_MISCCTL_S420D, OSD_MISCCTL);
				osd_write(sd, lconfig->line_length >> 5,
					  OSD_VIDWIN0OFST);
				osd_write(sd, lconfig->xpos, OSD_VIDWIN0XP);
				osd_write(sd, lconfig->xsize, OSD_VIDWIN0XL);
		} else
			osd_modify(sd, OSD_MISCCTL_S420D, ~OSD_MISCCTL_S420D,
				   OSD_MISCCTL);
		}

		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_VIDWIN1YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_VIDWIN1YL);
			if ((sd->vpbe_type == DM365_VPBE)
			  && lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos >> 1,
					  OSD_VIDWIN0YP);
				osd_write(sd, lconfig->ysize >> 1,
					  OSD_VIDWIN0YL);
			}
		} else {
			osd_write(sd, lconfig->ypos, OSD_VIDWIN1YP);
			osd_write(sd, lconfig->ysize, OSD_VIDWIN1YL);
			if ((sd->vpbe_type == DM365_VPBE)
			    && lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos, OSD_VIDWIN0YP);
				osd_write(sd, lconfig->ysize, OSD_VIDWIN0YL);
			}
		}
		break;
	}
}

static int osd_set_layer_config(struct osd_state *sd, enum osd_layer layer,
				struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_layer_config *cfg = &win->lconfig;
	int reject_config;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	reject_config = try_layer_config(sd, layer, lconfig);
	if (reject_config) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return reject_config;
	}

	/* update the current Cb/Cr order */
	if (is_yc_pixfmt(lconfig->pixfmt))
		osd->yc_pixfmt = lconfig->pixfmt;

	/*
	 * If we are switching OSD1 from normal mode to attribute mode or from
	 * attribute mode to normal mode, then we must disable the window.
	 */
	if (layer == WIN_OSD1) {
		if (((lconfig->pixfmt == PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt != PIXFMT_OSD_ATTR)) ||
		  ((lconfig->pixfmt != PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt == PIXFMT_OSD_ATTR))) {
			win->is_enabled = 0;
			_osd_disable_layer(sd, layer);
		}
	}

	_osd_set_layer_config(sd, layer, lconfig);

	if (layer == WIN_OSD1) {
		struct osd_osdwin_state *osdwin_state =
		    &osd->osdwin[OSDWIN_OSD1];

		if ((lconfig->pixfmt != PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt == PIXFMT_OSD_ATTR)) {
			/*
			 * We just switched OSD1 from attribute mode to normal
			 * mode, so we must initialize the CLUT select, the
			 * blend factor, transparency colorkey enable, and
			 * attenuation enable (DM6446 only) bits in the
			 * OSDWIN1MD register.
			 */
			_osd_set_osd_clut(sd, OSDWIN_OSD1,
						   osdwin_state->clut);
			_osd_set_blending_factor(sd, OSDWIN_OSD1,
							  osdwin_state->blend);
			if (osdwin_state->colorkey_blending) {
				_osd_enable_color_key(sd, OSDWIN_OSD1,
							       osdwin_state->
							       colorkey,
							       lconfig->pixfmt);
			} else
				_osd_disable_color_key(sd, OSDWIN_OSD1);
			_osd_set_rec601_attenuation(sd, OSDWIN_OSD1,
						    osdwin_state->
						    rec601_attenuation);
		} else if ((lconfig->pixfmt == PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt != PIXFMT_OSD_ATTR)) {
			/*
			 * We just switched OSD1 from normal mode to attribute
			 * mode, so we must initialize the blink enable and
			 * blink interval bits in the OSDATRMD register.
			 */
			_osd_set_blink_attribute(sd, osd->is_blinking,
							  osd->blink);
		}
	}

	/*
	 * If we just switched to a 1-, 2-, or 4-bits-per-pixel bitmap format
	 * then configure a default palette map.
	 */
	if ((lconfig->pixfmt != cfg->pixfmt) &&
	  ((lconfig->pixfmt == PIXFMT_1BPP) ||
	  (lconfig->pixfmt == PIXFMT_2BPP) ||
	  (lconfig->pixfmt == PIXFMT_4BPP))) {
		enum osd_win_layer osdwin =
		    ((layer == WIN_OSD0) ? OSDWIN_OSD0 : OSDWIN_OSD1);
		struct osd_osdwin_state *osdwin_state =
		    &osd->osdwin[osdwin];
		unsigned char clut_index;
		unsigned char clut_entries = 0;

		switch (lconfig->pixfmt) {
		case PIXFMT_1BPP:
			clut_entries = 2;
			break;
		case PIXFMT_2BPP:
			clut_entries = 4;
			break;
		case PIXFMT_4BPP:
			clut_entries = 16;
			break;
		default:
			break;
		}
		/*
		 * The default palette map maps the pixel value to the clut
		 * index, i.e. pixel value 0 maps to clut entry 0, pixel value
		 * 1 maps to clut entry 1, etc.
		 */
		for (clut_index = 0; clut_index < 16; clut_index++) {
			osdwin_state->palette_map[clut_index] = clut_index;
			if (clut_index < clut_entries) {
				_osd_set_palette_map(sd, osdwin, clut_index,
						     clut_index,
						     lconfig->pixfmt);
			}
		}
	}

	*cfg = *lconfig;
	/* DM6446: configure the RGB888 enable and window selection */
	if (osd->win[WIN_VID0].lconfig.pixfmt == PIXFMT_RGB888)
		_osd_enable_vid_rgb888(sd, WIN_VID0);
	else if (osd->win[WIN_VID1].lconfig.pixfmt == PIXFMT_RGB888)
		_osd_enable_vid_rgb888(sd, WIN_VID1);
	else
		_osd_disable_vid_rgb888(sd);

	if (layer == WIN_VID0) {
		osd->pingpong =
		    _osd_dm6446_vid0_pingpong(sd, osd->field_inversion,
						       win->fb_base_phys,
						       cfg);
	}

	spin_unlock_irqrestore(&osd->lock, flags);

	return 0;
}

static void osd_init_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	enum osd_win_layer osdwin;
	struct osd_osdwin_state *osdwin_state;
	struct osd_layer_config *cfg = &win->lconfig;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	win->is_enabled = 0;
	_osd_disable_layer(sd, layer);

	win->h_zoom = ZOOM_X1;
	win->v_zoom = ZOOM_X1;
	_osd_set_zoom(sd, layer, win->h_zoom, win->v_zoom);

	win->fb_base_phys = 0;
	_osd_start_layer(sd, layer, win->fb_base_phys, 0);

	cfg->line_length = 0;
	cfg->xsize = 0;
	cfg->ysize = 0;
	cfg->xpos = 0;
	cfg->ypos = 0;
	cfg->interlaced = 0;
	switch (layer) {
	case WIN_OSD0:
	case WIN_OSD1:
		osdwin = (layer == WIN_OSD0) ? OSDWIN_OSD0 : OSDWIN_OSD1;
		osdwin_state = &osd->osdwin[osdwin];
		/*
		 * Other code relies on the fact that OSD windows default to a
		 * bitmap pixel format when they are deallocated, so don't
		 * change this default pixel format.
		 */
		cfg->pixfmt = PIXFMT_8BPP;
		_osd_set_layer_config(sd, layer, cfg);
		osdwin_state->clut = RAM_CLUT;
		_osd_set_osd_clut(sd, osdwin, osdwin_state->clut);
		osdwin_state->colorkey_blending = 0;
		_osd_disable_color_key(sd, osdwin);
		osdwin_state->blend = OSD_8_VID_0;
		_osd_set_blending_factor(sd, osdwin, osdwin_state->blend);
		osdwin_state->rec601_attenuation = 0;
		_osd_set_rec601_attenuation(sd, osdwin,
						     osdwin_state->
						     rec601_attenuation);
		if (osdwin == OSDWIN_OSD1) {
			osd->is_blinking = 0;
			osd->blink = BLINK_X1;
		}
		break;
	case WIN_VID0:
	case WIN_VID1:
		cfg->pixfmt = osd->yc_pixfmt;
		_osd_set_layer_config(sd, layer, cfg);
		break;
	}

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_release_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	if (!win->is_allocated) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&osd->lock, flags);
	osd_init_layer(sd, layer);
	spin_lock_irqsave(&osd->lock, flags);

	win->is_allocated = 0;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static int osd_request_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	if (win->is_allocated) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return -1;
	}
	win->is_allocated = 1;

	spin_unlock_irqrestore(&osd->lock, flags);
	return 0;
}

static void _osd_init(struct osd_state *sd)
{
	osd_write(sd, 0, OSD_MODE);
	osd_write(sd, 0, OSD_VIDWINMD);
	osd_write(sd, 0, OSD_OSDWIN0MD);
	osd_write(sd, 0, OSD_OSDWIN1MD);
	osd_write(sd, 0, OSD_RECTCUR);
	osd_write(sd, 0, OSD_MISCCTL);
	if (sd->vpbe_type == DM355_VPBE) {
		osd_write(sd, 0, OSD_VBNDRY);
		osd_write(sd, 0, OSD_EXTMODE);
		osd_write(sd, OSD_MISCCTL_DMANG, OSD_MISCCTL);
	}
}

static void osd_set_left_margin(struct osd_state *sd, u32 val)
{
	osd_write(sd, val, OSD_BASEPX);
}

static void osd_set_top_margin(struct osd_state *sd, u32 val)
{
	osd_write(sd, val, OSD_BASEPY);
}

static int osd_initialize(struct osd_state *osd)
{
	if (osd == NULL)
		return -ENODEV;
	_osd_init(osd);

	/* set default Cb/Cr order */
	osd->yc_pixfmt = PIXFMT_YCbCrI;

	if (osd->vpbe_type == DM355_VPBE) {
		/*
		 * ROM CLUT1 on the DM355 is similar (identical?) to ROM CLUT0
		 * on the DM6446, so make ROM_CLUT1 the default on the DM355.
		 */
		osd->rom_clut = ROM_CLUT1;
	}

	_osd_set_field_inversion(osd, osd->field_inversion);
	_osd_set_rom_clut(osd, osd->rom_clut);

	osd_init_layer(osd, WIN_OSD0);
	osd_init_layer(osd, WIN_VID0);
	osd_init_layer(osd, WIN_OSD1);
	osd_init_layer(osd, WIN_VID1);

	return 0;
}

static const struct vpbe_osd_ops osd_ops = {
	.set_clut_ycbcr = osd_set_clut_ycbcr,
	.set_clut_rgb = osd_set_clut_rgb,
	.set_osd_clut = osd_set_osd_clut,
	.get_osd_clut = osd_get_osd_clut,
	.enable_color_key = osd_enable_color_key,
	.disable_color_key = osd_disable_color_key,
	.set_blending_factor = osd_set_blending_factor,
	.get_blending_factor = osd_get_blending_factor,
	.set_rec601_attenuation = osd_set_rec601_attenuation,
	.get_rec601_attenuation = osd_get_rec601_attenuation,
	.set_palette_map = osd_set_palette_map,
	.get_palette_map = osd_get_palette_map,
	.set_blink_attribute = osd_set_blink_attribute,
	.get_blink_attribute = osd_get_blink_attribute,
	.cursor_enable = osd_cursor_enable,
	.cursor_disable = osd_cursor_disable,
	.cursor_is_enabled = osd_cursor_is_enabled,
	.set_cursor_config = osd_set_cursor_config,
	.get_cursor_config = osd_get_cursor_config,
	.set_field_inversion = osd_set_field_inversion,
	.get_field_inversion = osd_get_field_inversion,
	.initialize = osd_initialize,
	.request_layer = osd_request_layer,
	.release_layer = osd_release_layer,
	.enable_layer = osd_enable_layer,
	.disable_layer = osd_disable_layer,
	.layer_is_enabled = osd_layer_is_enabled,
	.set_layer_config = osd_set_layer_config,
	.try_layer_config = osd_try_layer_config,
	.get_layer_config = osd_get_layer_config,
	.set_interpolation_filter = osd_set_interpolation_filter,
	.get_interpolation_filter = osd_get_interpolation_filter,
	.set_osd_expansion = osd_set_osd_expansion,
	.get_osd_expansion = osd_get_osd_expansion,
	.set_vid_expansion = osd_set_vid_expansion,
	.get_vid_expansion = osd_get_vid_expansion,
	.set_zoom = osd_set_zoom,
	.get_zoom = osd_get_zoom,
	.set_background = osd_set_background,
	.get_background = osd_get_background,
	.set_rom_clut = osd_set_rom_clut,
	.get_rom_clut = osd_get_rom_clut,
	.start_layer = osd_start_layer,
	.set_left_margin = osd_set_left_margin,
	.set_top_margin = osd_set_top_margin,
};

static int osd_probe(struct platform_device *pdev)
{
	struct osd_state *osd;
	struct resource *res;
	struct osd_platform_data *pdata;
	int ret = 0;

	osd = kzalloc(sizeof(struct osd_state), GFP_KERNEL);
	if (osd == NULL)
		return -ENOMEM;

	osd->dev = &pdev->dev;
	pdata = (struct osd_platform_data *)pdev->dev.platform_data;
	osd->vpbe_type = (enum vpbe_types)pdata->vpbe_type;
	if (NULL == pdev->dev.platform_data) {
		dev_err(osd->dev, "No platform data defined for OSD"
			" sub device\n");
		ret = -ENOENT;
		goto free_mem;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(osd->dev, "Unable to get OSD register address map\n");
		ret = -ENODEV;
		goto free_mem;
	}
	osd->osd_base_phys = res->start;
	osd->osd_size = res->end - res->start + 1;
	if (!request_mem_region(osd->osd_base_phys, osd->osd_size,
				MODULE_NAME)) {
		dev_err(osd->dev, "Unable to reserve OSD MMIO region\n");
		ret = -ENODEV;
		goto free_mem;
	}
	osd->osd_base = (unsigned long)ioremap_nocache(res->start,
							osd->osd_size);
	if (!osd->osd_base) {
		dev_err(osd->dev, "Unable to map the OSD region\n");
		ret = -ENODEV;
		goto release_mem_region;
	}
	spin_lock_init(&osd->lock);
	osd->ops = osd_ops;
	platform_set_drvdata(pdev, osd);
	dev_notice(osd->dev, "OSD sub device probe success\n");
	return ret;

release_mem_region:
	release_mem_region(osd->osd_base_phys, osd->osd_size);
free_mem:
	kfree(osd);
	return ret;
}

static int osd_remove(struct platform_device *pdev)
{
	struct osd_state *osd = platform_get_drvdata(pdev);

	iounmap((void *)osd->osd_base);
	release_mem_region(osd->osd_base_phys, osd->osd_size);
	kfree(osd);
	return 0;
}

static struct platform_driver osd_driver = {
	.probe		= osd_probe,
	.remove		= osd_remove,
	.driver		= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
};

static int osd_init(void)
{
	if (platform_driver_register(&osd_driver)) {
		printk(KERN_ERR "Unable to register davinci osd driver\n");
		return -ENODEV;
	}

	return 0;
}

static void osd_exit(void)
{
	platform_driver_unregister(&osd_driver);
}

module_init(osd_init);
module_exit(osd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DaVinci OSD Manager Driver");
MODULE_AUTHOR("Texas Instruments");
