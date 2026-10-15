# openwrt-hiveton

OpenWrt board support overlay for **MediaTek MT7987A** devices, built on top of
stock OpenWrt releases via GitHub Actions.

Devices added:

| Device            | compatible          | Notes                                   |
| ----------------- | ------------------- | --------------------------------------- |
| Hiveton H5000M    | `hiveton,h5000m`    | MT7987A + MT7992 Wi-Fi, eMMC            |
| Hiveton H5S TE-42 | `hiveton,h5s-te42`  | MT7987A + MT7992 Wi-Fi, eMMC, SPI LCD, I2C touch, 5G modem on PCIe1 |
| Hiveton H5S TE-43 | `hiveton,h5s-te43`  | H5S core, USB/PCIe modem, CH585M over SPI/I2C/USB, PCM controller, PWM/tach fan |
| EdgePi E87N       | `edgepi,e87n`       | MT7987A, eMMC, NVMe, factory-EEPROM MAC |

## How it works

This repo does **not** fork the whole OpenWrt tree. It only carries the delta as
a context-free overlay, so the same overlay applies cleanly across OpenWrt tags:

- `files/dts/*.dts` — the four device trees (new files, copied in).
- `files/filogic-devices.mk` — the four `define Device` recipes, **appended** to
  `target/linux/mediatek/image/filogic.mk` (position does not matter).
- `files/arms/*` — standalone `case` arms injected right after the target
  function's `case ... in` (`platform_do_upgrade()`, `platform_check_image()`,
  `platform_copy_config()`, `mediatek_setup_interfaces()`, and the Wi-Fi MAC
  hotplug case).
- `files/packages/te43-modem-audio/` — standalone MT7987 AFE and TE-43 ALSA
  sound-card modules, with a device-tree binding. No kernel source patch.

`apply.sh` performs the injection. It anchors on **stable function names** rather
than line numbers or neighbouring device names, and is idempotent. Reapplying
refreshes source files and the TE-43 recipe without duplicating case arms.
Trees with the earlier three-device overlay are extended with TE-43 support.
The audio modules currently target the Linux 6.12 series.

## Build with GitHub Actions

Actions → **Build MT7987A (Hiveton / EdgePi) + ImageBuilder** → *Run workflow*:

- `tag` — OpenWrt release to build against, e.g. `25.12.5` (no leading `v`).
- `make_release` — also publish the images as a GitHub Release.

The workflow downloads stock OpenWrt, seeds `.config` from the official
`config.buildinfo`, disables every stock device, applies this overlay, enables
only the four devices plus the ImageBuilder, then builds. Artifacts: the
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
# select the desired devices under Target Profile, then:
make defconfig
make -j"$(nproc)"
```

## Layout

```
apply.sh                    # overlay injector (run against an OpenWrt tree)
files/dts/                  # the four .dts files
files/filogic-devices.mk    # image recipes appended to filogic.mk
files/arms/                 # case arms injected into the base-files scripts
files/packages/             # board-specific OpenWrt packages
.github/workflows/build.yml # CI: fetch OpenWrt, apply overlay, build
```

## TE-43 hardware mapping

The TE-43 DTS uses the validated TE-42 eMMC, MT7992 calibration and Ethernet
configuration. Carrier connections were checked against the live
`TE-43_立式MIFI` project and its schematic PDFs exported on 2026-10-15. GPIO
numbers follow the MT7987A datasheet supplied with the H5S core documentation.

| Connection | Configuration in OpenWrt |
| --- | --- |
| H5S core eMMC / on-module MT7992 | 8-bit eMMC at 48 MHz; PCIe0 Wi-Fi; existing `factory` calibration layout |
| RJ3 / RJ4 | Internal 2.5GbE PHY on `eth1` (LAN); RTL8221B at MDIO address 1 on `eth0` (WAN) |
| RTL8221B EEE | Preserve TE-42's `eee-broken-100tx` and `eee-broken-1000t` on `eth0` / RJ4; internal PHY unchanged |
| MT7987 USB3 → CH634M | Hub ports 1/2: Type-A; port 3: M.2 modem over USB3; port 4: CH585M over USB2 |
| PCIe1 → M.2 modem | Enabled together with USB; one lane; GPIO36/37/38 provide PERST/CLKREQ/WAKE |
| I2C0 → CH585M PA4/PA5 | GPIO43/44, 100 kHz, `/dev/i2c-0`; MCU address and protocol depend on its firmware |
| UART0 → debug pads and CH585M | Console retained at 115200 8N1; MCU communication uses SPI, USB or I2C |
| Fan CN2 | PWM1 on GPIO7 at 20 kHz; tachometer on GPIO6; assumes two pulses per revolution |
| APP_LED1 / APP_LED2 | GPIO3 drives RJ3's LED **active high**; GPIO4 drives blue LED5 **active low** |
| Reset / WPS | GPIO1 is the reset input; GPIO0/WPS is unconnected and omitted |
| SPI2 → CH585M SPI0 | Enabled as host, GPIO25..28, `/dev/spidev2.0`; initial 1 MHz limit |
| PCM → M.2 modem | AFE and `TE43Modem` sound card; GPIO8..11; RM500Q short-frame profile: 8 kHz mono S16_LE, 2.048 MHz BCLK, MT7987 supplies clocks |
| SPI0 / SPI1 | Disabled as SPI controllers; their pins belong to the core's eight-bit eMMC |

The CH585M manages the LCD, touch panel, NFC, charger/BMS and both AW9523
expanders. These peripherals are **not** direct Linux I2C devices. In
particular, the communications-board AW9523 controls `DC5GEN` (modem 3.8 V),
`DCUSEN` (USB 5 V), `FANEN` (fan power), `LYEN` (core power), and modem
power/reset. The MCU firmware must establish these supplies and the modem's
startup sequence. The DTS's fixed USB regulator describes the enabled rail;
it cannot switch the MCU-controlled supply itself.

SPI2 SCK/MOSI/MISO reach CH585M SPI0, while native CS reaches the MCU-owned
AW9523 P0.2. The DTS selects MT7987 as host and exposes the link through
spidev, using the same placeholder binding convention as TE-42. Configure
AW9523 P0.2 as an input; CH585M's hardware SCS pin is unconnected, so MCU
firmware must implement transaction framing. SPI mode can be selected by the
user-space application; the initial maximum clock is 1 MHz pending MCU
validation. The LCD is driven by CH585M SPI1 and has no Linux display node.

PCM uses GPIO8 (TX to module pin 22), GPIO9 (RX from pin 24), GPIO10 (clock,
pin 20), and GPIO11 (sync, pin 28), through the U11 level converter. GPIO12
(MCLK) is only a test point. OpenWrt v25.12.5's MT7987 pinctrl driver requires
function `audio` and group `pcm`; the base DTS's `pcm` / `pcm0_1` combination
does not match that driver. The image includes `kmod-sound-soc-te43-modem`,
which depends on the AFE driver, and `alsa-utils` for audio bring-up.

The chosen board design baseline is the RM500Q-GL default PCM profile:
the modem consumes clocks, short frames, 16-bit linear samples, 8 kHz frame
sync and a 2.048 MHz bit clock. The machine driver exposes mono S16_LE at
8 kHz and selects DSP_A with inverted BCLK for rising-edge transmit and
falling-edge sampling. Its 256 BCLK/frame setting produces sixteen 16-bit
serial slots; the audio sample is carried in the first slot. The extra slots
pad the frame and are not sixteen ALSA audio channels. These clock-generation
parameters are supported by the MT7987A datasheet v1.2, Table 9-71.

Audio support is delivered entirely as external modules:
`snd-soc-te43-afe.ko` provides the AFE/eTDM implementation and
`snd-soc-te43-modem.ko` provides the board's `TE43Modem` sound card. They are
built by OpenWrt against its selected kernel; no Linux driver source is
patched. The AFE sources are adapted from Linux v6.12's MT7986 driver with
the original copyright notices retained. The package depends on
`kmod-sound-soc-mt7986` to reuse its MediaTek common ASoC helper module, but
the DTS uses a dedicated `hiveton,te43-mt7987-afe` compatible so the stock
MT7986 AFE driver cannot bind to this controller.

The standalone eTDM driver implements the BCLK ratio, applies the bit-clock
polarity, and keeps shared clocks running until both audio directions stop.
It uses the MT7987 OUT5 base `0x15c0` from Table 9-73 rather than the MT7986
base `0x1570`. The other AFE register definitions still follow the original
MT7986 driver and require hardware verification.

Module references used for this design:

| Reference | Relevant PCM information |
| --- | --- |
| [RM500Q-GL Hardware Design V1.1, sections 4.4 / pp. 47–48](https://the-wireless-haven-media.s3.us-east-2.amazonaws.com/wp-content/uploads/2021/07/13111322/Quectel_RM500Q-GL_Hardware_Design_V1.1.pdf) | The selected default above; short frames support module master/slave. Mode and clock configuration use `AT+QDAI`. |
| [RM520N Series Hardware Design V1.1, pp. 20, 55–57](https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/138/rm520_5F00_modem_5F00_datasheet.pdf) | RM520N-EU has the same short-frame timing; RM520N-GL lists pins 20/22/28 as reserved and pin 24 as VDDIO_1V8. This document does not cover CN. |
| Supplied MT5700M-CN Hardware Design V1.0, section 4.7 / p. 24 | Same four pin positions and 1.8 V levels; clock/sync are shown as module outputs. Frame format/rate are unspecified, and PCM is marked as under development in that revision. |

Using the RM500Q profile for interchangeable modules is the selected design
assumption, not confirmation that every module/firmware supports it. Configure
the fitted modem to consume clocks with the same timing before opening the
PCM stream; the driver does not issue AT commands or control modem power.

After CI produces a firmware and the sound card registers on the board,
the AFE mixer paths must be enabled before audio flows. Playback uses
`O124 I032_Switch`; capture uses `O018 I150_Switch`. For example:

```sh
amixer -c TE43Modem sset 'O124 I032_Switch' on
amixer -c TE43Modem sset 'O018 I150_Switch' on
aplay -D hw:TE43Modem,0 -t raw -f S16_LE -r 8000 -c 1 audio.pcm
arecord -D hw:TE43Modem,1 -t raw -f S16_LE -r 8000 -c 1 -d 5 capture.pcm
```

These are bring-up commands, not a claim of validated audio. Check the actual
card/control list and the BCLK/SYNC/data waveforms on the first hardware run.

USB devices enumerate dynamically. The image includes common USB modem, CDC ACM and PCIe MHI drivers;
the fitted module's firmware, USB/PCI IDs and operating mode still determine
which driver binds. No modem connection/APN is configured automatically.

PCIe1 is enabled for interchangeable endpoint-capable modules such as RM520N.
The supplied MT5700M-CN guide (V1.0, section 4.9) documents RC mode only, so
its PCIe endpoint operation is not established by this DTS. Its power-control
sequence and some M.2 control-pin functions also differ from RM520N; those
differences belong in the MCU firmware and module compatibility checks.

The initial TE-43 draft passed `dtc` against OpenWrt v25.12.5 with the same
warnings as TE-42. Subsequent SPI2/PCM and standalone-driver changes receive
static checks only; compilation is deferred to CI. The current audio modules
have not been compiled or run on TE-43 hardware.
