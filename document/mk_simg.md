[English](#) | [简体中文](./sc/mk_simg.md)

# `mk_simg.sh` (batch-build `simg`)

**Flashing a `simg` image is extremely risky. Please make sure you fully understand what you are doing, and that you have prepared sufficient backup and recovery measures.**

- Location: repository root `mk_simg.sh`
- Purpose: automatically scans the “board folders” under `mt798x_simg/`, infers the device type / dual-image mode from the folder name, and calls `mk_image.sh` in the ATF tools directory to batch-generate single images.

## Custom partition information (passing a partition YAML)

By default, `mk_simg.sh` does not explicitly pass `-c`, so the invoked `mk_image.sh` uses its built-in default partition configuration (`./partitions/<flash_type>.yml`, relative to the **ATF tools directory**).

If you want to use a “custom / analyzed” partition layout YAML, you can pass it in the following ways:

- Method 1 (recommended): place a `*.yml/*.yaml` inside the board folder
  - The filename must be: `<board>/<board>.yml` (or `.yaml`)
  - Example: `mt798x_simg/mt7986-myboard-emmc/mt7986-myboard-emmc.yml`
  - Highest priority: this file will be automatically passed as `mk_image.sh -c <board>.yml`
  - If there are other `.yml/.yaml` files in the board folder that do not match this naming rule, `mk_simg.sh` will warn and stop (to prevent accidentally using the wrong layout).

- Method 2: specify a global partition configuration file (applies to all boards)
  - `./mk_simg.sh --config /path/to/layout.yml`
  - Or via environment variable: `PARTITION_CONFIG=/path/to/layout.yml ./mk_simg.sh`

- Method 3: specify a global partition configuration directory (select `<device>.yml` by device type)
  - `./mk_simg.sh --config-dir /path/to/partitions`
  - The directory must contain: `emmc.yml`, `sd.yml`, `spim-nand.yml`, `snfi-nand.yml`, `spim-nor.yml`
  - Or via environment variable: `PARTITION_CONFIG=/path/to/partitions ./mk_simg.sh`

Partition configuration priority:

1) `*.yml` inside the board directory → 2) file specified by `--config` → 3) `<device>.yml` under the directory specified by `--config-dir` → 4) `mk_image.sh` default configuration

## Directory and naming conventions

- Input directory: `mt798x_simg/<board>/`
- Output directory: `output_simg/`
- Output filename: `output_simg/<board>-simg.bin`
- `<board>` folder naming rules:
  - The prefix must be the platform: `mt7981/mt7986/mt7988/mt7987`
  - Device type inference:
    - Ends with `-emmc`: treat as eMMC (`-d emmc`)
    - Ends with `-sd`: treat as SD (`-d sd`)
    - Otherwise: treat as non-MMC (default `-d spim-nand`)
    - (Optional) if the folder name contains `-snfi-nand` / `-spim-nand` / `-spim-nor`, it will take precedence and be treated as the corresponding non-MMC device
  - Dual-image: if the folder name contains `-dual-image` (or `-dual_image`), `--dual-image` will be appended automatically

## What files to put in a board folder (auto-detected, case-insensitive)

The script will automatically search for the following inputs in the board folder (supports `.img/.bin`), matching by keywords such as `*bl2*`, `*fip*`, `*gpt*`:

- eMMC (`-emmc`): must have `GPT` + `FIP` (BL2 is not needed, and the script will not pass `-b`)
- SD (`-sd`): must have `GPT` + `BL2` + `FIP`
- Non-MMC (default / `-snfi-nand` / `-spim-nand` / `-spim-nor`): must have `BL2` + `FIP`
- Optional files:
  - `RF/Factory`: filename contains `factory` (passed to `-r`)
  - `kernel`: filename contains `sysupgrade/kernel/firmware/openwrt` (passed to `-k`)

> You can provide `kernel` images of type `.ubi/.itb`, as well as `kernel` images that include `factory` in the filename; however, that conflicts with the RF naming, so you will need to rename it.

If required files are missing, the script will print what is missing and the detected candidate paths, continue processing other boards, and finally exit with a non-zero code to indicate an overall failure.

## Usage examples

- Non-MMC (defaults to `spim-nand`):
  - Directory: `mt798x_simg/mt7981-cmcc_a10/` containing `bl2*.img`, `fip*.bin` (optional `factory*.bin`, `*kernel*`, `mt7981-cmcc_a10.yml`...)
  - Run: `./mk_simg.sh`

- MMC (eMMC):
  - Directory: `mt798x_simg/mt7986-rfb-emmc/` containing `gpt*.bin`, `fip*.bin` (optional `*sysupgrade*`, `factory*.bin`...)
  - Run: `./mk_simg.sh`

- Dual-image (NAND example):
  - Directory: `mt798x_simg/mt7981-ax3000-spim-nand-dual-image/` containing `bl2*.img`, `fip*.bin`, and a UBI/kernel image suitable for dual-image (optional `mt7981-ax3000-spim-nand-dual-image.yaml`...)
  - Run: `./mk_simg.sh`
