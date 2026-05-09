[English](../atftool.md) | [简体中文](#)

# Mediatek ATF 工具

本文档来自 AI 分析，具体内容可能存在偏差，请谨慎使用。

介绍了 ATF（Arm Trusted Firmware）相关的几个工具：`ar-table`、`ar-tool`、`gen_salt_header`、`bromimage`、`bl2plimage` 和 `simg`。这些工具主要用于处理 MediaTek 平台的固件构建与镜像生成，涉及反回滚版本表、BL2 payload 头部、单一烧录镜像等方面。

## `ar-table/`（ar-table.py）

- 读取 ar_table.xml，解析 `ar_entry` 中的 `RELEASE_INDEX/BL2_AR_VER/FIP_AR_VER/FIT_AR_VER`。
- 生成两类输出：
  - C 代码：`mtk_ar_table[]` + 记录数，用于反回滚版本表（anti-rollback）。
  - Makefile 变量：`TFW_NVCTR_VAL/NTFW_NVCTR_VAL/BL2_AR_VER/FIT_AR_VER`，用于构建配置。
- 命令格式：`ar-table [create_ar_table|create_ar_conf] input_config output_file`。

## `ar-tool/`（ar-tool.py）

- 读取 bl_ar_table.xml 或 fw_ar_table.xml，解析 `bl_ar_entry/fw_ar_entry` 中 `USED` 与 `*_AR_VER`。
- 生成两类输出：
  - C 代码：`const uint32_t bl_ar_ver` 或 `fw_ar_ver`。
  - Makefile 变量：`BL_AR_VER` 或 `FW_AR_VER`。
- 命令格式：`ar-tool [bl_ar_table|fw_ar_table] [create_ar_ver|create_ar_conf] input_file output_file`。

## `gen_salt_header/`（gen_salt_header.sh）

- 把二进制盐值文件转成 C 头文件宏数组（十六进制字节序列）。
- 生成包含头防护与宏定义的 `.h`，用于把 salt 编译进固件。

## `bromimage/`

- 仅提供多平台预编译可执行文件（`bromimage-*`），无源码。
- 从命名推断用于生成/处理 MediaTek BootROM 相关镜像。
- 附带 `musl/openssl` 相关许可证。

## `bl2plimage`（bl2plimage.c）

- 用于生成 **BL2 payload image**：在原始 payload（二进制）前追加一个 `bl2pl_hdr` 头部，然后输出为新文件。
- 头部字段（来自 `bl2pl.h` 的 `struct bl2pl_hdr`）：
  - `magic`：`BL2PL_HDR_MAGIC`
  - `load_addr`：payload 的加载地址（由 `-a` 指定）
  - `size`：payload 大小（字节）
  - `unused`：保留字段，写入 0
- 支持大小端输出：
  - 默认 **little-endian**
  - `-b`：将 `magic/load_addr/size` 按 **big-endian** 写入
- 命令格式：`bl2plimage [options] <input_file> <output_file>`
- 选项说明：
  - `-h`：显示帮助
  - `-a <addr>`：加载地址（十六进制字符串，例如 `0x201000`）
  - `-b`：使用 big-endian 写入头部字段
- 示例：
  - 生成 little-endian 头部：`bl2plimage -a 0x201000 bl2_payload.bin bl2_payload.img`
  - 生成 big-endian 头部：`bl2plimage -b -a 0x201000 bl2_payload.bin bl2_payload_be.img`

## `simg`（single image wrapper，single_img_wrapper/mk_image.sh）

- 用于生成 **single image**（单一烧录镜像）：根据分区布局（YAML）把多个输入镜像（GPT/BL2/RF/FIP/kernel/rootfs 等）按 offset 打包进同一个输出文件，便于一次性烧录到 flash / eMMC / SD。
- 脚本：`single_img_wrapper/mk_image.sh`
  - 支持平台：`mt7981/mt7986/mt7988/mt7987`
  - 支持介质类型：`snfi-nand/spim-nand/spim-nor/emmc/sd`
  - 支持 dual image 模式：`--dual-image`（或 `--dual_image`）
- 分区配置：
  - 默认读取：`single_img_wrapper/partitions/<flash_type>.yml`
  - 可用 `-c <partition config>` 指定其他 YAML
  - YAML 中按 `.<platform>.<default|"dual-image">.<partition>.(start/size)` 描述 offset/大小（脚本用 `yq` 解析）
- 关键输入/校验规则（脚本内置检查）：
  - `-p <platform>`、`-d <flash_type>` 必填
  - `-f <FIP image>` 必填（默认文件名 `fip.bin`）
  - 非 eMMC：需要 `-b <BL2 image>`（默认 `bl2.img`）
  - eMMC/SD：必须提供 `-g <GPT table>`；其中 eMMC 镜像**不包含** BL2（指定 `-b` 会报错）
  - `-r <RF image>` 可选（脚本按配置里的 `factory` 分区写入，常用于 RF/Factory 数据）
  - `-k <kernel image>` 可选
- eMMC/SD 的 kernel/rootfs 处理：
  - `-k` 通常传入类似 OpenWrt sysupgrade 的 tar 包；脚本会解包并抽取 `*kernel*` 与 `*root*`，再按分区 `kernel.size/rootfs.size` 重新拼成一个 `kernel_single.bin`（dual-image 时会生成 `kernel_dual.bin`）。
- NAND dual-image 的 UBI 处理（`--dual-image` + `spim-nand/snfi-nand`）：
  - 使用 `ubireader_extract_images/ubireader_display_info` 提取并读取 UBI 信息
  - 结合 YAML 中 `.ubi.<volume>.size`（如 `rootfs/rootfs2/rootfs_data/u-boot-env` 等）生成 `ubinize` 配置并重建 UBI 镜像
- Enable ubi image 的处理
  - 暂未实装
- 依赖（脚本启动时会检查）：
  - 基础命令：`awk/tar/sed/dd`
  - `yq`：用于解析 YAML（仓库内提供 `single_img_wrapper/bin/yq_linux_amd64`，在 Linux/WSL 下可用）
  - UBI 相关（dual-image 的 NAND 场景）：`ubireader_extract_images/ubireader_display_info`（Python 包 `ubi_reader`）、`ubinize`（通常来自 `mtd-utils`）
- 命令格式：`./mk_image.sh -p <platform> -d <flash_type> [options]`
- 常见示例：
  - eMMC single image（GPT + FIP + kernel）：`./mk_image.sh -p mt7986 -d emmc -g GPT.bin -f fip.bin -k sysupgrade.tar`
  - SPIM-NAND single image（BL2 + FIP + kernel + 可选 RF）：`./mk_image.sh -p mt7986 -d spim-nand -b bl2.img -f fip.bin -k nand-kernel.bin -r factory.bin`
  - SPIM-NAND dual-image：`./mk_image.sh -p mt7986 -d spim-nand -b bl2.img -f fip.bin -k nand-ubi.bin --dual-image`

### 存储/分区布局分析（`partitions/*.yml`）

`single_img_wrapper/partitions/` 下的 `.yml` 文件可以视为“**存储布局分析结果**”：用统一的键结构描述不同 SoC（`mt7981/mt7986/mt7987/mt7988`）在不同启动介质（eMMC/SD/NAND/NOR）上的分区起始地址与大小。

- 文件列表（按介质类型划分）：
  - `emmc.yml`：eMMC 布局（含 `gpt/fip/kernel/rootfs` 等）
  - `sd.yml`：SD 布局（比 eMMC 多 `bl2`）
  - `spim-nand.yml` / `snfi-nand.yml`：NAND 布局（固件区通常使用 `ubi`）
  - `spim-nor.yml`：NOR 布局

- 通用结构约定：
  - 顶层 key 是平台名：`mt7981/mt7986/mt7987/mt7988`
  - 第二层是模式：`default` 或 `"dual-image"`
  - 叶子节点常用字段：
    - `start`：起始 offset（十六进制，单位：byte）
    - `size`：预留空间大小（十六进制，单位：byte，可选）

- 常见分区键（不同介质会不同）：
  - eMMC/SD：`gpt`、`bl2`（仅 SD）、`factory`、`fip`、`kernel`、`rootfs`，dual-image 时会多 `kernel2/rootfs2`
  - NAND：`bl2`、`factory`、`fip`、`ubi`（其下包含 volume/区域信息）
  - NOR：`bl2`、`rf`、`fip`、`kernel`

> 注意：当前 `mk_image.sh` 读取 RF 分区时使用的是 `factory` 键（即 `...factory.start`）。但 `spim-nor.yml` 里 RF 使用 `rf` 键。
> 如果你希望在 NOR 上也正确写入 RF 数据，有两种做法：
>
> 1) 将 `spim-nor.yml` 中的 `rf:` 改名为 `factory:`（保持字段不变）；或
> 2) 修改实际使用的 `mk_image.sh`，让其在 `factory` 不存在时回退读取 `rf`。

## `fiptool` Mediatek 部分（mediatek）

- plat_fiptool.mk
  - 启用 `PLAT_DEF_FIP_UUID` 和 `MTK_FIP_CHKSUM`。
  - 将 plat_def_uuid_config.c、sha256.c、crc32.c 编入 `fiptool`。

- plat_def_uuid_config.c
  - 向 FIP 的 ToC 增加平台自定义条目：`UUID_MTK_FIP_CHECKSUM`，命令名 `fip-chksum`。
  - 实现 `mtk_fip_chksum_*`：计算并写入 `mtk_fip_checksum`（`len + crc32 + sha256`）。

- `crc32.*` / `sha256.*` / alignment.h
  - 提供 CRC32 和 SHA-256 的本地实现，避免依赖外部库。
  - alignment.h 用于非对齐读取与字节序处理（来自 mbedtls 代码）。

- fiptool.c 中的 Mediatek 逻辑
  - 当启用 `MTK_FIP_CHKSUM`：
    - 自动创建 `fip-chksum` 条目并写入校验数据。
    - 打包时对 ToC 和所有 payload 进行 CRC32 + SHA256 计算。
    - 明确禁止用户手动指定 `--fip-chksum`。
  - 相关结构与 UUID 定义来自 plat_def_fip_uuid.h：
    - `UUID_MTK_FIP_CHECKSUM`
    - `struct mtk_fip_checksum { len, crc, sha256sum[0x20]; }`

## `gpt_editor`（gpt_editor.py）

已经存在文档 [README](/atf-20250711/tools/dev/gpt_editor/README.md)，故不再赘述。
