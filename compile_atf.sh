#!/bin/sh

TOOLCHAIN_ARM=arm-linux-gnueabi-
TOOLCHAIN_AARCH64=aarch64-linux-gnu-

ATFCFG_DIR="${ATFCFG_DIR:-mt798x_atf}"
CFG_SUBDIR="${CFG_SUBDIR:-}"
OUTPUT_DIR="${OUTPUT_DIR:-output_bl2}"

VERSION=${VERSION:-2025}

if [ -z "$ATF_DIR" ]; then
    if [ "$VERSION" = "2022" ]; then
        ATF_DIR=atf-20220606-637ba581b
    elif [ "$VERSION" = "2023" ]; then
        ATF_DIR=atf-20231013-0ea67d76a
    elif [ "$VERSION" = "2024" ]; then
        ATF_DIR=atf-20240117-bacca82a8
    elif [ "$VERSION" = "2025" ]; then
        ATF_DIR=atf-20250711
    elif [ "$VERSION" = "2026" ]; then
        ATF_DIR=atf-20260123
    else
        echo "Error: Unsupported VERSION. Please specify VERSION=2025/2026 or set ATF_DIR."
        exit 1
    fi
fi

if [ ! -d "$ATFCFG_DIR" ]; then
    echo "Error: ATFCFG_DIR '$ATFCFG_DIR' not found."
    exit 1
fi

if [ ! -d "$ATF_DIR" ]; then
    echo "Error: ATF_DIR '$ATF_DIR' not found."
    exit 1
fi

if [ -e "$ATF_DIR/makefile" ]; then
    ATF_MKFILE="makefile"
else
    ATF_MKFILE="Makefile"
fi

append_unique_line() {
    line="$1"
    file="$2"
    grep -qxF "$line" "$file" 2>/dev/null || echo "$line" >> "$file"
}

mkdir -p "$OUTPUT_DIR"
mkdir -p "$ATF_DIR/build"

CONFIG_LIST=""

# Always include configs directly under ATFCFG_DIR.
for cfg in "$ATFCFG_DIR"/*.config; do
    [ -f "$cfg" ] && CONFIG_LIST="$CONFIG_LIST $cfg"
done

# Also include configs from selected subdir (default: ram).
if [ -n "$CFG_SUBDIR" ]; then
    if [ ! -d "$ATFCFG_DIR/$CFG_SUBDIR" ]; then
        echo "Error: CFG_SUBDIR '$CFG_SUBDIR' not found in '$ATFCFG_DIR'."
        exit 1
    fi
    for cfg in "$ATFCFG_DIR/$CFG_SUBDIR"/*.config; do
        [ -f "$cfg" ] && CONFIG_LIST="$CONFIG_LIST $cfg"
    done
fi

if [ -z "$CONFIG_LIST" ]; then
    echo "Error: no .config files found in '$ATFCFG_DIR' or '$ATFCFG_DIR/$CFG_SUBDIR'"
    exit 1
fi

SUCCESS_COUNT=0
FAIL_COUNT=0
FAILED_CONFIGS=""

for cfg_file in $CONFIG_LIST; do
    cfg_rel=${cfg_file#"$ATFCFG_DIR"/}
    cfg_name=$(basename "$cfg_file")
    cfg_base=${cfg_name%.config}
    cfg_tag=$(echo "$cfg_rel" | sed -e 's/\.config$//' -e 's#/#_#g')
    feature_tag=""
    # Example filenames:
    #   mt7981-ddr3-bga-ram.config  -> soc=mt7981
    #   atf-mt7986-ddr4-ram.config  -> soc=mt7986
    soc=$(echo "$cfg_base" | sed -e 's/^atf-//' | cut -d'-' -f1)

    if [ -z "$TOOLCHAIN" ]; then
        if [ "$soc" = "mt7629" ]; then
            TOOLCHAIN=$TOOLCHAIN_ARM
        else
            TOOLCHAIN=$TOOLCHAIN_AARCH64
        fi
        echo "Using toolchain $TOOLCHAIN for SOC $soc"
    fi

    command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1
    [ "$?" != "0" ] && { echo "${TOOLCHAIN}gcc not found!"; exit 1; }
    export CROSS_COMPILE="$TOOLCHAIN"

    echo "======================================================================"
    echo "Configration overview:"
    echo "======================================================================"
    echo "SOC: $soc"
    echo "Version: $VERSION"
    echo "Using ATF_DIR: $ATF_DIR"
    echo "Using toolchain: ${TOOLCHAIN}gcc"
    echo "Building BL2: $cfg_rel"

    rm -rf "$ATF_DIR/build"
    mkdir -p "$ATF_DIR/build"
    cp -f "$cfg_file" "$ATF_DIR/build/.config"

    # Extra options only apply to configs under normal/.
    is_normal_cfg=0
    case "$cfg_rel" in
        normal/*) is_normal_cfg=1 ;;
    esac

    if [ "$is_normal_cfg" = "1" ]; then
        variant_upper=$(echo "${VARIANT:-}" | tr '[:lower:]' '[:upper:]')
        if [ "$variant_upper" = "NONMBM" ]; then
            append_unique_line "_NAND_SKIP_BAD=y" "$ATF_DIR/build/.config"
            feature_tag="${feature_tag}-nonmbm"
        elif [ "$variant_upper" = "UBOOTMOD" ]; then
            append_unique_line "_NAND_UBI=y" "$ATF_DIR/build/.config"
            feature_tag="${feature_tag}-ubootmod"
        fi

        if [ -n "${OC7981:-}" ] && [ "$soc" = "mt7981" ]; then
            case "$OC7981" in
                *[!0-9]*|"")
                    echo "Error: OC7981 must be an integer in range 13~18."
                    exit 1
                    ;;
            esac
            if [ "$OC7981" -lt 13 ] || [ "$OC7981" -gt 18 ]; then
                echo "Error: OC7981 out of range (13~18): $OC7981"
                exit 1
            fi
            sed -i '/^MT7981_ARMPLL_FREQ_[0-9][0-9][0-9][0-9]=y$/d' "$ATF_DIR/build/.config"
            append_unique_line "MT7981_ARMPLL_FREQ_${OC7981}00=y" "$ATF_DIR/build/.config"
            feature_tag="${feature_tag}-OC${OC7981}00"
        fi

        if [ -n "${OC7986:-}" ] && [ "$soc" = "mt7986" ]; then
            case "$OC7986" in
                *[!0-9]*|"")
                    echo "Error: OC7986 must be an integer in range 16~25."
                    exit 1
                    ;;
            esac
            if [ "$OC7986" -lt 16 ] || [ "$OC7986" -gt 25 ]; then
                echo "Error: OC7986 out of range (16~25): $OC7986"
                exit 1
            fi
            sed -i '/^MT7986_ARMPLL_FREQ_[0-9][0-9][0-9][0-9]=y$/d' "$ATF_DIR/build/.config"
            append_unique_line "MT7986_ARMPLL_FREQ_${OC7986}00=y" "$ATF_DIR/build/.config"
            feature_tag="${feature_tag}-OC${OC7986}00"
        fi
    fi

    if echo "$feature_tag" | grep -q "OC"; then
        echo "Feature: Overclocking enabled ($feature_tag)"
    fi
    if echo "$feature_tag" | grep -q "nonmbm"; then
        echo "Feature: NAND_SKIP_BAD enabled"
    fi
    if echo "$feature_tag" | grep -q "ubootmod"; then
        echo "Feature: NAND_UBI enabled"
    fi

    echo "======================================================================"
    echo "Build ATF with config: $cfg_name"
    echo "======================================================================"
    build_ok=1
    if [ "$VERSION" = "2025" ] || [ "$VERSION" = "2026" ]; then
        make -C "$ATF_DIR" olddefconfig || build_ok=0
    else
        make -C "$ATF_DIR" defconfig || build_ok=0
    fi
    make -C "$ATF_DIR" -f "$ATF_MKFILE" clean CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN" || build_ok=0
    if [ "$build_ok" = "1" ]; then
        make -C "$ATF_DIR" -f "$ATF_MKFILE" all CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN" -j $(nproc) || build_ok=0
    fi

    echo "======================================================================"

    if [ "$build_ok" = "1" ] && [ -f "$ATF_DIR/build/${soc}/release/bl2.img" ]; then
        src_file="$ATF_DIR/build/${soc}/release/bl2.img"
        bl2_md5=$(md5sum "$src_file" | awk '{print $1}')
        out_name="bl2-${cfg_tag}-${VERSION}${feature_tag}-Yuzhii_md5-${bl2_md5}.img"
        cp -f "$src_file" "$OUTPUT_DIR/$out_name"
        echo "bl2-${cfg_tag}-${VERSION}${feature_tag} build done"
        echo "Output file: output_bl2/$out_name"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    elif [ "$build_ok" = "1" ] && [ -f "$ATF_DIR/build/${soc}/release/bl2.bin" ]; then
        src_file="$ATF_DIR/build/${soc}/release/bl2.bin"
        bl2_md5=$(md5sum "$src_file" | awk '{print $1}')
        out_name="bl2-${cfg_tag}-${VERSION}${feature_tag}-Yuzhii_md5-${bl2_md5}.bin"
        cp -f "$src_file" "$OUTPUT_DIR/$out_name"
        echo "Warning: bl2.img not found, fallback to bl2.bin"
        echo "bl2-${cfg_tag}-${VERSION}${feature_tag} build done (using bl2.bin)"
        echo "Output file: output_bl2/$out_name"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    else
        echo "bl2 build fail for $cfg_rel! (neither bl2.img nor bl2.bin found)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_CONFIGS="$FAILED_CONFIGS $cfg_rel"
    fi
done

echo "======================================================================"
echo "Build summary: success=$SUCCESS_COUNT, failed=$FAIL_COUNT"
if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "Failed configs:$FAILED_CONFIGS"
fi

if [ "$SUCCESS_COUNT" -eq 0 ]; then
    echo "Error: all BL2 builds failed."
    exit 1
fi

echo "At least one BL2 build succeeded, continue workflow."
