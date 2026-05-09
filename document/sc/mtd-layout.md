[English](../mtd-layout.md) | [简体中文](#)

# `mtd-layout` 节点参数说明（U-Boot MTK 2025）

影响文件：

mtd_layout.c、mtdparts.c、failsafe.c、showlayout.c

## 1. 节点结构与选择机制

- 根节点路径：`/mtd-layout`
- 子节点形式：`layout@<n>`（如 `layout@0`, `layout@1`）
- 通过环境变量 `mtd_layout_label` 选择当前布局  
  - 若未设置该变量，则默认使用 `label = "default"` 的布局  
  - 逻辑见 mtd_layout.c

## 2. 支持的属性列表

以下属性在 `layout@*` 子节点中被读取并生效：

| 属性名 | 类型 | 是否必须 | 作用 |
| --- | --- | --- | --- |
| `label` | string | ✅ | 布局名称，用于选择匹配 |
| `mtdids` | string | ✅ | MTD 设备映射字符串（供 mtdparts 解析） |
| `mtdparts` | string | ✅ | 分区描述字符串（供 mtdparts 解析） |
| `cmdline` | string | 可选 | 写入 `bootargs`，作为内核命令行 |
| `boot_part` | string | 可选 | 写入环境变量 `ubi_boot_part` |
| `factory_part` | string | 可选 | 写入环境变量 `factory_part` |
| `sysupgrade_kernel_ubipart` | string | 可选 | 写入 `sysupgrade_kernel_ubipart` |
| `sysupgrade_rootfs_ubipart` | string | 可选 | 写入 `sysupgrade_rootfs_ubipart` |

> 这些属性由 `board_mtdparts_default()` 读取并写入环境变量（见 mtd_layout.c）。

---

## 3. 各属性含义与使用方式

### `label`

- 用于标识布局名称
- 环境变量 `mtd_layout_label` 与其匹配，决定使用哪套布局
- 默认值是 `"default"`

### `mtdids`

- 格式参考 mtdparts.c：

  ```plaintext
  mtdids=<dev-id>=<mtd-id>[,<dev-id>=<mtd-id>...]
  ```

- 示例：

  ```plaintext
  mtdids = "nmbm0=nmbm0";
  ```

### `mtdparts`

- 格式参考 mtdparts.c：

  ```plaintext
  mtdparts=[mtdparts=]<mtd-id>:<part-def>[,<part-def>...]
  <part-def> := <size>[@<offset>][(name)][ro]
  ```

- 示例（你的 DTS 中的用法）：

  ```plaintext
  mtdparts = "nmbm0:1024k(bl2),512k(u-boot-env),... ,35328k(firmware)";
  ```

### `cmdline`

- 被写入环境变量 `bootargs`  
- 用于传递 Linux 内核命令行  
- 示例：

  ```plaintext
  cmdline = "console=ttyS0,115200n1 ... bootpart=firmware";
  ```

### `boot_part`

- 被写入环境变量 `ubi_boot_part`  
- 在 `mtd_boot_image()` 中用于决定从哪个分区启动  
- 如果你是多布局固件（比如 firmware/firmware2 切换），这一项很关键

### `factory_part`

- 被写入环境变量 `factory_part`  
- 在升级流程 `mtd_upgrade_image()` 中作为 UBI 写入目标分区  
- 对多布局刷写很重要

### `sysupgrade_kernel_ubipart`

- 被写入环境变量 `sysupgrade_kernel_ubipart`  
- 在多布局且启用 `CONFIG_MEDIATEK_MULTI_MTD_LAYOUT` 时，用于 sysupgrade 处理 **内核 UBI 分区**

### `sysupgrade_rootfs_ubipart`

- 被写入环境变量 `sysupgrade_rootfs_ubipart`  
- 类似上面，用于 sysupgrade 的 **rootfs UBI 分区**

---

## 4. 相关命令与调试接口

### `showlayout`

- 由 showlayout.c 提供  
- 功能：打印所有 `mtd-layout` 子节点的 `label / mtdids / mtdparts`

### failsafe HTTP 接口

- 在 failsafe.c 中提供  
- 会返回当前布局以及所有布局的 label 与 mtdparts

## 在配置文件中

受影响配置选项：

- `CONFIG_MTDIDS_DEFAULT`（默认 mtdids）
    e.g. `nmbm0=nmbm0`
- `CONFIG_MTDPARTS_DEFAULT`（默认 mtdparts）
    e.g. `nmbm0:1024k(bl2),512k(u-boot-env),...`
    > 当最后一个分区没有指定大小时（例如：`nmbm0:1024k(bl2),512k(u-boot-env),... ,-(ubi)`），会自动计算剩余空间作为该分区大小，之后，该分区后不能再添加其他分区。
- `CONFIG_CMD_MTDPARTS`
- `CONFIG_CMD_SHOW_MTD_LAYOUT`
- `CONFIG_MEDIATEK_MULTI_MTD_LAYOUT`

---

## 5. 示例模板

```dts
mtd-layout {
    layout@0 {
        label = "default";
        cmdline = "console=ttyS0,115200n1 loglevel=8";
        mtdids = "nmbm0=nmbm0";
        mtdparts = "nmbm0:1024k(bl2),512k(u-boot-env),...";

        boot_part = "firmware";
        factory_part = "firmware";
        sysupgrade_kernel_ubipart = "firmware";
        sysupgrade_rootfs_ubipart = "firmware";
    };
};
```

---

## 6. 注意事项与实践建议

- `mtdids` 和 `mtdparts` **必须同时存在** 才会生效  
- `cmdline` 为空时会清空 `bootargs`
- 若切换布局，建议同步设置 `mtd_layout_label`
- 如果要区分 UBI 与 raw 分区，确保 `boot_part` / `factory_part` 与 `mtdparts` 中分区名一致
