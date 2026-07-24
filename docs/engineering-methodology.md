# 软件工程方法论

本文档定义了 ArmSightStitch 项目的工程纪律标准，适用于日常开发、PR review、release 流程。

---

## 1. 分层测试金字塔

```
        /\
       /E2E\        ← 少而精：全链路烟雾测试（相机→扫描→拼接→检测）
      /------\
     /集成测试\      ← 关键交互：arm 通信 + 运动控制的 mock 测试
    /----------\
   /  单元测试   \    ← 最多：纯计算逻辑（stitch/detect/config）
  /--------------\
```

**原则：**

- 优先级按风险排序：**硬件控制 > 纯计算 > UI 交互**
- 不追求覆盖率数字，追求关键路径全覆盖
- 每个 PR 如涉及核心逻辑变更，必须包含对应测试
- CI 中 `ctest` 不通过则 PR 不能合并

**测试分类参考：**

| 层级 | 覆盖对象 | 工具 | 频率 |
|------|---------|------|------|
| 单元 | ConfigManager, StitchAlgorithm, Detector | GTest | 每次 commit |
| 集成 | ModbusConnection + ModbusRegisterIO, CameraHandler | GTest + mock | 每次 PR |
| E2E | 全流程冒烟 (arm connect → scan → stitch → detect → save) | 手动 / CI nightly | 每次 release |

---

## 2. 单一职责 + 依赖注入

**拆分指标：**

- 一个类超过 **500 行**（含 .h + .cpp）→ 考虑拆分
- 一个类超过 **20 个 public 方法** → 考虑拆分
- 一个函数超过 **80 行** → 考虑提取子函数
- 一个文件超过 **1000 行** → 必须拆分

**依赖注入方式：**

```cpp
// ✅ 好：通过构造函数注入具体依赖
class ScanController : public QObject {
    ScanController(Ui::MainWindow* ui, AppController& ctrl, QObject* parent);
};

// ❌ 避免：通过单例隐式获取
class ScanController {
    void doSomething() {
        auto& cfg = ConfigManager::instance(); // 隐式依赖，难以测试
    }
};
```

**策略模式适用范围：**

- 仅当存在 **≥2 个真实实现** 且**有运行时切换需求**时，才引入抽象接口
- 例如 `IStitchAlgorithm`（4 种算法可互换）— 正确
- 单实现的接口 → 删除，直接依赖具体类（YAGNI 原则）

---

## 3. 编译防火墙

**规则：**

| 文件类型 | 内容 | include 范围 |
|----------|------|-------------|
| `.h` 头文件 | 仅声明 + 类型别名 + inline getter | 最小化，仅类型的必要依赖 |
| `.cpp` 实现文件 | 全部实现代码 | 无限制（不会影响编译传播） |

**第三方库隔离：**

```
禁止在公开头文件中 include:
  - <opencv2/...>         → 使用前向声明 cv::Mat
  - <modbus.h>             → Pimpl 或 helper 类封装
  - <ncnn/...>             → Pimpl 或 helper 类封装
  - <toupcam.h>            → Pimpl 或 helper 类封装
```

项目已通过 `AppController::Impl` (Pimpl) 实现此隔离，`ModbusConnection` / `ModbusRegisterIO` 封装了 libmodbus 依赖。

**好处：**
- 修改 `.cpp` 不会触发全量重编译
- include 链缩短，增量编译时间从分钟级降到秒级
- 第三方库版本升级时仅需重新编译封装层

---

## 4. CI/CD 作为强制门槛

**流程：**

```
PR 提交
  ├─ CI: MSVC 构建 + 测试
  ├─ CI: MinGW 构建 + 测试
  ├─ 静态分析 (clang-tidy)
  └─ Pre-commit 检查 (格式 + 基础 lint)
       ↓
  全部通过 → Code Review → 合并
  任一失败 → 修复后重新提交
```

**CI 性能目标：**

- 热启动 (有缓存): < 5 分钟
- 冷启动 (无缓存): < 15 分钟
- 通过 `actions/cache@v4` 缓存 Qt、vcpkg 依赖

**Pre-commit 检查（本地）：**

```bash
# 安装一次
pip install pre-commit
pre-commit install

# 每次 commit 自动运行：trailing-whitespace + clang-format + check-yaml
```

---

## 5. 文档即代码的一部分

**必须维护的文档：**

| 文档 | 触发条件 | 受众 | 位置 |
|------|---------|------|------|
| `README.md` | 新功能/结构变更 | 新开发者 | 仓库根 |
| `CHANGELOG.md` | 每次 release | 用户/团队 | 仓库根 |
| `docs/security-notes.md` | 安全相关决策 | 安全审计 | `docs/` |
| `docs/i18n-notes.md` | 国际化策略变更 | 开发者 | `docs/` |
| `docs/config.schema.md` | 新增配置字段 | 运维/用户 | `docs/` |
| `docs/release-checklist.md` | 发布流程变更 | 发布负责人 | `docs/` |

**PR review 检查项：**
- [ ] 公开 API 变更 → 更新 README 或对应文档
- [ ] 新增配置字段 → 更新 `docs/config.schema.md`
- [ ] 变更影响用户流程 → 更新 README 功能概览
- [ ] 新增第三方依赖 → 更新 README 依赖表

---

## 6. 技术债管理

**核心原则：不是消灭所有技术债，而是记录 + 排序 + 定期偿还。**

**记录：**

```cpp
// TODO(#123): 替换为 Qt 原生线程池 — 预计 v2.1 处理
// FIXME(#456): 大网格 (>100×100) 时加载速度慢 — 需异步缩略图
// HACK(#789): 绕过 NSIS 绝对路径限制，待 NSIS 3.13 修复后移除
```

每个 TODO/FIXME/HACK 必须有对应的 GitHub Issue 编号。

**偿还节奏：**

- 每次 release 前预留 **20%** 迭代时间用于技术债偿还
- 每个月的第一个周五为 "技术债偿还日"
- 重构前必须编写测试（确保行为不变的前提是有测试保护）

**技术债优先级排序：**

1. **安全** — 可能导致数据丢失或硬件损坏的缺陷
2. **可靠性** — 可能导致崩溃或功能不可用的缺陷
3. **性能** — 明显影响用户体验的性能问题
4. **可维护性** — 阻碍后续开发的技术债

---

## 7. 版本管理

**语义化版本 (SemVer):** `v<MAJOR>.<MINOR>.<PATCH>`

| 版本号段 | 递增条件 |
|----------|---------|
| MAJOR | 不兼容的 API 变更 / 架构重构 |
| MINOR | 新增后向兼容的功能 |
| PATCH | 后向兼容的 Bug 修复 |

**发布流程：** 参见 [docs/release-checklist.md](docs/release-checklist.md)

**分支策略：**

```
main       ← releases only (tagged: v2.0.0, v2.0.1, ...)
  ↑
develop    ← active development (PR target)
  ↑
feature/*  ← individual features / fixes
```

---

## 8. 代码审查清单

每个 PR 必须通过以下检查：

**设计：**
- [ ] 是否遵循单一职责原则？
- [ ] 新增的类是否超过 500 行？（若是，需说明理由）
- [ ] 是否引入了无必要的新接口？（YAGNI）
- [ ] 头文件是否泄漏了第三方 #include？

**正确性：**
- [ ] 是否有对应的测试？关键路径测试是否通过？
- [ ] 错误处理是否完整？（不能吞异常静默失败）
- [ ] 异步操作是否有完成/失败回调？

**可维护性：**
- [ ] 变量/函数命名是否清晰？（不使用缩写，除非是行业通用缩写）
- [ ] 是否有残留的调试代码或注释掉的代码？
- [ ] 是否添加了必要的 TODO/FIXME（带 Issue 编号）？

**文档：**
- [ ] README / CHANGELOG / docs/ 是否需要更新？
- [ ] 新增的配置字段是否已文档化？

**构建：**
- [ ] MSVC + MinGW 双编译器是否都能构建成功？（CI 自动验证）
- [ ] `ctest` 是否全部通过？

---

---

## 9. 可观测性 (Observability)

**结构化日志：**

```cpp
// ✅ 键值对格式，便于 grep / 日志分析工具解析
SPDLOG_INFO("[Arm] action=connect port={} status=ok latency_ms={}", port, latency);
SPDLOG_ERROR("[Camera] action=capture error='{}' retry_count={}", err, retries);

// ❌ 自由文本格式
SPDLOG_INFO("[Arm] Connecting to arm device on port {}...", port);
```

**健康检查：**

```cpp
// AppController 中暴露统一接口
struct SystemHealth {
    bool armConnected;       chrono::steady_clock::time_point armLastComms;
    bool cameraConnected;    chrono::steady_clock::time_point cameraLastFrame;
    bool modelLoaded;        string modelName;
    bool scanInProgress;     int scanProgressPct;
};
SystemHealth healthCheck();
```

UI 中可在状态栏显示绿色/黄色/红色指示灯（类似现有 `armStatusLabel` 的 `●` 指示）。

**性能埋点：**

关键路径使用 `QElapsedTimer` + spdlog timing 字段：

```cpp
QElapsedTimer t; t.start();
result = algo.stitch(images, grid);
SPDLOG_DEBUG("[Perf] action=stitch algo={} grid={}x{} timing_ms={}", algo, grid.w, grid.h, t.elapsed());
```

---

## 10. 配置热加载 (Hot Reload)

- `QFileSystemWatcher` 监听 `config.json` 文件变化
- 各模块实现 `reloadConfig(const ConfigSections&)` 方法接收新配置
- 软件运行时 UI 修改 → 即时写入 config.json；外部修改 → 自动检测重载
- 机械臂运动 / 扫描中加 `config_locked_` 标志，延迟到空闲时应用

---

## 11. 优雅降级 (Graceful Degradation)

编译宏 `ARM_SIGHT_STITCH_NO_*` 改为运行时检测：

| 模块 | 运行时检测方式 | fallback 行为 |
|------|---------------|---------------|
| NCNN 模型 | `QFileInfo::exists(modelPath)` | 检测按钮置灰，提示 "模型未加载" |
| 相机 | `isConnected()` + 心跳超时 | 自动重试 3 次 (2s/5s/10s)，失败后预览区显示 "相机已断开 — 点击重连" |
| 机械臂 | `isConnected()` + 心跳超时 | 断线自动重连，离线时扫描按钮置灰 |
| CUDA | `cv::cuda::getCudaEnabledDeviceCount()` | fallback 到 CPU 拼接 |

每个模块实现 `bool isAvailable() const` 查询接口。

---

## 12. 崩溃恢复 (Crash Recovery)

**扫描恢复文件 `scan_state.json`：**

```json
{
  "version": 1,
  "scan_dir": "D:/ScannerData/20260724_093000",
  "grid": {"x": 10, "y": 10},
  "completed_cells": [{"row": 0, "col": 0}, {"row": 0, "col": 1}],
  "next_cell": {"row": 0, "col": 2},
  "arm_position": {"x": 86000, "y": 0, "z": 80000},
  "timestamp": "2026-07-24T09:35:00"
}
```

- 每个 cell 扫描完成时原子写入（`QSaveFile`）
- 启动时检查是否存在 → 提示用户 "检测到未完成的扫描，从断点继续？"
- 扫描正常结束 / 用户手动停止时删除恢复文件

---

## 13. 依赖版本锁定

**vcpkg 基线：** `builtin-baseline` 固定 SHA + 文档化日期。每次更新基线时在 CHANGELOG 记录兼容性验证结果。

**overrides 精确锁定：**

```json
"overrides": [
  { "name": "opencv4", "version": "4.12.0" },
  { "name": "spdlog", "version": "1.17.0" }
]
```

**兼容性矩阵：** 在 `docs/compatibility.md` 中维护已验证的依赖版本组合。

---

## 14. 性能基准测试

**基准测试文件：** `tests/bench_*.cpp`，使用固定大小测试图像，跑 5 次取中位数。

**CMake 控制：** `ARM_SIGHT_STITCH_BENCHMARK` option，默认 OFF（不阻塞 CI）。

**关键路径埋点：**

| 操作 | 测量指标 | 目标 |
|------|---------|------|
| GridStitch (10×10) | timing_ms, memory_mb | < 500ms |
| SeamFeather (10×10) | timing_ms, memory_mb | < 2000ms |
| YOLO detect (1920×1080) | timing_ms | < 100ms (GPU) |
| Scan per cell | timing_ms | < 3000ms |

**结果输出：** spdlog 以 `[Perf]` 标签 + `timing_ms=N memory_mb=N` 字段。

---

## 15. 日志轮转

```cpp
// 替代 basic_file_sink_mt(truncate=true)
auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
    logFile, 10 * 1024 * 1024, 5);  // 10MB × 5 文件 = 50MB 上限
```

- 上次运行日志在崩溃或重启后保留
- 旧日志自动归档为 `armsightstitch.1.log` ~ `.5.log`
- 总磁盘占用上限 50MB

---

## 16. 配置版本迁移

`config.json` 增加 `"version"` 字段：

```cpp
static constexpr int kCurrentConfigVersion = 3;

ConfigManager::migrate(int fromVersion) {
    if (fromVersion < 1) { /* 旧格式 → 新格式 */ }
    if (fromVersion < 2) { setStitchAlgorithm(3); }   // Grid→SeamFeather
    if (fromVersion < 3) { setDetectionAlgorithm(2); } // YOLO→Sobel
    version_ = kCurrentConfigVersion;
}
```

迁移函数集中维护，按版本号链式调用。加载完成后自动保存更新后的 config。

---

## 17. 自动化依赖更新

**GitHub Actions monthly schedule：**

```yaml
name: Dependency Update Check
on:
  schedule:
    - cron: '0 0 1 * *'  # 每月 1 号
```

**步骤：**

1. `vcpkg update` → 更新 baseline
2. 全量构建 (MSVC + MinGW)
3. 全量测试
4. 通过 → 自动开 PR（标题 `chore(deps): vcpkg baseline update YYYY-MM`）
5. 失败 → 开 Issue 记录兼容性问题

---

## 18. 键盘可访问性

**全局快捷键：**

| 快捷键 | 操作 | 备注 |
|--------|------|------|
| `Space` | 紧急停止 | 最高优先级，单键触发，全局响应 |
| `F5` | 开始/停止扫描 | 扫描状态切换 |
| `F6` | 开始拼接 | 触发拼接流程 |
| `F7` | 检测单帧 | 手动触发检测 |
| `Ctrl+E` | 实时检测开关 | toggle |
| `F11` / `Ctrl+F` | 全屏切换 | 已有 actionFullscreen |
| `Ctrl+O` | 打开图像 | 已有 |
| `Ctrl+S` | 保存结果 | 已有 |
| `F1` | 使用说明 | 已有 |

**实现方式：** `QShortcut` 或 `QAction::setShortcut()`，紧急停止始终 Enable。

在菜单「帮助 → 快捷键参考」中列出完整列表。

---

## 19. 扫描数据管理

**扫描元数据 `scan_meta.json`：**

```json
{
  "version": 1,
  "timestamp": "2026-07-24T09:30:00",
  "grid": {"x": 10, "y": 10},
  "stitch_params": {"algorithm": 3, "feather_width": 120},
  "detection_result_count": 47,
  "total_image_size_mb": 340
}
```

**自动清理策略：**

- 配置项 `max_scan_retention_days` (默认 30 天)
- 启动时检查工作目录，列出超过保留期的扫描
- 提示用户清理，提供 "清理旧扫描数据..." 菜单选项

**最近扫描列表：**

- 启动时扫描工作目录，读取所有 `scan_meta.json`
- 在主界面中显示 "最近扫描" 列表（最近 10 次），可直接双击打开


## 参考

- [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
- [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
