[English](#) | [简体中文](./sc/atftools.md)

# MediaTek ATF Tools

This document is generated from AI analysis; the content may be inaccurate. Use with caution.

This document introduces several ATF (Arm Trusted Firmware) related tools: `ar-table`, `ar-tool`, `gen_salt_header`, `bromimage`, `bl2plimage`, and `simg`. These tools are mainly used for firmware build and image generation on MediaTek platforms, covering anti-rollback version tables, BL2 payload headers, and single-flash images.

## `ar-table/` (`ar-table.py`)

- Reads `ar_table.xml` and parses `RELEASE_INDEX/BL2_AR_VER/FIP_AR_VER/FIT_AR_VER` in `ar_entry`.
- Generates two types of outputs:
  - C code: `mtk_ar_table[]` + record count, used for the anti-rollback version table.
  - Makefile variables: `TFW_NVCTR_VAL/NTFW_NVCTR_VAL/BL2_AR_VER/FIT_AR_VER`, used for build configuration.
- Command format: `ar-table [create_ar_table|create_ar_conf] input_config output_file`.

## `ar-tool/` (`ar-tool.py`)

- Reads `bl_ar_table.xml` or `fw_ar_table.xml`, parses `USED` and `*_AR_VER` in `bl_ar_entry/fw_ar_entry`.
- Generates two types of outputs:
  - C code: `const uint32_t bl_ar_ver` or `fw_ar_ver`.
  - Makefile variables: `BL_AR_VER` or `FW_AR_VER`.
- Command format: `ar-tool [bl_ar_table|fw_ar_table] [create_ar_ver|create_ar_conf] input_file output_file`.

## `gen_salt_header/` (`gen_salt_header.sh`)

- Converts a binary salt file into a C header macro array (hex byte sequence).
- Produces a `.h` file with include guards and macro definitions, used to compile the salt into firmware.

## `bromimage/`

- Provides only prebuilt executables for multiple platforms (`bromimage-*`), no source code.
- From the name, it is inferred to generate/process images related to MediaTek BootROM.
- Comes with licenses related to `musl/openssl`.

## `bl2plimage` (`bl2plimage.c`)

- Used to generate a **BL2 payload image**: prepends a `bl2pl_hdr` header to the raw payload (binary), then outputs a new file.
- Header fields (from `struct bl2pl_hdr` in `bl2pl.h`):
  - `magic`: `BL2PL_HDR_MAGIC`
  - `load_addr`: payload load address (specified by `-a`)
  - `size`: payload size (bytes)
  - `unused`: reserved field, written as 0
- Supports endianness output:
  - Default: **little-endian**
  - `-b`: write `magic/load_addr/size` in **big-endian**
- Command format: `bl2plimage [options] <input_file> <output_file>`
- Options:
  - `-h`: show help
  - `-a <addr>`: load address (hex string, e.g., `0x201000`)
  - `-b`: use big-endian for header fields
- Examples:
  - Little-endian header: `bl2plimage -a 0x201000 bl2_payload.bin bl2_payload.img`
  - Big-endian header: `bl2plimage -b -a 0x201000 bl2_payload.bin bl2_payload_be.img`

## `simg` (single image wrapper, `single_img_wrapper/mk_image.sh`)

- Used to generate a **single image** (one-shot flash image): based on a partition layout (YAML), it packs multiple input images (GPT/BL2/RF/FIP/kernel/rootfs, etc.) into one output file at specified offsets, enabling one-time flashing to flash / eMMC / SD.
- Script: `single_img_wrapper/mk_image.sh`
  - Supported platforms: `mt7981/mt7986/mt7988/mt7987`
  - Supported media types: `snfi-nand/spim-nand/spim-nor/emmc/sd`
  - Supports dual-image mode: `--dual-image` (or `--dual_image`)
- Partition configuration:
  - Default: `single_img_wrapper/partitions/<flash_type>.yml`
  - You can specify another YAML via `-c <partition config>`
  - The YAML uses `.<platform>.<default|"dual-image">.<partition>.(start/size)` to describe offsets/sizes (the script parses it with `yq`)
- Key input/validation rules (built-in checks in the script):
  - `-p <platform>` and `-d <flash_type>` are required
  - `-f <FIP image>` is required (default filename `fip.bin`)
  - For non-eMMC: `-b <BL2 image>` is required (default `bl2.img`)
  - For eMMC/SD: you must provide `-g <GPT table>`; eMMC images **do not include** BL2 (passing `-b` will error)
  - `-r <RF image>` is optional (written into the `factory` partition per config; often used for RF/factory data)
  - `-k <kernel image>` is optional
- Kernel/rootfs handling for eMMC/SD:
  - `-k` typically points to an OpenWrt sysupgrade tarball; the script extracts `*kernel*` and `*root*`, then repacks them into `kernel_single.bin` according to `kernel.size/rootfs.size` in the partition config (in dual-image mode it generates `kernel_dual.bin`)
- UBI handling for NAND dual-image (`--dual-image` + `spim-nand/snfi-nand`):
  - Uses `ubireader_extract_images/ubireader_display_info` to extract and read UBI info
  - Combines YAML `.ubi.<volume>.size` (e.g., `rootfs/rootfs2/rootfs_data/u-boot-env`, etc.) to generate an `ubinize` config and rebuild the UBI image
- Enable ubi image handle
  - Not used yet
- Dependencies (checked at script startup):
  - Basic commands: `awk/tar/sed/dd`
  - `yq`: parses YAML (repo includes `single_img_wrapper/bin/yq_linux_amd64`, usable on Linux/WSL)
  - UBI-related (for NAND dual-image): `ubireader_extract_images/ubireader_display_info` (Python package `ubi_reader`), `ubinize` (usually from `mtd-utils`)
- Command format: `./mk_image.sh -p <platform> -d <flash_type> [options]`
- Common examples:
  - eMMC single image (GPT + FIP + kernel): `./mk_image.sh -p mt7986 -d emmc -g GPT.bin -f fip.bin -k sysupgrade.tar`
  - SPIM-NAND single image (BL2 + FIP + kernel + optional RF): `./mk_image.sh -p mt7986 -d spim-nand -b bl2.img -f fip.bin -k nand-kernel.bin -r factory.bin`
  - SPIM-NAND dual-image: `./mk_image.sh -p mt7986 -d spim-nand -b bl2.img -f fip.bin -k nand-ubi.bin --dual-image`

### Storage / partition layout analysis (`partitions/*.yml`)

The `.yml` files under `single_img_wrapper/partitions/` can be treated as **partition layout analysis results**: they use a unified key structure to describe partition start offsets and sizes for different SoCs (`mt7981/mt7986/mt7987/mt7988`) across different boot media (eMMC/SD/NAND/NOR).

- File list (by media type):
  - `emmc.yml`: eMMC layout (includes `gpt/fip/kernel/rootfs`, etc.)
  - `sd.yml`: SD layout (adds `bl2` compared to eMMC)
  - `spim-nand.yml` / `snfi-nand.yml`: NAND layout (firmware area typically uses `ubi`)
  - `spim-nor.yml`: NOR layout

- Common structure conventions:
  - Top-level key is the platform name: `mt7981/mt7986/mt7987/mt7988`
  - Second-level key is the mode: `default` or `"dual-image"`
  - Common leaf fields:
    - `start`: start offset (hex, unit: bytes)
    - `size`: reserved size (hex, unit: bytes; optional)

- Common partition keys (vary by media type):
  - eMMC/SD: `gpt`, `bl2` (SD only), `factory`, `fip`, `kernel`, `rootfs`; in dual-image mode there are also `kernel2/rootfs2`
  - NAND: `bl2`, `factory`, `fip`, `ubi` (contains volume/region info underneath)
  - NOR: `bl2`, `rf`, `fip`, `kernel`

> Note: `mk_image.sh` currently reads the RF partition using the `factory` key (i.e., `...factory.start`). However, in `spim-nor.yml` the RF partition uses the `rf` key.
> If you want RF data to be written correctly on NOR as well, you have two options:
>
> 1) Rename `rf:` to `factory:` in `spim-nor.yml` (keep the fields unchanged); or
> 2) Modify the `mk_image.sh` you actually use so that when `factory` does not exist, it falls back to reading `rf`.

## `fiptool` MediaTek part (`mediatek`)

- `plat_fiptool.mk`
  - Enables `PLAT_DEF_FIP_UUID` and `MTK_FIP_CHKSUM`.
  - Builds `plat_def_uuid_config.c`, `sha256.c`, and `crc32.c` into `fiptool`.

- `plat_def_uuid_config.c`
  - Adds a platform-specific entry to the FIP ToC: `UUID_MTK_FIP_CHECKSUM`, command name `fip-chksum`.
  - Implements `mtk_fip_chksum_*`: computes and writes `mtk_fip_checksum` (`len + crc32 + sha256`).

- `crc32.*` / `sha256.*` / `alignment.h`
  - Provide local implementations of CRC32 and SHA-256 to avoid external dependencies.
  - `alignment.h` handles unaligned reads and endianness (sourced from mbedtls code).

- MediaTek logic in `fiptool.c`
  - When `MTK_FIP_CHKSUM` is enabled:
    - Automatically creates the `fip-chksum` entry and writes checksum data.
    - Computes CRC32 + SHA256 for the ToC and all payloads during packing.
    - Explicitly forbids users from manually specifying `--fip-chksum`.
  - Related structures and UUID definitions come from `plat_def_fip_uuid.h`:
    - `UUID_MTK_FIP_CHECKSUM`
    - `struct mtk_fip_checksum { len, crc, sha256sum[0x20]; }`

## `gpt_editor` (`gpt_editor.py`)

Documentation [README](/atf-20250711/tools/dev/gpt_editor/README.md) already exists, so it will not be repeated here.
