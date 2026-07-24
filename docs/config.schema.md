# config.json 字段说明

## 配置验证策略

加载 `config.json` 时：
- 缺失的字段回退到硬编码默认值，同时输出 `[WARN]` 级别日志
- 类型错误（如字符串填入了数字字段）同样回退并告警
- 不阻塞启动——所有字段都有安全默认值

## 字段列表

### 系统 (system)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `arm_ip` | string | `""` | 机械臂 IP 地址 |
| `arm_port` | int | `502` | Modbus TCP 端口 (1-65535) |
| `default_speed` | int | `30000` | 默认运动速度 (脉冲/秒) |
| `axis_min_pos_0..4` | int[] | `[0,0,0,-180000,-180000]` | 各轴最小位置 (脉冲) |
| `axis_max_pos_0..4` | int[] | `[384000,384000,80000,180000,180000]` | 各轴最大位置 (脉冲) |
| `log_path` | string | `""` | 日志文件路径 |
| `image_save_base_path` | string | `""` | 图像保存基准路径 |

### 相机 (camera)

| 字段 | 类型 | 默认值 | 范围 |
|------|------|--------|------|
| `camera_width` | int | `640` | — |
| `camera_height` | int | `480` | — |
| `camera_exposure` | float | `70.0` | 0.1 - 350 |
| `camera_gain` | float | `1.0` | 1.0 - 5.0 |
| `camera_sharpening` | int | `0` | 0 - 500 |

### 扫描 (scan)

| 字段 | 类型 | 默认值 | 范围 |
|------|------|--------|------|
| `grid_size_x` | int | `10` | 1 - 100 |
| `grid_size_y` | int | `10` | 1 - 100 |
| `step_size` | int | `43000` | — |
| `z_height` | int | `80000` | 0 - 80000 |
| `dwell_time_ms` | int | `300` | 0 - 5000 |

### 拼接 (stitch)

| 字段 | 类型 | 默认值 | 范围 |
|------|------|--------|------|
| `stitch_algorithm` | int | `3` | 0=Grid, 1=Feature, 2=ZScale, 3=SeamFeather |
| `center_crop_size` | int | `1775` | 0 - 9999 |
| `feather_width` | int | `120` | 0 - 1000 |
| `scale_mode` | int | `0` | 0=Z-scale auto, 1=manual scale-map |
| `z_correction_coef` | double | `1.0` | — |

### Z轴 (zaxis)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `z_mode` | int | `0` | 0=球冠, 1=手动Z-Map, 2=径向Z-Map |
| `sphere_radius` | int | `230000` | 球冠半径 (脉冲) |
| `sphere_cap_height` | int | `50000` | 球冠高度 (脉冲) |
| `sphere_height_offset` | int | `0` | 高度偏移 (脉冲) |
| `z_base_height` | int | `80000` | Z 基准高度 (脉冲) |

### 检测 (detection)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `detection_algorithm` | int | `1` | 0=YOLO, 1=Dust, 2=Edge(Sobel) |
| `model_param_path` | string | `""` | YOLO .param 文件路径 |
| `model_bin_path` | string | `""` | YOLO .bin 文件路径 |

### 灰尘检测 (dust)

| 字段 | 类型 | 默认值 | 范围 |
|------|------|--------|------|
| `dust_clahe_clip` | double | `2.0` | — |
| `dust_bg_blur` | int | `31` | 奇数 |
| `dust_min_area` | int | `50` | — |
| `dust_max_area` | int | `20000` | 0=不限 |
| `dust_dilate_iter` | int | `0` | — |
| `dust_max_iter` | int | `4` | — |
| `dust_nms_iou` | double | `0.1` | 0=关闭 |

### 边缘检测 (edge)

| 字段 | 类型 | 默认值 | 范围 |
|------|------|--------|------|
| `edge_clahe_clip` | double | `2.0` | — |
| `edge_clahe_tile_grid` | int | `8` | — |
| `edge_threshold` | int | `30` | 0 - 255 |
| `edge_sobel_ksize` | int | `3` | 1/3/5/7 |
| `edge_dilate_iter` | int | `1` | — |
| `edge_min_bbox_area` | int | `25` | — |
| `edge_nms_iou_thresh` | double | `0.4` | 0.0 - 1.0 |
| `edge_nms_contain_thresh` | double | `0.5` | 0.0 - 1.0 |
