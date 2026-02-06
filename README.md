# 适用于 MT798x 的 ATF 与 U-Boot（带 DHCPD）U-boot-2025版本

基于 hanwckf 的 MT798x U-Boot，由 Yuzhii 二次开发，支持 DHCPD，WEB UI由dailook再次修改

警告：刷写自定义引导加载程序可能导致设备变砖，风险自负，请谨慎操作。

## 关于 bl-mt798x

- <https://cmi.hanwckf.top/p/mt798x-uboot-usage>

[U-boot-2025版本]

## 准备环境

```bash
sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static
```
## 克隆U-boot源码

```bash
git clone -b 2025 --single-branch --filter=blob:none https://github.com/dailook/bl-mt798x-dhcp
```

## 本地编译命令示例

```bash
chmod +x build.sh
SOC=mt7981 BOARD=sn_r1 VERSION=2025 ./build.sh
SOC=mt7981 BOARD=cmcc_a10 VERSION=2025 MULTI_LAYOUT=1 ./build.sh
SOC=mt7986 BOARD=clx_s20l VERSION=2025 ./build.sh
SOC=mt7986 BOARD=clx_s20p VERSION=2025 ./build.sh
```

- SOC=mt7981/mt7986
- VERSION=2025
- MULTI_LAYOUT=1 （可选，仅多 layout 设备需加，例如 xiaomi-wr30u、redmi-ax6000）

生成文件位于 output 目录

## 使用 Python2.7 生成 GPT 镜像

> 安装依赖

```bash
sudo apt-get install python2-dev swig
```

> 执行

```bash
chmod +x generate_gpt.sh
./generate_gpt.sh
```

生成文件位于 output_gpt 目录。

> 需在 mt798x_gpt 目录内放置对应设备的分区表 JSON，例如 atf-dir/tools/dev/gpt_editor/example/gpt.json。

若启用 SDMMC=1（如 SDMMC=1 ./generate_gpt.sh），生成的 GPT 镜像将支持 MTK SDMMC。

### 查看 GPT 信息

在仓库根目录新建 mt798x_gpt_bin 文件夹，把 GPT 二进制文件放入其中，然后运行：

```bash
chmod +x show_gpt.sh
./show_gpt.sh
```

脚本会解析 mt798x_gpt_bin 内所有 GPT 文件，并将分区信息输出到 output_gpt/gpt_info.txt。

## 更换U-boot背景图

```bash
替换路径"uboot-mtk-20250711/failsafe/fsdata/bg.jpg"的bg.jpg文件，文件大小控制在200kB大小以内
```
