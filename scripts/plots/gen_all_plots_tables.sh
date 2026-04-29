#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <original_json> <custom_json>"
    exit 1
fi

# 生成时间戳，格式：YYYYMMDD_HHMMSS
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

ORIGINAL_JSON="$1"
CUSTOM_JSON="$2"

# 为每个输出路径添加时间戳
TABLE_OUTPUT="table/table_${TIMESTAMP}.tex"
BOX_PLOT_DIR="box_plot/fig_${TIMESTAMP}/"
CURVE_PLOT_DIR="curve_plot/fig_${TIMESTAMP}/"

# 确保输出目录存在
mkdir -p "$(dirname "$TABLE_OUTPUT")"
mkdir -p "$BOX_PLOT_DIR"
mkdir -p "$CURVE_PLOT_DIR"

python3 table/get_table.py "$ORIGINAL_JSON" "$CUSTOM_JSON" "$TABLE_OUTPUT"
python3 box_plot/box_plot_durations.py "$ORIGINAL_JSON" "$CUSTOM_JSON" "$BOX_PLOT_DIR"
python3 curve_plot/curve_plot.py /mydata/pangyue/WDBench/Queries/c2rpqs-connected.txt "$ORIGINAL_JSON" "$CUSTOM_JSON" --output "$CURVE_PLOT_DIR"