# 发布检查清单 (Release Checklist)

## 准备

- [ ] `CHANGELOG.md` 已更新 `[Unreleased]` → 新版本号
- [ ] `vcpkg.json` 版本号与 tag 一致
- [ ] 所有 PR 已合并到 `develop`，`develop` → `main` 未 merge 的待发布内容已确认
- [ ] CI 双编译器 (MSVC + MinGW) 均通过
- [ ] 本地 `ctest` 全部通过

## 构建

```bash
# MSVC
cmake --preset msvc -DBUILD_TESTING=ON
cmake --build build/vcpkg-msvc --config Release --parallel
ctest --test-dir build/vcpkg-msvc -C Release --output-on-failure

# MinGW
cmake --preset default -DCMAKE_BUILD_TYPE=Release
cmake --build build/vcpkg-mingw --config Release --parallel
```

## 打包

- [ ] `cmake --build build/vcpkg-mingw --config Release --target deploy`
- [ ] 删除 `bin/logs/` (spdlog 可能锁文件)
- [ ] 删除 `build/vcpkg-mingw/_CPack_Packages/` (残留状态)
- [ ] `cpack -G NSIS` (如有 NSIS 路径问题，按 README 手动执行 makensis)

## 发布

- [ ] git tag `v<major>.<minor>.<patch>` (如 `v2.0.2`)
- [ ] `git push origin main --tags`
- [ ] GitHub Releases 页面创建 Release，上传 NSIS 安装包
- [ ] Release notes 从 `CHANGELOG.md` 对应版本复制
