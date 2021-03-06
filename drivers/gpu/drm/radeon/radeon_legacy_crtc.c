/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/radeon_drm.h>
#include "radeon_fixed.h"
#include "radeon.h"

static void radeon_legacy_rmx_mode_set(struct drm_crtc *crtc,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int xres = mode->hdisplay;
	int yres = mode->vdisplay;
	bool hscale = true, vscale = true;
	int hsync_wid;
	int vsync_wid;
	int hsync_start;
	int blank_width;
	u32 scale, inc, crtc_more_cntl;
	u32 fp_horz_stretch, fp_vert_stretch, fp_horz_vert_active;
	u32 fp_h_sync_strt_wid, fp_crtc_h_total_disp;
	u32 fp_v_sync_strt_wid, fp_crtc_v_total_disp;
	struct radeon_native_mode *native_mode = &radeon_crtc->native_mode;

	fp_vert_stretch = RREG32(RADEON_FP_VERT_STRETCH) &
		(RADEON_VERT_STRETCH_RESERVED |
		 RADEON_VERT_AUTO_RATIO_INC);
	fp_horz_stretch = RREG32(RADEON_FP_HORZ_STRETCH) &
		(RADEON_HORZ_FP_LOOP_STRETCH |
		 RADEON_HORZ_AUTO_RATIO_INC);

	crtc_more_cntl = 0;
	if ((rdev->family == CHIP_RS100) ||
	    (rdev->family == CHIP_RS200)) {
		/* This is to workaround the asic bug for RMX, some versions
		   of BIOS dosen't have this register initialized correctly. */
		crtc_more_cntl |= RADEON_CRTC_H_CUTOFF_ACTIVE_EN;
	}


	fp_crtc_h_total_disp = ((((mode->crtc_htotal / 8) - 1) & 0x3ff)
				| ((((mode->crtc_hdisplay / 8) - 1) & 0x1ff) << 16));

	hsync_wid = (mode->crtc_hsync_end - mode->crtc_hsync_start) / 8;
	if (!hsync_wid)
		hsync_wid = 1;
	hsync_start = mode->crtc_hsync_start - 8;

	fp_h_sync_strt_wid = ((hsync_start & 0x1fff)
			      | ((hsync_wid & 0x3f) << 16)
			      | ((mode->flags & DRM_MODE_FLAG_NHSYNC)
				 ? RADEON_CRTC_H_SYNC_POL
				 : 0));

	fp_crtc_v_total_disp = (((mode->crtc_vtotal - 1) & 0xffff)
				| ((mode->crtc_vdisplay - 1) << 16));

	vsync_wid = mode->crtc_vsync_end - mode->crtc_vsync_start;
	if (!vsync_wid)
		vsync_wid = 1;

	fp_v_sync_strt_wid = (((mode->crtc_vsync_start - 1) & 0xfff)
			      | ((vsync_wid & 0x1f) << 16)
			      | ((mode->flags & DRM_MODE_FLAG_NVSYNC)
				 ? RADEON_CRTC_V_SYNC_POL
				 : 0));

	fp_horz_vert_active = 0;

	if (native_mode->panel_xres == 0 ||
	    native_mode->panel_yres == 0) {
		hscale = false;
		vscale = false;
	} else {
		if (xres > native_mode->panel_xres)
			xres = native_mode->panel_xres;
		if (yres > native_mode->panel_yres)
			yres = native_mode->panel_yres;

		if (xres == native_mode->panel_xres)
			hscale = false;
		if (yres == native_mode->panel_yres)
			vscale = false;
	}

	switch (radeon_crtc->rmx_type) {
	case RMX_FULL:
	case RMX_ASPECT:
		if (!hscale)
			fp_horz_stretch |= ((xres/8-1) << 16);
		else {
			inc = (fp_horz_stretch & RADEON_HORZ_AUTO_RATIO_INC) ? 1 : 0;
			scale = ((xres + inc) * RADEON_HORZ_STRETCH_RATIO_MAX)
				/ native_mode->panel_xres + 1;
			fp_horz_stretch |= (((scale) & RADEON_HORZ_STRETCH_RATIO_MASK) |
					RADEON_HORZ_STRETCH_BLEND |
					RADEON_HORZ_STRETCH_ENABLE |
					((native_mode->panel_xres/8-1) << 16));
		}

		if (!vscale)
			fp_vert_stretch |= ((yres-1) << 12);
		else {
			inc = (fp_vert_stretch & RADEON_VERT_AUTO_RATIO_INC) ? 1 : 0;
			scale = ((yres + inc) * RADEON_VERT_STRETCH_RATIO_MAX)
				/ native_mode->panel_yres + 1;
			fp_vert_stretch |= (((scale) & RADEON_VERT_STRETCH_RATIO_MASK) |
					RADEON_VERT_STRETCH_ENABLE |
					RADEON_VERT_STRETCH_BLEND |
					((native_mode->panel_yres-1) << 12));
		}
		break;
	case RMX_CENTER:
		fp_horz_stretch |= ((xres/8-1) << 16);
		fp_vert_stretch |= ((yres-1) << 12);

		crtc_more_cntl |= (RADEON_CRTC_AUTO_HORZ_CENTER_EN |
				RADEON_CRTC_AUTO_VERT_CENTER_EN);

		blank_width = (mode->crtc_hblank_end - mode->crtc_hblank_start) / 8;
		if (blank_width > 110)
			blank_width = 110;

		fp_crtc_h_total_disp = (((blank_width) & 0x3ff)
				| ((((mode->crtc_hdisplay / 8) - 1) & 0x1ff) << 16));

		hsync_wid = (mode->crtc_hsync_end - mode->crtc_hsync_start) / 8;
		if (!hsync_wid)
			hsync_wid = 1;

		fp_h_sync_strt_wid = ((((mode->crtc_hsync_start - mode->crtc_hblank_start) / 8) & 0x1fff)
				| ((hsync_wid & 0x3f) << 16)
				| ((mode->flags & DRM_MODE_FLAG_NHSYNC)
					? RADEON_CRTC_H_SYNC_POL
					: 0));

		fp_crtc_v_total_disp = (((mode->crtc_vblank_end - mode->crtc_vblank_start) & 0xffff)
				| ((mode->crtc_vdisplay - 1) << 16));

		vsync_wid = mode->crtc_vsync_end - mode->crtc_vsync_start;
		if (!vsync_wid)
			vsync_wid = 1;

		fp_v_sync_strt_wid = ((((mode->crtc_vsync_start - mode->crtc_vblank_start) & 0xfff)
					| ((vsync_wid & 0x1f) << 16)
					| ((mode->flags & DRM_MODE_FLAG_NVSYNC)
						? RADEON_CRTC_V_SYNC_POL
						: 0)));

		fp_horz_vert_active = (((native_mode->panel_yres) & 0xfff) |
				(((native_mode->panel_xres / 8) & 0x1ff) << 16));
		break;
	case RMX_OFF:
	default:
		fp_horz_stretch |= ((xres/8-1) << 16);
		fp_vert_stretch |= ((yres-1) << 12);
		break;
	}

	WREG32(RADEON_FP_HORZ_STRETCH,      fp_horz_stretch);
	WREG32(RADEON_FP_VERT_STRETCH,      fp_vert_stretch);
	WREG32(RADEON_CRTC_MORE_CNTL,       crtc_more_cntl);
	WREG32(RADEON_FP_HORZ_VERT_ACTIVE,  fp_horz_vert_active);
	WREG32(RADEON_FP_H_SYNC_STRT_WID,   fp_h_sync_strt_wid);
	WREG32(RADEON_FP_V_SYNC_STRT_WID,   fp_v_sync_strt_wid);
	WREG32(RADEON_FP_CRTC_H_TOTAL_DISP, fp_crtc_h_total_disp);
	WREG32(RADEON_FP_CRTC_V_TOTAL_DISP, fp_crtc_v_total_disp);
}

void radeon_restore_common_regs(struct drm_device *dev)
{
	/* don't need this yet */
}

static void radeon_pll_wait_for_read_update_complete(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	int i = 0;

	/* FIXME: Certain revisions of R300 can't recover here.  Not sure of
	   the cause yet, but this workaround will mask the problem for now.
	   Other chips usually will pass at the very first test, so the
	   workaround shouldn't have any effect on them. */
	for (i = 0;
	     (i < 10000 &&
	      RREG32_PLL(RADEON_PPLL_REF_DIV) & RADEON_PPLL_ATOMIC_UPDATE_R);
	     i++);
}

static void radeon_pll_write_update(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	while (RREG32_PLL(RADEON_PPLL_REF_DIV) & RADEON_PPLL_ATOMIC_UPDATE_R);

	WREG32_PLL_P(RADEON_PPLL_REF_DIV,
			   RADEON_PPLL_ATOMIC_UPDATE_W,
			   ~(RADEON_PPLL_ATOMIC_UPDATE_W));
}

static void radeon_pll2_wait_for_read_update_complete(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	int i = 0;


	/* FIXME: Certain revisions of R300 can't recover here.  Not sure of
	   the cause yet, but this workaround will mask the problem for now.
	   Other chips usually will pass at the very first test, so the
	   workaround shouldn't have any effect on them. */
	for (i = 0;
	     (i < 10000 &&
	      RREG32_PLL(RADEON_P2PLL_REF_DIV) & RADEON_P2PLL_ATOMIC_UPDATE_R);
	     i++);
}

static void radeon_pll2_write_update(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	while (RREG32_PLL(RADEON_P2PLL_REF_DIV) & RADEON_P2PLL_ATOMIC_UPDATE_R);

	WREG32_PLL_P(RADEON_P2PLL_REF_DIV,
			   RADEON_P2PLL_ATOMIC_UPDATE_W,
			   ~(RADEON_P2PLL_ATOMIC_UPDATE_W));
}

static uint8_t radeon_compute_pll_gain(uint16_t ref_freq, uint16_t ref_div,
				       uint16_t fb_div)
{
	unsigned int vcoFreq;

	if (!ref_div)
		return 1;

	vcoFreq = ((unsigned)ref_freq & fb_div) / ref_div;

	/*
	 * This is horribly crude: the VCO frequency range is divided into
	 * 3 parts, each part having a fixed PLL gain value.
	 */
	if (vcoFreq >= 30000)
		/*
		 * [300..max] MHz : 7
		 */
		return 7;
	else if (vcoFreq >= 18000)
		/*
		 * [180..300) MHz : 4
		 */
		return 4;
	else
		/*
		 * [0..180) MHz : 1
		 */
		return 1;
}

void radeon_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t mask;

	if (radeon_crtc->crtc_id)
		mask = (RADEON_CRTC2_EN |
			RADEON_CRTC2_DISP_DIS |
			RADEON_CRTC2_VSYNC_DIS |
			RADEON_CRTC2_HSYNC_DIS |
			RADEON_CRTC2_DISP_REQ_EN_B);
	else
		mask = (RADEON_CRTC_DISPLAY_DIS |
			RADEON_CRTC_VSYNC_DIS |
			RADEON_CRTC_HSYNC_DIS);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (radeon_crtc->crtc_id)
			WREG32_P(RADEON_CRTC2_GEN_CNTL, RADEON_CRTC2_EN, ~mask);
		else {
			WREG32_P(RADEON_CRTC_GEN_CNTL, RADEON_CRTC_EN, ~(RADEON_CRTC_EN |
									 RADEON_CRTC_DISP_REQ_EN_B));
			WREG32_P(RADEON_CRTC_EXT_CNTL, 0, ~mask);
		}
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (radeon_crtc->crtc_id)
			WREG32_P(RADEON_CRTC2_GEN_CNTL, mask, ~mask);
		else {
			WREG32_P(RADEON_CRTC_GEN_CNTL, RADEON_CRTC_DISP_REQ_EN_B, ~(RADEON_CRTC_EN |
										    RADEON_CRTC_DISP_REQ_EN_B));
			WREG32_P(RADEON_CRTC_EXT_CNTL, mask, ~mask);
		}
		break;
	}

	if (mode != DRM_MODE_DPMS_OFF) {
		radeon_crtc_load_lut(crtc);
	}
}

/* properly set crtc bpp when using atombios */
void radeon_legacy_atom_set_surface(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int format;
	uint32_t crtc_gen_cntl;
	uint32_t disp_merge_cntl;
	uint32_t crtc_pitch;

	switch (crtc->fb->bits_per_pixel) {
	case 15:      /*  555 */
		format = 3;
		break;
	case 16:      /*  565 */
		format = 4;
		break;
	case 24:      /*  RGB */
		format = 5;
		break;
	case 32:      /* xRGB */
		format = 6;
		break;
	default:
		return;
	}

	crtc_pitch  = ((((crtc->fb->pitch / (crtc->fb->bits_per_pixel / 8)) * crtc->fb->bits_per_pixel) +
			((crtc->fb->bits_per_pixel * 8) - 1)) /
		       (crtc->fb->bits_per_pixel * 8));
	crtc_pitch |= crtc_pitch << 16;

	WREG32(RADEON_CRTC_PITCH + radeon_crtc->crtc_offset, crtc_pitch);

	switch (radeon_crtc->crtc_id) {
	case 0:
		disp_merge_cntl = RREG32(RADEON_DISP_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP_RGB_OFFSET_EN;
		WREG32(RADEON_DISP_MERGE_CNTL, disp_merge_cntl);

		crtc_gen_cntl = RREG32(RADEON_CRTC_GEN_CNTL) & 0xfffff0ff;
		crtc_gen_cntl |= (format << 8);
		crtc_gen_cntl |= RADEON_CRTC_EXT_DISP_EN;
		WREG32(RADEON_CRTC_GEN_CNTL, crtc_gen_cntl);
		break;
	case 1:
		disp_merge_cntl = RREG32(RADEON_DISP2_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP2_RGB_OFFSET_EN;
		WREG32(RADEON_DISP2_MERGE_CNTL, disp_merge_cntl);

		crtc_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL) & 0xfffff0ff;
		crtc_gen_cntl |= (format << 8);
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc_gen_cntl);
		WREG32(RADEON_FP_H2_SYNC_STRT_WID,   RREG32(RADEON_CRTC2_H_SYNC_STRT_WID));
		WREG32(RADEON_FP_V2_SYNC_STRT_WID,   RREG32(RADEON_CRTC2_V_SYNC_STRT_WID));
		break;
	}
}

int radeon_crtc_set_base(struct drm_crtc *crtc, int x, int y,
			 struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_framebuffer *radeon_fb;
	struct drm_gem_object *obj;
	uint64_t base;
	uint32_t crtc_offset, crtc_offset_cntl, crtc_tile_x0_y0 = 0;
	uint32_t crtc_pitch, pitch_pixels;
	uint32_t tiling_flags;

	DRM_DEBUG("\n");

	radeon_fb = to_radeon_framebuffer(crtc->fb);

	obj = radeon_fb->obj;
	if (radeon_gem_object_pin(obj, RADEON_GEM_DOMAIN_VRAM, &base)) {
		return -EINVAL;
	}
	/* if scanout was in GTT this really wouldn't work */
	/* crtc offset is from display base addr not FB location */
	radeon_crtc->legacy_display_base_addr = rdev->mc.vram_location;

	base -= radeon_crtc->legacy_display_base_addr;

	crtc_offset_cntl = 0;

	pitch_pixels = crtc->fb->pitch / (crtc->fb->bits_per_pixel / 8);
	crtc_pitch  = (((pitch_pixels * crtc->fb->bits_per_pixel) +
			((crtc->fb->bits_per_pixel * 8) - 1)) /
		       (crtc->fb->bits_per_pixel * 8));
	crtc_pitch |= crtc_pitch << 16;

	radeon_object_get_tiling_flags(obj->driver_private,
				       &tiling_flags, NULL);
	if (tiling_flags & RADEON_TILING_MICRO)
		DRM_ERROR("trying to scanout microtiled buffer\n");

	if (tiling_flags & RADEON_TILING_MACRO) {
		if (ASIC_IS_R300(rdev))
			crtc_offset_cntl |= (R300_CRTC_X_Y_MODE_EN |
					     R300_CRTC_MICRO_TILE_BUFFER_DIS |
					     R300_CRTC_MACRO_TILE_EN);
		else
			crtc_offset_cntl |= RADEON_CRTC_TILE_EN;
	} else {
		if (ASIC_IS_R300(rdev))
			crtc_offset_cntl &= ~(R300_CRTC_X_Y_MODE_EN |
					      R300_CRTC_MICRO_TILE_BUFFER_DIS |
					      R300_CRTC_MACRO_TILE_EN);
		else
			crtc_offset_cntl &= ~RADEON_CRTC_TILE_EN;
	}

	if (tiling_flags & RADEON_TILING_MACRO) {
		if (ASIC_IS_R300(rdev)) {
			crtc_tile_x0_y0 = x | (y << 16);
			base &= ~0x7ff;
		} else {
			int byteshift = crtc->fb->bits_per_pixel >> 4;
			int tile_addr = (((y >> 3) * pitch_pixels +  x) >> (8 - byteshift)) << 11;
			base += tile_addr + ((x << byteshift) % 256) + ((y % 8) << 8);
			crtc_offset_cntl |= (y % 16);
		}
	} else {
		int offset = y * pitch_pixels + x;
		switch (crtc->fb->bits_per_pixel) {
		case 15:
		case 16:
			offset *= 2;
			break;
		case 24:
			offset *= 3;
			break;
		case 32:
			offset *= 4;
			break;
		default:
			return false;
		}
		base += offset;
	}

	base &= ~7;

	crtc_offset = (u32)base;

	WREG32(RADEON_DISPLAY_BASE_ADDR + radeon_crtc->crtc_offset, radeon_crtc->legacy_display_base_addr);

	if (ASIC_IS_R300(rdev)) {
		if (radeon_crtc->crtc_id)
			WREG32(R300_CRTC2_TILE_X0_Y0, crtc_tile_x0_y0);
		else
			WREG32(R300_CRTC_TILE_X0_Y0, crtc_tile_x0_y0);
	}
	WREG32(RADEON_CRTC_OFFSET_CNTL + radeon_crtc->crtc_offset, crtc_offset_cntl);
	WREG32(RADEON_CRTC_OFFSET + radeon_crtc->crtc_offset, crtc_offset);
	WREG32(RADEON_CRTC_PITCH + radeon_crtc->crtc_offset, crtc_pitch);

	if (old_fb && old_fb != crtc->fb) {
		radeon_fb = to_radeon_framebuffer(old_fb);
		radeon_gem_object_unpin(radeon_fb->obj);
	}
	return 0;
}

static bool radeon_set_crtc_timing(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int format;
	int hsync_start;
	int hsync_wid;
	int vsync_wid;
	uint32_t crtc_h_total_disp;
	uint32_t crtc_h_sync_strt_wid;
	uint32_t crtc_v_total_disp;
	uint32_t crtc_v_sync_strt_wid;

	DRM_DEBUG("\n");

	switch (crtc->fb->bits_per_pixel) {
	case 15:      /*  555 */
		format = 3;
		break;
	case 16:      /*  565 */
		format = 4;
		break;
	case 24:      /*  RGB */
		format = 5;
		break;
	case 32:      /* xRGB */
		format = 6;
		break;
	default:
		return false;
	}

	crtc_h_total_disp = ((((mode->crtc_htotal / 8) - 1) & 0x3ff)
			     | ((((mode->crtc_hdisplay / 8) - 1) & 0x1ff) << 16));

	hsync_wid = (mode->crtc_hsync_end - mode->crtc_hsync_start) / 8;
	if (!hsync_wid)
		hsync_wid = 1;
	hsync_start = mode->crtc_hsync_start - 8;

	crtc_h_sync_strt_wid = ((hsync_start & 0x1fff)
				| ((hsync_wid & 0x3f) << 16)
				| ((mode->flags & DRM_MODE_FLAG_NHSYNC)
				   ? RADEON_CRTC_H_SYNC_POL
				   : 0));

	/* This works for double scan mode. */
	crtc_v_total_disp = (((mode->crtc_vtotal - 1) & 0xffff)
			     | ((mode->crtc_vdisplay - 1) << 16));

	vsync_wid = mode->crtc_vsync_end - mode->crtc_vsync_start;
	if (!vsync_wid)
		vsync_wid = 1;

	crtc_v_sync_strt_wid = (((mode->crtc_vsync_start - 1) & 0xfff)
				| ((vsync_wid & 0x1f) << 16)
				| ((mode->flags & DRM_MODE_FLAG_NVSYNC)
				   ? RADEON_CRTC_V_SYNC_POL
				   : 0));

	/* TODO -> Dell Server */
	if (0) {
		uint32_t disp_hw_debug = RREG32(RADEON_DISP_HW_DEBUG);
		uint32_t tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
		uint32_t dac2_cntl = RREG32(RADEON_DAC_CNTL2);
		uint32_t crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);

		dac2_cntl &= ~RADEON_DAC2_DAC_CLK_SEL;
		dac2_cntl |= RADEON_DAC2_DAC2_CLK_SEL;

		/* For CRT on DAC2, don't turn it on if BIOS didn't
		   enable it, even it's detected.
		*/
		disp_hw_debug |= RADEON_CRT2_DISP1_SEL;
		tv_dac_cntl &= ~((1<<2) | (3<<8) | (7<<24) | (0xff<<16));
		tv_dac_cntl |= (0x03 | (2<<8) | (0x58<<16));

		WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
		WREG32(RADEON_DISP_HW_DEBUG, disp_hw_debug);
		WREG32(RADEON_DAC_CNTL2, dac2_cntl);
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	}

	if (radeon_crtc->crtc_id) {
		uint32_t crtc2_gen_cntl;
		uint32_t disp2_merge_cntl;

		/* check to see if TV DAC is enabled for another crtc and keep it enabled */
		if (RREG32(RADEON_CRTC2_GEN_CNTL) & RADEON_CRTC2_CRT2_ON)
			crtc2_gen_cntl = RADEON_CRTC2_CRT2_ON;
		else
			crtc2_gen_cntl = 0;

		crtc2_gen_cntl |= ((format << 8)
				   | RADEON_CRTC2_VSYNC_DIS
				   | RADEON_CRTC2_HSYNC_DIS
				   | RADEON_CRTC2_DISP_DIS
				   | RADEON_CRTC2_DISP_REQ_EN_B
				   | ((mode->flags & DRM_MODE_FLAG_DBLSCAN)
				      ? RADEON_CRTC2_DBL_SCAN_EN
				      : 0)
				   | ((mode->flags & DRM_MODE_FLAG_CSYNC)
				      ? RADEON_CRTC2_CSYNC_EN
				      : 0)
				   | ((mode->flags & DRM_MODE_FLAG_INTERLACE)
				      ? RADEON_CRTC2_INTERLACE_EN
				      : 0));

		disp2_merge_cntl = RREG32(RADEON_DISP2_MERGE_CNTL);
		disp2_merge_cntl &= ~RADEON_DISP2_RGB_OFFSET_EN;

		WREG32(RADEON_DISP2_MERGE_CNTL, disp2_merge_cntl);
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	} else {
		uint32_t crtc_gen_cntl;
		uint32_t crtc_ext_cntl;
		uint32_t disp_merge_cntl;

		crtc_gen_cntl = (RADEON_CRTC_EXT_DISP_EN
				 | (format << 8)
				 | RADEON_CRTC_DISP_REQ_EN_B
				 | ((mode->flags & DRM_MODE_FLAG_DBLSCAN)
				    ? RADEON_CRTC_DBL_SCAN_EN
				    : 0)
				 | ((mode->flags & DRM_MODE_FLAG_CSYNC)
				    ? RADEON_CRTC_CSYNC_EN
				    : 0)
				 | ((mode->flags & DRM_MODE_FLAG_INTERLACE)
				    ? RADEON_CRTC_INTERLACE_EN
				    : 0));

		crtc_ext_cntl = RREG32(RADEON_CRTC_EXT_CNTL);
		crtc_ext_cntl |= (RADEON_XCRT_CNT_EN |
				  RADEON_CRTC_VSYNC_DIS |
				  RADEON_CRTC_HSYNC_DIS |
				  RADEON_CRTC_DISPLAY_DIS);

		disp_merge_cntl = RREG32(RADEON_DISP_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP_RGB_OFFSET_EN;

		WREG32(RADEON_DISP_MERGE_CNTL, disp_merge_cntl);
		WREG32(RADEON_CRTC_GEN_CNTL, crtc_gen_cntl);
		WREG32(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl);
	}

	WREG32(RADEON_CRTC_H_TOTAL_DISP + radeon_crtc->crtc_offset, crtc_h_total_disp);
	WREG32(RADEON_CRTC_H_SYNC_STRT_WID + radeon_crtc->crtc_offset, crtc_h_sync_strt_wid);
	WREG32(RADEON_CRTC_V_TOTAL_DISP + radeon_crtc->crtc_offset, crtc_v_total_disp);
	WREG32(RADEON_CRTC_V_SYNC_STRT_WID + radeon_crtc->crtc_offset, crtc_v_sync_strt_wid);

	return true;
}

static void radeon_set_pll(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_encoder *encoder;
	uint32_t feedback_div = 0;
	uint32_t frac_fb_div = 0;
	uint32_t reference_div = 0;
	uint32_t post_divider = 0;
	uint32_t freq = 0;
	uint8_t pll_gain;
	int pll_flags = RADEON_PLL_LEGACY;
	bool use_bios_divs = false;
	/* PLL registers */
	uint32_t pll_ref_div = 0;
	uint32_t pll_fb_post_div = 0;
	uint32_t htotal_cntl = 0;

	struct radeon_pll *pll;

	struct {
		int divider;
		int bitvalue;
	} *post_div, post_divs[]   = {
		/* From RAGE 128 VR/RAGE 128 GL Register
		 * Reference Manual (Technical Reference
		 * Manual P/N RRG-G04100-C Rev. 0.04), page
		 * 3-17 (PLL_DIV_[3:0]).
		 */
		{  1, 0 },              /* VCLK_SRC                 */
		{  2, 1 },              /* VCLK_SRC/2               */
		{  4, 2 },              /* VCLK_SRC/4               */
		{  8, 3 },              /* VCLK_SRC/8               */
		{  3, 4 },              /* VCLK_SRC/3               */
		{ 16, 5 },              /* VCLK_SRC/16              */
		{  6, 6 },              /* VCLK_SRC/6               */
		{ 12, 7 },              /* VCLK_SRC/12              */
		{  0, 0 }
	};

	if (radeon_crtc->crtc_id)
		pll = &rdev->clock.p2pll;
	else
		pll = &rdev->clock.p1pll;

	if (mode->clock > 200000) /* range limits??? */
		pll_flags |= RADEON_PLL_PREFER_HIGH_FB_DIV;
	else
		pll_flags |= RADEON_PLL_PREFER_LOW_REF_DIV;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			if (encoder->encoder_type != DRM_MODE_ENCODER_DAC)
				pll_flags |= RADEON_PLL_NO_ODD_POST_DIV;
			if (encoder->encoder_type == DRM_MODE_ENCODER_LVDS) {
				struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
				struct radeon_encoder_lvds *lvds = (struct radeon_encoder_lvds *)radeon_encoder->enc_priv;
				if (lvds) {
					if (lvds->use_bios_dividers) {
						pll_ref_div = lvds->panel_ref_divider;
						pll_fb_post_div   = (lvds->panel_fb_divider |
								     (lvds->panel_post_divider << 16));
						htotal_cntl  = 0;
						use_bios_divs = true;
					}
				}
				pll_flags |= RADEON_PLL_USE_REF_DIV;
			}
		}
	}

	DRM_DEBUG("\n");

	if (!use_bios_divs) {
		radeon_compute_pll(pll, mode->clock,
				   &freq, &feedback_div, &frac_fb_div,
				   &reference_div, &post_divider,
				   pll_flags);

		for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
			if (post_div->divider == post_divider)
				break;
		}

		if (!post_div->divider)
			post_div = &post_divs[0];

		DRM_DEBUG("dc=%u, fd=%d, rd=%d, pd=%d\n",
			  (unsigned)freq,
			  feedback_div,
			  reference_div,
			  post_divider);

		pll_ref_div   = reference_div;
#if defined(__powerpc__) && (0) /* TODO */
		/* apparently programming this otherwise causes a hang??? */
		if (info->MacModel == RADEON_MAC_IBOOK)
			pll_fb_post_div = 0x000600ad;
		else
#endif
			pll_fb_post_div     = (feedback_div | (post_div->bitvalue << 16));

		htotal_cntl    = mode->htotal & 0x7;

	}

	pll_gain = radeon_compute_pll_gain(pll->reference_freq,
					   pll_ref_div & 0x3ff,
					   pll_fb_post_div & 0x7ff);

	if (radeon_crtc->crtc_id) {
		uint32_t pixclks_cntl = ((RREG32_PLL(RADEON_PIXCLKS_CNTL) &
					  ~(RADEON_PIX2CLK_SRC_SEL_MASK)) |
					 RADEON_PIX2CLK_SRC_SEL_P2PLLCLK);

		WREG32_PLL_P(RADEON_PIXCLKS_CNTL,
			     RADEON_PIX2CLK_SRC_SEL_CPUCLK,
			     ~(RADEON_PIX2CLK_SRC_SEL_MASK));

		WREG32_PLL_P(RADEON_P2PLL_CNTL,
			     RADEON_P2PLL_RESET
			     | RADEON_P2PLL_ATOMIC_UPDATE_EN
			     | ((uint32_t)pll_gain << RADEON_P2PLL_PVG_SHIFT),
			     ~(RADEON_P2PLL_RESET
			       | RADEON_P2PLL_ATOMIC_UPDATE_EN
			       | RADEON_P2PLL_PVG_MASK));

		WREG32_PLL_P(RADEON_P2PLL_REF_DIV,
			     pll_ref_div,
			     ~RADEON_P2PLL_REF_DIV_MASK);

		WREG32_PLL_P(RADEON_P2PLL_DIV_0,
			     pll_fb_post_div,
			     ~RADEON_P2PLL_FB0_DIV_MASK);

		WREG32_PLL_P(RADEON_P2PLL_DIV_0,
			     pll_fb_post_div,
			     ~RADEON_P2PLL_POST0_DIV_MASK);

		radeon_pll2_write_update(dev);
		radeon_pll2_wait_for_read_update_complete(dev);

		WREG32_PLL(RADEON_HTOTAL2_CNTL, htotal_cntl);

		WREG32_PLL_P(RADEON_P2PLL_CNTL,
			     0,
			     ~(RADEON_P2PLL_RESET
			       | RADEON_P2PLL_SLEEP
			       | RADEON_P2PLL_ATOMIC_UPDATE_EN));

		DRM_DEBUG("Wrote2: 0x%08x 0x%08x 0x%08x (0x%08x)\n",
			  (unsigned)pll_ref_div,
			  (unsigned)pll_fb_post_div,
			  (unsigned)htotal_cntl,
			  RREG32_PLL(RADEON_P2PLL_CNTL));
		DRM_DEBUG("Wrote2: rd=%u, fd=%u, pd=%u\n",
			  (unsigned)pll_ref_div & RADEON_P2PLL_REF_DIV_MASK,
			  (unsigned)pll_fb_post_div & RADEON_P2PLL_FB0_DIV_MASK,
			  (unsigned)((pll_fb_post_div &
				      RADEON_P2PLL_POST0_DIV_MASK) >> 16));

		mdelay(50); /* Let the clock to lock */

		WREG32_PLL_P(RADEON_PIXCLKS_CNTL,
			     RADEON_PIX2CLK_SRC_SEL_P2PLLCLK,
			     ~(RADEON_PIX2CLK_SRC_SEL_MASK));

		WREG32_PLL(RADEON_PIXCLKS_CNTL, pixclks_cntl);
	} else {
		if (rdev->flags & RADEON_IS_MOBILITY) {
			/* A temporal workaround for the occational blanking on certain laptop panels.
			   This appears to related to the PLL divider registers (fail to lock?).
			   It occurs even when all dividers are the same with their old settings.
			   In this case we really don't need to fiddle with PLL registers.
			   By doing this we can avoid the blanking problem with some panels.
			*/
			if ((pll_ref_div == (RREG32_PLL(RADEON_PPLL_REF_DIV) & RADEON_PPLL_REF_DIV_MASK)) &&
			    (pll_fb_post_div == (RREG32_PLL(RADEON_PPLL_DIV_3) &
						 (RADEON_PPLL_POST3_DIV_MASK | RADEON_PPLL_FB3_DIV_MASK)))) {
				WREG32_P(RADEON_CLOCK_CNTL_INDEX,
					 RADEON_PLL_DIV_SEL,
					 ~(RADEON_PLL_DIV_SEL));
				r100_pll_errata_after_index(rdev);
				return;
			}
		}

		WREG32_PLL_P(RADEON_VCLK_ECP_CNTL,
			     RADEON_VCLK_SRC_SEL_CPUCLK,
			     ~(RADEON_VCLK_SRC_SEL_MASK));
		WREG32_PLL_P(RADEON_PPLL_CNTL,
			     RADEON_PPLL_RESET
			     | RADEON_PPLL_ATOMIC_UPDATE_EN
			     | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN
			     | ((uint32_t)pll_gain << RADEON_PPLL_PVG_SHIFT),
			     ~(RADEON_PPLL_RESET
			       | RADEON_PPLL_ATOMIC_UPDATE_EN
			       | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN
			       | RADEON_PPLL_PVG_MASK));

		WREG32_P(RADEON_CLOCK_CNTL_INDEX,
			 RADEON_PLL_DIV_SEL,
			 ~(RADEON_PLL_DIV_SEL));
		r100_pll_errata_after_index(rdev);

		if (ASIC_IS_R300(rdev) ||
		    (rdev->family == CHIP_RS300) ||
		    (rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
			if (pll_ref_div & R300_PPLL_REF_DIV_ACC_MASK) {
				/* When restoring console mode, use saved PPLL_REF_DIV
				 * setting.
				 */
				WREG32_PLL_P(RADEON_PPLL_REF_DIV,
					     pll_ref_div,
					     0);
			} else {
				/* R300 uses ref_div_acc field as real ref divider */
				WREG32_PLL_P(RADEON_PPLL_REF_DIV,
					     (pll_ref_div << R300_PPLL_REF_DIV_ACC_SHIFT),
					     ~R300_PPLL_REF_DIV_ACC_MASK);
			}
		} else
			WREG32_PLL_P(RADEON_PPLL_REF_DIV,
				     pll_ref_div,
				     ~RADEON_PPLL_REF_DIV_MASK);

		WREG32_PLL_P(RADEON_PPLL_DIV_3,
			     pll_fb_post_div,
			     ~RADEON_PPLL_FB3_DIV_MASK);

		WREG32_PLL_P(RADEON_PPLL_DIV_3,
			     pll_fb_post_div,
			     ~RADEON_PPLL_POST3_DIV_MASK);

		radeon_pll_write_update(dev);
		radeon_pll_wait_for_read_update_complete(dev);

		WREG32_PLL(RADEON_HTOTAL_CNTL, htotal_cntl);

		WREG32_PLL_P(RADEON_PPLL_CNTL,
			     0,
			     ~(RADEON_PPLL_RESET
			       | RADEON_PPLL_SLEEP
			       | RADEON_PPLL_ATOMIC_UPDATE_EN
			       | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN));

		DRM_DEBUG("Wrote: 0x%08x 0x%08x 0x%08x (0x%08x)\n",
			  pll_ref_div,
			  pll_fb_post_div,
			  (unsigned)htotal_cntl,
			  RREG32_PLL(RADEON_PPLL_CNTL));
		DRM_DEBUG("Wrote: rd=%d, fd=%d, pd=%d\n",
			  pll_ref_div & RADEON_PPLL_REF_DIV_MASK,
			  pll_fb_post_div & RADEON_PPLL_FB3_DIV_MASK,
			  (pll_fb_post_div & RADEON_PPLL_POST3_DIV_MASK) >> 16);

		mdelay(50); /* Let the clock to lock */

		WREG32_PLL_P(RADEON_VCLK_ECP_CNTL,
			     RADEON_VCLK_SRC_SEL_PPLLCLK,
			     ~(RADEON_VCLK_SRC_SEL_MASK));

	}
}

static bool radeon_crtc_mode_fixup(struct drm_crtc *crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	if (!radeon_crtc_scaling_mode_fixup(crtc, mode, adjusted_mode))
		return false;
	return true;
}

static int radeon_crtc_mode_set(struct drm_crtc *crtc,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode,
				 int x, int y, struct drm_framebuffer *old_fb)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	/* TODO TV */
	radeon_crtc_set_base(crtc, x, y, old_fb);
	radeon_set_crtc_timing(crtc, adjusted_mode);
	radeon_set_pll(crtc, adjusted_mode);
	radeon_bandwidth_update(rdev);
	if (radeon_crtc->crtc_id == 0) {
		radeon_legacy_rmx_mode_set(crtc, mode, adjusted_mode);
	} else {
		if (radeon_crtc->rmx_type != RMX_OFF) {
			/* FIXME: only first crtc has rmx what should we
			 * do ?
			 */
			DRM_ERROR("Mode need scaling but only first crtc can do that.\n");
		}
	}
	return 0;
}

static void radeon_crtc_prepare(struct drm_crtc *crtc)
{
	radeon_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void radeon_crtc_commit(struct drm_crtc *crtc)
{
	radeon_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static const struct drm_crtc_helper_funcs legacy_helper_funcs = {
	.dpms = radeon_crtc_dpms,
	.mode_fixup = radeon_crtc_mode_fixup,
	.mode_set = radeon_crtc_mode_set,
	.mode_set_base = radeon_crtc_set_base,
	.prepare = radeon_crtc_prepare,
	.commit = radeon_crtc_commit,
};


void radeon_legacy_init_crtc(struct drm_device *dev,
			       struct radeon_crtc *radeon_crtc)
{
	if (radeon_crtc->crtc_id == 1)
		radeon_crtc->crtc_offset = RADEON_CRTC2_H_TOTAL_DISP - RADEON_CRTC_H_TOTAL_DISP;
	drm_crtc_helper_add(&radeon_crtc->base, &legacy_helper_funcs);
}
