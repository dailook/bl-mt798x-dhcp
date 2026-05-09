[English](../mk_simg.md) | [简体中文](#)

# `mk_simg.sh`（批量构建 simg）

**刷写 simg 具有巨大的风险，请务必确认你知道自己在做什么，并且做好了充分的备份和恢复准备。**

- 位置：仓库根目录 `mk_simg.sh`
- 用途：自动扫描 `mt798x_simg/` 下的“板型文件夹”，按文件夹命名推断设备类型/dual-image，并调用 ATF 工具目录中的 `mk_image.sh` 批量生成 single image。

## 自定义分区信息（传入分区 yml）

默认情况下，`mk_simg.sh` 不显式传入 `-c`，让被调用的 `mk_image.sh` 使用其自带的默认分区配置（`./partitions/<flash_type>.yml`，相对于 **ATF 工具目录**）。

如果你希望使用“自定义/分析得到的分区信息 yml”，可以通过以下方式传入：

- 方式 1（推荐）：在板型文件夹内放一个 `*.yml/*.yaml`
  - 命名必须为：`<board>/<board>.yml`（或 `.yaml`）
  - 例如：`mt798x_simg/mt7986-myboard-emmc/mt7986-myboard-emmc.yml`
  - 优先级最高：该文件会自动作为 `mk_image.sh -c <board>.yml`
  - 如果板型文件夹内存在其它 `.yml/.yaml` 但不匹配该命名规则，`mk_simg.sh` 会警告并停止（防止误用错误布局）。

- 方式 2：全局指定一个分区配置文件（对所有板型生效）
  - `./mk_simg.sh --config /path/to/layout.yml`
  - 或环境变量：`PARTITION_CONFIG=/path/to/layout.yml ./mk_simg.sh`

- 方式 3：全局指定一个分区配置目录（按设备类型选择 `<device>.yml`）
  - `./mk_simg.sh --config-dir /path/to/partitions`
  - 目录里需包含：`emmc.yml`、`sd.yml`、`spim-nand.yml`、`snfi-nand.yml`、`spim-nor.yml`
  - 或环境变量：`PARTITION_CONFIG=/path/to/partitions ./mk_simg.sh`

分区配置优先级为：

1) 板型目录内的 `*.yml` → 2) `--config` 指定的文件 → 3) `--config-dir` 指定目录下的 `<device>.yml` → 4) `mk_image.sh` 默认配置

## 目录与命名约定

- 输入目录：`mt798x_simg/<board>/`
- 输出目录：`output_simg/`
- 输出命名：`output_simg/<board>-simg.bin`
- `<board>` 文件夹名规则：
  - 前缀必须是平台：`mt7981/mt7986/mt7988/mt7987`
  - 设备类型推断：
    - 以 `-emmc` 结尾：按 eMMC 处理（`-d emmc`）
    - 以 `-sd` 结尾：按 SD 处理（`-d sd`）
    - 其它情况：按非 MMC 处理（默认 `-d spim-nand`）
    - （可选）如果文件夹名包含 `-snfi-nand` / `-spim-nand` / `-spim-nor`，会优先按对应非 MMC 设备处理
  - dual-image：文件夹名包含 `-dual-image`（或 `-dual_image`）时，自动附加 `--dual-image`

## 板型文件夹应放哪些文件（自动识别，大小写不敏感）

脚本会在板型文件夹内自动查找以下输入（允许 `.img/.bin`，并按关键字匹配，例如 `*bl2*`、`*fip*`、`*gpt*`）：

- eMMC（`-emmc`）：必须有 `GPT` + `FIP`（不需要 BL2，脚本也不会传入 `-b`）
- SD（`-sd`）：必须有 `GPT` + `BL2` + `FIP`
- 非 MMC（默认/`-snfi-nand`/`-spim-nand`/`-spim-nor`）：必须有 `BL2` + `FIP`
- 可选文件：
  - `RF/Factory`：文件名包含 `factory`（脚本会传给 `-r`）
  - `kernel`：文件名包含 `sysupgrade/kernel/firmware/openwrt`（脚本会传给 `-k`）

> 可以传送 `.ubi/.itb` 类型的 `kernel` 镜像，以及 `factory` 类型的 `kernel` 镜像，但是与 RF 重名了，所以你需要将其重命名。

若缺少必需文件，脚本会打印“缺少哪些文件”以及检测到的候选文件路径，并继续处理其它板型，最终以非 0 退出码提示整体失败。

## 使用示例

- 非 MMC（默认按 `spim-nand`）：
  - 目录：`mt798x_simg/mt7981-cmcc_a10/` 放入 `bl2*.img`、`fip*.bin`（可选 `factory*.bin`、`*kernel*`、`mt7981-cmcc_a10.yml`...）
  - 运行：`./mk_simg.sh`

- MMC（eMMC）：
  - 目录：`mt798x_simg/mt7986-rfb-emmc/` 放入 `gpt*.bin`、`fip*.bin`（可选 `*sysupgrade*`、`factory*.bin`...）
  - 运行：`./mk_simg.sh`

- dual-image（以 NAND 为例）：
  - 目录：`mt798x_simg/mt7981-ax3000-spim-nand-dual-image/` 放入 `bl2*.img`、`fip*.bin`、以及适配 dual-image 的 UBI/kernel 镜像（可选 `mt7981-ax3000-spim-nand-dual-image.yaml`...）
  - 运行：`./mk_simg.sh`
