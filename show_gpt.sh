#!/bin/bash

# 设置输入和输出文件夹路径
input_folder="./mt798x_gpt_bin"
output_folder="./output_gpt"

# 读取VERSION环境变量，默认为2024
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

# 遍历输入文件夹中的所有 bin/img 文件
for bin_file in "$input_folder"/*.bin "$input_folder"/*.img; do
    # 如果文件不存在，跳过
    [ -e "$bin_file" ] || continue

    # 提取文件名（不包含路径和扩展名）
    filename=$(basename -- "$bin_file")
    filename_no_extension="${filename%.*}"

    # 构建输出文本文件路径
    output_file="$output_folder/${filename_no_extension}_gptinfo.txt"

    echo
    echo "=============================="
    echo
    echo "正在处理: $filename"
    echo
    echo "=============================="
    echo

    # 执行 Python 命令获取 gpt 信息，并写入输出文本
    python2.7 "$tools_folder/mtk_gpt.py" --show "$bin_file" > "$output_file"

    if [ $? -eq 0 ]; then
        echo "处理完成: $filename，信息已写入：$output_file"
    else
        echo "处理失败: $filename"
    fi

    echo
    echo "=============================="
    echo
done

echo "所有文件处理完成"