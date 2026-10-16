# 开发进度

最后更新：2026-10-17

本文分别记录模块进度和已执行的验证；构建与非破坏性检查不作为 WinPE 实际部署通过的结论。

## 项目目标

开发面向 WinPE 的原生 C++17 Windows 部署程序，主要支持 x64 WinPE，同时保留 Win32 构建以兼容旧版 x86 WinPE。

安装页采用 EasyRCV 风格布局：蓝色标题栏、左侧导航、镜像选择、分区详情、Windows 版本选择、安装选项、启动分区选择和开始安装按钮。高级及离线设置放在独立页面。

## 模块进度

| 模块 | 主要源文件 | 状态 |
| --- | --- | --- |
| 界面 `ui` | `src/ui/Application.cpp` | 已实现 |
| 部署流程 `workflow` | `src/workflow/DeploymentPlan.cpp`、`src/workflow/DeploymentRunner.cpp`、`src/workflow/ImageSourceService.cpp` | 已实现 |
| 镜像 `imaging` | `src/imaging/IsoMount.cpp`、`src/imaging/WindowsImage.cpp` | 已实现 |
| 进程 `process` | `src/process/ProcessRunner.cpp` | 已实现 |
| 存储 `storage` | `src/storage/VolumeEnumerator.cpp` | 已实现 |
| 无人值守 `unattend` | `src/unattend/UnattendBuilder.cpp` | 已实现 |
| 离线配置 `config` | `src/config/OfflineSettings.cpp`、`src/config/Region.cpp` | 已实现 |

## 已实现功能

### 界面与构建

- 原生 Unicode Win32 界面，使用标准控件。
- CMake 为唯一构建定义，支持生成 Visual Studio/MSBuild 工程；不再维护根目录的手写 `.vcxproj`。
- Release 配置使用静态 MSVC 运行时，避免依赖 WinPE 中的 VC++ Redistributable。
- 界面截图位于 `docs/images/openrcv_main.png` 和 `docs/images/openrcv_setting.png`。
- 生成的可执行文件放在 `build/`，不纳入 Git。
- 统一 C++17、Unicode、MSVC 警告等级和静态运行时配置，程序及测试使用同一构建策略。
- 测试按进程、镜像、无人值守、离线配置和部署计划拆分，共用断言支持与一个入口。

### 镜像加载

- 通过 Virtual Disk API 只读挂载 ISO，动态加载 `virtdisk.dll`，缺少 DLL 时显示提示。
- 优先读取 ISO 中的 `sources\install.wim`，不存在时读取 `sources\install.esd`。
- 通过 WIMGAPI 获取镜像索引、名称、架构和 `EDITIONID`，使用 XML 解析器读取元数据。
- 支持区分 Home 与 Pro 及更高版本，供更新选项使用。
- DISM 用于应用镜像，不用于读取镜像列表。

### 分区与启动配置

- 枚举固定卷的盘符、磁盘及分区编号、GPT/MBR、文件系统、卷标、容量、可用空间和总线类型。
- 独立枚举 EFI 和活动启动分区，仅推荐与安装目标位于同一物理磁盘的启动分区。
- 为没有盘符的 EFI 分区临时分配盘符，BCDBoot 完成后移除临时挂载。

### 安装流程

- 部署计划包含格式化、DISM 应用镜像、无人值守文件写入、离线设置和 BCDBoot。
- 开始安装前，通过弹窗明确确认安装目标和启动分区。
- 界面工作线程调用独立的 `DeploymentRunner`：DiskPart 格式化 → `dism.exe /Apply-Image` → 配置写入 → `bcdboot.exe`；执行器通过日志和进度回调与界面解耦。
- 隐藏运行子进程，处理 Windows 参数引用，实时显示输出，并通过作业对象支持取消。
- 解析 DISM 输出中的进度百分比并更新进度条。
- 记录执行命令、输出和退出码，安装成功后提示重启。

### 无人值守与离线设置

- 生成无人值守 XML 和 `Oobe.xml`，支持本地管理员、语言及区域、OOBE 联网阻断和更新暂停任务。
- 支持文件扩展名、“此电脑”桌面图标、Widgets、快速启动、UAC、自动 BitLocker 和 Windows Update 驱动更新排除。
- “禁用 Windows Update”采用 `PauseWindowsUpdate` 计划任务，不再写入永久禁用更新策略；任务文件使用带 BOM 的 UTF-16LE，在 `specialize` 阶段注册并运行，每周刷新暂停期限。
- “Windows Update 仅通知”按版本处理：Home 写入 `NoAutoUpdate=0` 和 `AUOptions=2`；Pro 及更高版本写入 `Machine\Registry.pol`，保留有效的已有记录，并更新 `gpt.ini`。
- 更新暂停与仅通知选项互斥；未知版本不猜测处理。

## 已有安全机制

- 拒绝非根目录安装目标和 WinPE 的 `X:` 内存盘；格式化前要求确认盘符与目标盘符一致。
- 禁止格式化运行中的 Windows/WinPE、`OpenRCV.exe` 或 ISO 所在分区。
- 安装目标只列出 `DRIVE_FIXED` 固定卷，不列出 `X:` 和可移动介质。
- 分区刷新会默认选中第一行；实际安装入口的确认依据是明确列出目标与启动分区的弹窗，而非要求主动点击分区行。
- 格式化命令执行期间不可中断，取消请求在格式化完成后生效。
- 高级选项校验在临时挂载引导分区前完成，避免校验失败的提前返回遗留临时盘符。

## Windows 11 硬件检查选项

已接入默认开启的高级选项“跳过 Windows 11 硬件检查”：

- 在 `AdvancedSettings` 中增加 `skipWindows11HardwareChecks`。
- 复选框已接入 `StartInstallation`。
- 向离线 SYSTEM 注册表的 `Setup\LabConfig` 写入 `BypassTPMCheck`、`BypassSecureBootCheck`、`BypassRAMCheck`、`BypassCPUCheck` 和 `BypassStorageCheck`。

## 编译与测试分工

- 自动验证仅要求编译通过；代码改动后，由开发助手确认相关构建编译成功。
- 不要求新增或运行编译以外的自动测试，包括单元测试、自动化功能测试和集成测试。
- 其余功能测试由项目负责人本人手动进入 PE 环境完成，不由开发助手代为执行。
- 编译通过仅代表构建成功，不代表功能测试通过；实际功能结果以项目负责人的 PE 手动测试反馈为准。

## 本次结构整理验证

- 环境：Windows 10.0.26300、Visual Studio 2026 / MSVC 19.51、Windows SDK 10.0.26100.0、CMake 4.3.1。
- x64、x86 Release 程序和核心测试均构建成功；两个架构的 CTest 均通过。
- 两个架构均通过独立、无窗口的部署执行冒烟：使用非破坏性命令验证顺序执行、逐行及无换行输出、进度回调、非零退出码中止、启动前取消及运行中取消。
- 两个架构均实际启动界面，验证安装页与高级页切换、更新选项互斥、未选择镜像时的安装拦截和正常退出；已查看实际窗口截图。
- 两个 Release 程序的导入依赖仅含 Windows 系统 DLL，未依赖动态 MSVC 运行时。
- 未执行格式化、DISM 应用、BCDBoot、离线注册表写入或 WinPE 完整安装。

## 后续事项

- 应用尚无 `resources/` 目录、自定义图标、manifest 和版本资源，窗口目前使用 `IDI_APPLICATION`。

## 安全约束

- 不在开发宿主机上执行格式化、DISM 应用镜像或 BCDBoot 等破坏性操作。
- 破坏性操作仅在一次性虚拟机中执行。
- 每条破坏性命令必须记录退出码。
- 执行前必须明确确认安装目标及启动分区。
