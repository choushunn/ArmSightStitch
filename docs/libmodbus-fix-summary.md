# libmodbus Windows MinGW 兼容性修复总结

> 日期: 2026-07-06 | 编译器: Qt MinGW 13.1.0 (MSVCRT) | libmodbus: v3.1.12

## 问题现象

连接 Modbus TCP 机械臂 (`192.168.0.1:502`) 时，`modbus_connect()` 返回 `-1`，`errno=22 (EINVAL)`，`WSAGetLastError()=0`。

即使网络可达（`Test-NetConnection` 成功），错误依然存在。

## 排查过程

### 第一阶段：排除代码逻辑问题

- 检查 `connect()` 函数中 IP 地址、端口、从站ID、超时参数 —— 均为有效值
- `config.json` 中 `arm_ip: "192.168.0.1"`, `arm_port: 502` 正确
- `modbus_set_slave(1)` — slave=1 在有效范围 (1-247)
- `modbus_set_response_timeout(5, 0)` — sec=5, usec=0，条件 `(sec==0 && usec==0)` 不成立
- 增强错误诊断：捕获 `errno` 和 `WSAGetLastError()`，确认 `wsa=0`（WinSock 无错误）

**结论：EINVAL 是 libmodbus 自身设置的，非 socket 操作失败。**

### 第二阶段：排查 DLL/链接问题

- 原始代码 `#define MODBUS_API` 后 `#include <modbus.h>` + CMake 定义 `DLLBUILD`
- 导致声明侧 `MODBUS_API` 为空，定义侧 `MODBUS_API=__declspec(dllexport)`，不一致
- **修复：** 移除 `DLLBUILD` 和 `#define MODBUS_API`，统一静态链接
- **结果：** 无效，错误依旧

### 第三阶段：排查 FD_SETSIZE

在 `modbus-tcp.c` 的 `_modbus_tcp_connect()` 中发现：

```c
// modbus-tcp.c:357
ctx->s = socket(PF_INET, SOCK_STREAM, 0);  // socket() 成功
if (ctx->s >= FD_SETSIZE) {  // FD_SETSIZE 默认 64!
    errno = EINVAL;           // ← 这里设置了 errno=22
    return -1;
}
```

**WinSock 的 `socket()` 返回的是内核句柄，值可以为任意整数**（不像 Unix 的 fd 是连续小整数）。用 C# 测试确认当前系统 socket 句柄值为 **2424**，远超默认 FD_SETSIZE=64。

- 尝试 `#define FD_SETSIZE 1024` 在头文件中 — **无效**，因为 Qt 头文件先于我们的定义 include 了 `<winsock2.h>`
- 尝试 `#define FD_SETSIZE 1024` 通过 CMake 编译器参数 — **部分有效**，连接成功但读写仍失败

### 第四阶段：发现所有检查点

连接成功后，寄存器读写仍返回 EINVAL。在 `modbus.c` 的 `_modbus_receive_msg()` 中发现了第三处 FD_SETSIZE 检查：

```c
// modbus.c:388 — 每次 modbus 读写都会调用这个函数！
FD_ZERO(&rset);
if (ctx->s < 0 || ctx->s >= FD_SETSIZE) {
    errno = EINVAL;  // 读写操作全部失败
    return -1;
}
```

libmodbus 3.1.12 中共有 **8 处** `FD_SETSIZE` 检查：

| 文件 | 函数 | 影响 |
|------|------|------|
| `modbus-tcp.c:364` | `_modbus_tcp_connect()` | 连接 |
| `modbus-tcp.c:302` | `_connect()` | 连接（select 等待） |
| `modbus-tcp.c:469` | `_modbus_tcp_accept()` | 服务端 accept |
| `modbus-tcp.c:542` | `_modbus_tcp_select()` | select 调用 |
| `modbus-tcp.c:788` | `modbus_tcp_listen()` | 服务端监听 |
| `modbus-tcp.c:836` | `modbus_tcp_pi_listen()` | 服务端监听 |
| `modbus-tcp.c:873` | `modbus_tcp_pi_accept()` | 服务端 accept |
| `modbus.c:390` | `_modbus_receive_msg()` | **所有读写操作** |

逐个 patch 不现实，最终方案：通过编译器参数 `-DFD_SETSIZE=32768` 全局覆盖。

## 最终修复

### 1. CMakeLists.txt — 全局 FD_SETSIZE

```cmake
# Windows SOCKET handles can be any kernel handle value (often > 2000),
# but libmodbus checks `fd >= FD_SETSIZE` in many places. Use a large
# FD_SETSIZE so all valid handles pass the check. (32768 = 256KB per fd_set)
target_compile_definitions(${PROJECT_NAME} PRIVATE FD_SETSIZE=32768)
```

### 2. CMakeLists.txt — 移除 DLLBUILD（静态链接）

```cmake
# libmodbus is statically linked — no DLLBUILD needed
```

### 3. ModbusArmController.h — 移除 MODBUS_API 重定义

```cpp
#pragma once
// (removed: #define MODBUS_API)
#include <modbus.h>
```

## 核心教训

1. **Windows SOCKET ≠ Unix fd**：WinSock 的 `socket()` 返回内核句柄，值可以远超 FD_SETSIZE(64)。libmodbus 是 Unix 优先设计的库，`fd >= FD_SETSIZE` 检查在 Windows 上是错误的假设。

2. **`#define` 的生效时机**：头文件中的 `#define FD_SETSIZE` 太晚——Qt 的 `<winsock2.h>` 已经用默认值 64 处理过 `#ifndef FD_SETSIZE`。必须用编译器 `-D` 参数在所有头文件之前定义。

3. **errno vs WSAGetLastError()**：在 WinSock 操作成功但 libmodbus 自身检查失败时，`WSAGetLastError()` 返回 0，只有 `errno` 被手动设置为 EINVAL。`wsa=0` 是区分"socket 错误"和"libmodbus 内部检查错误"的关键诊断线索。

4. **环境差异触发隐藏 bug**：旧环境（Qt 打开句柄少）socket 恰好分到 < 64 的值，新环境（Qt 6.11.1 打开句柄多）socket 分到 > 2000。同样的代码，不同环境表现完全不同。

## 相关文件

- [deploy-dll-summary.md](deploy-dll-summary.md) — 部署依赖 DLL 清单
- [ModbusArmController.cpp](../core/arm/ModbusArmController.cpp) — 修复后的 Modbus 驱动
- [CMakeLists.txt](../CMakeLists.txt) — 包含 FD_SETSIZE 和 DLLBUILD 修复
