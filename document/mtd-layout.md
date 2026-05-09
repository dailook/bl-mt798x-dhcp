[English](#) | [简体中文](./sc/mtd-layout.md)

# `mtd-layout` Node Parameter Description (U-Boot MTK 2025)

Affected Files:

mtd_layout.c, mtdparts.c, failsafe.c, showlayout.c

## 1. Node Structure and Selection Mechanism

- Root Node Path: `/mtd-layout`
- Child Node Format: `layout@<n>` (e.g., `layout@0`, `layout@1`)
- Select the current layout via the environment variable `mtd_layout_label`
  - If this variable is not set, the layout with `label = "default"` is used by default.
  - See mtd_layout.c for logic.

## 2. Supported Attribute List

The following attributes are read and applied in `layout@*` child nodes:

| Attribute Name | Type | Required | Function |
| --- | --- | --- | --- |
| `label` | string | ✅ | Layout name, used for selection matching |
| `mtdids` | string | ✅ | MTD device mapping string (for mtdparts parsing) |
| `mtdparts` | string | ✅ | Partition description string (for mtdparts parsing) |
| `cmdline` | string | Optional | Writes to `bootargs` as the kernel command line |
| `boot_part` | string | Optional | Writes to the environment variable `ubi_boot_part` |
| `factory_part` | string | Optional | Writes to the environment variable `factory_part` |
| `sysupgrade_kernel_ubipart` | string | Optional | Writes to `sysupgrade_kernel_ubipart` |
| `sysupgrade_rootfs_ubipart` | string | Optional | Writes to `sysupgrade_rootfs_ubipart` |

> These attributes are defined by `board_mtdparts_default()` Read and write environment variables (see mtd_layout.c).

---

## 3. Meaning and Usage of Each Attribute

### `label`

- Used to identify the layout name
- Matches the environment variable `mtd_layout_label`, determining which layout to use
- Default value is `"default"`

### `mtdids`

- Format reference: mtdparts.c

    ```plaintext
    mtdids=<dev-id>=<mtd-id>[,<dev-id>=<mtd-id>...]
    ```

- Example:

    ```plaintext
    mtdids = "nmbm0=nmbm0";
    ```

### `mtdparts`

- Format reference: mtdparts.c

    ```plaintext
    mtdparts=[mtdparts=]<mtd-id>:<part-def>[,<part-def>...]
    <part-def> := <size>[@<offset>][(name)][ro]
    ```

- Example (usage in your DTS):

    ```plaintext
    mtdparts = "nmbm0:1024k(bl2),512k(u-boot-env),... ,35328k(firmware)";
    ```

### `cmdline`

- Written to the environment variable `bootargs`
- Used to pass Linux kernel command lines
- Example:

    ```plaintext
    cmdline = "console=ttyS0,115200n1 ... bootpart=firmware";
    ```

### `boot_part`

- Written to the environment variable `ubi_boot_part`
- Used in `mtd_boot_image()` to determine which partition to boot from
- If you have multi-layout firmware (e.g., firmware/firmware2) (Switch), this is crucial

### `factory_part`

- Written to the environment variable `factory_part`
- Written as UBI to the target partition during the upgrade process `mtd_upgrade_image()`
- Important for multi-layout flashing

### `sysupgrade_kernel_ubipart`

- Written to the environment variable `sysupgrade_kernel_ubipart`
- Used for sysupgrade processing of the **kernel UBI partition** when multi-layout is enabled and `CONFIG_MEDIATEK_MULTI_MTD_LAYOUT` is enabled

### `sysupgrade_rootfs_ubipart`

- Written to the environment variable `sysupgrade_rootfs_ubipart`
- Similar to the above, used for the **rootfs UBI partition** of sysupgrade

---

## 4. Related Commands and Debugging Interfaces

### `showlayout`

- Provided by showlayout.c
- Function: Prints the `label/mtdids/mtdparts` of all `mtd-layout` child nodes

### failsafe HTTP Interface

- Provided in failsafe.c
- Returns the current layout and the labels and mtdparts of all layouts

## In the configuration file

Affected configuration options:

- `CONFIG_MTDIDS_DEFAULT` (default mtdids)
    e.g. `nmbm0=nmbm0`
- `CONFIG_MTDPARTS_DEFAULT` (default mtdparts)
    e.g. `nmbm0:1024k(bl2),512k(u-boot-env),...`
    > When the last partition is not specified in size, e.g., `nmbm0:1024k(bl2),512k(u-boot-env),...-(ubi)` The `-(ubi)` will automatically calculate the remaining space as the size of that partition. After that, no other partitions can be added to that partition.
- `CONFIG_CMD_MTDPARTS`
- `CONFIG_CMD_SHOW_MTD_LAYOUT`
- `CONFIG_MEDIATEK_MULTI_MTD_LAYOUT`

---

## 5. Sample template

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

## 6. Precautions and Practical Suggestions

- `mtdids` and `mtdparts` **must both exist** to take effect.
- `cmdline` being empty will clear `bootargs`.
- If switching layouts, it is recommended to set `mtd_layout_label` synchronously.
- To distinguish between UBI and raw partitions, ensure that the partition names in `boot_part` / `factory_part` are consistent with those in `mtdparts`.
