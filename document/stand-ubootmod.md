[English](#) | [简体中文](./sc/stand-ubootmod.md)

# Standard `ubootmod` mode

For boards that use MTD devices, switching to the `ubootmod` variant does **not** mean you can immediately use `ubootmod` mode without additional work.

> `ubootmod` mode is a special U-Boot mode. What it actually supports is the `OpenWrt U-Boot layout`.

It does **not** mean you can directly flash images from upstream/mainline. The following conditions must be met:

- The device tree must correctly define the nodes and properties required for `ubootmod` mode.
- The device tree must correctly define the nodes and properties for the MTD devices.
- U-Boot must support `ubootmod` mode, and it must be configured and built correctly with that mode enabled.

If all of the above are satisfied, in theory you should be able to use `ubootmod` mode to flash upstream/mainline images. In practice, the outcome may vary depending on the specific hardware platform, U-Boot version, and device tree configuration. Therefore, after switching to the `ubootmod` variant, it is strongly recommended to carefully verify the device tree configuration and U-Boot support status to ensure you can successfully flash upstream/mainline images in `ubootmod` mode.

## Firmware requirements

1. The firmware must be built using the `OpenWrt U-Boot layout` format.
2. [Optional] The firmware must disable NMBM support.

### OpenWrt U-Boot layout

The `&chosen` node must include the following properties:

- `rootdisk = <&ubi_fit_volume>;`
    A phandle pointing to a UBI volume, indicating which UBI volume contains the root filesystem.
- [Optional] `bootargs = "root=/dev/fit0 rootwait";`
    Kernel boot arguments telling the kernel to load the root filesystem from the `fit0` device and wait until the device is ready.

The `&partitions` node must include the following partition/volume:

e.g.:

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

Other [optional] volumes/nodes:

e.g.:

```dts
ubi_ubootenv: ubi-volume-ubootenv {
    volname = "ubootenv";
};

ubi_ubootenv2: ubi-volume-ubootenv2 {
    volname = "ubootenv2";
};
```

If the two `ubi_ubootenv` volumes above are present, the following nodes are [optional] but also required:

e.g.:

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

### Makefile-related configuration

- Remove the configuration related to `IMAGE`.
- Add the following configuration:

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

## U-Boot requirements

This project has already migrated the relevant device configurations in principle; however, due to the large number of models, it cannot guarantee that every model has been migrated perfectly. Therefore, after switching to the `ubootmod` variant, it is recommended to carefully verify the device tree configuration and U-Boot support status to ensure you can successfully flash upstream/mainline images in `ubootmod` mode.

- Remove configuration related to `CONFIG_ENV_SIZE` and `CONFIG_ENV_OFFSET`.
- Remove configuration related to `CONFIG_ENV_IS_IN_MTD` and `CONFIG_ENV_SIZE_REDUND`.
- Remove configuration related to `CONFIG_ENV_MTD_DEV` / `CONFIG_ENV_MTD_NAME`.
- [Optional] Remove/Modify configuration related to `NMBM`

- Add the following configuration:

```makefile
CONFIG_ENV_IS_IN_UBI=y
CONFIG_ENV_SIZE=0x1f000 # follow your needs, e.g., 0x20000
CONFIG_SYS_REDUNDAND_ENVIRONMENT=y
CONFIG_ENV_UBI_PART="ubi"
CONFIG_ENV_UBI_VOLUME="ubootenv"
CONFIG_ENV_UBI_VOLUME_REDUND="ubootenv2"
CONFIG_ENV_UBI_VOLUME_CREATE=y
CONFIG_MTK_BOOTMENU_UBI=y
```

[optional] configurations:

```makefile
CONFIG_MTK_UBI_RESERVED_VOLUMES="factory=2m" # default enbaled, follow your needs to adjust the reserved volume size and name
CONFIG_MTK_DEFAULT_FIT_BOOT_CONF="mt7981-rfb-spim-nand"
CONFIG_UBI_SILENCE_MSG=y
```

You must correctly configure the partition table, keeping it consistent with the partition table used in the firmware, so that U-Boot can correctly recognize and access the UBI volumes.

For details, see the relevant notes in [mtd-layout.md](./mtd-layout.md).
