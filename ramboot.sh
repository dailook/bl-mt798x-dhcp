#!/bin/sh

TOOLCHAIN=aarch64-linux-gnu-

RAMBOOT_DIR="${RAMBOOT_DIR:-ramboot_cfg}"
OUTPUT_DIR="${OUTPUT_DIR:-output_ramboot}"

VERSION=${VERSION:-2025}

if [ -z "$ATF_DIR" ]; then
    if [ "$VERSION" = "2025" ]; then
        ATF_DIR=atf-20250711
    elif [ "$VERSION" = "2026" ]; then
        ATF_DIR=atf-20260123
    else
        echo "Error: Unsupported VERSION. Please specify VERSION=2024/2025/2026 or set ATF_DIR."
        exit 1
    fi
fi

if [ ! -d "$RAMBOOT_DIR" ]; then
    echo "Error: RAMBOOT_DIR '$RAMBOOT_DIR' not found."
    exit 1
fi

if [ ! -d "$ATF_DIR" ]; then
    echo "Error: ATF_DIR '$ATF_DIR' not found."
    exit 1
fi

if [ "$CLEAN" = "1" ]; then
    if [ -d "$ATF_DIR/build" ]; then
        rm -rf "$ATF_DIR/build"
    else
        echo "$ATF_DIR/build does not exist."
        exit 1
    fi
    echo "Cleaned $ATF_DIR/build"
    exit 0
fi

command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1
[ "$?" != "0" ] && { echo "${TOOLCHAIN}gcc not found!"; exit 1; }
export CROSS_COMPILE="$TOOLCHAIN"

if [ -e "$ATF_DIR/makefile" ]; then
    ATF_MKFILE="makefile"
else
    ATF_MKFILE="Makefile"
fi

mkdir -p "$OUTPUT_DIR"
mkdir -p "$ATF_DIR/build"

CONFIG_LIST=$(ls "$RAMBOOT_DIR"/*.config 2>/dev/null)
if [ -z "$CONFIG_LIST" ]; then
    echo "Error: no .config files found in $RAMBOOT_DIR"
    exit 1
fi

for cfg_file in $CONFIG_LIST; do
    cfg_name=$(basename "$cfg_file")
    cfg_base=${cfg_name%.config}
    soc=$(echo "$cfg_base" | sed -e 's/^ramboot-//' -e 's/-.*$//')
    echo "Building RAMBOOT: $cfg_name (soc=$soc)"
    rm -rf "$ATF_DIR/build"
    mkdir -p "$ATF_DIR/build"
    cp -f "$cfg_file" "$ATF_DIR/build/.config"
    echo "Starting build for $cfg_name ..."
    echo "----------------------------------------"
    make -C "$ATF_DIR" -f "$ATF_MKFILE" CROSS_COMPILE="$TOOLCHAIN" -j $(nproc)

    if [ -f "$ATF_DIR/build/${soc}/release/bl2.bin" ]; then
        out_name="${cfg_base}.bl2.bin"
        cp -f "$ATF_DIR/build/${soc}/release/bl2.bin" "$OUTPUT_DIR/$out_name"
        echo "$out_name build done"
        echo "----------------------------------------"
    else
        echo "bl2 build fail for $cfg_name!"
    echo "----------------------------------------"
        exit 1
    fi

done
