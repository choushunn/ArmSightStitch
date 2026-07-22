"""
流水线（bbox_nms 专用）：
  原图 → CLAHE 增强 → Sobel + 膨胀 → 轮廓 → NMS 去重叠 → 加框图（保存）
输出到 test2/bbox_nms.jpg
"""

import numpy as np
from PIL import Image
import cv2
from pathlib import Path

# ===== 配置 =====
img_path = r"d:\项目文件\灰尘检测2\data01\2026-07-21_20-39-35\negative\4_8_43702.jpg"
output_dir = Path(r"d:\项目文件\灰尘检测2\test2")
output_dir.mkdir(parents=True, exist_ok=True)

# NMS 参数
iou_threshold = 0.4
containment_threshold = 0.5
min_bbox_area = 25  # 过滤 w*h < 25 的极小方框

# ===== 1. 读取原图 =====
pil_img = Image.open(img_path)
img_gray = np.array(pil_img.convert("L"))
img_color = np.array(pil_img.convert("RGB"))

# ===== 2. CLAHE 增强 =====
clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
img_clahe = clahe.apply(img_gray)

# ===== 3. Sobel 边缘提取 + 二值化 + 膨胀 =====
sobel_x = cv2.Sobel(img_clahe, cv2.CV_64F, 1, 0, ksize=3)
sobel_y = cv2.Sobel(img_clahe, cv2.CV_64F, 0, 1, ksize=3)
sobel_mag = np.uint8(np.clip(np.sqrt(sobel_x**2 + sobel_y**2), 0, 255))
edges_sobel = (sobel_mag > 30).astype(np.uint8) * 255

kernel = np.ones((3, 3), np.uint8)
edges_sobel_dilated = cv2.dilate(edges_sobel, kernel, iterations=1)

# ===== 4. 查找轮廓 =====
contours, _ = cv2.findContours(edges_sobel_dilated, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

# ===== 5. NMS 函数 =====
def nms(boxes, scores, iou_threshold=0.4, containment_threshold=0.5):
    if len(boxes) == 0:
        return []
    boxes = np.array(boxes, dtype=np.float32)
    scores = np.array(scores)
    x1 = boxes[:, 0]
    y1 = boxes[:, 1]
    x2 = boxes[:, 0] + boxes[:, 2]
    y2 = boxes[:, 1] + boxes[:, 3]
    areas = (x2 - x1 + 1) * (y2 - y1 + 1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1 + 1)
        h = np.maximum(0.0, yy2 - yy1 + 1)
        inter = w * h
        iou = inter / (areas[i] + areas[order[1:]] - inter)
        min_area = np.minimum(areas[i], areas[order[1:]])
        containment = np.zeros_like(iou)
        mask = min_area > 0
        containment[mask] = inter[mask] / min_area[mask]
        suppress = (iou > iou_threshold) | (containment > containment_threshold)
        inds = np.where(~suppress)[0]
        order = order[inds + 1]
    return keep

# ===== 6. 过滤 + NMS + 绘制方框（保存）=====
bboxes = []
scores = []
for cnt in contours:
    x, y, w, h = cv2.boundingRect(cnt)
    if w * h < min_bbox_area:
        continue
    area = cv2.contourArea(cnt)
    bboxes.append([x, y, w, h])
    scores.append(area)

keep = nms(bboxes, scores, iou_threshold, containment_threshold)

bbox_nms = img_color.copy()
for idx in keep:
    x, y, w, h = bboxes[idx]
    cv2.rectangle(bbox_nms, (x, y), (x + w, y + h), (0, 255, 0), 2)
cv2.imencode(".jpg", cv2.cvtColor(bbox_nms, cv2.COLOR_RGB2BGR))[1].tofile(
    str(output_dir / "bbox_nms.jpg"))

print(f"NMS 加框图: {output_dir / 'bbox_nms.jpg'}")
print("完成")