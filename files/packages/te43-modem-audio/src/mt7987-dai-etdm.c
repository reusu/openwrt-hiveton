// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC Audio DAI eTDM Control
 *
 * Copyright (c) 2023 MediaTek Inc.
 * Authors: Vic Wu <vic.wu@mediatek.com>
 *          Maso Huang <maso.huang@mediatek.com>
 *
 * TE43 adaptation of the Linux v6.12 MT7986 driver. Built out of tree;
 * the OpenWrt/Linux source is not patched. MT7987 OUT5 offsets follow
 * MT7987A Datasheet v1.2, Table 9-73. Modem framing follows RM500Q-GL.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <sound/pcm_params.h>
#include "mt7987-afe-common.h"
#include "mt7987-reg.h"

#define HOPPING_CLK  0
#define APLL_CLK     1
#define MTK_DAI_ETDM_FORMAT_I2S   0
#define MTK_DAI_ETDM_FORMAT_DSPA  4
#define MTK_DAI_ETDM_FORMAT_DSPB  5

enum {
	MTK_ETDM_RATE_8K = 0,
	MTK_ETDM_RATE_12K = 1,
	MTK_ETDM_RATE_16K = 2,
	MTK_ETDM_RATE_24K = 3,
	MTK_ETDM_RATE_32K = 4,
	MTK_ETDM_RATE_48K = 5,
	MTK_ETDM_RATE_96K = 7,
	MTK_ETDM_RATE_192K = 9,
	MTK_ETDM_RATE_11K = 16,
	MTK_ETDM_RATE_22K = 17,
	MTK_ETDM_RATE_44K = 18,
	MTK_ETDM_RATE_88K = 19,
	MTK_ETDM_RATE_176K = 20,
};

struct mtk_dai_etdm_priv {
	bool bck_inv;
	unsigned int format;
	unsigned int bclk_ratio;
	unsigned int users;
	unsigned long running;
	struct mutex users_lock;
	spinlock_t trigger_lock;
};

static struct mtk_dai_etdm_priv *mtk_dai_etdm_data(struct mtk_base_afe *afe)
{
	struct mt7987_afe_private *afe_priv = afe->platform_priv;

	return afe_priv->dai_priv[MT7987_DAI_ETDM];
}

static unsigned int mt7987_etdm_rate_transform(struct device *dev, unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_ETDM_RATE_8K;
	case 11025:
		return MTK_ETDM_RATE_11K;
	case 12000:
		return MTK_ETDM_RATE_12K;
	case 16000:
		return MTK_ETDM_RATE_16K;
	case 22050:
		return MTK_ETDM_RATE_22K;
	case 24000:
		return MTK_ETDM_RATE_24K;
	case 32000:
		return MTK_ETDM_RATE_32K;
	case 44100:
		return MTK_ETDM_RATE_44K;
	case 48000:
		return MTK_ETDM_RATE_48K;
	case 88200:
		return MTK_ETDM_RATE_88K;
	case 96000:
		return MTK_ETDM_RATE_96K;
	case 176400:
		return MTK_ETDM_RATE_176K;
	case 192000:
		return MTK_ETDM_RATE_192K;
	default:
		dev_warn(dev, "%s(), rate %u invalid, using %d!!!\n",
			 __func__, rate, MTK_ETDM_RATE_48K);
		return MTK_ETDM_RATE_48K;
	}
}

static int get_etdm_wlen(unsigned int bitwidth)
{
	return bitwidth <= 16 ? 16 : 32;
}

/* dai component */
/* interconnection */

static const struct snd_kcontrol_new o124_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I032_Switch", AFE_CONN124_1, 0, 1, 0),
};

static const struct snd_kcontrol_new o125_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I033_Switch", AFE_CONN125_1, 1, 1, 0),
};

static const struct snd_soc_dapm_widget mtk_dai_etdm_widgets[] = {

	/* DL */
	SND_SOC_DAPM_MIXER("I150", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I151", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* UL */
	SND_SOC_DAPM_MIXER("O124", SND_SOC_NOPM, 0, 0, o124_mix, ARRAY_SIZE(o124_mix)),
	SND_SOC_DAPM_MIXER("O125", SND_SOC_NOPM, 0, 0, o125_mix, ARRAY_SIZE(o125_mix)),
};

static const struct snd_soc_dapm_route mtk_dai_etdm_routes[] = {
	{"I150", NULL, "ETDM Capture"},
	{"I151", NULL, "ETDM Capture"},
	{"ETDM Playback", NULL, "O124"},
	{"ETDM Playback", NULL, "O125"},
	{"O124", "I032_Switch", "I032"},
	{"O125", "I033_Switch", "I033"},
};

/* dai ops */
static int mtk_dai_etdm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt7987_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *data = mtk_dai_etdm_data(afe);
	int ret = 0;

	mutex_lock(&data->users_lock);
	if (!data->users) {
		ret = clk_bulk_prepare_enable(afe_priv->num_clks, afe_priv->clks);
		if (ret)
			goto out;
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON2,
				   CLK_OUT5_PDN_MASK | CLK_IN5_PDN_MASK, 0);
	}
	data->users++;
out:
	mutex_unlock(&data->users_lock);
	return ret;
}

static void mtk_dai_etdm_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt7987_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *data = mtk_dai_etdm_data(afe);

	mutex_lock(&data->users_lock);
	if (data->users && !--data->users) {
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON2,
				   CLK_OUT5_PDN_MASK | CLK_IN5_PDN_MASK,
				   CLK_OUT5_PDN | CLK_IN5_PDN);
		clk_bulk_disable_unprepare(afe_priv->num_clks, afe_priv->clks);
	}
	mutex_unlock(&data->users_lock);
}

static unsigned int get_etdm_ch_fixup(unsigned int channels)
{
	if (channels > 16)
		return 24;
	else if (channels > 8)
		return 16;
	else if (channels > 4)
		return 8;
	else if (channels > 2)
		return 4;
	else
		return 2;
}

static int mtk_dai_etdm_config(struct mtk_base_afe *afe,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai,
			       int stream)
{
	struct mt7987_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data = afe_priv->dai_priv[dai->id];
	unsigned int rate = params_rate(params);
	unsigned int etdm_rate = mt7987_etdm_rate_transform(afe->dev, rate);
	unsigned int afe_rate = mt7987_afe_rate_transform(afe->dev, rate);
	unsigned int channels = params_channels(params);
	unsigned int bit_width = params_width(params);
	unsigned int wlen = get_etdm_wlen(bit_width);
	unsigned int frame_channels = get_etdm_ch_fixup(channels);
	unsigned int val = 0;
	unsigned int mask = 0;

	dev_dbg(afe->dev, "%s(), stream %d, rate %u, bitwidth %u\n",
		 __func__, stream, rate, bit_width);

	/* Serial framing can contain padding slots beyond the two audio channels. */
	if (etdm_data->bclk_ratio) {
		if (etdm_data->bclk_ratio % wlen ||
		    rate * etdm_data->bclk_ratio > 12288000 ||
		    (etdm_data->format != MTK_DAI_ETDM_FORMAT_DSPA &&
		     etdm_data->format != MTK_DAI_ETDM_FORMAT_DSPB))
			return -EINVAL;
		frame_channels = etdm_data->bclk_ratio / wlen;
		switch (frame_channels) {
		case 2: case 4: case 6: case 8:
		case 12: case 16: case 24: case 32:
			break;
		default:
			return -EINVAL;
		}
	}

	/* CON0 */
	mask |= ETDM_BIT_LEN_MASK;
	val |= FIELD_PREP(ETDM_BIT_LEN_MASK, bit_width - 1);
	mask |= ETDM_WRD_LEN_MASK;
	val |= FIELD_PREP(ETDM_WRD_LEN_MASK, wlen - 1);
	mask |= ETDM_FMT_MASK;
	val |= FIELD_PREP(ETDM_FMT_MASK, etdm_data->format);
	mask |= ETDM_CH_NUM_MASK;
	val |= FIELD_PREP(ETDM_CH_NUM_MASK, frame_channels - 1);
	mask |= RELATCH_SRC_MASK;
	val |= FIELD_PREP(RELATCH_SRC_MASK, APLL_CLK);

	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		/* set ETDM_OUT5_CON0 */
		regmap_update_bits(afe->regmap,
				   ETDM_OUT5_CON0,
				   mask, val);

		/* set ETDM_OUT5_CON4 */
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON4,
				   OUT_RELATCH_MASK, OUT_RELATCH(afe_rate));
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON4,
				   OUT_CLK_SRC_MASK, OUT_CLK_SRC(APLL_CLK));
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON4,
				   OUT_SEL_FS_MASK, OUT_SEL_FS(etdm_rate));

		/* set ETDM_OUT5_CON5 */
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON5,
				   ETDM_CLK_DIV_MASK, ETDM_CLK_DIV);
		if (etdm_data->bclk_ratio)
			regmap_update_bits(afe->regmap,
				ETDM_OUT5_CON5,
				OUT_CLK_INV_MASK, etdm_data->bck_inv ? OUT_CLK_INV : 0);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		/* set ETDM_IN5_CON0 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON0, mask, val);
		regmap_update_bits(afe->regmap, ETDM_IN5_CON0,
				   ETDM_SYNC_MASK, ETDM_SYNC);

		/* set ETDM_IN5_CON2 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON2,
				   IN_CLK_SRC_MASK, IN_CLK_SRC(APLL_CLK));

		/* set ETDM_IN5_CON3 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON3,
				   IN_SEL_FS_MASK, IN_SEL_FS(etdm_rate));

		/* set ETDM_IN5_CON4 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON4,
				   IN_RELATCH_MASK, IN_RELATCH(afe_rate));
		if (etdm_data->bclk_ratio)
			regmap_update_bits(afe->regmap, ETDM_IN5_CON4,
				IN_CLK_INV_MASK, etdm_data->bck_inv ? IN_CLK_INV : 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_dai_etdm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	unsigned int rate = params_rate(params);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int ret;

	switch (rate) {
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		ret = mtk_dai_etdm_config(afe, params, dai, SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			return ret;
		return mtk_dai_etdm_config(afe, params, dai, SNDRV_PCM_STREAM_CAPTURE);
	default:
		dev_err(afe->dev,
			"Sample rate %d invalid. Supported rates: 8/12/16/24/32/48/96/192 kHz\n",
			rate);
		return -EINVAL;
	}
}

static int mtk_dai_etdm_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mtk_dai_etdm_priv *data = mtk_dai_etdm_data(afe);
	unsigned long flags, previous;
	int ret = 0;

	spin_lock_irqsave(&data->trigger_lock, flags);
	previous = data->running;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		data->running |= BIT(substream->stream);
		if (!previous) {
			regmap_update_bits(afe->regmap, ETDM_IN5_CON0,
					   ETDM_EN_MASK, ETDM_EN);
			regmap_update_bits(afe->regmap,
				ETDM_OUT5_CON0,
				ETDM_EN_MASK, ETDM_EN);
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		data->running &= ~BIT(substream->stream);
		/* TX/RX share clocks: stopping one direction must not stop the other. */
		if (previous && !data->running) {
			regmap_update_bits(afe->regmap, ETDM_IN5_CON0, ETDM_EN_MASK, 0);
			regmap_update_bits(afe->regmap,
				ETDM_OUT5_CON0, ETDM_EN_MASK, 0);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&data->trigger_lock, flags);
	return ret;
}

static int mtk_dai_etdm_set_bclk_ratio(struct snd_soc_dai *dai,
				     unsigned int ratio)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mtk_dai_etdm_priv *data = mtk_dai_etdm_data(afe);

	if (dai->id != MT7987_DAI_ETDM || ratio > 1024 || (ratio && ratio < 32))
		return -EINVAL;
	data->bclk_ratio = ratio;
	return 0;
}

static int mtk_dai_etdm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt7987_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;

	switch (dai->id) {
	case MT7987_DAI_ETDM:
		break;
	default:
		dev_warn(afe->dev, "%s(), id %d not support\n",
			 __func__, dai->id);
		return -EINVAL;
	}

	etdm_data = afe_priv->dai_priv[dai->id];

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_DSPA;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_DSPB;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		etdm_data->bck_inv = false;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		etdm_data->bck_inv = true;
		break;
	default:
		return -EINVAL;
	}

	/* ASoC passes the CPU-relative role: TE43 provides both clocks. */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_etdm_ops = {
	.startup = mtk_dai_etdm_startup,
	.shutdown = mtk_dai_etdm_shutdown,
	.hw_params = mtk_dai_etdm_hw_params,
	.trigger = mtk_dai_etdm_trigger,
	.set_fmt = mtk_dai_etdm_set_fmt,
	.set_bclk_ratio = mtk_dai_etdm_set_bclk_ratio,
};

/* dai driver */
#define MTK_ETDM_RATES (SNDRV_PCM_RATE_8000_48000 |\
			SNDRV_PCM_RATE_88200 |\
			SNDRV_PCM_RATE_96000 |\
			SNDRV_PCM_RATE_176400 |\
			SNDRV_PCM_RATE_192000)

#define MTK_ETDM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_etdm_driver[] = {
	{
		.name = "ETDM",
		.id = MT7987_DAI_ETDM,
		.capture = {
			.stream_name = "ETDM Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.playback = {
			.stream_name = "ETDM Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
};

int mt7987_dai_etdm_register(struct mtk_base_afe *afe)
{
	struct mt7987_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *data;
	struct mtk_base_afe_dai *dai;

	data = devm_kzalloc(afe->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	mutex_init(&data->users_lock);
	spin_lock_init(&data->trigger_lock);
	afe_priv->dai_priv[MT7987_DAI_ETDM] = data;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_etdm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_etdm_driver);

	dai->dapm_widgets = mtk_dai_etdm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_etdm_widgets);
	dai->dapm_routes = mtk_dai_etdm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_etdm_routes);

	return 0;
}
