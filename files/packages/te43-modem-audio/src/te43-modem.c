// SPDX-License-Identifier: GPL-2.0-only
/*
 * TE43 digital modem audio link.
 *
 * The board uses the RM500Q-GL short-frame PCM profile. Modem control remains
 * with its AT interface and CH585M firmware; this driver carries PCM only.
 * The passive codec component describes the digital endpoint's constraints.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define TE43_PCM_RATE		8000
#define TE43_PCM_BCLK_RATIO	256
#define TE43_NUM_LINKS		3

struct te43_modem {
	struct snd_soc_card card;
	struct snd_soc_dai_link links[TE43_NUM_LINKS];
	struct snd_soc_dai_link_component cpus[TE43_NUM_LINKS];
	struct snd_soc_dai_link_component platforms[TE43_NUM_LINKS];
	struct snd_soc_dai_link_component dummy;
	struct snd_soc_dai_link_component modem;
};

static const struct snd_soc_dapm_widget te43_modem_widgets[] = {
	SND_SOC_DAPM_INPUT("Modem RX"),
	SND_SOC_DAPM_OUTPUT("Modem TX"),
};

static const struct snd_soc_dapm_route te43_modem_routes[] = {
	{ "Modem TX", NULL, "Modem PCM Playback" },
	{ "Modem PCM Capture", NULL, "Modem RX" },
};

static struct snd_soc_dai_driver te43_modem_dai = {
	.name = "te43-modem-pcm",
	.playback = {
		.stream_name = "Modem PCM Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Modem PCM Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static const struct snd_soc_component_driver te43_modem_component = {
	.name = "te43-modem-codec",
	.dapm_widgets = te43_modem_widgets,
	.num_dapm_widgets = ARRAY_SIZE(te43_modem_widgets),
	.dapm_routes = te43_modem_routes,
	.num_dapm_routes = ARRAY_SIZE(te43_modem_routes),
	.endianness = 1,
};

static int te43_pcm_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	ret = snd_pcm_hw_constraint_single(runtime, SNDRV_PCM_HW_PARAM_RATE,
					  TE43_PCM_RATE);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_single(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 1);
	if (ret)
		return ret;

	return snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT,
					   SNDRV_PCM_FMTBIT_S16_LE);
}

static const struct snd_soc_ops te43_pcm_ops = {
	.startup = te43_pcm_startup,
};

static int te43_pcm_fixup(struct snd_soc_pcm_runtime *rtd,
			  struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *format = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	rate->min = rate->max = TE43_PCM_RATE;
	channels->min = channels->max = 1;
	snd_mask_none(format);
	snd_mask_set(format, SNDRV_PCM_FORMAT_S16_LE);
	return 0;
}

static int te43_pcm_init(struct snd_soc_pcm_runtime *rtd)
{
	/* 8000 frames/s * 256 BCLK/frame = 2.048 MHz, 16 serial slots. */
	return snd_soc_dai_set_bclk_ratio(snd_soc_rtd_to_cpu(rtd, 0),
					TE43_PCM_BCLK_RATIO);
}

static void te43_put_node(void *node)
{
	of_node_put(node);
}

static int te43_modem_probe(struct platform_device *pdev)
{
	static const char * const cpu_names[] = { "DL1", "UL1", "ETDM" };
	static const char * const link_names[] = {
		"Modem Playback", "Modem Capture", "Modem PCM",
	};
	struct device *dev = &pdev->dev;
	struct device_node *afe;
	struct te43_modem *priv;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	afe = of_parse_phandle(dev->of_node, "mediatek,audio-platform", 0);
	if (!afe)
		return dev_err_probe(dev, -EINVAL, "Missing audio platform\n");
	ret = devm_add_action_or_reset(dev, te43_put_node, afe);
	if (ret)
		return ret;
	if (!of_device_is_compatible(afe, "mediatek,mt7987-afe"))
		return dev_err_probe(dev, -EINVAL, "TE43 requires the MT7987 AFE\n");

	ret = devm_snd_soc_register_component(dev, &te43_modem_component,
					    &te43_modem_dai, 1);
	if (ret)
		return ret;

	for (i = 0; i < TE43_NUM_LINKS; i++) {
		struct snd_soc_dai_link *link = &priv->links[i];

		priv->cpus[i].of_node = afe;
		priv->cpus[i].dai_name = cpu_names[i];
		priv->platforms[i].of_node = afe;
		link->name = link_names[i];
		link->stream_name = link_names[i];
		link->cpus = &priv->cpus[i];
		link->num_cpus = 1;
		link->platforms = &priv->platforms[i];
		link->num_platforms = 1;
		link->id = i;
	}

	/* ASoC supplies dummy codecs for the two memory-side front ends. */
	priv->links[0].codecs = &priv->dummy;
	priv->links[0].dynamic = 1;
	priv->links[0].dpcm_playback = 1;
	priv->links[0].ops = &te43_pcm_ops;
	priv->links[0].trigger[SNDRV_PCM_STREAM_PLAYBACK] = SND_SOC_DPCM_TRIGGER_POST;
	priv->links[1].codecs = &priv->dummy;
	priv->links[1].dynamic = 1;
	priv->links[1].dpcm_capture = 1;
	priv->links[1].ops = &te43_pcm_ops;
	priv->links[1].trigger[SNDRV_PCM_STREAM_CAPTURE] = SND_SOC_DPCM_TRIGGER_POST;

	priv->modem.of_node = dev->of_node;
	priv->modem.dai_name = te43_modem_dai.name;
	priv->links[2].codecs = &priv->modem;
	priv->links[2].num_codecs = 1;
	priv->links[2].no_pcm = 1;
	priv->links[2].dpcm_playback = 1;
	priv->links[2].dpcm_capture = 1;
	priv->links[2].init = te43_pcm_init;
	priv->links[2].be_hw_params_fixup = te43_pcm_fixup;
	/* Rising-edge transmit / falling-edge sample, one-clock sync pulse. */
	priv->links[2].dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF |
		SND_SOC_DAIFMT_CBC_CFC | SND_SOC_DAIFMT_CONT;

	priv->card.dev = dev;
	priv->card.owner = THIS_MODULE;
	priv->card.name = "TE43Modem";
	priv->card.dai_link = priv->links;
	priv->card.num_links = TE43_NUM_LINKS;

	ret = devm_snd_soc_register_card(dev, &priv->card);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register modem PCM card\n");
	return 0;
}

static const struct of_device_id te43_modem_match[] = {
	{ .compatible = "hiveton,te43-modem-audio" },
	{ }
};
MODULE_DEVICE_TABLE(of, te43_modem_match);

static struct platform_driver te43_modem_driver = {
	.probe = te43_modem_probe,
	.driver = {
		.name = "te43-modem-audio",
		.of_match_table = te43_modem_match,
	},
};
module_platform_driver(te43_modem_driver);

MODULE_DESCRIPTION("TE43 RM500Q-profile modem PCM sound card");
MODULE_LICENSE("GPL");
