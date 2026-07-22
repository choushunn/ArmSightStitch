# 高级网格拼接算法 — 需求规格文档

## 背景

ArmSightStitch 使用 XYZ 滑台带动相机对球面样品进行扫描。由于相机景深有限，Z 轴需上下移动以保证每格图像清晰，导致：

1. **放大率差异**：不同 Z 高度下，同一物体的成像大小不同（物距变化）
2. **成像区域偏移**：球面扫描时边缘位置的光轴角度偏转，有效 ROI 偏离图像中心
3. **镜头非线性**：镜头放大率与物距并非严格线性反比关系

因此需要一套新的拼接参数体系，支持：缩放校正、裁剪偏移、自定义裁剪尺寸、接缝羽化。

## 算法定位

- **算法编号**：4
- **算法名称**：高级网格拼接 (Advanced Grid Stitch)
- **新文件**：`core/stitch/AdvancedGridStitchAlgorithm.h`
- **旧算法保留**：算法 0/1/2/3 完整保留，不受影响

## 功能需求

### F1: 网格位置放置

- 同算法 0（GridStitchAlgorithm），按 `(row, col)` 将每张瓦片放置在网格画布中的固定位置
- 瓦片排序方式：S 形（偶数行左→右，奇数行右→左）
- 自动从文件名解析 `row_col_z` 位置信息

### F2: Z 轴统一缩放 + 校正系数

- 基准缩放因子：`zRef / z_i`（zRef 为所有 Z 值的中位数）
- 校正系数：全局统一浮点数 `z_correction_coef`，默认 1.0
- 最终缩放因子：`(zRef / z_i) × z_correction_coef`
- 缩放后图像插值回原始像素尺寸，保持瓦片统一大小
- 缩放模式支持：
  - Mode 0（Z 自动）：使用 Z 值计算缩放因子
  - Mode 1（手动映射）：从 `scale_map_file` 读取 `scale_values[row][col]`，作为 Z 自动缩放后的**乘数叠加**（不再替代 Z 缩放）

### F3: 逐格裁剪偏移

- 每格独立存储 `offsetX`、`offsetY`（像素值，相对图像中心的偏移）
- 执行顺序：**先缩放 → 再偏移裁剪**（偏移量为缩放后图像上的像素偏移）
- 存储文件：`crop_offset_file` 指定的 JSON 文件
- JSON 格式：
  ```json
  {
    "description": "Crop Offset Map — 每格裁剪中心偏移量（缩放后图像坐标）",
    "offsets": [
      [{"ox": 0, "oy": 0}, {"ox": 0, "oy": 0}, ...],
      ...
    ]
  }
  ```
- 偏移量可为正负值，正值表示向右/向下偏移
- 偏移后仍然以全局 `center_crop_size` 为裁剪尺寸（正方形）

### F4: 全局裁剪尺寸

- 复用现有 `center_crop_size` 配置项
- 默认 1775px，范围 0~5000px
- 值为 0 表示不裁剪，使用完整缩放后图像

### F5: 接缝羽化融合

- 复用现有 `feather_width` 配置项
- 相邻瓦片重叠区使用余弦权重渐变：`0.5 - 0.5 × cos(π × t)`
- `feather_width == 0` 时退化为硬拼接（`copyTo`）
- GPU 加速路径：`HAVE_OPENCV_CUDA` 时使用 CUDA 计算

### F6: 图像加载与位置解析

- 从文件名 `row_col_z.jpg` 解析位置信息
- 自动检测网格维度（max row/col + 1）
- RTL 列映射：`col = maxCol - filenameCol`
- 1-indexed 显示坐标 → 0-indexed 内部坐标转换

### F7: 进度回调

- 加载阶段：0% ~ 10%
- 拼接阶段：20% ~ 100%（每张瓦片处理时更新）
- 通过 `setProgressCallback` / `setStatusCallback` 上报

## 配置项清单

### 新增配置项（需加入 ConfigManager + config.json）

| 配置键 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `z_correction_coef` | double | 1.0 | Z 缩放统一校正系数 |
| `crop_offset_file` | string | `~/Documents/ScannerData/crop_offset.json` | 逐格裁剪偏移 JSON 文件路径 |

### 复用现有配置项

| 配置键 | 说明 |
|--------|------|
| `center_crop_size` | 全局正方形裁剪尺寸 |
| `scale_mode` | 0 = Z 自动，1 = 手动映射（叠加模式） |
| `scale_map_file` | 手动缩放映射 JSON 文件 |
| `feather_width` | 接缝羽化宽度 |
| `grid_size_x` / `grid_size_y` | 网格尺寸 |

## UI 变更

### StitchingSettingsDialog

- `algorithmCombo` 增加选项："4 - 高级网格拼接 (Advanced Grid)"
- 新增控件：
  - `zCorrectionSpin` (QDoubleSpinBox)：Z 缩放校正系数，范围 0.50~2.00，步长 0.01，默认 1.00
  - `cropOffsetEdit` (QLineEdit) + Browse 按钮：裁剪偏移文件路径
- 当选择算法 4 时，显示以下参数组：
  - 缩放校正系数
  - 裁剪偏移文件
  - 裁剪尺寸（复用 `centerCropSpin`）
  - 羽化宽度（复用 `featherWidthSpin`）
  - 缩放模式 + 缩放映射文件（复用现有控件）

### 右键菜单（网格单元格）

新增菜单项：

- **"设置裁剪偏移 [行X,列Y]"** — 弹出对话框：
  - `offsetX` (QSpinBox)：X 方向偏移，范围 -500~500 px
  - `offsetY` (QSpinBox)：Y 方向偏移，范围 -500~500 px
  - 读取/写入 `crop_offset_file` 指定的 JSON 文件

### main.cpp 模板文件生成

新增 `ensureCropOffsetTemplate()` 函数，生成默认的 `crop_offset.json` 模板（全 0 偏移），网格尺寸与当前 `grid_size` 一致。

## ImageStitcher 变更

- `setAlgorithm(4)` 选择新算法
- 新增 setter（通过 `dynamic_cast` 转发到 AdvancedGridStitchAlgorithm）：
  - `setZCorrectionCoef(double coef)`
  - `setCropOffsetFile(const std::string& path)`
- `stitchImagesWithPositions` 调度支持算法 4

## ConfigManager 变更

- 新增成员：`z_correction_coef_` (double, 1.0), `crop_offset_file_` (string)
- 新增 getter/setter：`zCorrectionCoef()`, `setZCorrectionCoef()`, `cropOffsetFile()`, `setCropOffsetFile()`
- JSON 序列化/反序列化新增两个键
- 环境变量覆盖：`ARM_SIGHT_STITCH_Z_CORRECTION_COEF`、`ARM_SIGHT_STITCH_CROP_OFFSET_FILE`

## 裁剪偏移执行流程（伪代码）

```
for each image i at (row, col):
    1. img = images[i]
    2. scale = (zRef / z_i) × z_correction_coef
       if scale_mode == 1: scale *= scale_map[row][col]
    3. img_scaled = resize(img, Size(w*scale, h*scale))
    4. img_final = resize(img_scaled, original_size)  // 插值回原始尺寸
    5. offset = crop_offset[row][col]  // {ox, oy}
    6. center_x = img_final.width / 2 + offset.ox
       center_y = img_final.height / 2 + offset.oy
    7. crop_rect = centered at (center_x, center_y) with size center_crop_size × center_crop_size
    8. tile = img_final(crop_rect)
    9. feather_blend tile into canvas at grid position (row, col)
```

## 文件变更清单

| 操作 | 文件 |
|------|------|
| 新建 | `core/stitch/AdvancedGridStitchAlgorithm.h` |
| 修改 | `core/stitch/ImageStitcher.h` / `.cpp` |
| 修改 | `core/stitch/IStitcher.h` |
| 修改 | `infra/config/ConfigManager.h` / `.cpp` |
| 修改 | `ui/StitchingSettingsDialog.h` / `.cpp` / `.ui` |
| 修改 | `ui/MainWindow.cpp`（右键菜单 + on_stitchRun + onStitchingFinished） |
| 修改 | `app/main.cpp`（crop_offset.json 模板生成） |
| 修改 | `config.json`（新增两个键的默认值） |

## 实施步骤

1. **实现 AdvancedGridStitchAlgorithm** — 继承 IStitchAlgorithm，整合 F1~F7
2. **扩展 ImageStitcher** — 注册算法 4，新增 setter 转发
3. **扩展 ConfigManager** — 新增 2 个配置项
4. **扩展 StitchingSettingsDialog** — 新增算法选项和参数控件
5. **扩展 MainWindow 右键菜单** — 新增"设置裁剪偏移"
6. **main.cpp 模板生成** — 新增 crop_offset.json 模板
7. **构建验证** — 编译 + 功能测试
