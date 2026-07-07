import os
import re
from PIL import Image

# 图片目录
img_dir = r".\\2026-07-07_15-34-30"

# 解析文件名中的行列号
def parse_row_col(filename):
    """从文件名中解析出行号和列号，支持 0_0.jpg、0_0 .jpg 等格式"""
    name = filename.replace(" ", "")  # 去除空格
    # 匹配 row_col.jpg 或 row_col (2).jpg 等
    match = re.match(r"(\d+)_(\d+)", name)
    if match:
        return int(match.group(1)), int(match.group(2))
    return None, None

# 收集所有图片
tiles = {}  # (row, col) -> image path
for f in os.listdir(img_dir):
    if f.lower().endswith((".jpg", ".jpeg", ".png")):
        r, c = parse_row_col(f)
        if r is not None and c is not None:
            tiles[(r, c)] = os.path.join(img_dir, f)

if not tiles:
    print("未找到任何图片文件")
    exit(1)

# 确定网格尺寸
max_row = max(r for r, c in tiles)
max_col = max(c for r, c in tiles)
print(f"检测到网格: {max_row+1} 行 x {max_col+1} 列, 共 {len(tiles)} 张图片")

# 读取图片统一尺寸（取任意一张为参考）
sample = Image.open(next(iter(tiles.values())))
tile_w, tile_h = sample.size
print(f"单张图片尺寸: {tile_w} x {tile_h}")

# 裁剪参数：取中心 1775x1775 方形
crop_size = 1775
left = (tile_w - crop_size) // 2
top = (tile_h - crop_size) // 2
right = left + crop_size
bottom = top + crop_size
print(f"裁剪区域: ({left}, {top}) -> ({right}, {bottom}), 尺寸: {crop_size}x{crop_size}")

# 创建画布
canvas = Image.new("RGB", (crop_size * (max_col + 1), crop_size * (max_row + 1)))

# 逐张裁剪并粘贴
for (r, c), path in sorted(tiles.items()):
    img = Image.open(path).crop((left, top, right, bottom))
    canvas.paste(img, (c * crop_size, r * crop_size))
    print(f"  粘贴 [{r},{c}] -> ({c*crop_size}, {r*crop_size})")

# 保存结果
output_path = os.path.join(img_dir, "..", "stitched_result.jpg")
canvas.save(output_path, quality=95)
print(f"\n拼接完成! 结果保存至: {os.path.abspath(output_path)}")
print(f"最终图像尺寸: {canvas.size}")
