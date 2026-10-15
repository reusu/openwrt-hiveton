/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt7987-afe-common.h  --  MediaTek 7987 audio driver definitions
 *
 * Copyright (c) 2023 MediaTek Inc.
 * Authors: Vic Wu <vic.wu@mediatek.com>
 *          Maso Huang <maso.huang@mediatek.com>
 */

#ifndef _MT_7987_AFE_COMMON_H_
#define _MT_7987_AFE_COMMON_H_

#include <sound/soc.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include "mtk-base-afe.h"

enum {
	MT7987_MEMIF_DL1,
	MT7987_MEMIF_VUL12,
	MT7987_MEMIF_NUM,
	MT7987_DAI_ETDM = MT7987_MEMIF_NUM,
	MT7987_DAI_NUM,
};

enum {
	MT7987_IRQ_0,
	MT7987_IRQ_1,
	MT7987_IRQ_2,
	MT7987_IRQ_NUM,
};

struct mt7987_afe_private {
	struct clk_bulk_data *clks;
	int num_clks;

	int pm_runtime_bypass_reg_ctl;

	/* dai */
	void *dai_priv[MT7987_DAI_NUM];
};

unsigned int mt7987_afe_rate_transform(struct device *dev,
				       unsigned int rate);

/* dai register */
int mt7987_dai_etdm_register(struct mtk_base_afe *afe);
#endif
