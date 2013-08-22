/*
 * Copyright (C) 2010 Texas Instruments Inc
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
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/mux.h>
#include <mach/cputype.h>
#include <mach/gpio.h>
#include <asm/io.h>
#include <linux/i2c.h>

#include <linux/io.h>

#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe_venc.h>
#include <media/davinci/vpss.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>

#include "vpbe_venc_regs.h"

#define MODULE_NAME	VPBE_VENC_SUBDEV_NAME

static int debug = 2;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level 0-2");

struct venc_state {
	struct v4l2_subdev sd;
	struct venc_callback *callback;
	struct venc_platform_data *pdata;
	struct device *pdev;
	u32 output;
	v4l2_std_id std;
	spinlock_t lock;
	void __iomem *venc_base;
	void __iomem *vdaccfg_reg;
};

static inline struct venc_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct venc_state, sd);
}

static inline u32 venc_read(struct v4l2_subdev *sd, u32 offset)
{
	struct venc_state *venc = to_state(sd);

	return readl(venc->venc_base + offset);
}

static inline u32 venc_write(struct v4l2_subdev *sd, u32 offset, u32 val)
{
	struct venc_state *venc = to_state(sd);
	writel(val, (venc->venc_base + offset));
	return val;
}

static inline u32 venc_modify(struct v4l2_subdev *sd, u32 offset,
				 u32 val, u32 mask)
{
	u32 new_val = (venc_read(sd, offset) & ~mask) | (val & mask);

	venc_write(sd, offset, new_val);
	return new_val;
}

static inline u32 vdaccfg_write(struct v4l2_subdev *sd, u32 val)
{
	struct venc_state *venc = to_state(sd);

	writel(val, venc->vdaccfg_reg);

	val = readl(venc->vdaccfg_reg);
	return val;
}

/* This function sets the dac of the VPBE for various outputs
 */
static int venc_set_dac(struct v4l2_subdev *sd, u32 out_index)
{
	int ret = 0;

	switch (out_index) {
	case 0:
		v4l2_dbg(debug, 1, sd, "Setting output to Composite\n");
		venc_write(sd, VENC_DACSEL, 0);
		break;
	case 1:
		v4l2_dbg(debug, 1, sd, "Setting output to Component\n");
		venc_write(sd, VENC_DACSEL, 0x543);
		break;
	case 2:
		v4l2_dbg(debug, 1, sd, "Setting output to S-video\n");
		venc_write(sd, VENC_DACSEL, 0x210);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void venc_enabledigitaloutput(struct v4l2_subdev *sd, int benable)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;
	void __iomem *vpss_clkctl_reg;

	v4l2_dbg(debug, 2, sd, "venc_enabledigitaloutput\n");

	vpss_clkctl_reg = DAVINCI_SYSMODULE_VIRT(0x44);

	if (benable) {
		venc_write(sd, VENC_VMOD, 0);
		venc_write(sd, VENC_CVBS, 0);
#if 0
		if (cpu_is_davinci_dm368()) {
			enable_lcd();

			/* Select EXTCLK as video clock source */
			__raw_writel(0x1a, vpss_clkctl_reg);

			/* Set PINMUX for GPIO82 */
			davinci_cfg_reg(DM365_GPIO82);
			gpio_request(82, "lcd_oe");

			/* Set GPIO82 low */
			gpio_direction_output(82, 0);
			gpio_set_value(82, 0);
		}
#endif

		venc_write(sd, VENC_LCDOUT, 0);
		venc_write(sd, VENC_HSPLS, 0);
		venc_write(sd, VENC_HSTART, 0);
		venc_write(sd, VENC_HVALID, 0);
		venc_write(sd, VENC_HINT, 0);
		venc_write(sd, VENC_VSPLS, 0);
		venc_write(sd, VENC_VSTART, 0);
		venc_write(sd, VENC_VVALID, 0);
		venc_write(sd, VENC_VINT, 0);
		venc_write(sd, VENC_YCCCTL, 0);
		venc_write(sd, VENC_DACSEL, 0);
	} else {
		venc_write(sd, VENC_VMOD, 0);
		/* disable VCLK output pin enable */
		venc_write(sd, VENC_VIDCTL, 0x141);

		/* Disable output sync pins */
		venc_write(sd, VENC_SYNCCTL, 0);

		/* Disable DCLOCK */
		venc_write(sd, VENC_DCLKCTL, 0);
		venc_write(sd, VENC_DRGBX1, 0x0000057C);

		/* Disable LCD output control (accepting default polarity) */
		venc_write(sd, VENC_LCDOUT, 0);
		if (pdata->venc_type != DM355_VPBE)
			venc_write(sd, VENC_CMPNT, 0x100);
		venc_write(sd, VENC_HSPLS, 0);
		venc_write(sd, VENC_HINT, 0);
		venc_write(sd, VENC_HSTART, 0);
		venc_write(sd, VENC_HVALID, 0);

		venc_write(sd, VENC_VSPLS, 0);
		venc_write(sd, VENC_VINT, 0);
		venc_write(sd, VENC_VSTART, 0);
		venc_write(sd, VENC_VVALID, 0);

		venc_write(sd, VENC_HSDLY, 0);
		venc_write(sd, VENC_VSDLY, 0);

		venc_write(sd, VENC_YCCCTL, 0);
		venc_write(sd, VENC_VSTARTA, 0);

		/* Set OSD clock and OSD Sync Adavance registers */
		venc_write(sd, VENC_OSDCLK0, 1);
		venc_write(sd, VENC_OSDCLK1, 2);
	}
}

/*
 * setting NTSC mode
 */
static int venc_set_ntsc(struct v4l2_subdev *sd)
{
	u32 val;
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_ntsc\n");

	/* Setup clock at VPSS & VENC for SD */
	vpss_enable_clock(VPSS_VENC_CLOCK_SEL, 1);
	if (pdata->setup_clock(VPBE_ENC_STD, V4L2_STD_525_60) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (pdata->venc_type == DM355_VPBE) {
		venc_write(sd, VENC_CLKCTL, 0x01);
		venc_write(sd, VENC_VIDCTL, 0);
		val = vdaccfg_write(sd, 0x0E21A6B6);
	} else if (pdata->venc_type == DM365_VPBE) {
		venc_write(sd, VENC_CLKCTL, 0x01);
		venc_write(sd, VENC_VIDCTL, 0);
		vdaccfg_write(sd, 0x081141CF);
	} else {
		/* to set VENC CLK DIV to 1 - final clock is 54 MHz */
		venc_modify(sd, VENC_VIDCTL, 0, 1 << 1);
		/* Set REC656 Mode */
		venc_write(sd, VENC_YCCCTL, 0x1);
		venc_modify(sd, VENC_VDPRO, 0, VENC_VDPRO_DAFRQ);
		venc_modify(sd, VENC_VDPRO, 0, VENC_VDPRO_DAUPS);
	}

	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
			VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, (0 << VENC_VMOD_VMD), VENC_VMOD_VMD);
	venc_modify(sd, VENC_VMOD, (0 << VENC_VMOD_TVTYP_SHIFT),
			VENC_VMOD_TVTYP);
	venc_write(sd, VENC_DACTST, 0x0);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	return 0;
}

/*
 * setting PAL mode
 */
static int venc_set_pal(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_pal\n");

	/* Setup clock at VPSS & VENC for SD */
	vpss_enable_clock(VPSS_VENC_CLOCK_SEL, 1);
	if (venc->pdata->setup_clock(VPBE_ENC_STD, V4L2_STD_625_50) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (pdata->venc_type == DM355_VPBE) {
		venc_write(sd, VENC_CLKCTL, 0x1);
		venc_write(sd, VENC_VIDCTL, 0);
		vdaccfg_write(sd, 0x0E21A6B6);
	} else if (pdata->venc_type == DM365_VPBE) {
		venc_write(sd, VENC_CLKCTL, 0x1);
		venc_write(sd, VENC_VIDCTL, 0);
		vdaccfg_write(sd, 0x081141CF);
	} else {
		/* to set VENC CLK DIV to 1 - final clock is 54 MHz */
		venc_modify(sd, VENC_VIDCTL, 0, 1 << 1);
		/* Set REC656 Mode */
		venc_write(sd, VENC_YCCCTL, 0x1);
	}

	venc_modify(sd, VENC_SYNCCTL, 1 << VENC_SYNCCTL_OVD_SHIFT,
			VENC_SYNCCTL_OVD);
	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD,
			(1 << VENC_VMOD_VIE_SHIFT),
			VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD,
			(0 << VENC_VMOD_VMD), VENC_VMOD_VMD);
	venc_modify(sd, VENC_VMOD,
			(1 << VENC_VMOD_TVTYP_SHIFT),
			VENC_VMOD_TVTYP);
	venc_write(sd, VENC_DACTST, 0x0);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	return 0;
}

/*
 * venc_set_480p59_94
 *
 * This function configures the video encoder to EDTV(525p) component setting.
 */
static int venc_set_480p59_94(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_480p59_94\n");
	if ((pdata->venc_type != DM644X_VPBE) &&
	    (pdata->venc_type != DM365_VPBE))
		return -EINVAL;

	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_DV_PRESET, V4L2_DV_480P59_94) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (pdata->venc_type == DM365_VPBE)
		vdaccfg_write(sd, 0x081141EF);
	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);

	if (pdata->venc_type == DM644X_VPBE) {
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAFRQ,
			    VENC_VDPRO_DAFRQ);
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAUPS,
			    VENC_VDPRO_DAUPS);
	}

	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
		    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_525P << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VDMD_YCBCR8 <<
		    VENC_VMOD_VDMD_SHIFT, VENC_VMOD_VDMD);

	venc_write(sd, VENC_DACTST, 0x0);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	return 0;
}

/*
 * venc_set_display_timing
 *
 * This sets venc in non-standard mode and set timings for standard
 */
static void venc_set_display_timing(struct v4l2_subdev *sd,
				    struct vpbe_enc_mode_info *mode)
{
	v4l2_dbg(debug, 2, sd, "venc_set_display_timing\n");
	venc_write(sd, VENC_HSPLS, mode->hsync_len);
	venc_write(sd, VENC_HSTART, mode->left_margin);
	venc_write(sd, VENC_HVALID, mode->xres);
	venc_write(sd, VENC_HINT,
		   mode->xres + mode->left_margin + mode->right_margin - 1);
	venc_write(sd, VENC_VSPLS, mode->vsync_len);
	venc_write(sd, VENC_VSTART, mode->upper_margin);
	venc_write(sd, VENC_VVALID, mode->yres);
	venc_write(sd,
		   VENC_VINT, mode->yres + mode->upper_margin +
		   mode->lower_margin);
};

/*
 * venc_set_prgb
 *
 * setting DLCD 480P PRGB mode
 */
static int venc_set_prgb(struct v4l2_subdev *sd,
			 struct vpbe_enc_mode_info *mode_info)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_prgb\n");

	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_CUSTOM_TIMINGS,
		CUSTOM_TIMING_480_272) < 0)
		return -EINVAL;

	/* setup pinmux */
	if (pdata->setup_pinmux(pdata->if_params, 0) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 1);

	venc_write(sd, VENC_VIDCTL, 0x141);
	/* set VPSS clock */
	vpss_enable_clock(VPSS_VPBE_CLOCK, 1);
	vpss_enable_clock(VPSS_VENC_CLOCK_SEL, 1);
	venc_write(sd, VENC_DCLKCTL, 0);
	venc_write(sd, VENC_DCLKPTN0, 0);

	/* Set the OSD Divisor to 1. */
	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);
	/* Clear composite mode register */
	venc_write(sd, VENC_CVBS, 0);

	if (pdata->venc_type == DM355_VPBE)
		/* Enable the venc and dlcd clocks. */
		venc_write(sd, VENC_CLKCTL, 0x11);

	else if (pdata->venc_type == DM365_VPBE)
		/* DM365 pinmux */
		venc_write(sd, VENC_CLKCTL, 0x11);
	else
		venc_write(sd, VENC_CMPNT, 0x100);

	/* Set VIDCTL to select VCLKE = 1,
	VCLKZ =0, SYDIR = 0 (set o/p), DOMD = 0 */
	venc_modify(sd, VENC_VIDCTL, 1 << VENC_VIDCTL_VCLKE_SHIFT,
		    VENC_VIDCTL_VCLKE);
	venc_modify(sd, VENC_VIDCTL, 0 << VENC_VIDCTL_VCLKZ_SHIFT,
		    VENC_VIDCTL_VCLKZ);
	venc_modify(sd, VENC_VIDCTL, 0 << VENC_VIDCTL_SYDIR_SHIFT,
		    VENC_VIDCTL_SYDIR);
	venc_modify(sd, VENC_VIDCTL, 0 << VENC_VIDCTL_YCDIR_SHIFT,
		    VENC_VIDCTL_YCDIR);

	venc_modify(sd, VENC_DCLKCTL,
			1 << VENC_DCLKCTL_DCKEC_SHIFT, VENC_DCLKCTL_DCKEC);

	venc_write(sd, VENC_DCLKPTN0, 0x1);

	venc_set_display_timing(sd, mode_info);
	venc_write(sd, VENC_SYNCCTL,
	   (VENC_SYNCCTL_SYEV |
		   VENC_SYNCCTL_SYEH | VENC_SYNCCTL_HPL
		   | VENC_SYNCCTL_VPL));

	/* Configure VMOD. No change in VENC bit */
	venc_write(sd, VENC_VMOD, 0x2011);
	venc_write(sd, VENC_LCDOUT, 0x1);
	//if (cpu_is_davinci_dm368()) {
	//	/* Turn on LCD display */
	//	mdelay(200);
	//	gpio_set_value(82, 1);
	//}
	return 0;
}

/*
 * venc_set_720p60_external
 *
 * setting 720p60 mode for external encoders
 */
static int venc_set_720p60_external(struct v4l2_subdev *sd,
				    struct vpbe_enc_mode_info *mode_info)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_720p60\n");

	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_DV_PRESET, V4L2_DV_720P60) < 0)
		return -EINVAL;

	/* setup pinmux */
	if (pdata->setup_pinmux(pdata->if_params, 0) < 0)
		return -EINVAL;

	vdaccfg_write(sd, 0x081141EF);

	/* Reset video encoder module */
	venc_write(sd, VENC_VMOD, 0);

	venc_enabledigitaloutput(sd, 1);

	venc_write(sd, VENC_VIDCTL, (VENC_VIDCTL_VCLKE | VENC_VIDCTL_VCLKP));
	/* Setting DRGB Matrix registers back to default values */
	venc_write(sd, VENC_DRGBX0, 0x00000400);
	venc_write(sd, VENC_DRGBX1, 0x00000576);
	venc_write(sd, VENC_DRGBX2, 0x00000159);
	venc_write(sd, VENC_DRGBX3, 0x000002cb);
	venc_write(sd, VENC_DRGBX4, 0x000006ee);

	/* Enable DCLOCK */
	venc_write(sd, VENC_DCLKCTL, VENC_DCLKCTL_DCKEC);
	/* Set DCLOCK pattern */
	venc_write(sd, VENC_DCLKPTN0, 1);
	venc_write(sd, VENC_DCLKPTN1, 0);
	venc_write(sd, VENC_DCLKPTN2, 0);
	venc_write(sd, VENC_DCLKPTN3, 0);
	venc_write(sd, VENC_DCLKPTN0A, 2);
	venc_write(sd, VENC_DCLKPTN1A, 0);
	venc_write(sd, VENC_DCLKPTN2A, 0);
	venc_write(sd, VENC_DCLKPTN3A, 0);
	venc_write(sd, VENC_DCLKHS, 0);
	venc_write(sd, VENC_DCLKHSA, 1);
	venc_write(sd, VENC_DCLKHR, 0);
	venc_write(sd, VENC_DCLKVS, 0);
	venc_write(sd, VENC_DCLKVR, 0);
	/* Set brightness start position and pulse width to zero */
	venc_write(sd, VENC_BRTS, 0);
	venc_write(sd, VENC_BRTW, 0);
	/* Set LCD AC toggle interval and horizontal position to zero */
	venc_write(sd, VENC_ACCTL, 0);

	/* Set PWM period and width to zero */
	venc_write(sd, VENC_PWMP, 0);
	venc_write(sd, VENC_PWMW, 0);

	venc_write(sd, VENC_CVBS, 0);
	venc_write(sd, VENC_CMPNT, 0);
	/* turning on horizontal and vertical syncs */
	venc_write(sd, VENC_SYNCCTL, (VENC_SYNCCTL_SYEV | VENC_SYNCCTL_SYEH));
	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);
	venc_write(sd, VENC_OSDHADV, 0);

	/*__raw_writel(0xa, IO_ADDRESS(SYS_VPSS_CLKCTL));*/
	if (pdata->venc_type == DM355_VPBE)
		venc_write(sd, VENC_CLKCTL, 0x11);

	/* Set VENC for non-standard timing */
	venc_set_display_timing(sd, mode_info);

	venc_write(sd, VENC_HSDLY, 0);
	venc_write(sd, VENC_VSDLY, 0);
	venc_write(sd, VENC_YCCCTL, 0);
	venc_write(sd, VENC_VSTARTA, 0);

	/*
	 * Enable all VENC, non-standard timing mode, master timing, HD,
	 * progressive
	 */
	if (pdata->venc_type == DM355_VPBE)
		venc_write(sd, VENC_VMOD, (VENC_VMOD_VENC | VENC_VMOD_VMD));
	else
		venc_write(sd, VENC_VMOD,
			   (VENC_VMOD_VENC | VENC_VMOD_VMD |
			   VENC_VMOD_HDMD));
	venc_write(sd, VENC_LCDOUT, 1);
	return 0;
}

static int venc_set_1080i30_external(struct v4l2_subdev *sd,
				     struct vpbe_enc_mode_info *mode_info)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_1080i30\n");

	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_DV_PRESET, V4L2_DV_1080I30) < 0)
		return -EINVAL;

	/* setup pinmux */
	if (pdata->setup_pinmux(pdata->if_params, 1) < 0)
		return -EINVAL;

	vdaccfg_write(sd, 0x081141EF);

	/* Reset video encoder module */
	venc_write(sd, VENC_VMOD, 0);

	venc_enabledigitaloutput(sd, 1);
	venc_write(sd, VENC_VIDCTL, (VENC_VIDCTL_VCLKE | VENC_VIDCTL_VCLKP));

	/* Setting DRGB Matrix registers back to default values */
	venc_write(sd, VENC_DRGBX0, 0x00000400);
	venc_write(sd, VENC_DRGBX1, 0x00000576);
	venc_write(sd, VENC_DRGBX2, 0x00000159);
	venc_write(sd, VENC_DRGBX3, 0x000002cb);
	venc_write(sd, VENC_DRGBX4, 0x000006ee);

	/* Enable DCLOCK */
	/*venc_write(sd, VENC_DCLKCTL, VENC_DCLKCTL_DCKEC);*/

	/* Set DCLOCK pattern */
	venc_write(sd, VENC_DCLKPTN0, 1);
	venc_write(sd, VENC_DCLKPTN1, 0);
	venc_write(sd, VENC_DCLKPTN2, 0);
	venc_write(sd, VENC_DCLKPTN3, 0);
	venc_write(sd, VENC_DCLKPTN0A, 2);
	venc_write(sd, VENC_DCLKPTN1A, 0);
	venc_write(sd, VENC_DCLKPTN2A, 0);
	venc_write(sd, VENC_DCLKPTN3A, 0);
	venc_write(sd, VENC_DCLKHS, 0);
	venc_write(sd, VENC_DCLKHSA, 1);
	venc_write(sd, VENC_DCLKHR, 0);
	venc_write(sd, VENC_DCLKVS, 0);
	venc_write(sd, VENC_DCLKVR, 0);

	/* Set brightness start position and pulse width to zero */
	venc_write(sd, VENC_BRTS, 0);
	venc_write(sd, VENC_BRTW, 0);

	/* Set LCD AC toggle interval and horizontal position to zero */
	venc_write(sd, VENC_ACCTL, 0);

	/* Set PWM period and width to zero */
	venc_write(sd, VENC_PWMP, 0);
	venc_write(sd, VENC_PWMW, 0);

	venc_write(sd, VENC_CVBS, 0);
	venc_write(sd, VENC_CMPNT, 0);

	/* turning on horizontal and vertical syncs */
	venc_write(sd, VENC_SYNCCTL, (VENC_SYNCCTL_SYEV | VENC_SYNCCTL_SYEH));
	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);
	venc_write(sd, VENC_OSDHADV, 0);

	venc_write(sd, VENC_HSDLY, 0);
	venc_write(sd, VENC_VSDLY, 0);
	venc_write(sd, VENC_YCCCTL, 0);
	venc_write(sd, VENC_VSTARTA, 13);

	/*__raw_writel(0xa, IO_ADDRESS(SYS_VPSS_CLKCTL));*/
	if (pdata->venc_type == DM355_VPBE)
		venc_write(sd, VENC_CLKCTL, 0x11);

	/* Set VENC for non-standard timing */
	venc_set_display_timing(sd, mode_info);

	/*
	* Enable all VENC, non-standard timing mode, master timing,
	* HD, interlaced
	*/
	if (pdata->venc_type == DM355_VPBE) {
		venc_write(sd, VENC_VMOD,
			   (VENC_VMOD_VENC | VENC_VMOD_VMD |
			   VENC_VMOD_NSIT));
	} else {
		venc_write(sd, VENC_VMOD,
			   (VENC_VMOD_VENC | VENC_VMOD_VMD | VENC_VMOD_HDMD |
			   VENC_VMOD_NSIT));
	}
	venc_write(sd, VENC_LCDOUT, 1);
	return 0;
}

static int venc_set_srgb(struct v4l2_subdev *sd,
			 struct vpbe_enc_mode_info *mode_info)
{
	/* No support for srgb modes yet */
	return -EINVAL;
}

static int venc_set_ycc8_modes(struct v4l2_subdev *sd,
			       struct vpbe_enc_mode_info *mode_info)
{
	/* No support for srgb modes yet */
	return -EINVAL;
}

static int venc_set_ycc16_modes(struct v4l2_subdev *sd,
				struct vpbe_enc_mode_info *mode_info)
{
	int ret = -EINVAL;

	if (mode_info->timings_type == VPBE_ENC_DV_PRESET) {
		switch (mode_info->timings.dv_preset) {
		case V4L2_DV_720P60:
			ret = venc_set_720p60_external(sd, mode_info);
			break;
		case V4L2_DV_1080I30:
			ret = venc_set_1080i30_external(sd, mode_info);
			break;
		default:
			return ret;
		}
	}
	return ret;
}

/*
 * venc_set_625p
 *
 * This function configures the video encoder to HDTV(625p) component setting
 */
static int venc_set_576p50(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_576p50\n");

	if ((pdata->venc_type != DM644X_VPBE) &&
	  (pdata->venc_type != DM365_VPBE))
		return -EINVAL;
	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_DV_PRESET, V4L2_DV_576P50) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	/*if (venc->pdata->venc_type != DM365_VPBE) {
	__raw_writel(0x19, IO_ADDRESS(SYS_VPSS_CLKCTL));*/

	if (pdata->venc_type == DM365_VPBE)
		vdaccfg_write(sd, 0x081141EF);

	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);

	if (pdata->venc_type == DM644X_VPBE) {
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAFRQ,
			    VENC_VDPRO_DAFRQ);
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAUPS,
			    VENC_VDPRO_DAUPS);
	}

	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
		    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_625P << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);

	venc_modify(sd, VENC_VMOD, VENC_VMOD_VDMD_YCBCR8 <<
		    VENC_VMOD_VDMD_SHIFT, VENC_VMOD_VDMD);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	venc_write(sd, VENC_DACTST, 0x0);
	return 0;
}

/*
 * venc_set_720p60_internal - Setup 720p60 in venc for dm365 only
 */
static int venc_set_720p60_internal(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_1080i30\n");

	if (pdata->setup_clock(VPBE_ENC_DV_PRESET, V4L2_DV_720P60) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);

	venc_write(sd, VENC_VMOD, 0);
	/* DM365 component HD mode */
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
	    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_720P << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	venc_write(sd, VENC_XHINTVL, 0);
	venc_write(sd, VENC_DACTST, 0x0);
	return 0;
}

/*
 * venc_set_1080i30_internal - Setup 1080i30 in venc for dm365 only
 */
static int venc_set_1080i30_internal(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	if (pdata->setup_clock(VPBE_ENC_DV_PRESET, V4L2_DV_1080P30) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);


	venc_write(sd, VENC_VMOD, 0);
	/* DM365 component HD mode */
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
		    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_1080I << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	venc_write(sd, VENC_XHINTVL, 0);
	venc_write(sd, VENC_DACTST, 0x0);
	return 0;
}

static int venc_s_std_output(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	v4l2_dbg(debug, 1, sd, "venc_s_std_output\n");

	if (norm & V4L2_STD_525_60)
		return venc_set_ntsc(sd);
	else if (norm & V4L2_STD_625_50)
		return venc_set_pal(sd);
	return -EINVAL;
}

static int venc_s_dv_preset(struct v4l2_subdev *sd,
			    struct v4l2_dv_preset *dv_preset)
{
	int ret = -EINVAL;
	struct venc_state *venc = to_state(sd);
	v4l2_dbg(debug, 1, sd, "venc_s_dv_preset\n");

	if (dv_preset->preset == V4L2_DV_576P50)
		return venc_set_576p50(sd);
	else if (dv_preset->preset == V4L2_DV_480P59_94)
		return venc_set_480p59_94(sd);
	else if (dv_preset->preset == V4L2_DV_720P60) {
		if (venc->pdata->venc_type == DM365_VPBE) {
			/* TBD setup internal 720p mode here */
			ret = venc_set_720p60_internal(sd);
			/* for DM365 VPBE, there is DAC inside */
			vdaccfg_write(sd, 0x081141EF);
			return ret;
		}
	} else if (dv_preset->preset == V4L2_DV_1080I30) {
		if (venc->pdata->venc_type == DM365_VPBE) {
			/* TBD setup internal 1080i mode here */
			ret = venc_set_1080i30_internal(sd);
			/* for DM365 VPBE, there is DAC inside */
			vdaccfg_write(sd, 0x081141EF);
			return ret;
		}
	}
	return ret;
}

static int venc_s_routing(struct v4l2_subdev *sd, u32 input, u32 output,
			  u32 config)
{
	struct venc_state *venc = to_state(sd);
	int ret = 0;

	v4l2_dbg(debug, 1, sd, "venc_s_routing\n");

	ret = venc_set_dac(sd, output);
	if (!ret)
		venc->output = output;
	return ret;
}

static long venc_ioctl(struct v4l2_subdev *sd,
			unsigned int cmd,
			void *arg)
{
	u32 val;
	int ret = 0;
	unsigned long flags;
	unsigned event = 0;
	struct venc_callback *next, *prev, *callback;
	struct venc_state *venc = to_state(sd);
	struct vpbe_enc_mode_info *mode_info;
	struct venc_platform_data *pdata = venc->pdata;

	switch (cmd) {
	case VENC_GET_FLD:
		val = venc_read(sd, VENC_VSTAT);
		*((int *)arg) = ((val & VENC_VSTAT_FIDST) ==
		VENC_VSTAT_FIDST);
		break;
	case VENC_REG_CALLBACK:
		spin_lock_irqsave(&venc->lock, flags);
		callback = (struct venc_callback *)arg;
		next = venc->callback;
		venc->callback = callback;
		callback->next = next;
		spin_unlock_irqrestore(&venc->lock, flags);
		break;
	case VENC_UNREG_CALLBACK:
		spin_lock_irqsave(&venc->lock, flags);
		callback = (struct venc_callback *)arg;
		prev = venc->callback;
		if (!prev)
			return -EINVAL;
		else if (prev == callback)
			venc->callback = callback->next;
		else {
			while (prev->next && (prev->next != callback))
				prev = prev->next;
			if (!prev->next)
				return -EINVAL;
			else
				prev->next = callback->next;
		}
		spin_unlock_irqrestore(&venc->lock, flags);
		break;
	case VENC_INTERRUPT:
		callback = venc->callback;
		event = *((unsigned *)arg);
		while (callback) {
			if (callback->mask & event)
				callback->handler(event, callback->arg);
		callback = callback->next;
		}
		break;
	case VENC_CONFIGURE:
		mode_info = (struct vpbe_enc_mode_info *)arg;

		if (NULL == mode_info)
			return -EINVAL;

		if (pdata->if_params == V4L2_MBUS_FMT_FIXED)
			return 0;
		switch (pdata->if_params) {
		case V4L2_MBUS_FMT_RGB565_2X8_BE:
			ret = venc_set_prgb(sd, mode_info);
			break;
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			ret = venc_set_srgb(sd, mode_info);
			break;
		case V4L2_MBUS_FMT_YUYV10_1X20:
			ret = venc_set_ycc16_modes(sd, mode_info);
			break;
		case V4L2_MBUS_FMT_Y10_1X10:
			ret = venc_set_ycc8_modes(sd, mode_info);
			break;
		default:
			ret = -EINVAL;
		}
	default:
		v4l2_err(sd, "Wrong IOCTL cmd:%x\n", cmd);
		break;
	}
	return ret;
}

static const struct v4l2_subdev_core_ops venc_core_ops = {
	.ioctl      = venc_ioctl,
};

static const struct v4l2_subdev_video_ops venc_video_ops = {
	.s_routing = venc_s_routing,
	.s_std_output = venc_s_std_output,
	.s_dv_preset = venc_s_dv_preset,
};

static const struct v4l2_subdev_ops venc_ops = {
	.core = &venc_core_ops,
	.video = &venc_video_ops,
};

static int venc_initialize(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	int ret = 0;

	/* Set default to output to composite and std to NTSC */
	venc->output = 0;
	venc->std = V4L2_STD_525_60;

	ret = venc_s_routing(sd, 0, venc->output, 0);
	if (ret < 0) {
		v4l2_err(sd, "Error setting output during init\n");
		return -EINVAL;
	}

	ret = venc_s_std_output(sd, venc->std);
	if (ret < 0) {
		v4l2_err(sd, "Error setting std during init\n");
		return -EINVAL;
	}
	return ret;
}

static int venc_device_get(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct venc_state **venc = data;

	if (strcmp(MODULE_NAME, pdev->name) == 0)
		*venc = platform_get_drvdata(pdev);
	return 0;
}

struct v4l2_subdev *venc_sub_dev_init(struct v4l2_device *v4l2_dev,
		const char *venc_name)
{
	struct venc_state *venc;
	int err;

	err = bus_for_each_dev(&platform_bus_type, NULL, &venc,
			venc_device_get);
	if (venc == NULL)
		return NULL;

	v4l2_subdev_init(&venc->sd, &venc_ops);

	strcpy(venc->sd.name, venc_name);
	if (v4l2_device_register_subdev(v4l2_dev, &venc->sd) < 0) {
		v4l2_err(v4l2_dev,
			"vpbe unable to register venc sub device\n");
		return NULL;
	}
	if (venc_initialize(&venc->sd)) {
		v4l2_err(v4l2_dev,
			"vpbe venc initialization failed\n");
		return NULL;
	}
	return &venc->sd;
}
EXPORT_SYMBOL(venc_sub_dev_init);

static int venc_probe(struct platform_device *pdev)
{
	struct venc_state *venc;
	struct resource *res;
	int ret;

	venc = kzalloc(sizeof(struct venc_state), GFP_KERNEL);
	if (venc == NULL)
		return -ENOMEM;

	venc->pdev = &pdev->dev;
	venc->pdata = pdev->dev.platform_data;
	if (NULL == venc->pdata) {
		dev_err(venc->pdev, "Unable to get platform data for"
			" VENC sub device");
		ret = -ENOENT;
		goto free_mem;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(venc->pdev,
			"Unable to get VENC register address map\n");
		ret = -ENODEV;
		goto free_mem;
	}

	if (!request_mem_region(res->start, resource_size(res), "venc")) {
		dev_err(venc->pdev, "Unable to reserve VENC MMIO region\n");
		ret = -ENODEV;
		goto free_mem;
	}

	venc->venc_base = ioremap_nocache(res->start, resource_size(res));
	if (!venc->venc_base) {
		dev_err(venc->pdev, "Unable to map VENC IO space\n");
		ret = -ENODEV;
		goto release_venc_mem_region;
	}

	if (venc->pdata->venc_type != DM644X_VPBE) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!res) {
			dev_err(venc->pdev,
				"Unable to get VDAC_CONFIG address map\n");
			ret = -ENODEV;
			goto unmap_venc_io;
		}

		if (!request_mem_region(res->start,
					resource_size(res), "venc")) {
			dev_err(venc->pdev,
				"Unable to reserve VDAC_CONFIG  MMIO region\n");
			ret = -ENODEV;
			goto unmap_venc_io;
		}

		venc->vdaccfg_reg = ioremap_nocache(res->start,
						    resource_size(res));
		if (!venc->vdaccfg_reg) {
			dev_err(venc->pdev,
				"Unable to map VDAC_CONFIG IO space\n");
			ret = -ENODEV;
			goto release_vdaccfg_mem_region;
		}
	}
	spin_lock_init(&venc->lock);
	platform_set_drvdata(pdev, venc);
	dev_notice(venc->pdev, "VENC sub device probe success\n");
	return 0;

release_vdaccfg_mem_region:
	release_mem_region(res->start, resource_size(res));
unmap_venc_io:
	iounmap(venc->venc_base);
release_venc_mem_region:
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));
free_mem:
	kfree(venc);
	return ret;
}

static int venc_remove(struct platform_device *pdev)
{
	struct venc_state *venc = platform_get_drvdata(pdev);
	struct resource *res;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap((void *)venc->venc_base);
	release_mem_region(res->start, resource_size(res));
	if (venc->pdata->venc_type != DM644X_VPBE) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		iounmap((void *)venc->vdaccfg_reg);
		release_mem_region(res->start, resource_size(res));
	}
	kfree(venc);
	return 0;
}

static struct platform_driver venc_driver = {
	.probe		= venc_probe,
	.remove		= venc_remove,
	.driver		= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
};

static int venc_init(void)
{
	if (platform_driver_register(&venc_driver)) {
		printk(KERN_ERR "Unable to register venc driver\n");
		return -ENODEV;
	}
	return 0;
}

static void venc_exit(void)
{
	platform_driver_unregister(&venc_driver);
	return;
}

module_init(venc_init);
module_exit(venc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VPBE VENC Driver");
MODULE_AUTHOR("Texas Instruments");
