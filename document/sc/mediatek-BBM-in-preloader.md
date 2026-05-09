# MediaTek 平台 SPIM-NAND 三种坏块管理模式详解（`nand_skip_bad` / `nand_nmbm` / `nand_ubi`）

本文档面向 `plat/mediatek`（尤其 `mt7981/mt7986/mt7987/mt7988`）下使用 `BOOT_DEVICE=spim-nand` 的场景，详细说明以下三种 NAND 坏块管理配置的差异、代码路径、构建行为、适用场景与调试要点：

- `nand_skip_bad`（Kconfig: `_NAND_SKIP_BAD`）
- `nand_nmbm`（Kconfig: `_NAND_NMBM`，Make 变量: `NMBM=1`）
- `nand_ubi`（Kconfig: `_NAND_UBI`，Make 变量: `UBI=1`）

---

## 1. 配置入口与约束关系

在 `plat/mediatek/apsoc_common/Config.in` 中：

- 菜单路径：`Advanced boot device configuration -> NAND bad block management`
- 该 `choice` 仅在 `_BOOT_DEVICE_SNFI_NAND || _BOOT_DEVICE_SPIM_NAND` 时可见。
- 三个互斥选项：
  - `_NAND_SKIP_BAD`
  - `_NAND_NMBM`
  - `_NAND_UBI`

### 1.1 互斥关系（关键）

`NMBM` 与 `UBI` 在构建系统中是互斥的：

- 来自 `plat/mediatek/apsoc_common/bl2/bl2_image.mk`：若 `NMBM=1` 且 `UBI=1`，会直接触发构建错误。
- 结论：三种模式只能三选一。

### 1.2 与 Dual-FIP 的关系（关键）

在 NAND 场景下：

- 若 **未启用 `UBI`**，则 `Dual-FIP` 会被视为不支持（构建时阻止）。
- 若 **启用 `UBI`**，可配合 `Dual-FIP`（通过 UBI 卷 `fip` / `fip2` 实现）。

这也是为什么 UBI 模式在“高可靠升级/回滚”场景下更常见。

---

## 2. 三种模式对应的源码与数据路径

当 `BOOT_DEVICE=spim-nand` 时，平台 `bl2.mk`（如 `mt7981/7986/7987/7988`）统一走：

- `$(call BL2_BOOT_SPI_NAND,0,0)`

随后在 `bl2_image.mk` 的 `BL2_NAND_COMMON` 中根据 `NMBM/UBI` 决定具体实现。

### 2.1 `nand_skip_bad` 模式

- 典型状态：`NMBM!=1` 且 `UBI!=1`
- 主要源码：
  - `plat/mediatek/apsoc_common/bl2/bl2_boot_nand.c`
- 核心逻辑：
  1. 根据逻辑 FIP offset（位于分区内）进行“坏块跳过式”地址调整。
  2. 调整函数逐块调用 `mtd_block_is_bad()`，遇坏块则跳过并继续向后映射。
  3. 最终通过 `nand_read()` 从“调整后的物理地址”读取。

#### 特性

- **优点**
  - 依赖最少，实现简单。
  - 不需要 UBI 元数据，也不引入 NMBM 管理层。
- **缺点**
  - 只做“读取时跳坏块映射”，不具备完整逻辑块管理能力。
  - 介质老化后可维护性和可预测性不如 NMBM/UBI。
- **更适合**
  - 小体量、低复杂度、对升级策略要求不高的场景。

### 2.2 `nand_nmbm` 模式

- 典型状态：`NMBM=1` 且 `UBI!=1`
- 主要源码：
  - `plat/mediatek/apsoc_common/bl2/bl2_boot_nand_nmbm.c`
  - `lib/nmbm/*`
- 核心逻辑：
  1. 初始化底层 NAND 回调（读页、坏块判断、日志输出）。
  2. 以 `NMBM_F_CREATE | NMBM_F_READ_ONLY` 挂接 NMBM 实例。
  3. 后续 FIP 读取走 `nmbm_read_range()`。

#### 可调参数

来自 `Config.in` 的高级选项（启用 `_NMBM_CONFIGS` 后可见）：

- `NMBM_MAX_RATIO`：保留管理块比例（默认 1，即 $1/16$）。
- `NMBM_MAX_RESERVED_BLOCKS`：保留管理块上限（默认 256）。
- `NMBM_DEFAULT_LOG_LEVEL`：日志级别。

这些参数通过 `BL2_CPPFLAGS` 传入，影响 NMBM 初始化行为。

#### 特性

- **优点**
  - 比 skip-bad 更完整的坏块管理抽象，读取路径鲁棒性更高。
  - 不依赖 UBI 卷管理，介于“轻量”和“可靠”之间。
- **缺点**
  - 仍不等同于 UBI 的卷级管理能力。
  - 与 UBI 互斥，且在当前实现下 NAND + 非 UBI 不支持 Dual-FIP。
- **更适合**
  - 希望提升坏块容忍能力，但暂不引入 UBI 卷体系的项目。

### 2.3 `nand_ubi` 模式

- 典型状态：`UBI=1` 且 `NMBM!=1`
- 主要源码：
  - `plat/mediatek/apsoc_common/bl2/bl2_boot_nand_ubi.c`
  - `drivers/io/ubi/io_ubi.c`
  - `drivers/io/ubi/ubispl.c`
- 核心逻辑：
  1. 初始化 UBI 设备描述（PEB 大小、起始 PEB、计数、VID offset、LEB 起点）。
  2. 通过 `register_io_dev_ubi` 打开 UBI 设备。
  3. 默认按卷名 `fip` 读取 FIP。
  4. 若启用 `DUAL_FIP`，可在 `fip` / `fip2` 间选择与切换。

#### UBI 地址窗口

可通过以下配置覆盖默认 UBI 区间：

- `OVERRIDE_UBI_START_ADDR`（默认 `0x200000`）
- `OVERRIDE_UBI_END_ADDR`（默认 `0`，表示到 NAND 末尾）

#### 特性

- **优点**
  - 卷级抽象清晰，适合量产和升级系统。
  - 与 Dual-FIP 结合自然，便于容灾与回滚。
- **缺点**
  - 体系更复杂，对镜像制作/烧录流程要求更高。
- **更适合**
  - 量产设备、A/B 或双镜像策略、强调升级可靠性的场景。

---

## 3. 三种模式横向对比

| 维度 | `nand_skip_bad` | `nand_nmbm` | `nand_ubi` |
|---|---|---|---|
| 核心文件 | `bl2_boot_nand.c` | `bl2_boot_nand_nmbm.c` + `lib/nmbm` | `bl2_boot_nand_ubi.c` + `drivers/io/ubi` |
| 坏块处理方式 | 读取时逐块跳过 | NMBM 映射层 | UBI 卷/PEB 层 |
| 配置复杂度 | 低 | 中 | 高 |
| Dual-FIP 支持 | 否（NAND 非 UBI） | 否（NAND 非 UBI） | 是 |
| 可调参数 | 少 | NMBM ratio/reserved/log level | UBI 起止地址、卷名流程 |
| 适配量产升级 | 一般 | 较好 | 最好 |

---

## 4. 推荐选型

### 4.1 快速建议

- 追求最小改动、早期 bring-up：优先 `nand_skip_bad`。
- 需要更强坏块管理，但暂不做 UBI 卷体系：选择 `nand_nmbm`。
- 需要稳健升级和双镜像策略：选择 `nand_ubi`（推荐量产）。

### 4.2 实际工程建议

1. 如果你的系统已经采用 UBI（Linux 侧 rootfs/数据分区也基于 UBI），BL2 也建议统一到 `nand_ubi`，降低系统模型割裂。
2. 若当前已有历史产线流程依赖“固定偏移 + 简单跳坏块”，可先 `nand_skip_bad`，再规划向 NMBM/UBI 演进。
3. 对于高坏块增长风险的 NAND 批次，不建议长期停留在 skip-bad 模式。

---

## 5. 与 `spim-nand` 相关的构建注意事项

在 `mt7981/mt7986/mt7987/mt7988` 的 `bl2.mk` 中，`BOOT_DEVICE=spim-nand` 已受支持，且常见默认 `NAND_TYPE` 为 `spim:2k+64`。

建议同时确认：

- `NAND_TYPE` 是否与实际颗粒几何参数一致（`2k+64/2k+128/4k+256`）。
- `UBI` 模式下 `OVERRIDE_UBI_START_ADDR/END_ADDR` 是否与分区规划一致。
- 需要 Dual-FIP 时，确保使用 UBI 模式并准备 `fip`/`fip2` 卷。

---

## 6. 常见问题（FAQ）

### Q1：为什么我选了 Dual-FIP 但 NAND 下编不过？

因为当前构建规则要求 NAND 场景下仅 UBI 模式支持 Dual-FIP；非 UBI（skip-bad/NMBM）会被判定不支持。

### Q2：`nand_skip_bad` 是否“完全不管坏块”？

不是。它会在读取时根据坏块表做地址偏移调整，跳过坏块后再读；但它不是完整的逻辑块管理系统。

### Q3：NMBM 和 UBI 可以叠加吗？

当前不可以。构建逻辑明确禁止 `NMBM=1` 与 `UBI=1` 同时开启。

---

## 7. 维护建议

1. 在项目文档中固定“模式选择原则”（例如：量产默认 UBI，实验板可 skip-bad）。
2. 对每个发布版本记录：`BOOT_DEVICE`、`NAND_TYPE`、`NMBM/UBI`、`DUAL_FIP`、UBI 地址窗口。
3. 若切换模式（尤其 NMBM -> UBI），同步更新镜像打包/烧录流水线，避免现场升级失败。

---

## 8. 关键源码索引

- 配置入口：`plat/mediatek/apsoc_common/Config.in`
- 模式选择构建逻辑：`plat/mediatek/apsoc_common/bl2/bl2_image.mk`
- skip-bad 实现：`plat/mediatek/apsoc_common/bl2/bl2_boot_nand.c`
- NMBM 实现：`plat/mediatek/apsoc_common/bl2/bl2_boot_nand_nmbm.c`
- UBI 实现：`plat/mediatek/apsoc_common/bl2/bl2_boot_nand_ubi.c`
- SoC 侧 spim-nand 入口示例：
  - `plat/mediatek/mt7981/bl2/bl2.mk`
  - `plat/mediatek/mt7986/bl2/bl2.mk`
  - `plat/mediatek/mt7987/bl2/bl2.mk`
  - `plat/mediatek/mt7988/bl2/bl2.mk`
