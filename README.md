# openwrt-hiveton

OpenWrt board support overlay for **MediaTek MT7987A** devices, built on top of
stock OpenWrt releases via GitHub Actions.

Devices added:

| Device            | compatible          | Notes                                   |
| ----------------- | ------------------- | --------------------------------------- |
| Hiveton H5000M    | `hiveton,h5000m`    | MT7987A + MT7992 Wi-Fi, eMMC            |
| Hiveton H5S TE-42 | `hiveton,h5s-te42`  | MT7987A + MT7992 Wi-Fi, eMMC, SPI LCD, I2C touch, 5G modem on PCIe1 |
| EdgePi E87N       | `edgepi,e87n`       | MT7987A, eMMC, NVMe, factory-EEPROM MAC |

## How it works

This repo does **not** fork the whole OpenWrt tree. It only carries the delta as
a context-free overlay, so the same overlay applies cleanly across OpenWrt tags:

- `files/dts/*.dts` — the three device trees (new files, copied in).
- `files/filogic-devices.mk` — the three `define Device` recipes, **appended** to
  `target/linux/mediatek/image/filogic.mk` (position does not matter).
- `files/arms/*` — standalone `case` arms injected right after the target
  function's `case ... in` (`platform_do_upgrade()`, `platform_check_image()`,
  `platform_copy_config()`, `mediatek_setup_interfaces()`, and the Wi-Fi MAC
  hotplug case).

`apply.sh` performs the injection. It anchors on **stable function names** rather
than line numbers or neighbouring device names, and is idempotent.

## Build with GitHub Actions

Actions → **Build MT7987A (Hiveton / EdgePi) + ImageBuilder** → *Run workflow*:

- `tag` — OpenWrt release to build against, e.g. `25.12.5` (no leading `v`).
- `make_release` — also publish the images as a GitHub Release.

The workflow downloads stock OpenWrt, seeds `.config` from the official
`config.buildinfo`, disables every stock device, applies this overlay, enables
only the three devices plus the ImageBuilder, then builds. Artifacts: the
sysupgrade images and the ImageBuilder tarball.

> Public repos get unlimited free Actions minutes; the main constraint is runner
> disk, which the workflow frees up before building.

## Build locally

```sh
wget https://github.com/openwrt/openwrt/archive/refs/tags/v25.12.5.tar.gz
tar xzf v25.12.5.tar.gz
./apply.sh openwrt-25.12.5      # inject the overlay
cd openwrt-25.12.5
./scripts/feeds update -a && ./scripts/feeds install -a
# select the three devices under Target Profile, then:
make defconfig
make -j"$(nproc)"
```

## Layout

```
apply.sh                    # overlay injector (run against an OpenWrt tree)
files/dts/                  # the three .dts files
files/filogic-devices.mk    # image recipes appended to filogic.mk
files/arms/                 # case arms injected into the base-files scripts
.github/workflows/build.yml # CI: fetch OpenWrt, apply overlay, build
```
