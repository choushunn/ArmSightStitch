"""
灰尘颗粒检测流程（支持反复迭代检测）
读取原始图像 → CLAHE 对比度增强 → 背景减除法提取暗斑 → 连通域过滤噪声
→ 填充已检测区域 → 反复检测 → 合并结果输出
"""
import cv2
import numpy as np
from PIL import Image
import os


def _single_pass(
    gray: np.ndarray,
    bg_blur_size: int,
    min_area: int,
    dilate_iterations: int,
    compute_labels: bool = False,
):
    """单次检测：背景减除 → Otsu 阈值 → 形态学去噪 → 连通域过滤 → 膨胀
    返回 (mask, bg, num_labels, labels, stats)。仅当 compute_labels=True（调用方需要
    做 max_area 过滤）时才对 mask 重算连通域，否则返回 (mask, bg, 0, None, None) 避免白算。"""
    # 背景估计（gray 已是 float32，无需重复转换）；GaussianBlur 要求奇数核
    bg_blur_size |= 1
    bg = cv2.GaussianBlur(gray, (bg_blur_size, bg_blur_size), bg_blur_size // 2)
    residual = gray - bg
    dark_spots = np.clip(-residual, 0, None).astype(np.uint8)

    # Otsu 阈值
    _, spots_bin = cv2.threshold(dark_spots, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)

    # 形态学去噪
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    spots_clean = cv2.morphologyEx(spots_bin, cv2.MORPH_OPEN, kernel, iterations=2)
    spots_clean = cv2.morphologyEx(spots_clean, cv2.MORPH_CLOSE, kernel, iterations=1)

    # 连通域面积过滤（min_area）
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(spots_clean, connectivity=8)
    mask = np.zeros_like(spots_clean)
    for i in range(1, num_labels):
        if stats[i, cv2.CC_STAT_AREA] >= min_area:
            mask[labels == i] = 255

    # 仅在调用方需要做 max_area 过滤时，才对过滤后的 mask 重算连通域（避免重复扫描全图）
    if compute_labels:
        num_labels_m, labels_m, stats_m, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    else:
        num_labels_m, labels_m, stats_m = 0, None, None

    # 膨胀（iterations=0 时跳过，保持紧凑边界避免跨颗粒粘连）
    if dilate_iterations > 0:
        dilate_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
        mask = cv2.dilate(mask, dilate_kernel, iterations=dilate_iterations)

    return mask, bg, num_labels_m, labels_m, stats_m


def _merge_boxes(components: list, iou_threshold: float) -> list:
    """对检测框做贪心合并：如果两框的 IoU > threshold，则合并为一个包围框"""
    if len(components) <= 1 or iou_threshold <= 0:
        return components

    # 按面积降序排列，大框优先
    sorted_comp = sorted(components, key=lambda c: c["area"], reverse=True)
    suppressed = [False] * len(sorted_comp)
    merged = []

    for i in range(len(sorted_comp)):
        if suppressed[i]:
            continue
        x1, y1, w1, h1 = sorted_comp[i]["bbox"]
        box = [x1, y1, x1 + w1, y1 + h1]  # [x1, y1, x2, y2]
        pix_area = sorted_comp[i]["area"]  # 累加真实像素面积，而非包围框面积

        for j in range(i + 1, len(sorted_comp)):
            if suppressed[j]:
                continue
            x2, y2, w2, h2 = sorted_comp[j]["bbox"]

            # 计算 IoU，分母取小框面积避免大框吞并远小框
            ix1, iy1 = max(box[0], x2), max(box[1], y2)
            ix2, iy2 = min(box[2], x2 + w2), min(box[3], y2 + h2)
            if ix2 > ix1 and iy2 > iy1:
                inter = (ix2 - ix1) * (iy2 - iy1)
                area_a = (box[2] - box[0]) * (box[3] - box[1])
                area_b = w2 * h2
                iou = inter / min(area_a, area_b)
                if iou > iou_threshold:
                    box[0] = min(box[0], x2)
                    box[1] = min(box[1], y2)
                    box[2] = max(box[2], x2 + w2)
                    box[3] = max(box[3], y2 + h2)
                    pix_area += sorted_comp[j]["area"]
                    suppressed[j] = True

        merged.append({
            "bbox": (box[0], box[1], box[2] - box[0], box[3] - box[1]),
            "area": pix_area,  # 合并后颗粒的真实像素面积（口径与未合并时一致）
        })

    return merged


def detect_dust(
    image_path: str,
    output_dir: str = "output",
    clahe_clip_limit: float = 2.0,
    bg_blur_size: int = 31,
    min_area: int = 50,
    dilate_iterations: int = 0,
    max_iterations: int = 3,
    max_area: int = 0,
    nms_iou_threshold: float = 0.0,
    save_intermediate: bool = True,
):
    """
    迭代检测图像中的灰尘颗粒（暗斑）。
    每轮检测后，将已检测区域填充为背景色，再进行下一轮，直至到达次数上限或检测不出新颗粒。

    参数:
        image_path:       输入图像路径
        output_dir:       输出目录
        clahe_clip_limit: CLAHE 对比度限制参数
        bg_blur_size:     背景估计的高斯核大小（奇数）
        min_area:         连通域最小面积（像素），小于此值的视为噪声
        dilate_iterations: mask 膨胀次数
        max_iterations:   最大迭代检测次数
        max_area:         面积上限（像素），面积 >= 此值的将被过滤掉，0 表示不过滤
        nms_iou_threshold: NMS 合并阈值（0~1），IoU 超过此值的重叠框将被合并，0 表示不合并
        save_intermediate: 是否保存中间结果
    返回:
        final_mask:       合并后的最终二值掩膜 (numpy array)
        all_components:   所有迭代次数的检测结果列表
    """
    os.makedirs(output_dir, exist_ok=True)
    basename = os.path.splitext(os.path.basename(image_path))[0]
    bg_blur_size |= 1  # GaussianBlur 要求奇数核，偶数入参兜底为奇数

    # ---- 1. 读取图像（PIL 支持中文路径） ----
    pil_img = Image.open(image_path).convert("RGB")
    img_bgr = cv2.cvtColor(np.array(pil_img), cv2.COLOR_RGB2BGR)
    print(f"读取图像: {image_path}  ({img_bgr.shape[1]}x{img_bgr.shape[0]})")

    # ---- 2. 转灰度 + CLAHE 对比度增强 ----
    gray = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2GRAY)
    clahe = cv2.createCLAHE(clipLimit=clahe_clip_limit, tileGridSize=(8, 8))
    gray_enh = clahe.apply(gray)
    img_enh = cv2.cvtColor(gray_enh, cv2.COLOR_GRAY2BGR)  # 用于可视化的3通道灰度图

    # ---- 3. 迭代检测 ----
    gray_current = gray_enh.astype(np.float32)
    final_mask = np.zeros(gray_current.shape, dtype=np.uint8)
    all_components = []
    iteration_masks = []

    for iteration in range(1, max_iterations + 1):
        # 单次检测（gray_current 已是 float32，_single_pass 内部不再重复转换）
        # 仅当启用 max_area 过滤时才要求 _single_pass 重算连通域
        mask, bg_estimate, num_labels_iter, labels_iter, stats_iter = _single_pass(
            gray_current, bg_blur_size, min_area, dilate_iterations,
            compute_labels=(max_area > 0)
        )

        # ---- 对本轮 mask 做 max_area 过滤（复用 _single_pass 返回的连通域信息） ----
        if max_area > 0:
            mask_accepted = np.zeros_like(mask)
            for i in range(1, num_labels_iter):
                if stats_iter[i, cv2.CC_STAT_AREA] < max_area:
                    mask_accepted[labels_iter == i] = 255
        else:
            mask_accepted = mask

        new_pixels = cv2.countNonZero(mask_accepted)
        if new_pixels == 0:
            print(f"  第 {iteration} 次迭代: 未检测到新颗粒（过滤后），停止迭代")
            break

        # 只记录本次新增部分（排除已有区域）
        already_detected = final_mask > 0
        new_mask = mask_accepted.copy()
        new_mask[already_detected] = 0
        new_count = cv2.countNonZero(new_mask)

        print(f"  第 {iteration} 次迭代: 检测到 {new_count} 个新像素")

        if new_count == 0:
            break

        # 合并到最终 mask（使用过滤后的 mask）
        final_mask = cv2.bitwise_or(final_mask, mask_accepted)
        iteration_masks.append(mask_accepted)

        # 保存本轮中间结果
        if save_intermediate:
            iter_mask_path = os.path.join(output_dir, f"{basename}_iter{iteration}_mask.jpg")
            Image.fromarray(mask_accepted).save(iter_mask_path)

            # 本轮累积检测框（包含前几轮结果）
            cnts, _ = cv2.findContours(final_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            img_iter = img_enh.copy()
            for c in cnts:
                x, y, w, h = cv2.boundingRect(c)
                cv2.rectangle(img_iter, (x, y), (x + w, y + h), (0, 0, 255), 1)
            iter_det_path = os.path.join(output_dir, f"{basename}_iter{iteration}_detected.jpg")
            Image.fromarray(cv2.cvtColor(img_iter, cv2.COLOR_BGR2RGB)).save(iter_det_path, quality=95)

        # ---- 填充已检测区域为背景色（使用原始mask填充，确保所有检出区域都被覆盖） ----
        fill_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
        fill_mask = cv2.dilate(mask, fill_kernel, iterations=1)

        # 用背景估计值填充
        bg_fill = np.clip(bg_estimate, 0, 255).astype(np.float32)
        gray_current = np.where(fill_mask > 0, bg_fill, gray_current)

        # 对填充边界做平滑过渡（减小模糊核，避免过度侵蚀邻近颗粒）
        blur_mask = cv2.GaussianBlur(fill_mask.astype(np.float32), (7, 7), 3)
        blur_mask = blur_mask / 255.0
        gray_current = gray_current * (1 - blur_mask) + bg_fill * blur_mask

    # ---- 4. 最终面积过滤：逐轮过滤后合并的 mask 中，跨轮粘连的连通域仍需再次过滤 ----
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(final_mask, connectivity=8)
    all_components = []
    filtered_mask = np.zeros_like(final_mask)
    for i in range(1, num_labels):
        area = int(stats[i, cv2.CC_STAT_AREA])
        if max_area > 0 and area >= max_area:
            continue
        all_components.append({
            "area": area,
            "bbox": (
                int(stats[i, cv2.CC_STAT_LEFT]),
                int(stats[i, cv2.CC_STAT_TOP]),
                int(stats[i, cv2.CC_STAT_WIDTH]),
                int(stats[i, cv2.CC_STAT_HEIGHT]),
            ),
        })
        filtered_mask[labels == i] = 255

    final_mask = filtered_mask

    # ---- 4.5 NMS 合并重叠检测框 ----
    if nms_iou_threshold > 0:
        before_nms = len(all_components)
        all_components = _merge_boxes(all_components, nms_iou_threshold)
        print(f"NMS 合并: {before_nms} → {len(all_components)} (IoU > {nms_iou_threshold})")

        # 用合并后的框重建 final_mask：仅保留框内原有的颗粒像素，避免整框填白使 mask 退化为矩形块
        pre_nms_mask = final_mask
        final_mask = np.zeros_like(pre_nms_mask)
        for comp in all_components:
            x, y, w, h = comp["bbox"]
            final_mask[y:y + h, x:x + w] = pre_nms_mask[y:y + h, x:x + w]

    print(f"\n检测完成: 共 {len(all_components)} 个灰尘颗粒")

    # ---- 5. 面积统计（前 20 大） ----
    sorted_components = sorted(all_components, key=lambda c: c["area"], reverse=True)
    print("\n面积最大的前 20 个颗粒:")
    print(f"{'序号':>4} | {'面积(px)':>10} | {'位置(x,y,w,h)':>25}")
    print("-" * 50)
    img_top20 = img_enh.copy()
    for rank, comp in enumerate(sorted_components[:20], start=1):
        x, y, w, h = comp["bbox"]
        print(f"{rank:>4} | {comp['area']:>10} | ({x},{y},{w},{h})")
        cv2.rectangle(img_top20, (x, y), (x + w, y + h), (0, 0, 255), 2)
        cv2.putText(img_top20, str(rank), (x + 2, y + 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)
    top20_path = os.path.join(output_dir, f"{basename}_top20.jpg")
    Image.fromarray(cv2.cvtColor(img_top20, cv2.COLOR_BGR2RGB)).save(top20_path, quality=95)
    print(f"前 20 大颗粒标注图已保存: {top20_path}")

    # ---- 6. 输出最终结果 ----
    mask_path = os.path.join(output_dir, f"{basename}_mask.jpg")
    Image.fromarray(final_mask).save(mask_path)
    print(f"mask 已保存: {mask_path}")

    # 轮廓 + 矩形框
    contours, _ = cv2.findContours(final_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    img_detected = img_enh.copy()
    for cnt in contours:
        x, y, w, h = cv2.boundingRect(cnt)
        cv2.rectangle(img_detected, (x, y), (x + w, y + h), (0, 0, 255), 1)
        cv2.drawContours(img_detected, [cnt], -1, (0, 255, 0), 1)
    detected_path = os.path.join(output_dir, f"{basename}_detected.jpg")
    Image.fromarray(cv2.cvtColor(img_detected, cv2.COLOR_BGR2RGB)).save(detected_path, quality=95)
    print(f"检测结果已保存: {detected_path}")

    # mask 叠加在原图上
    mask_rgb = cv2.cvtColor(final_mask, cv2.COLOR_GRAY2BGR)
    mask_rgb[final_mask > 0] = [0, 0, 255]
    overlay = cv2.addWeighted(img_bgr, 0.7, mask_rgb, 0.3, 0)
    overlay_path = os.path.join(output_dir, f"{basename}_overlay.jpg")
    Image.fromarray(cv2.cvtColor(overlay, cv2.COLOR_BGR2RGB)).save(overlay_path, quality=95)
    print(f"覆盖图已保存: {overlay_path}")

    return final_mask, all_components


if __name__ == "__main__":
    detect_dust(
        image_path=r"d:\项目文件\灰尘检测\data\negative\2_4_53587.jpg",
        output_dir=r"d:\项目文件\灰尘检测\output\2_4_53587",
        max_iterations=4,
        max_area=20000,
        nms_iou_threshold=0.1,
    )
