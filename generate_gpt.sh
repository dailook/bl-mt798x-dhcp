#!/bin/bash

# 设置输入和输出文件夹路径
input_folder="./mt798x_gpt"
output_folder="./output_gpt"

# 读取VERSION环境变量，默认为2024
# 实际几个 ATF 版本在 GPT 工具上无区别
VERSION=${VERSION:-2024}

if [ "$VERSION" = "2022" ]; then
    tools_folder="./atf-20220606-637ba581b/tools/dev/gpt_editor"
elif [ "$VERSION" = "2023" ]; then
    tools_folder="./atf-20231013-0ea67d76a/tools/dev/gpt_editor"
elif [ "$VERSION" = "2024" ]; then
    tools_folder="./atf-20240117-bacca82a8/tools/dev/gpt_editor"
elif [ "$VERSION" = "2025" ]; then
    tools_folder="./atf-20250711/tools/dev/gpt_editor"
else
    echo "Error: Unsupported VERSION. Please specify VERSION=2022/2023/2024/2025."
    exit 1
fi

echo "Using GPT tools from: $tools_folder"

# 确保输出文件夹存在，不存在则创建
mkdir -p "$output_folder"

# 遍历输入文件夹中的所有json文件
for json_file in "$input_folder"/*.json; do
    # 提取文件名（不包含路径和扩展名）
    filename=$(basename -- "$json_file")
    filename_no_extension="${filename%.*}"

    # 构建输出文件路径
    output_file="$output_folder/gpt-$filename_no_extension.bin"
    output_file_sdmmc="$output_folder/gpt-$filename_no_extension.sdmmc.bin"

    echo
    echo "=============================="
    echo
    echo "正在处理: $filename"
    echo
    echo "=============================="
    echo

    # 执行Python命令
    if [ "$SDMMC" = "1" ]; then
        python2.7 "$tools_folder/mtk_gpt.py" --i "$json_file" --o "$output_file_sdmmc" --sdmmc
    else
        python2.7 "$tools_folder/mtk_gpt.py" --i "$json_file" --o "$output_file"
    fi

    # 输出执行结果
    echo
    echo "=============================="
    echo
    echo "转换完成: $filename"
    echo
    echo "=============================="
    echo
done

echo "所有文件转换完成"