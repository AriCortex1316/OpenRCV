# OpenRCV

OpenRCV 是一款面向 Windows 预安装环境（WinPE）的系统部署工具。

它提供图形界面，用于选择 Windows 镜像、目标分区和安装选项，并完成 Windows 系统部署。

目前支持 Windows 10 和 Windows 11 官方 ISO 镜像。

不支持 Ghost、GHO、第三方封装镜像或其他非标准 Windows 安装镜像。

## 界面截图

系统重装：

![OpenRCV 系统重装界面](docs/images/openrcv_main.png)

高级设置：

![OpenRCV 高级设置界面](docs/images/openrcv_setting.png)

## 功能

- 挂载 Windows 10 / Windows 11 ISO 镜像
- 读取 WIM / ESD 中的 Windows 版本
- 选择系统安装分区
- 选择 EFI / 引导分区
- 配置无人值守安装选项
- 格式化目标分区
- 应用 Windows 镜像
- 配置 Windows 启动文件
- 显示部署进度和运行日志
- 将 Windows Update 配置为仅通知下载和安装（Home 使用离线注册表，Pro
  及以上使用本地计算机策略）

## 支持环境

目前主要针对 Windows 11 x64 WinPE 进行开发和测试。

## 项目状态

OpenRCV 目前仍处于开发阶段，部分功能和界面可能继续调整。

## 开发与构建

需要 Windows、Visual Studio 的“使用 C++ 的桌面开发”工作负载、Windows SDK
和 CMake 3.20 或更高版本。在 Visual Studio Developer PowerShell 中执行：

```powershell
# x64
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release
ctest --test-dir build/x64 -C Release --output-on-failure

# x86
cmake -S . -B build/x86 -A Win32
cmake --build build/x86 --config Release
ctest --test-dir build/x86 -C Release --output-on-failure
```

程序输出到 `build/<架构>/Release/OpenRCV.exe`。Release 使用静态 MSVC
运行时，不依赖 VC++ Redistributable。仅构建程序时，可在配置命令中加入
`-DOPENRCV_BUILD_TESTS=OFF`。

CMake 是唯一构建定义，不再维护根目录的手写 `.vcxproj`。Visual Studio
可直接打开项目目录，也可用 `cmake --open build/x64` 打开生成的工程。
所有生成文件均放在 `build/` 或 `out/`。

## 代码结构

| 目录 | 职责 |
| --- | --- |
| `src/ui/` | Win32 界面、选项收集、目标确认、工作线程和消息桥接 |
| `src/workflow/` | 镜像源管理、部署计划生成、同步部署执行及日志/进度回调 |
| `src/imaging/` | ISO 只读挂载、WIMGAPI 镜像元数据读取 |
| `src/process/` | 命令行引用、隐藏进程、输出捕获与取消、DISM 进度解析 |
| `src/storage/` | 分区枚举和临时引导盘符管理 |
| `src/unattend/` | 无人值守及 OOBE XML 生成 |
| `src/config/` | 区域配置、离线注册表和组策略设置 |
| `include/openrcv/` | 与源码模块对应的公开接口 |
| `tests/` | 按模块拆分的核心测试和共享断言支持 |
| `docs/` | 开发记录与界面截图 |

`openrcv_core` 不依赖界面模块。`DeploymentRunner` 在调用线程同步执行，
通过回调报告日志和进度；界面负责将这些通知转发为窗口消息。
调用执行器前必须完成部署计划校验、目标安全检查和用户确认。

## 验证与安全

核心测试只验证命令行、元数据、配置生成、部署计划和非破坏性进程行为，
不执行格式化、镜像应用或引导写入。构建及这些测试通过不代表 WinPE
部署验证通过。

完整安装流程必须在一次性虚拟机的 WinPE 中验证，不能以开发宿主机的
物理磁盘为目标。格式化、DISM 应用和 BCDBoot 均会修改磁盘，必须先明确
确认目标分区与引导分区。
