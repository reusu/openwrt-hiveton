# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=te43-modem-audio
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define KernelPackage/sound-soc-te43-modem
  SUBMENU:=Sound Support
  TITLE:=TE43 modem PCM sound card
  DEPENDS:=@TARGET_mediatek_filogic +kmod-sound-soc-mt7986
  FILES:=$(PKG_BUILD_DIR)/snd-soc-te43-afe.ko \
    $(PKG_BUILD_DIR)/snd-soc-te43-modem.ko
  AUTOLOAD:=$(call AutoProbe,snd-soc-te43-afe snd-soc-te43-modem)
endef

define KernelPackage/sound-soc-te43-modem/description
  TE43 four-wire modem audio link, using the RM500Q short-frame profile:
  MT7987 clock provider, 8 kHz mono S16_LE, 2.048 MHz bit clock.
  Includes a standalone MT7987 AFE driver; no kernel source patch is needed.
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,sound-soc-te43-modem))
