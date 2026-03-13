// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_log.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif
//checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP
#include "../mediatek/mediatek_v2/mtk_disp_notify.h"

#include "../../../misc/mediatek/gate_ic/gate_i2c.h"

static char tianma_bl_tb0[] = {0x51, 0xf, 0xff};

static int tianma_current_fps = 144;
#define SUPPORT_90Hz 0

extern bool lcd_esd_check_flag;//checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP

static bool tm_paper_tp_wakeup_flag = false;
void set_tm_paper_tp_wakeup_flag(bool flag)
{
	tm_paper_tp_wakeup_flag = flag;
}
EXPORT_SYMBOL_GPL(set_tm_paper_tp_wakeup_flag);
struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
	unsigned int lanes;
};

struct tianma {
	struct device *dev;
	struct mipi_dsi_device *dsi;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	struct gpio_desc *vddio_1v8;
	struct gpio_desc *lcm_led_en;
	struct regulator *reg;
	bool prepared;
	bool enabled;
	int error;
	unsigned int gate_ic;
	bool display_dual_swap;
};

#define tianma_dcs_write_seq(ctx, seq...)                                     \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		tianma_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})
#define tianma_dcs_write_seq_static(ctx, seq...)                              \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		tianma_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

static inline struct tianma *panel_to_tianma(struct drm_panel *panel)
{
	return container_of(panel, struct tianma, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int tianma_dcs_read(struct tianma *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void tianma_panel_get_data(struct tianma *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = tianma_dcs_read(ctx, 0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void tianma_dcs_write(struct tianma *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);

	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

/* rc_buf_thresh will right shift 6bits (which means the values here will be divided by 64)
 * when setting to PPS8~PPS11 registers in mtk_dsc_config() function, so the original values
 * need left sihft 6bit (which means the original values are multiplied by 64), so that
 * PPS8~PPS11 registers can get right setting
 */
static unsigned int tianma_rc_buf_thresh[14] = {
//The original values VS values multiplied by 64
//14, 28,  42,	 56,   70,	 84,   98,	 105,  112,  119,  121,  123,  125,  126
896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064};
static unsigned int tianma_range_min_qp[15] = {0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 13};
static unsigned int tianma_range_max_qp[15] = {4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 11, 12, 13, 13, 15};
static int tianma_range_bpg_ofs[15] = {2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12};

static void tianma_panel_init(struct tianma *ctx)
{
	pr_info("%s +\n", __func__);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	//+Peridot-checklist-98167,zhangshaoxiong1.wt,modify,20240305,modify power on sequence
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	//-Peridot-checklist-98167,zhangshaoxiong1.wt,modify,20240305,modify power on sequence
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x24);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x93, 0x1A, 0x00);
	tianma_dcs_write_seq_static(ctx, 0x94, 0xB8, 0x00);
	tianma_dcs_write_seq_static(ctx, 0xDA, 0x06);
	//+Peridot-32,zhangshaoxiong1.wt,add,20240516,add for gamma dither
	tianma_dcs_write_seq_static(ctx, 0x5B, 0x53);
	tianma_dcs_write_seq_static(ctx, 0xDB, 0x78);
	tianma_dcs_write_seq_static(ctx, 0xDE, 0x09);
	//-Peridot-32,zhangshaoxiong1.wt,add,20240516,add for gamma dither
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x20);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x58, 0x43);
	//+Peridot-32,zhangshaoxiong1.wt,add,20240516,add for gamma dither
	tianma_dcs_write_seq_static(ctx, 0x01, 0x55);
	tianma_dcs_write_seq_static(ctx, 0x02, 0x55);
	tianma_dcs_write_seq_static(ctx, 0x08, 0x2D);
	tianma_dcs_write_seq_static(ctx, 0x65, 0xFF);
	tianma_dcs_write_seq_static(ctx, 0x6D, 0xFF);
	//-Peridot-32,zhangshaoxiong1.wt,add,20240516,add for gamma dither
	//+Peridot-32,zhangshaoxiong1.wt,add,20240328,add TE detection code
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x27);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x13, 0x06);
	tianma_dcs_write_seq_static(ctx, 0xD0, 0x71);
	tianma_dcs_write_seq_static(ctx, 0xD1, 0x84);
	tianma_dcs_write_seq_static(ctx, 0xD2, 0x38);
	tianma_dcs_write_seq_static(ctx, 0xDE, 0x43);
	tianma_dcs_write_seq_static(ctx, 0xDF, 0x02);
	//-Peridot-32,zhangshaoxiong1.wt,add,20240328,add TE detection code
	//+Peridot-32,zhangshaoxiong1.wt,add,20240328,add TE detection code
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x20);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x0D, 0x43);
	tianma_dcs_write_seq_static(ctx, 0x69, 0xDD);
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x2A);
	tianma_dcs_write_seq_static(ctx, 0xB1, 0x00, 0x01);
	//-Peridot-32,zhangshaoxiong1.wt,add,20240328,add TE detection code
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x2A);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x1A, 0x1D);
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x25);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x0F, 0x20);
	//+Peridot-32,zhangshaoxiong1.wt,add,20240516,add for gamma dither
	tianma_dcs_write_seq_static(ctx, 0x20, 0x53);
	tianma_dcs_write_seq_static(ctx, 0x27, 0x53);
	tianma_dcs_write_seq_static(ctx, 0x49, 0x53);
	tianma_dcs_write_seq_static(ctx, 0x62, 0x53);

	tianma_dcs_write_seq_static(ctx, 0xFF, 0x26);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x52, 0x52);
	tianma_dcs_write_seq_static(ctx, 0x58, 0x52);
	tianma_dcs_write_seq_static(ctx, 0x5C, 0x52);
	tianma_dcs_write_seq_static(ctx, 0x65, 0x52);
	tianma_dcs_write_seq_static(ctx, 0x73, 0x52);
	tianma_dcs_write_seq_static(ctx, 0x84, 0x1C, 0x1C, 0x1C);
	//-Peridot-32,zhangshaoxiong1.wt,add,20240516,add for gamma dither
	tianma_dcs_write_seq_static(ctx, 0xFF, 0x10);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0x35, 0x00);
	tianma_dcs_write_seq_static(ctx, 0x3B, 0x03, 0xB8, 0x1A, 0x0A, 0x0A, 0x00);
	tianma_dcs_write_seq_static(ctx, 0x9D, 0x01);
	//+Peridot-442,zhangshaoxiong1.wt,modify,20240305,modify B2/B3 register to 80/00 for switching table
	tianma_dcs_write_seq_static(ctx, 0xB2, 0x80);
	tianma_dcs_write_seq_static(ctx, 0xB3, 0x00);
	//-Peridot-442,zhangshaoxiong1.wt,modify,20240305,modify B2/B3 register to 80/00 for switching table
	tianma_dcs_write_seq_static(ctx, 0x11);
	msleep(150);
	tianma_dcs_write_seq_static(ctx, 0x29);
	msleep(20);
	pr_info("%s -\n", __func__);
}

static int tianma_disable(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);
	int data = MTK_DISP_BLANK_POWERDOWN;//checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP

	pr_info("%s+++\n", __func__);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
//+checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP
	if (lcd_esd_check_flag){    //fangzhihua
		printk("esd tianma_unprepare");
		mtk_disp_notifier_call_chain(MTK_DISP_EARLY_EVENT_BLANK,&data);
	}
//-checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP

	pr_info("%s---\n", __func__);

	return 0;
}

static int tianma_unprepare(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);

	pr_info("%s+++\n", __func__);

	if (!ctx->prepared)
		return 0;
		
	tianma_dcs_write_seq_static(ctx, 0x28);
	msleep(20);
	tianma_dcs_write_seq_static(ctx, 0x10);
	msleep(120);

//+peridot-179,fangzhihua.wt,add, 20240307, add wakeup by TP double-click
	pr_info("%s--tm_paper_tp_wakeup_flag tm:%d\n", __func__,tm_paper_tp_wakeup_flag);
	if (!tm_paper_tp_wakeup_flag){
	    //+Peridot-checklist-98167,zhangshaoxiong1.wt,modify,20240305,modify power on sequence
            ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
            gpiod_set_value(ctx->reset_gpio, 0);
	    usleep_range(5 * 1000, 5 * 1000);
            devm_gpiod_put(ctx->dev, ctx->reset_gpio);
            //-Peridot-checklist-98167,zhangshaoxiong1.wt,modify,20240305,modify power on sequenc

	    ctx->bias_neg = devm_gpiod_get(ctx->dev, "bias_n", GPIOD_OUT_HIGH);
	    gpiod_set_value(ctx->bias_neg, 0);
	    devm_gpiod_put(ctx->dev, ctx->bias_neg);

	    usleep_range(2000, 2001);

	    ctx->bias_pos = devm_gpiod_get(ctx->dev, "bias_p", GPIOD_OUT_HIGH);
	    gpiod_set_value(ctx->bias_pos, 0);
	    devm_gpiod_put(ctx->dev, ctx->bias_pos);
	}
//-peridot-179,fangzhihua.wt,add, 20240307, add wakeup by TP double-click
	ctx->error = 0;
	ctx->prepared = false;
	pr_info("%s---\n", __func__);

	return 0;
}
static int tianma_prepare(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);
	int ret;

	pr_info("%s+++\n", __func__);

	if (ctx->prepared)
		return 0;

	ctx->bias_pos = devm_gpiod_get(ctx->dev, "bias_p", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);

	usleep_range(2000, 2001);
	ctx->bias_neg = devm_gpiod_get(ctx->dev, "bias_n", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
	//+Peridot-32,zhangshaoxiong1.wt,add,20240305,add kernel bias control
	usleep_range(200, 201);
	_lcm_i2c_panel_bias_enable();
	//-Peridot-32,zhangshaoxiong1.wt,add,20240305,add kernel bias control

	//Peridot-32,zhangshaoxiong1.wt,modify,20240321,modify backlight current to 21.5ma
	lcm_backlight_init();
    //lcm_backlight_register_readback();
    lcm_second_backlight_init();
    //lcm_second_backlight_register_readback();
    //Peridot-32,zhangshaoxiong1.wt,modify,20240321,modify backlight current to 21.5ma
	tianma_panel_init(ctx);
	ret = ctx->error;
	if (ret < 0) {
		pr_info("Send initial code error!\n");
		tianma_unprepare(panel);
	}

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif

#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif
	pr_info("%s---\n", __func__);

	return ret;
}

static int tianma_enable(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);
	int data = MTK_DISP_BLANK_UNBLANK;//checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP

	pr_info("%s+++\n", __func__);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;
//+checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP
	if (lcd_esd_check_flag){//fangzhihua
		printk("esd tianma_prepare");
		mtk_disp_notifier_call_chain(MTK_DISP_EVENT_BLANK,&data);
	}
//-checlist-98559,fangzhihua.wt,add,20240328,lcd esd recovery notifier suspend/resume to TP

	pr_info("%s---\n", __func__);

	return 0;
}

//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
static const struct drm_display_mode default_mode = {
	.clock = 890618,
	.hdisplay = 2944,
	.hsync_start = 2944 + 36,
	.hsync_end = 2944 + 36 + 6,
	.htotal = 2944 + 36 + 6 + 31,
	.vdisplay = 1840,
	.vsync_start = 1840 + 26,
	.vsync_end = 1840 + 26 + 2,
	.vtotal = 1840 + 26 + 2 + 182,
};

#if SUPPORT_90Hz
static const struct drm_display_mode performance_mode_90hz = {
	.clock = 890618,
	.hdisplay = 2944,
	.hsync_start = 2944 + 36,
	.hsync_end = 2944 + 36 + 6,
	.htotal = 2944 + 36 + 6 + 31,
	.vdisplay = 1840,
	.vsync_start = 1840 + 1256,
	.vsync_end = 1840 + 1256 + 2,
	.vtotal = 1840 + 1256 + 2 + 182,
};
#endif

static const struct drm_display_mode performance_mode_120hz = {
	.clock = 770472,
	.hdisplay = 2944,
	.hsync_start = 2944 + 90,
	.hsync_end = 2944 + 90 + 10,
	.htotal = 2944 + 90 + 10 + 88,
	.vdisplay = 1840,
	.vsync_start = 1840 + 26,
	.vsync_end = 1840 + 26 + 2,
	.vtotal = 1840 + 26 + 2 + 182,
};

static const struct drm_display_mode performance_mode_60hz = {
	.clock = 770472,
	.hdisplay = 2944,
	.hsync_start = 2944 + 90,
	.hsync_end = 2944 + 90 + 10,
	.htotal = 2944 + 90 + 10 + 88,
	.vdisplay = 1840,
	.vsync_start = 1840 + 2076,
	.vsync_end = 1840 + 2076 + 2,
	.vtotal = 1840 + 2076 + 2 + 182,
};

static const struct drm_display_mode performance_mode_30hz = {
	.clock = 770472,
	.hdisplay = 2944,
	.hsync_start = 2944 + 90,
	.hsync_end = 2944 + 90 + 10,
	.htotal = 2944 + 90 + 10 + 88,
	.vdisplay = 1840,
	.vsync_start = 1840 + 6176,
	.vsync_end = 1840 + 6176 + 2,
	.vtotal = 1840 + 6176 + 2 + 182,
};
//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.pll_clk = 502,
	.data_rate = 1004,
	.data_rate_khz = 998137,
	//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.physical_width_um = 273615,
	.physical_height_um = 171009,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	//+Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.cust_esd_check = 0, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x00, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
		.count = 1,
		.para_list[0] = 0x9c,
	},
	//-Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.dsc_params = {
		.enable = 1,
		.dual_dsc_enable = 1,
		.ver = 0x11, /* [7:4] major [3:0] minor */
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 1840, /* need to check */
		.pic_width = 1472,  /* need to check */
		.slice_height = 20,
		.slice_width = 736,
		.chunk_size = 736,
		.xmit_delay = 512,
		.dec_delay = 656,
		.scale_value = 32,
		.increment_interval = 537,
		.decrement_interval = 10,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 940,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = tianma_rc_buf_thresh,
			.range_min_qp = tianma_range_min_qp,
			.range_max_qp = tianma_range_max_qp,
			.range_bpg_ofs = tianma_range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 144,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xB2, 0x80} },
		.dfps_cmd_table[3] = {0, 2, {0xB3, 0x00} },
	},
	.dyn = {
		.switch_en = 1,
		.vfp = 26,
		//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
		.hfp = 36,
		.hbp = 31,
		//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	},
};

#if SUPPORT_90Hz
static struct mtk_panel_params ext_params_90hz = {
	//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.pll_clk = 502,
	.data_rate = 1004,
	.data_rate_khz = 998137,
	//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.physical_width_um = 273615,
	.physical_height_um = 171009,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	//+Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.cust_esd_check = 0, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x00, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
		.count = 1,
		.para_list[0] = 0x9c,
	},
	//-Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.dsc_params = {
		.enable = 1,
		.dual_dsc_enable = 1,
		.ver = 0x11, /* [7:4] major [3:0] minor */
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 1840, /* need to check */
		.pic_width = 1472,  /* need to check */
		.slice_height = 20,
		.slice_width = 736,
		.chunk_size = 736,
		.xmit_delay = 512,
		.dec_delay = 656,
		.scale_value = 32,
		.increment_interval = 537,
		.decrement_interval = 10,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 940,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = tianma_rc_buf_thresh,
			.range_min_qp = tianma_range_min_qp,
			.range_max_qp = tianma_range_max_qp,
			.range_bpg_ofs = tianma_range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 144,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xB2, 0x80} },
		.dfps_cmd_table[3] = {0, 2, {0xB3, 0x00} },
	},
	.dyn = {
		.switch_en = 1,
		.vfp = 1256,
		//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
		.hfp = 36,
		.hbp = 31,
		//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	},
};
#endif

static struct mtk_panel_params ext_params_120hz = {
	//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.pll_clk = 502,
	.data_rate = 1004,
	.data_rate_khz = 998137,
	//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.physical_width_um = 273615,
	.physical_height_um = 171009,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	//+Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.cust_esd_check = 0, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x00, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
		.count = 1,
		.para_list[0] = 0x9c,
	},
	//-Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.dsc_params = {
		.enable = 1,
		.dual_dsc_enable = 1,
		.ver = 0x11, /* [7:4] major [3:0] minor */
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 1840, /* need to check */
		.pic_width = 1472,  /* need to check */
		.slice_height = 20,
		.slice_width = 736,
		.chunk_size = 736,
		.xmit_delay = 512,
		.dec_delay = 656,
		.scale_value = 32,
		.increment_interval = 537,
		.decrement_interval = 10,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 940,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = tianma_rc_buf_thresh,
			.range_min_qp = tianma_range_min_qp,
			.range_max_qp = tianma_range_max_qp,
			.range_bpg_ofs = tianma_range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xB2, 0x91} },
		.dfps_cmd_table[3] = {0, 2, {0xB3, 0x40} },
	},
	.dyn = {
		.switch_en = 1,
		.vfp = 26,
		//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
		.hfp = 90,
		.hbp = 88,
		//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	},
};

static struct mtk_panel_params ext_params_60hz = {
	//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.pll_clk = 502,
	.data_rate = 1004,
	.data_rate_khz = 998137,
	//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.physical_width_um = 273615,
	.physical_height_um = 171009,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	//+Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.cust_esd_check = 0, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x00, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
		.count = 1,
		.para_list[0] = 0x9c,
	},
	//-Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.dsc_params = {
		.enable = 1,
		.dual_dsc_enable = 1,
		.ver = 0x11, /* [7:4] major [3:0] minor */
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 1840, /* need to check */
		.pic_width = 1472,  /* need to check */
		.slice_height = 20,
		.slice_width = 736,
		.chunk_size = 736,
		.xmit_delay = 512,
		.dec_delay = 656,
		.scale_value = 32,
		.increment_interval = 537,
		.decrement_interval = 10,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 940,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = tianma_rc_buf_thresh,
			.range_min_qp = tianma_range_min_qp,
			.range_max_qp = tianma_range_max_qp,
			.range_bpg_ofs = tianma_range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xB2, 0x91} },
		.dfps_cmd_table[3] = {0, 2, {0xB3, 0x40} },
	},
	.dyn = {
		.switch_en = 1,
		.vfp = 2076,
		//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
		.hfp = 90,
		.hbp = 88,
		//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	},
};

static struct mtk_panel_params ext_params_30hz = {
	//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.pll_clk = 502,
	.data_rate = 1004,
	.data_rate_khz = 998137,
	//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	.physical_width_um = 273615,
	.physical_height_um = 171009,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	//+Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.cust_esd_check = 0, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x00, //Peridot-32,zhangshaoxiong1.wt,modify,20240328,change esd check to TE mode
		.count = 1,
		.para_list[0] = 0x9c,
	},
	//-Peridot-chk98181,zhangshaoxiong1.wt,modify,20240321,enable esd check func
	.dsc_params = {
		.enable = 1,
		.dual_dsc_enable = 1,
		.ver = 0x11, /* [7:4] major [3:0] minor */
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 1840, /* need to check */
		.pic_width = 1472,  /* need to check */
		.slice_height = 20,
		.slice_width = 736,
		.chunk_size = 736,
		.xmit_delay = 512,
		.dec_delay = 656,
		.scale_value = 32,
		.increment_interval = 537,
		.decrement_interval = 10,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 940,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = tianma_rc_buf_thresh,
			.range_min_qp = tianma_range_min_qp,
			.range_max_qp = tianma_range_max_qp,
			.range_bpg_ofs = tianma_range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xB2, 0x91} },
		.dfps_cmd_table[3] = {0, 2, {0xB3, 0x40} },
	},
	.dyn = {
		.switch_en = 1,
		.vfp = 6176,
		//+Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
		.hfp = 90,
		.hbp = 88,
		//-Peridot-32,zhangshaoxiong1.wt,modify,20240408,set mipi clk to 502
	},
};

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct tianma *ctx = panel_to_tianma(panel);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

//+Peridot-32,zhangshaoxiong1.wt,modify,20240425,modify power on&off sequence
static int panel_ext_power_on(struct drm_panel *panel)
{
    struct tianma *ctx = panel_to_tianma(panel);

    ctx->vddio_1v8 = devm_gpiod_get(ctx->dev, "vddio_1v8", GPIOD_OUT_HIGH);//modify specific pin in dts
    if (IS_ERR(ctx->vddio_1v8)) {
        printk("cannot get vddio_1v8.\n");
        return PTR_ERR(ctx->vddio_1v8);
    } else {
        pr_info("get vddio_1v8-gpios.\n");
    }
    gpiod_set_value(ctx->vddio_1v8, 1);
    devm_gpiod_put(ctx->dev, ctx->vddio_1v8);

    return 0;
}

static int panel_ext_power_off(struct drm_panel *panel)
{
    struct tianma *ctx = panel_to_tianma(panel);
//+peridot-7257,fangzhihua.wt,mod, 20240516, add wakeup by TP double-click
    pr_info("%s--tm_paper_tp_wakeup_flag boe:%d\n", __func__,tm_paper_tp_wakeup_flag);
    if (!tm_paper_tp_wakeup_flag){//{  //fangzhihua.wt
        ctx->vddio_1v8 = devm_gpiod_get(ctx->dev, "vddio_1v8", GPIOD_OUT_HIGH);//modify specific pin in dts
        if (IS_ERR(ctx->vddio_1v8)) {
            printk("cannot get vddio_1v8.\n");
            return PTR_ERR(ctx->vddio_1v8);
        } else {
            pr_info("get vddio_1v8-gpios.\n");
        }
        gpiod_set_value(ctx->vddio_1v8, 0);
        devm_gpiod_put(ctx->dev, ctx->vddio_1v8);
    }
//-peridot-7257,fangzhihua.wt,mod, 20240516, add wakeup by TP double-click
    return 0;
}
//-Peridot-32,zhangshaoxiong1.wt,modify,20240425,modify power on&off sequence

static int tianma_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_tb[] = {0x51, 0x07, 0xff};

	if (level) {
		tianma_bl_tb0[1] = (level >> 8) & 0xFF;
		tianma_bl_tb0[2] = level & 0xFF;
	}
	bl_tb[1] = (level >> 8) & 0xFF;
	bl_tb[2] = level & 0xFF;

	if (!cb)
		return -1;
	pr_info("%s %d %d %d\n", __func__, level, bl_tb[1], bl_tb[2]);
	cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));
	return 0;
}

struct drm_display_mode *get_mode_by_id_hfp(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}
static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);

	if (drm_mode_vrefresh(m) == 144) {
		ext->params = &ext_params;
		tianma_current_fps = 144;
	}
#if SUPPORT_90Hz
	else if (drm_mode_vrefresh(m) == 90) {
		ext->params = &ext_params_90hz;
		tianma_current_fps = 90;
	}
#endif
	else if (drm_mode_vrefresh(m) == 120) {
		ext->params = &ext_params_120hz;
		tianma_current_fps = 120;
	} else if (drm_mode_vrefresh(m) == 60) {
		ext->params = &ext_params_60hz;
		tianma_current_fps = 60;
	} else if (drm_mode_vrefresh(m) == 30) {
		ext->params = &ext_params_30hz;
		tianma_current_fps = 30;
	} else
		ret = 1;

	return ret;
}

static void mode_switch_to_144(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);

	tianma_dcs_write_seq_static(ctx, 0xFF, 0x10);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0xB2, 0x80);//144hz
	tianma_dcs_write_seq_static(ctx, 0xB3, 0x00);
}

static void mode_switch_to_120(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);

	tianma_dcs_write_seq_static(ctx, 0xFF, 0x10);
	tianma_dcs_write_seq_static(ctx, 0xFB, 0x01);
	tianma_dcs_write_seq_static(ctx, 0xB2, 0x91);//120hz
	tianma_dcs_write_seq_static(ctx, 0xB3, 0x40);
}

static int mode_switch(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, dst_mode);

	if (!m) {
		pr_info("ERROR!! drm_display_mode m is null\n");
		return -ENOMEM;
	}

	pr_info("%s cur_mode = %d dst_mode %d\n", __func__, cur_mode, dst_mode);

	if (drm_mode_vrefresh(m) == 144)
		mode_switch_to_144(panel);
#if SUPPORT_90Hz
	else if (drm_mode_vrefresh(m) == 90)
		mode_switch_to_144(panel);
#endif
	else if (drm_mode_vrefresh(m) == 120)
		mode_switch_to_120(panel);
	else if (drm_mode_vrefresh(m) == 60)
		mode_switch_to_120(panel);
	else if (drm_mode_vrefresh(m) == 30)
		mode_switch_to_120(panel);
	else
		ret = 1;

	return ret;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = tianma_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.mode_switch = mode_switch,
	//+Peridot-32,zhangshaoxiong1.wt,modify,20240425,modify power on&off sequence
	.power_on_pin = panel_ext_power_on,
	.power_off_pin = panel_ext_power_off,
	//-Peridot-32,zhangshaoxiong1.wt,modify,20240425,modify power on&off sequence
};
#endif

static int tianma_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
#if SUPPORT_90Hz
	struct drm_display_mode *mode2;
#endif
	struct drm_display_mode *mode3;
	struct drm_display_mode *mode4;
	struct drm_display_mode *mode5;

	pr_info("%s+++\n", __func__);

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		pr_info("failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

#if SUPPORT_90Hz
	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90hz.hdisplay, performance_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);
#endif

	mode3 = drm_mode_duplicate(connector->dev, &performance_mode_120hz);
	if (!mode3) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_120hz.hdisplay, performance_mode_120hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_120hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode3);

	mode4 = drm_mode_duplicate(connector->dev, &performance_mode_60hz);
	if (!mode4) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_60hz.hdisplay, performance_mode_60hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode4);
	mode4->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode4);

	mode5 = drm_mode_duplicate(connector->dev, &performance_mode_30hz);
	if (!mode5) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_30hz.hdisplay, performance_mode_30hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_30hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode5);
	mode5->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode5);

	connector->display_info.width_mm = 273;
	connector->display_info.height_mm = 171;

	return 1;
}

static const struct drm_panel_funcs tianma_drm_funcs = {
	.disable = tianma_disable,
	.unprepare = tianma_unprepare,
	.prepare = tianma_prepare,
	.enable = tianma_enable,
	.get_modes = tianma_get_modes,
};

static int tianma_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *backlight;
	struct tianma *ctx;
	unsigned int value;
	int ret;

	pr_info("%s+++\n", __func__);

	ctx = devm_kzalloc(dev, sizeof(struct tianma), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->display_dual_swap = of_property_read_bool(dev->of_node,
					      "display-dual-swap");
	pr_notice("ctx->display_dual_swap=%d\n", ctx->display_dual_swap);
	if (ctx->display_dual_swap) {
		ext_params.dual_swap = true;
#if SUPPORT_90Hz
		ext_params_90hz.dual_swap = true;
#endif
		ext_params_120hz.dual_swap = true;
		ext_params_60hz.dual_swap = true;
		ext_params_30hz.dual_swap = true;
	}

	ret = of_property_read_u32(dev->of_node, "gate-ic", &value);
	if (ret < 0) {
		pr_info("%s, Failed to find gate-ic\n", __func__);
		value = 0;
	} else {
		pr_info("%s, Find gate-ic, value=%d\n", __func__, value);
		ctx->gate_ic = value;
	}

	if (ctx->gate_ic == 0) {
		ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			dev_info(dev, "cannot get bias-gpios 0 %ld\n",
				 PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		devm_gpiod_put(dev, ctx->bias_pos);

		ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			dev_info(dev, "cannot get bias-gpios 1 %ld\n",
				 PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		devm_gpiod_put(dev, ctx->bias_neg);
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	/*
	 * ctx->hwen = devm_gpiod_get(dev, "pm-enable", GPIOD_OUT_HIGH);
	 * if (IS_ERR(ctx->hwen)) {
	 *	dev_err(dev, "cannot get hwen-gpios %ld\n",
	 *	PTR_ERR(ctx->hwen));
	 *	return PTR_ERR(ctx->hwen);
	 * }
	 * devm_gpiod_put(dev, ctx->hwen);
	 */

	ctx->prepared = true;
	ctx->enabled = true;
	drm_panel_init(&ctx->panel, dev, &tianma_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	ctx->panel.dev = dev;
	ctx->panel.funcs = &tianma_drm_funcs;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		dev_err(dev, "mipi_dsi_attach fail, ret=%d\n", ret);
		return -EPROBE_DEFER;
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("%s-\n", __func__);

	return ret;
}
static void tianma_remove(struct mipi_dsi_device *dsi)
{
	struct tianma *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	if (ext_ctx != NULL) {
		mtk_panel_detach(ext_ctx);
		mtk_panel_remove(ext_ctx);
	}
#endif
}
static const struct of_device_id tianma_of_match[] = {
	{
		.compatible = "tianma,paper,nt36532,dsi,vdo",
	},
	{}
};
MODULE_DEVICE_TABLE(of, tianma_of_match);
static struct mipi_dsi_driver tianma_driver = {
	.probe = tianma_probe,
	.remove = tianma_remove,
	.driver = {
			.name = "panel-tianma-paper-nt36532-dsi-vdo",
			.owner = THIS_MODULE,
			.of_match_table = tianma_of_match,
		},
};
module_mipi_dsi_driver(tianma_driver);
MODULE_AUTHOR("Huijuan Xie <huijuan.xie@mediatek.com>");
MODULE_DESCRIPTION("tianma wqxga2944 Panel Driver");
MODULE_LICENSE("GPL");

