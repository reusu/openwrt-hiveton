
define Device/edgepi_e87n
  DEVICE_VENDOR := EdgePi
  DEVICE_MODEL := E87N
  DEVICE_DTS := mt7987a-edgepi-e87n
  DEVICE_DTS_DIR := ../dts
  DEVICE_PACKAGES := kmod-hwmon-pwmfan kmod-spi-dev kmod-usb3 kmod-nvme mt7987-2p5g-phy-firmware  \
        e2fsprogs f2fsck mkf2fs
  KERNEL_LOADADDR := 0x40080000
  IMAGE/sysupgrade.bin := sysupgrade-tar | append-metadata
endef
TARGET_DEVICES += edgepi_e87n

define Device/hiveton_h5000m
  DEVICE_VENDOR := Hiveton
  DEVICE_MODEL := H5000M
  DEVICE_DTS := mt7987a-hiveton-h5000m
  DEVICE_DTS_DIR := ../dts
  DEVICE_PACKAGES := kmod-hwmon-pwmfan kmod-usb3 mt7987-2p5g-phy-firmware \
    kmod-mt7996e kmod-mt7992-23-firmware f2fsck mkf2fs
  KERNEL_LOADADDR := 0x40000000
  IMAGE/sysupgrade.bin := sysupgrade-tar | append-metadata
endef
TARGET_DEVICES += hiveton_h5000m

define Device/hiveton_h5s-te42
  DEVICE_VENDOR := Hiveton
  DEVICE_MODEL := H5S TE-42
  DEVICE_DTS := mt7987a-hiveton-h5s-te42
  DEVICE_DTS_DIR := ../dts
  DEVICE_PACKAGES := kmod-hwmon-pwmfan kmod-i2c-core kmod-spi-dev kmod-usb3 mt7987-2p5g-phy-firmware \
        kmod-mt7996e kmod-mt7992-23-firmware e2fsprogs f2fsck mkf2fs
  KERNEL_LOADADDR := 0x40000000
  IMAGE/sysupgrade.bin := sysupgrade-tar | append-metadata
endef
TARGET_DEVICES += hiveton_h5s-te42
