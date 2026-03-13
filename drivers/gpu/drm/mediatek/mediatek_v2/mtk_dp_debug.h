/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_DP_DEBUG_H__
#define __MTK_DP_DEBUG_H__
#include <linux/types.h>

void mtk_dp_debug_enable(bool enable);
bool mtk_dp_debug_get(void);
void mtk_dp_debug(const char *opt);
#ifdef MTK_DPINFO
int mtk_dp_debugfs_init(void);
void mtk_dp_debugfs_deinit(void);
#endif
void mtk_dp_switch_ext_modes(int h, int v, int fps, int aspect_ratio);
void mtk_dp_get_switched_modes(int *cur_h, int *cur_v, int *cur_fps, int *prefer_h, int *prefer_v, int *prefer_fps);

#define DPTXFUNC(fmt, arg...)		\
	pr_info("[DPTX][%s line:%d]"pr_fmt(fmt), __func__, __LINE__, ##arg)

#define DPTXDBG(fmt, arg...)              \
	do {                                 \
		if (mtk_dp_debug_get())                  \
			pr_info("[DPTX]"pr_fmt(fmt), ##arg);     \
	} while (0)

#define DPTXMSG(fmt, arg...)                                  \
		pr_info("[DPTX]"pr_fmt(fmt), ##arg)

#define DPTXERR(fmt, arg...)                                   \
		pr_err("[DPTX][ERROR]"pr_fmt(fmt), ##arg)


#endif

