#!/bin/sh
# apply.sh — inject Hiveton H5000M / H5S TE-42 + EdgePi E87N support into an
# OpenWrt source tree.
#
# Goal: stay independent of line numbers and surrounding context so the same
# overlay applies across many OpenWrt tags.
#   - DTS         : brand-new files, just copied in.
#   - filogic.mk  : the 3 `define Device` blocks are appended to the end of the
#                   file (location is irrelevant for image recipes).
#   - case files  : a standalone `case` arm is injected right after the target
#                   function's first `case ... in`. This only depends on the
#                   stable function names (platform_do_upgrade(), ...), never on
#                   neighbouring device names or line numbers.
#
# It also drops the upstream netgear_eax17 device: its custom NETGEAR FIT recipe
# has a broken .its in the 25.12.x images build, which aborts the (all-profiles)
# build. Removed by exact def/TARGET_DEVICES name match, so it is line-agnostic.
#
# Usage: ./apply.sh <openwrt-source-root>
# Idempotent: if already applied (filogic.mk already has hiveton_h5000m) it is a
# no-op.
set -eu

SRC="${1:-.}"
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
MTK="$SRC/target/linux/mediatek"
FILO="$MTK/filogic/base-files"

die() { echo "apply.sh: $*" >&2; exit 1; }
[ -d "$MTK/dts" ] || die "'$SRC' does not look like an OpenWrt tree (missing $MTK/dts)"

if grep -q 'Device/hiveton_h5000m' "$MTK/image/filogic.mk" 2>/dev/null; then
	echo "apply.sh: target tree already carries these devices, nothing to do."
	exit 0
fi

# Inject the contents of <armfile> right after the first `case ... in` line that
# follows <marker> (a function name). An empty marker injects after the very
# first `case ... in` in the file.
inject_after_case() {
	_file="$1"; _marker="$2"; _arm="$3"
	awk -v marker="$_marker" -v armfile="$_arm" '
		BEGIN { while ((getline l < armfile) > 0) buf = buf l ORS }
		{ print }
		(marker == "" || index($0, marker)) { armed = 1 }
		armed && !done && /^[ \t]*case .* in[ \t]*$/ { printf "%s", buf; done = 1 }
		END { if (!done) { print "apply.sh: injection point not found: [" marker "] in " FILENAME > "/dev/stderr"; exit 3 } }
	' "$_file" > "$_file.new"
	mv "$_file.new" "$_file"
	echo "  + injected case arm -> $_file  (anchor: ${_marker:-<first case>})"
}

# Delete `define Device/<dev> ... endef` and its `TARGET_DEVICES += <dev>` line.
remove_device() {
	_file="$1"; _dev="$2"
	if awk -v dev="$_dev" '
		$0 == "define Device/" dev { skip = 1 }
		skip && $0 == "TARGET_DEVICES += " dev { skip = 0; removed = 1; next }
		skip { next }
		{ print }
		END { exit (removed ? 0 : 5) }
	' "$_file" > "$_file.new"; then
		mv "$_file.new" "$_file"
		echo "  - removed Device/$_dev"
	else
		rm -f "$_file.new"
		echo "  = Device/$_dev not present, skipped"
	fi
}

echo "==> 1/4 copy DTS"
cp -v "$HERE"/files/dts/*.dts "$MTK/dts/"

echo "==> 2/4 append device recipes to filogic.mk"
cat "$HERE/files/filogic-devices.mk" >> "$MTK/image/filogic.mk"
echo "  + appended edgepi_e87n / hiveton_h5000m / hiveton_h5s-te42"

echo "==> 3/4 remove upstream netgear_eax17 (broken FIT .its in 25.12.x)"
remove_device "$MTK/image/filogic.mk" 'netgear_eax17'

echo "==> 4/4 inject case arms"
inject_after_case "$FILO/etc/board.d/02_network"                  'mediatek_setup_interfaces()' "$HERE/files/arms/network"
inject_after_case "$FILO/lib/upgrade/platform.sh"                 'platform_do_upgrade()'       "$HERE/files/arms/do_upgrade"
inject_after_case "$FILO/lib/upgrade/platform.sh"                 'platform_check_image()'      "$HERE/files/arms/check_image"
inject_after_case "$FILO/lib/upgrade/platform.sh"                 'platform_copy_config()'      "$HERE/files/arms/copy_config"
inject_after_case "$FILO/etc/hotplug.d/ieee80211/11_fix_wifi_mac" ''                            "$HERE/files/arms/wifimac"

echo "==> done. devices: edgepi,e87n / hiveton,h5000m / hiveton,h5s-te42"
