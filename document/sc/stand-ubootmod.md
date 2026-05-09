[English](../stand-ubootmod.md) | [简体中文](#)

# 标准 ubootmod 模式

对于使用 MTD 设备的板型，切换到 ubootmod 变体后，并非意味着能够直接使用 ubootmod 模式。

> ubootmod 模式是一种特殊的 U-Boot 模式，实际支持的是 `OpenWrt U-Boot layout`。

不代表能直接刷写来自主线的镜像，必须满足以下条件：

- 需要在设备树中正确配置 ubootmod 模式的相关节点和属性。
- 需要在设备树中正确配置 MTD 设备的相关节点和属性。
- U-Boot 需要支持 ubootmod 模式，并且需要正确配置和编译 U-Boot 以启用该模式。

如果满足以上条件，理论上应该能够使用 ubootmod 模式来刷写来自主线的镜像。然而，实际情况可能会因具体的硬件平台、U-Boot 版本和设备树配置而有所不同。因此，在切换到 ubootmod 变体后，建议仔细检查设备树配置和 U-Boot 的支持情况，以确保能够成功使用 ubootmod 模式来刷写来自主线的镜像。

## 对于固件的要求

1. 固件必须使用 `OpenWrt U-Boot layout` 格式进行构建。
2. [可选] 固件必须禁用 NMBM 支持。

### OpenWrt U-Boot layout

`&chosen` 节点下必须包含以下属性：

- rootdisk = <&ubi_fit_volume>;
    指向 UBI 卷的 phandle，表示根文件系统所在的 UBI 卷。
- [可选] bootargs = "root=/dev/fit0 rootwait";
    指定内核启动参数，告诉内核从 fit0 设备加载根文件系统，并等待设备准备就绪。

`&partitions` 节点下必须包含以下分区/卷：

e.g.：

```dts
partition@580000 {
    compatible = "linux,ubi";
    label = "ubi";
    reg = <0x580000 0x7a80000>;

    volumes {
        ubi_rootdisk: ubi-volume-fit {
            volname = "fit";
        };
    };
};
```

其他 [可选] 卷、节点：

e.g.：

```dts
ubi_ubootenv: ubi-volume-ubootenv {
    volname = "ubootenv";
};

ubi_ubootenv2: ubi-volume-ubootenv2 {
    volname = "ubootenv2";
};
```

如果包含上面两个 ubi_ubootenv 卷，[可选] 还需以下节点：

e.g.：

```dts
&ubi_ubootenv {
    nvmem-layout {
        compatible = "u-boot,env-redundant-bool";
    };
};

&ubi_ubootenv2 {
    nvmem-layout {
        compatible = "u-boot,env-redundant-bool";
    };
};
```

### Makefile 相关配置

- 删除 IMAGE 相关的配置。
- 添加以下配置：

```makefile
KERNEL_IN_UBI := 1
UBOOTENV_IN_UBI := 1
IMAGES := sysupgrade.itb
KERNEL_INITRAMFS_SUFFIX := -recovery.itb
KERNEL := kernel-bin | gzip
KERNEL_INITRAMFS := kernel-bin | lzma | \
    fit lzma $$(KDIR)/image-$$(firstword $$(DEVICE_DTS)).dtb with-initrd | pad-to 64k
IMAGE/sysupgrade.itb := append-kernel | \
    fit gzip $$(KDIR)/image-$$(firstword $$(DEVICE_DTS)).dtb external-static-with-rootfs | append-metadata
ARTIFACTS := preloader.bin bl31-uboot.fip
ARTIFACT/preloader.bin := mt7981-bl2 spim-nand-ddr3
ARTIFACT/bl31-uboot.fip := mt7981-bl31-uboot board_name
```

## 对于 U-Boot 的要求

本项目实际已经完成相关设备配置的迁移，但由于型号过多，无法保证所有型号都正确迁移了相关配置，因此在切换到 ubootmod 变体后，建议仔细检查设备树配置和 U-Boot 的支持情况，以确保能够成功使用 ubootmod 模式来刷写来自主线的镜像。

- 删除 `CONFIG_ENV_SIZE` 和 `CONFIG_ENV_OFFSET` 相关的配置。
- 删除 `CONFIG_ENV_IS_IN_MTD` 和 `CONFIG_ENV_SIZE_REDUND` 相关的配置。
- 删除 `CONFIG_ENV_MTD_DEV`/`CONFIG_ENV_MTD_NAME` 相关的配置。
- [可选] 删除/调整与 `NMBM` 相关的配置。

- 添加以下配置：

```makefile
CONFIG_ENV_IS_IN_UBI=y
CONFIG_ENV_SIZE=0x1f000 # 或者根据实际情况设置合适的大小，e.g. 0x20000
CONFIG_SYS_REDUNDAND_ENVIRONMENT=y
CONFIG_ENV_UBI_PART="ubi"
CONFIG_ENV_UBI_VOLUME="ubootenv"
CONFIG_ENV_UBI_VOLUME_REDUND="ubootenv2"
CONFIG_ENV_UBI_VOLUME_CREATE=y
CONFIG_MTK_BOOTMENU_UBI=y
```

[可选] 配置：

```makefile
CONFIG_MTK_UBI_RESERVED_VOLUMES="factory=2m" # 默认启用，根据实际情况调整大小和名称
CONFIG_MTK_DEFAULT_FIT_BOOT_CONF="mt7981-rfb-spim-nand"
CONFIG_UBI_SILENCE_MSG=y
```

需要正确配置分区表，与固件中配置的分区表保持一致，确保 U-Boot 能够正确识别和访问 UBI 卷。

具体可以查看 [mtd-layout.md](./mtd-layout.md) 文档中的相关说明。
