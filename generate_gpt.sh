#!/bin/bash

input_folder="./mt798x_gpt"
input_folder_show="./mt798x_gpt_bin"
output_folder="./output_gpt"

VERSION=${VERSION:-2025}

if [ "$VERSION" = "2022" ]; then
    tools_folder="./atf-20220606-637ba581b/tools/dev/gpt_editor"
elif [ "$VERSION" = "2023" ]; then
    tools_folder="./atf-20231013-0ea67d76a/tools/dev/gpt_editor"
elif [ "$VERSION" = "2024" ]; then
    tools_folder="./atf-20240117-bacca82a8/tools/dev/gpt_editor"
elif [ "$VERSION" = "2025" ]; then
    tools_folder="./atf-20250711/tools/dev/gpt_editor"
elif [ "$VERSION" = "2026" ]; then
    tools_folder="./atf-20260123/tools/dev/gpt_editor"
else
    echo "Error: Unsupported VERSION. Please specify VERSION=2022/2023/2024/2025/2026."
    exit 1
fi

# Check if Python is installed on the system
echo "Trying python2.7..."
command -v python2.7
[ "$?" != "0" ] && { echo "Error: Python2.7 is not installed on this system."; exit 0; }

echo "Using GPT tools from: $tools_folder"

mkdir -p "$output_folder"
mkdir -p "$output_folder/picture"
mkdir -p "$output_folder/info"

# Success and failure counters
built_count=0
fail_count=0
png_built_count=0
png_fail_count=0

if [ "$DRAW" = "1" ]; then
    echo "Trying python3..."
    command -v python3
    [ "$?" != "0" ] && { echo "Error: Python3 is not installed on this system."; exit 0; }
fi

if [ "$SHOW" = "1" ]; then
    for bin_file in "$input_folder_show"/*.bin "$input_folder_show"/*.img; do
        [ -e "$bin_file" ] || continue

        filename=$(basename -- "$bin_file")
        filename_no_extension="${filename%.*}"

        output_file="$output_folder/info/${filename_no_extension}_gptinfo.txt"

        echo
        echo "=============================="
        echo
        echo "Processing: $filename"
        echo
        echo "=============================="
        echo

        python2.7 "$tools_folder/mtk_gpt.py" --show "$bin_file" > "$output_file"

        if [ -f "$output_file" ]; then
            echo "Done: $filename, info written to: $output_file"
            built_count=$((built_count + 1))
        else
            echo "Failed: $filename (output not found: $output_file)"
            fail_count=$((fail_count + 1))
        fi

        echo
        echo "=============================="
        echo
    done

    echo "All files processed"
    echo "Success: $built_count  Failed: $fail_count"
else
    for json_file in "$input_folder"/*.json; do
        filename=$(basename -- "$json_file")
        filename_no_extension="${filename%.*}"

        output_file="$output_folder/gpt-$filename_no_extension.bin"
        output_file_sdmmc="$output_folder/gpt-$filename_no_extension.sdmmc.bin"
        output_png="$output_folder/picture/gpt-$filename_no_extension.png"

        echo
        echo "=============================="
        echo
        echo "Processing: $filename"
        echo
        echo "=============================="
        echo

        if [ "$SDMMC" = "1" ]; then
            python2.7 "$tools_folder/mtk_gpt.py" --i "$json_file" --o "$output_file_sdmmc" --sdmmc
            built_out_file_raw="$output_file_sdmmc"
        else
            python2.7 "$tools_folder/mtk_gpt.py" --i "$json_file" --o "$output_file"
            built_out_file_raw="$output_file"
        fi

        if [ "$DRAW" = "notitle" ]; then
            python3 "$tools_folder/partition_layout.py" --i "$json_file" --o "$output_png"
        fi
        if [ "$DRAW" = "1" ]; then
            python3 "$tools_folder/partition_layout.py" --i "$json_file" --o "$output_png" --title
        fi

        if [ -f "$built_out_file_raw" ]; then
            gpt_md5=$(md5sum "$built_out_file_raw" | awk '{print $1}')
            built_base=$(basename -- "$built_out_file_raw")
            built_name_no_extension="${built_base%.*}"
            built_extension="${built_base##*.}"
            built_out_file="$output_folder/${built_name_no_extension}-Yuzhii_md5-${gpt_md5}.${built_extension}"
            mv -f "$built_out_file_raw" "$built_out_file"
            echo "Built: $built_out_file"
            built_count=$((built_count + 1))
        else
            echo "Error: output not found: $built_out_file_raw"
            fail_count=$((fail_count + 1))
        fi

        if [ "$DRAW" = "1" ]; then
            if [ -f "$output_png" ]; then
                echo "Built: $output_png"
                png_built_count=$((png_built_count + 1))
            else
                echo "Error: output not found: $output_png"
                png_fail_count=$((png_fail_count + 1))
            fi
        fi

        echo
        echo "=============================="
        echo
        echo "Converted: $filename"
        echo
        echo "=============================="
        echo
    done

    echo "All files converted"
    echo "GPT bin Success: $built_count  Failed: $fail_count"
    if [ "$DRAW" = "1" ]; then
        echo "PNG Success: $png_built_count  Failed: $png_fail_count"
    fi
fi