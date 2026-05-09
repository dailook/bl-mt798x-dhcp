#!/bin/sh

VERSION=${VERSION:-2025}
SOC="${SOC:-mt7621}"
BOARD="${BOARD:-nand_ax_rfb}"
fixedparts=${FIXED_MTDPARTS:-0}
multilayout=${MULTI_LAYOUT:-0}

# URL of the prebuilt OpenWrt toolchain archive (can be overridden by env)
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://downloads.openwrt.org/releases/25.12.0/targets/ramips/mt7621/openwrt-toolchain-25.12.0-ramips-mt7621_gcc-14.3.0_musl.Linux-x86_64.tar.zst}"

TOOLCHAIN_BIN=$(cd ./openwrt*/toolchain-mipsel*/bin 2>/dev/null; pwd)
if [ -z "$TOOLCHAIN_BIN" ]; then
	echo "Error:  Toolchain not found!  Please check openwrt*/toolchain-mipsel*/ exists."
	exit 1
fi

TOOLCHAIN="${TOOLCHAIN_BIN}/mipsel-openwrt-linux-"
Staging="${TOOLCHAIN_BIN%/bin}"
Staging="${Staging%/toolchain-*}"

if [ "$VERSION" = "2025" ]; then
    UBOOT_DIR=uboot-mtk-20250711
elif [ "$VERSION" = "2026" ]; then
    UBOOT_DIR=uboot-mtk-20260123
else
    echo "Error: Unsupported VERSION. Please specify VERSION=2025/2026."
    exit 1
fi

if [ -z "$SOC" ] || [ -z "$BOARD" ]; then
	echo "Usage: SOC=mt7621 BOARD=<board name> VERSION=2025 $0"
	echo "eg: SOC=mt7621 BOARD=nmbm_rfb VERSION=2025 $0"
	exit 1
fi

echo "======================================================================"
echo "Checking environment..."
echo "======================================================================"

# Check if Python is installed on the system
echo "Trying python3..."
command -v python3
[ "$?" != "0" ] && { echo "Error: Python3 is not installed on this system."; exit 0; }

echo "Trying cross compiler..."
command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1
if [ "$?" != "0" ]; then
	echo "Cross-compiler ${TOOLCHAIN}gcc not found."
	echo "Please get toolchian from $TOOLCHAIN_URL."
	# offer to download the toolchain archive if available
	read -p "Do you want to download the toolchain now? [Y/n] " dlcc
	dlcc=${dlcc:-Y}
	case "$dlcc" in
		[Yy]* )
			if command -v wget >/dev/null 2>&1; then
				echo "Downloading and extracting toolchain..."
				wget -O - "$TOOLCHAIN_URL" | tar --zstd -xf - || { echo "Download or extraction failed"; exit 1; }
			elif command -v curl >/dev/null 2>&1; then
				echo "Downloading and extracting toolchain with curl..."
				curl -L "$TOOLCHAIN_URL" | tar --zstd -xf - || { echo "Download or extraction failed"; exit 1; }
			else
				echo "Neither wget nor curl is available. Please install one or download the toolchain manually: $TOOLCHAIN_URL"
				exit 1
			fi
			# try to detect TOOLCHAIN_BIN and compiler again
			TOOLCHAIN_BIN=$(cd ./openwrt*/toolchain-mipsel*/bin 2>/dev/null; pwd)
			if [ -n "$TOOLCHAIN_BIN" ]; then
				TOOLCHAIN="${TOOLCHAIN_BIN}/mipsel-openwrt-linux-"
				command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1
				if [ "$?" = "0" ]; then
					echo "Found cross-compiler: ${TOOLCHAIN}gcc"
				else
					echo "Cross-compiler still not found after extraction."
					exit 1
				fi
			else
				echo "Toolchain folder not detected after extraction. Please check the extracted files."
				exit 1
			fi
			;;
		* )
			echo "Cross-compiler required. Exiting."
			exit 1
			;;
	esac
fi

echo "CROSS_COMPILE=${TOOLCHAIN}"
echo "STAGING_DIR=${Staging}"

CONFIGS_DIR_DEFAULT="configs"

UBOOT_CFG="${SOC}_${BOARD}_defconfig"
UBOOT_CFG_PATH="$UBOOT_DIR/$CONFIGS_DIR_DEFAULT/$UBOOT_CFG"

if [ "$multilayout" = "1" ]; then
	UBOOT_CFG="${SOC}_${BOARD}_multi_layout_defconfig"
fi

echo "======================================================================"
echo "Configuration:"
echo "======================================================================"

echo "VERSION: $VERSION"
echo "TARGET:  ${SOC}_${BOARD}"
echo "U-Boot Dir: $UBOOT_DIR"
echo "U-Boot CFG: $UBOOT_CFG_PATH"
echo "Features: fixed-mtdparts: $fixedparts, multi-layout: $multilayout"

if [ ! -d "$UBOOT_DIR" ]; then
	echo "Error: U-Boot directory '$UBOOT_DIR' not found!"
	exit 1
fi

if [ ! -f "$UBOOT_DIR/configs/$UBOOT_CFG" ]; then
	echo "Error: U-Boot config '$UBOOT_CFG' not found in $UBOOT_DIR/configs/"
	exit 1
fi

echo "======================================================================"
echo "Build u-boot..."
echo "======================================================================"

rm -f "$UBOOT_DIR/u-boot.bin"
cp -f "$UBOOT_DIR/configs/$UBOOT_CFG" "$UBOOT_DIR/.config"

if [ "$fixedparts" = "1" ]; then
	echo "Build u-boot with fixed-mtdparts!"
	echo "CONFIG_MEDIATEK_UBI_FIXED_MTDPARTS=y" >> "$UBOOT_DIR/.config"
	echo "CONFIG_MTK_FIXED_MTD_MTDPARTS=y" >> "$UBOOT_DIR/.config"
fi

make -C "$UBOOT_DIR" olddefconfig
make -C "$UBOOT_DIR" clean
make -C "$UBOOT_DIR" CROSS_COMPILE="${TOOLCHAIN}" STAGING_DIR="${Staging}" -j $(nproc) all

if [ -f "$UBOOT_DIR/u-boot.bin" ]; then
	echo "u-boot build done!"
else
	echo "u-boot build fail!"
	exit 1
fi

echo "======================================================================"
echo "Copying output files..."
echo "======================================================================"

mkdir -p "output_mt7621"
if [ -f "$UBOOT_DIR/u-boot.bin" ]; then
	MD5SUM=$(md5sum "$UBOOT_DIR/u-boot.bin" | awk '{print $1}')
	echo "u-boot.bin md5sum: $MD5SUM"
	UBOOTNAME="$SOC-u-boot-$BOARD-${VERSION}_md5-${MD5SUM}.bin"
	cp -f "$UBOOT_DIR/u-boot.bin" "output_mt7621/$UBOOTNAME"
	echo "$SOC-u-boot-$BOARD-${VERSION} build done"
	echo "Output:  output_mt7621/$UBOOTNAME"
else
	echo "$SOC-uboot-$BOARD-${VERSION} build fail!"
	exit 1
fi
