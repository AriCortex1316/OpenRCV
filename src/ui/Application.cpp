#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "openrcv/config/OfflineSettings.h"
#include "openrcv/config/Region.h"
#include "openrcv/storage/VolumeEnumerator.h"
#include "openrcv/process/ProcessRunner.h"
#include "openrcv/ui/Application.h"
#include "openrcv/unattend/UnattendBuilder.h"
#include "openrcv/workflow/DeploymentPlan.h"
#include "openrcv/workflow/ImageSourceService.h"

namespace {

constexpr wchar_t kWindowClass[] = L"OpenRCVWindow";
constexpr COLORREF kBlue = RGB(0, 102, 164);
constexpr COLORREF kBlueDark = RGB(0, 82, 137);
constexpr COLORREF kBackground = RGB(245, 246, 247);
constexpr COLORREF kSidebar = RGB(229, 231, 233);
constexpr COLORREF kSurface = RGB(255, 255, 255);
constexpr COLORREF kText = RGB(43, 48, 53);
constexpr COLORREF kMuted = RGB(105, 112, 119);
constexpr COLORREF kBorder = RGB(194, 199, 204);
constexpr COLORREF kDisabled = RGB(154, 160, 166);

enum ControlId : int {
    IDC_NAV_INSTALL = 100,
    IDC_NAV_ADVANCED,
    IDC_ISO_PATH,
    IDC_BROWSE,
    IDC_IMAGE,
    IDC_VOLUME_LIST,
    IDC_REFRESH_VOLUMES,
    IDC_BOOT_VOLUME,
    IDC_FORMAT_TARGET,
    IDC_SKIP_NETWORK,
    IDC_LOCAL_ADMIN,
    IDC_REGION,
    IDC_VALIDATE,
    IDC_PROGRESS,
    IDC_LOG,
    IDC_ADV_DISABLE_UPDATE,
    IDC_ADV_NOTIFY_UPDATE,
    IDC_ADV_SHOW_EXTENSIONS,
    IDC_ADV_SHOW_THIS_PC,
    IDC_ADV_DISABLE_WIDGETS,
    IDC_ADV_DISABLE_FAST_STARTUP,
    IDC_ADV_DISABLE_UAC,
    IDC_ADV_PREVENT_BITLOCKER,
    IDC_ADV_EXCLUDE_DRIVERS,
};

struct AppState {
    HWND window{};
    HWND isoPath{};
    HWND image{};
    HWND volumeList{};
    HWND bootVolume{};
    HWND localAdministrator{};
    HWND region{};
    HWND progress{};
    HWND log{};
    HFONT brandFont{};
    HFONT headingFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HBRUSH surfaceBrush{};
    HBRUSH backgroundBrush{};
    int activePage{};
    std::wstring imageFile;
    std::vector<openrcv::imaging::WindowsImage> images;
    std::vector<openrcv::storage::VolumeInfo> volumes;
    std::vector<openrcv::storage::VolumeInfo> bootVolumes;
    openrcv::workflow::ImageSourceService imageSource;
    std::atomic_bool installationRunning{};
    std::atomic_bool cancellationRequested{};
    std::thread installationThread;
};

AppState g_app;

constexpr UINT WM_INSTALL_LOG = WM_APP + 1;
constexpr UINT WM_INSTALL_PROGRESS = WM_APP + 2;
constexpr UINT WM_INSTALL_COMPLETE = WM_APP + 3;

struct InstallationTask {
    openrcv::workflow::DeploymentPlan plan;
    openrcv::unattend::UnattendOptions unattendOptions;
    openrcv::config::AdvancedSettings advancedSettings;
    std::wstring targetRoot;
    std::wstring temporaryBootRoot;
};

struct InstallationResult {
    bool succeeded{};
    bool cancelled{};
    std::wstring temporaryBootRoot;
};

HFONT CreateUiFont(HWND window, int pointSize, int weight) {
    HDC dc = GetDC(window);
    const int height = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(window, dc);

    LOGFONTW font{};
    font.lfHeight = height;
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfOutPrecision = OUT_DEFAULT_PRECIS;
    font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    font.lfQuality = CLEARTYPE_QUALITY;
    font.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(font.lfFaceName, L"Microsoft YaHei UI");
    return CreateFontIndirectW(&font);
}

void SetControlFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND AddControl(DWORD exStyle, const wchar_t* cls, const wchar_t* text,
                DWORD style, int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, g_app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SetControlFont(control, g_app.bodyFont);
    return control;
}

HWND AddLabel(const wchar_t* text, int x, int y, int width, int height,
              HFONT font = nullptr) {
    HWND label = AddControl(0, L"STATIC", text, SS_LEFT | SS_CENTERIMAGE,
                            x, y, width, height, -1);
    if (font) SetControlFont(label, font);
    return label;
}

HWND AddButton(const wchar_t* text, int x, int y, int width, int height, int id,
               bool enabled = true) {
    HWND button = AddControl(0, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP,
                             x, y, width, height, id);
    EnableWindow(button, enabled);
    return button;
}

void AppendLog(const std::wstring& text) {
    if (!g_app.log) return;
    const int length = GetWindowTextLengthW(g_app.log);
    SendMessageW(g_app.log, EM_SETSEL, length, length);
    const std::wstring line = text + L"\r\n";
    SendMessageW(g_app.log, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(line.c_str()));
}

void PublishInstallationLog(const std::wstring& text) {
    auto* postedText = new std::wstring(text);
    if (!PostMessageW(g_app.window, WM_INSTALL_LOG, 0,
                      reinterpret_cast<LPARAM>(postedText))) {
        delete postedText;
    }
}

void SetInstallationControlsEnabled(bool enabled) {
    const int ids[] = {
        IDC_NAV_INSTALL, IDC_NAV_ADVANCED, IDC_ISO_PATH, IDC_BROWSE, IDC_IMAGE,
        IDC_VOLUME_LIST, IDC_REFRESH_VOLUMES, IDC_BOOT_VOLUME, IDC_FORMAT_TARGET,
        IDC_SKIP_NETWORK, IDC_LOCAL_ADMIN, IDC_REGION,
        IDC_ADV_DISABLE_UPDATE, IDC_ADV_NOTIFY_UPDATE, IDC_ADV_SHOW_EXTENSIONS,
        IDC_ADV_SHOW_THIS_PC, IDC_ADV_DISABLE_WIDGETS,
        IDC_ADV_DISABLE_FAST_STARTUP, IDC_ADV_DISABLE_UAC,
        IDC_ADV_PREVENT_BITLOCKER, IDC_ADV_EXCLUDE_DRIVERS,
    };
    for (const int id : ids) EnableWindow(GetDlgItem(g_app.window, id), enabled);
}

bool IsChecked(int id) {
    return SendMessageW(GetDlgItem(g_app.window, id), BM_GETCHECK, 0, 0) ==
           BST_CHECKED;
}

bool SetUnattendArchitecture(
    openrcv::unattend::UnattendOptions& options,
    openrcv::imaging::ImageArchitecture architecture) {
    switch (architecture) {
    case openrcv::imaging::ImageArchitecture::X86:
        options.architecture = openrcv::unattend::ProcessorArchitecture::X86;
        return true;
    case openrcv::imaging::ImageArchitecture::Amd64:
        options.architecture = openrcv::unattend::ProcessorArchitecture::Amd64;
        return true;
    case openrcv::imaging::ImageArchitecture::Arm64:
        options.architecture = openrcv::unattend::ProcessorArchitecture::Arm64;
        return true;
    case openrcv::imaging::ImageArchitecture::Unknown:
        return false;
    }
    return false;
}

std::wstring FormatGiB(std::uint64_t bytes) {
    wchar_t text[32]{};
    swprintf_s(text, L"%.1f GB",
               static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return text;
}

std::wstring PartitionIdentity(const openrcv::storage::VolumeInfo& volume) {
    if (volume.diskNumber == static_cast<unsigned long>(-1)) return L"—";
    return std::to_wstring(volume.diskNumber) + L":" +
           std::to_wstring(volume.partitionNumber);
}

void SetListItemText(int row, int column, const std::wstring& text) {
    ListView_SetItemText(g_app.volumeList, row, column,
                         const_cast<wchar_t*>(text.c_str()));
}

void RecommendBootVolume(int targetIndex) {
    if (targetIndex < 0 ||
        static_cast<std::size_t>(targetIndex) >= g_app.volumes.size()) return;
    const auto& target = g_app.volumes[static_cast<std::size_t>(targetIndex)];
    for (std::size_t i = 0; i < g_app.bootVolumes.size(); ++i) {
        const auto& candidate = g_app.bootVolumes[i];
        if (candidate.diskNumber != target.diskNumber) continue;
        const bool correctScheme =
            (target.partitionStyle == L"GPT" && candidate.efiSystemPartition) ||
            (target.partitionStyle == L"MBR" && candidate.activePartition);
        if (!correctScheme) continue;
        SendMessageW(g_app.bootVolume, CB_SETCURSEL, i, 0);
        return;
    }
    SendMessageW(g_app.bootVolume, CB_SETCURSEL,
                 static_cast<WPARAM>(CB_ERR), 0);
}

bool IsAdvancedControl(int id) {
    return id >= IDC_ADV_DISABLE_UPDATE && id <= IDC_ADV_EXCLUDE_DRIVERS;
}

BOOL CALLBACK SetPageVisibility(HWND control, LPARAM page) {
    RECT rect{};
    GetWindowRect(control, &rect);
    POINT point{rect.left, rect.top};
    ScreenToClient(g_app.window, &point);
    if (point.x < 180) return TRUE;

    const bool advanced = IsAdvancedControl(GetDlgCtrlID(control));
    ShowWindow(control, (page == 1) == advanced ? SW_SHOW : SW_HIDE);
    return TRUE;
}

void SelectPage(int page) {
    g_app.activePage = page;
    EnumChildWindows(g_app.window, SetPageVisibility, page);
    InvalidateRect(g_app.window, nullptr, TRUE);
    InvalidateRect(GetDlgItem(g_app.window, IDC_NAV_INSTALL), nullptr, TRUE);
    InvalidateRect(GetDlgItem(g_app.window, IDC_NAV_ADVANCED), nullptr, TRUE);
}

void RefreshVolumes() {
    ListView_DeleteAllItems(g_app.volumeList);
    SendMessageW(g_app.bootVolume, CB_RESETCONTENT, 0, 0);
    g_app.volumes = openrcv::storage::EnumerateInstallVolumes();

    for (std::size_t i = 0; i < g_app.volumes.size(); ++i) {
        const auto& volume = g_app.volumes[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(volume.root.c_str());
        item.lParam = static_cast<LPARAM>(i);
        const int row = ListView_InsertItem(g_app.volumeList, &item);
        SetListItemText(row, 1, PartitionIdentity(volume));
        const std::wstring partition = volume.partitionStyle.empty()
            ? L"主分区" : L"主分区, " + volume.partitionStyle;
        SetListItemText(row, 2, partition);
        SetListItemText(row, 3, volume.label.empty() ? L"(无卷标)" : volume.label);
        SetListItemText(row, 4, FormatGiB(volume.totalBytes));
        SetListItemText(row, 5, FormatGiB(volume.freeBytes));
        SetListItemText(row, 6, volume.fileSystem.empty() ? L"未知" : volume.fileSystem);
        SetListItemText(row, 7, volume.busType.empty() ? L"未知" : volume.busType);

    }
    g_app.bootVolumes = openrcv::storage::EnumerateBootVolumes();
    for (const auto& volume : g_app.bootVolumes) {
        const std::wstring mount = volume.root.empty() ? L"(无盘符)" : volume.root;
        const wchar_t* role = volume.efiSystemPartition ? L"EFI 系统分区" : L"活动分区";
        const std::wstring boot = mount + L"  " + role + L"  [" +
            PartitionIdentity(volume) + L"]  " +
            (volume.fileSystem.empty() ? L"未知格式" : volume.fileSystem) + L"  " +
            FormatGiB(volume.totalBytes);
        SendMessageW(g_app.bootVolume, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(boot.c_str()));
    }
    if (!g_app.volumes.empty()) {
        ListView_SetItemState(g_app.volumeList, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        RecommendBootVolume(0);
    }
    AppendLog(L"已刷新固定磁盘卷，共 " +
              std::to_wstring(g_app.volumes.size()) + L" 个；引导分区 " +
              std::to_wstring(g_app.bootVolumes.size()) + L" 个。");
}

void LoadImages() {
    SendMessageW(g_app.image, CB_RESETCONTENT, 0, 0);
    g_app.imageFile.clear();
    g_app.images.clear();
    wchar_t path[32768]{};
    GetWindowTextW(g_app.isoPath, path, static_cast<int>(std::size(path)));
    const auto result = g_app.imageSource.LoadIso(path);
    if (result.success) g_app.imageFile = result.imageFile;
    for (const auto& message : result.messages) AppendLog(message);
    g_app.images = result.images;
    for (const auto& image : g_app.images) {
        const std::wstring item = std::to_wstring(image.index) + L" - " + image.name;
        const LRESULT position = SendMessageW(g_app.image, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(g_app.image, CB_SETITEMDATA, position, image.index);
    }
    if (result.success) SendMessageW(g_app.image, CB_SETCURSEL, 0, 0);
}

void BrowseIso() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = g_app.window;
    dialog.lpstrFilter = L"Windows 镜像 (*.iso)\0*.iso\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"iso";
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(g_app.isoPath, path);
        AppendLog(L"选择镜像：" + std::wstring(path));
        LoadImages();
    }
}

bool WriteUnattendFile(const std::wstring& path, const std::wstring& xml) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring directory = path.substr(0, slash);
        if (!CreateDirectoryW(directory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, xml.data(),
        static_cast<int>(xml.size()), nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) return false;
    std::string utf8(static_cast<std::size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, 0, xml.data(), static_cast<int>(xml.size()),
                        utf8.data(), byteCount, nullptr, nullptr);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool success = WriteFile(file, utf8.data(),
        static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE &&
        written == utf8.size();
    CloseHandle(file);
    return success;
}

bool PathIsOnDrive(const std::wstring& path, const std::wstring& driveRoot) {
    return path.size() >= 2 && driveRoot.size() >= 2 && path[1] == L':' &&
        driveRoot[1] == L':' &&
        std::towupper(path[0]) == std::towupper(driveRoot[0]);
}

std::wstring FormatTargetSafetyError(const std::wstring& targetRoot) {
    wchar_t windowsDirectory[32768]{};
    const UINT windowsLength = GetWindowsDirectoryW(
        windowsDirectory, static_cast<UINT>(std::size(windowsDirectory)));
    if (windowsLength != 0 && windowsLength < std::size(windowsDirectory) &&
        PathIsOnDrive(windowsDirectory, targetRoot)) {
        return L"不能格式化当前正在运行的 Windows/WinPE 所在分区 " + targetRoot;
    }

    wchar_t executablePath[32768]{};
    const DWORD executableLength = GetModuleFileNameW(
        nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    if (executableLength != 0 && executableLength < std::size(executablePath) &&
        PathIsOnDrive(executablePath, targetRoot)) {
        return L"不能格式化 OpenRCV 程序所在分区 " + targetRoot;
    }

    wchar_t isoPath[32768]{};
    GetWindowTextW(g_app.isoPath, isoPath, static_cast<int>(std::size(isoPath)));
    if (PathIsOnDrive(isoPath, targetRoot)) {
        return L"不能格式化 ISO 文件所在分区 " + targetRoot +
            L"；请先把 ISO 移到其他磁盘。";
    }
    return {};
}

bool CreateFormatScript(const std::wstring& targetRoot,
                        std::wstring& scriptPath, std::wstring& error) {
    wchar_t temporaryDirectory[MAX_PATH]{};
    const DWORD directoryLength = GetTempPathW(
        static_cast<DWORD>(std::size(temporaryDirectory)), temporaryDirectory);
    if (directoryLength == 0 || directoryLength >= std::size(temporaryDirectory)) {
        error = L"无法取得 WinPE 临时目录，系统错误 " +
                std::to_wstring(GetLastError());
        return false;
    }
    wchar_t temporaryFile[MAX_PATH]{};
    if (!GetTempFileNameW(temporaryDirectory, L"orc", 0, temporaryFile)) {
        error = L"无法创建 DiskPart 临时脚本，系统错误 " +
                std::to_wstring(GetLastError());
        return false;
    }
    scriptPath = temporaryFile;
    const std::string drive(1, static_cast<char>(targetRoot[0]));
    const std::string script =
        "select volume " + drive + "\r\n"
        "format fs=ntfs quick label=Windows override\r\n"
        "exit\r\n";
    HANDLE file = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"无法写入 DiskPart 临时脚本，系统错误 " +
                std::to_wstring(GetLastError());
        DeleteFileW(scriptPath.c_str());
        scriptPath.clear();
        return false;
    }
    DWORD written = 0;
    const bool success = WriteFile(file, script.data(),
        static_cast<DWORD>(script.size()), &written, nullptr) != FALSE &&
        written == script.size();
    const DWORD writeError = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!success) {
        error = L"无法写入 DiskPart 临时脚本，系统错误 " +
                std::to_wstring(writeError);
        DeleteFileW(scriptPath.c_str());
        scriptPath.clear();
    }
    return success;
}

void RunInstallation(InstallationTask task) {
    bool succeeded = true;
    bool cancelled = false;

    for (const auto& operation : task.plan.operations) {
        if (g_app.cancellationRequested.load(std::memory_order_relaxed)) {
            cancelled = true;
            succeeded = false;
            break;
        }

        PublishInstallationLog(L"开始：" + operation.description);
        if (operation.type == openrcv::workflow::DeploymentOperationType::WriteUnattend) {
            if (operation.arguments.size() < 2 ||
                !WriteUnattendFile(operation.arguments[0],
                    openrcv::unattend::BuildUnattendXml(task.unattendOptions)) ||
                !WriteUnattendFile(operation.arguments[1],
                    openrcv::unattend::BuildOobeXml(task.unattendOptions.region))) {
                PublishInstallationLog(
                    L"失败：无法写入无人值守或 OOBE 配置，系统错误 " +
                    std::to_wstring(GetLastError()));
                succeeded = false;
                break;
            }
            const auto regionResult = openrcv::config::ApplyOfflineDeviceRegion(
                task.targetRoot, task.unattendOptions.region.geoId);
            if (!regionResult.success) {
                PublishInstallationLog(L"失败：" + regionResult.error);
                succeeded = false;
                break;
            }
            PublishInstallationLog(
                L"完成：无人值守、OOBE 和离线设备区域配置已写入。");
            continue;
        }
        if (operation.type ==
            openrcv::workflow::DeploymentOperationType::ConfigureOfflineSettings) {
            const auto settingsResult = openrcv::config::ApplyOfflineSettings(
                operation.arguments.front(), task.advancedSettings);
            if (!settingsResult.success) {
                PublishInstallationLog(L"失败：" + settingsResult.error);
                succeeded = false;
                break;
            }
            PublishInstallationLog(L"完成：高级设置已写入离线 Windows。");
            continue;
        }

        std::wstring executable = operation.executable;
        std::vector<std::wstring> arguments = operation.arguments;
        std::wstring formatScript;
        if (operation.type == openrcv::workflow::DeploymentOperationType::FormatTarget) {
            std::wstring scriptError;
            if (!CreateFormatScript(operation.arguments.front(),
                                    formatScript, scriptError)) {
                PublishInstallationLog(L"失败：" + scriptError);
                succeeded = false;
                break;
            }
            arguments = {L"/s", formatScript};
        }

        std::wstring command = executable;
        for (const auto& argument : arguments) {
            command += L" " + openrcv::process::QuoteCommandLineArgument(argument);
        }
        PublishInstallationLog(L"执行：" + command);

        std::wstring pendingOutput;
        auto flushOutput = [&](bool flushAll) {
            for (;;) {
                const std::size_t end = pendingOutput.find_first_of(L"\r\n");
                if (end == std::wstring::npos) break;
                if (end != 0) {
                    const std::wstring line = pendingOutput.substr(0, end);
                    PublishInstallationLog(line);
                    if (operation.type == openrcv::workflow::DeploymentOperationType::ApplyImage) {
                        const auto progress =
                            openrcv::process::ParseDismProgressPercentage(line);
                        if (progress) PostMessageW(g_app.window, WM_INSTALL_PROGRESS,
                                                   *progress, 0);
                    }
                }
                std::size_t next = end + 1;
                while (next < pendingOutput.size() &&
                       (pendingOutput[next] == L'\r' || pendingOutput[next] == L'\n')) {
                    ++next;
                }
                pendingOutput.erase(0, next);
            }
            if (flushAll && !pendingOutput.empty()) {
                PublishInstallationLog(pendingOutput);
                if (operation.type == openrcv::workflow::DeploymentOperationType::ApplyImage) {
                    const auto progress =
                        openrcv::process::ParseDismProgressPercentage(pendingOutput);
                    if (progress) PostMessageW(g_app.window, WM_INSTALL_PROGRESS,
                                               *progress, 0);
                }
                pendingOutput.clear();
            }
        };

        openrcv::process::ProcessRunOptions runOptions;
        runOptions.outputCallback = [&](std::wstring_view chunk) {
            pendingOutput.append(chunk.data(), chunk.size());
            flushOutput(false);
        };
        if (operation.type != openrcv::workflow::DeploymentOperationType::FormatTarget) {
            runOptions.cancellationRequested = &g_app.cancellationRequested;
        }
        const auto result = openrcv::process::RunHidden(
            executable, arguments, runOptions);
        if (!formatScript.empty()) DeleteFileW(formatScript.c_str());
        flushOutput(true);
        if (result.cancelled) {
            PublishInstallationLog(L"命令已取消。");
            cancelled = true;
            succeeded = false;
            break;
        }
        if (!result.Succeeded()) {
            PublishInstallationLog(
                L"命令失败，退出码 " + std::to_wstring(result.exitCode) +
                L"，系统错误 " + std::to_wstring(result.systemError));
            succeeded = false;
            break;
        }
        PublishInstallationLog(L"完成，退出码 0。");
    }

    if (task.temporaryBootRoot.empty() == false) {
        openrcv::storage::RemoveTemporaryMount(task.temporaryBootRoot);
        PublishInstallationLog(
            L"已移除引导分区临时盘符 " + task.temporaryBootRoot);
    }
    if (cancelled) PublishInstallationLog(L"安装已由用户取消。");

    auto* result = new InstallationResult{
        succeeded, cancelled, std::move(task.temporaryBootRoot)};
    PostMessageW(g_app.window, WM_INSTALL_COMPLETE, 0,
                 reinterpret_cast<LPARAM>(result));
}

void StartInstallation() {
    if (g_app.installationRunning.load(std::memory_order_relaxed)) {
        if (!g_app.cancellationRequested.exchange(true, std::memory_order_relaxed)) {
            AppendLog(L"正在请求取消；如果正在格式化，将在格式化完成后停止。");
            SetWindowTextW(GetDlgItem(g_app.window, IDC_VALIDATE), L"正在取消…");
            EnableWindow(GetDlgItem(g_app.window, IDC_VALIDATE), FALSE);
        }
        return;
    }
    const int targetIndex = ListView_GetNextItem(g_app.volumeList, -1, LVNI_SELECTED);
    if (targetIndex < 0 ||
        static_cast<std::size_t>(targetIndex) >= g_app.volumes.size()) {
        MessageBoxW(g_app.window, L"请选择分区。",
                    L"OpenRCV", MB_ICONWARNING);
        return;
    }
    if (SendMessageW(g_app.image, CB_GETCURSEL, 0, 0) == CB_ERR) {
        MessageBoxW(g_app.window, L"请先选择 Windows ISO 和系统版本。",
                    L"OpenRCV", MB_ICONWARNING);
        return;
    }
    const LRESULT bootIndex = SendMessageW(g_app.bootVolume, CB_GETCURSEL, 0, 0);
    if (bootIndex == CB_ERR ||
        static_cast<std::size_t>(bootIndex) >= g_app.bootVolumes.size()) {
        MessageBoxW(g_app.window,
            L"目标磁盘上没有匹配的 EFI 系统分区或 MBR 活动分区。",
            L"OpenRCV", MB_ICONWARNING);
        return;
    }
    const bool createLocalAdministrator = SendMessageW(
        GetDlgItem(g_app.window, IDC_SKIP_NETWORK),
        BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t localAdministrator[64]{};
    GetWindowTextW(g_app.localAdministrator, localAdministrator,
                   static_cast<int>(std::size(localAdministrator)));
    if (createLocalAdministrator && localAdministrator[0] == L'\0') {
        MessageBoxW(g_app.window, L"请填写本地管理员用户名。",
                    L"OpenRCV", MB_ICONWARNING);
        SetFocus(g_app.localAdministrator);
        return;
    }
    const auto& target = g_app.volumes[static_cast<std::size_t>(targetIndex)];
    auto& boot = g_app.bootVolumes[static_cast<std::size_t>(bootIndex)];
    const bool format = SendMessageW(GetDlgItem(g_app.window, IDC_FORMAT_TARGET),
                                     BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (format) {
        const std::wstring safetyError = FormatTargetSafetyError(target.root);
        if (!safetyError.empty()) {
            MessageBoxW(g_app.window, safetyError.c_str(), L"OpenRCV",
                        MB_ICONERROR);
            return;
        }
    }
    const LRESULT imageSelection = SendMessageW(g_app.image, CB_GETCURSEL, 0, 0);
    if (imageSelection < 0 ||
        static_cast<std::size_t>(imageSelection) >= g_app.images.size()) {
        MessageBoxW(g_app.window, L"无法读取所选镜像的信息。",
                    L"OpenRCV", MB_ICONERROR);
        return;
    }
    const auto& selectedImage =
        g_app.images[static_cast<std::size_t>(imageSelection)];
    const LRESULT regionSelection =
        SendMessageW(g_app.region, CB_GETCURSEL, 0, 0);
    const auto& regions = openrcv::config::SupportedRegions();
    if (regionSelection < 0 ||
        static_cast<std::size_t>(regionSelection) >= regions.size()) {
        MessageBoxW(g_app.window, L"请选择区域。", L"OpenRCV", MB_ICONWARNING);
        return;
    }
    const auto& selectedRegion =
        regions[static_cast<std::size_t>(regionSelection)];
    const int imageIndex = selectedImage.index;
    openrcv::unattend::UnattendOptions selectedUnattendOptions;
    if (!SetUnattendArchitecture(selectedUnattendOptions,
                                  selectedImage.architecture)) {
        MessageBoxW(g_app.window, L"DISM 没有返回所选镜像的处理器架构。",
                    L"OpenRCV", MB_ICONERROR);
        return;
    }

    const std::wstring bootDisplay = boot.root.empty() ? L"无盘符" : boot.root;
    std::wstring confirmation = L"即将把 Windows 安装到：\n\n" + target.root +
        L"  磁盘/分区 " + PartitionIdentity(target) + L"\n引导分区：" +
        bootDisplay + L"  " + PartitionIdentity(boot) + L"  " +
        (boot.efiSystemPartition ? L"EFI 系统分区" : L"MBR 活动分区") + L"\n\n";
    if (format) confirmation += L"目标分区将被格式化，现有数据会永久丢失。\n\n";
    confirmation += L"是否确认开始安装？";
    if (MessageBoxW(g_app.window, confirmation.c_str(), L"确认安装目标",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    const bool temporaryBootMount = boot.root.empty();
    std::wstring mountError;
    if (!openrcv::storage::MountVolumeWithTemporaryLetter(boot, mountError)) {
        MessageBoxW(g_app.window, mountError.c_str(), L"OpenRCV", MB_ICONERROR);
        return;
    }
    if (temporaryBootMount) {
        AppendLog(L"已将引导分区临时挂载到 " + boot.root);
    }

    openrcv::workflow::DeploymentRequest request;
    request.imageFile = g_app.imageFile;
    request.imageIndex = imageIndex;
    request.targetRoot = target.root;
    request.systemPartitionRoot = boot.root;
    request.bootFirmware = boot.efiSystemPartition
        ? openrcv::workflow::BootFirmware::Uefi
        : openrcv::workflow::BootFirmware::Bios;
    request.formatTarget = format;
    if (format) request.confirmedFormatTarget = target.root;
    openrcv::config::AdvancedSettings advancedSettings;
    advancedSettings.disableWindowsUpdate = IsChecked(IDC_ADV_DISABLE_UPDATE);
    advancedSettings.notifyBeforeWindowsUpdateDownload =
        IsChecked(IDC_ADV_NOTIFY_UPDATE);
    if (advancedSettings.disableWindowsUpdate &&
        advancedSettings.notifyBeforeWindowsUpdateDownload) {
        MessageBoxW(g_app.window,
            L"“禁用 Windows Update”和“仅通知下载”不能同时启用。",
            L"OpenRCV", MB_ICONWARNING);
        return;
    }
    if (advancedSettings.notifyBeforeWindowsUpdateDownload) {
        if (selectedImage.editionClass ==
            openrcv::imaging::ImageEditionClass::Unknown) {
            MessageBoxW(g_app.window,
                L"所选镜像未提供 EDITIONID，无法安全选择 Home 注册表或 Pro+ 本地策略实现。",
                L"OpenRCV", MB_ICONERROR);
            return;
        }
        advancedSettings.windowsUpdateNotificationMethod =
            selectedImage.editionClass ==
                    openrcv::imaging::ImageEditionClass::Home
                ? openrcv::config::WindowsUpdateNotificationMethod::Registry
                : openrcv::config::WindowsUpdateNotificationMethod::LocalGroupPolicy;
    }
    advancedSettings.showFileExtensions = IsChecked(IDC_ADV_SHOW_EXTENSIONS);
    advancedSettings.showThisPcOnDesktop = IsChecked(IDC_ADV_SHOW_THIS_PC);
    advancedSettings.disableWidgets = IsChecked(IDC_ADV_DISABLE_WIDGETS);
    advancedSettings.disableFastStartup = IsChecked(IDC_ADV_DISABLE_FAST_STARTUP);
    advancedSettings.disableUac = IsChecked(IDC_ADV_DISABLE_UAC);
    advancedSettings.preventBitLocker = IsChecked(IDC_ADV_PREVENT_BITLOCKER);
    advancedSettings.excludeWindowsUpdateDrivers = IsChecked(IDC_ADV_EXCLUDE_DRIVERS);
    request.configureOfflineSettings = advancedSettings.AnyEnabled();
    const auto plan = openrcv::workflow::BuildDeploymentPlan(request);
    if (!plan.IsValid()) {
        std::wstring errors = L"无法开始安装：\n";
        for (const auto& error : plan.errors) errors += L"\n• " + error;
        if (temporaryBootMount) {
            openrcv::storage::RemoveTemporaryMount(boot.root);
            boot.root.clear();
        }
        MessageBoxW(g_app.window, errors.c_str(), L"OpenRCV", MB_ICONERROR);
        return;
    }

    InstallationTask task;
    task.plan = plan;
    task.unattendOptions = selectedUnattendOptions;
    task.unattendOptions.pauseWindowsUpdate =
        advancedSettings.disableWindowsUpdate;
    task.unattendOptions.createLocalAdministrator = createLocalAdministrator;
    task.unattendOptions.skipOnlineAccountScreens = createLocalAdministrator;
    task.unattendOptions.localAdministrator = localAdministrator;
    task.unattendOptions.region = selectedRegion;
    task.advancedSettings = advancedSettings;
    task.targetRoot = target.root;
    if (temporaryBootMount) task.temporaryBootRoot = boot.root;

    g_app.cancellationRequested.store(false, std::memory_order_relaxed);
    g_app.installationRunning.store(true, std::memory_order_relaxed);
    SetInstallationControlsEnabled(false);
    SendMessageW(g_app.progress, PBM_SETPOS, 0, 0);
    SetWindowTextW(GetDlgItem(g_app.window, IDC_VALIDATE), L"取消安装");
    EnableWindow(GetDlgItem(g_app.window, IDC_VALIDATE), TRUE);
    AppendLog(L"安装任务已启动。");
    g_app.installationThread = std::thread(RunInstallation, std::move(task));
}

void AddListColumn(int index, int width, const wchar_t* text) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    column.cx = width;
    column.pszText = const_cast<wchar_t*>(text);
    column.fmt = index >= 4 && index <= 5 ? LVCFMT_RIGHT : LVCFMT_LEFT;
    ListView_InsertColumn(g_app.volumeList, index, &column);
}

void CreateUi(HWND window) {
    g_app.window = window;
    g_app.brandFont = CreateUiFont(window, 15, FW_SEMIBOLD);
    g_app.headingFont = CreateUiFont(window, 11, FW_SEMIBOLD);
    g_app.bodyFont = CreateUiFont(window, 10, FW_NORMAL);
    g_app.smallFont = CreateUiFont(window, 9, FW_NORMAL);
    g_app.surfaceBrush = CreateSolidBrush(kSurface);
    g_app.backgroundBrush = CreateSolidBrush(kBackground);

    AddButton(L"▣\n系统重装", 0, 58, 166, 104, IDC_NAV_INSTALL);
    AddButton(L"⚙\n高级设置", 0, 162, 166, 104, IDC_NAV_ADVANCED);

    AddLabel(L"请选择系统安装文件：", 194, 76, 230, 24, g_app.headingFont);
    g_app.isoPath = AddControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
        ES_AUTOHSCROLL | WS_TABSTOP, 194, 102, 778, 31, IDC_ISO_PATH);
    SendMessageW(g_app.isoPath, EM_SETMARGINS,
                 EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    AddButton(L"选择镜像", 984, 101, 120, 33, IDC_BROWSE);

    AddLabel(L"请选择要安装的分区：", 194, 146, 220, 24, g_app.headingFont);
    AddLabel(L"单击列表选择目标；写盘功能接入后将要求再次确认磁盘身份。",
             416, 147, 520, 22, g_app.smallFont);
    AddButton(L"刷新分区", 984, 141, 120, 30, IDC_REFRESH_VOLUMES);
    g_app.volumeList = AddControl(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
        194, 176, 910, 205, IDC_VOLUME_LIST);
    ListView_SetExtendedListViewStyle(g_app.volumeList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES |
        LVS_EX_LABELTIP);
    ListView_SetBkColor(g_app.volumeList, kSurface);
    ListView_SetTextBkColor(g_app.volumeList, kSurface);
    ListView_SetTextColor(g_app.volumeList, kText);
    AddListColumn(0, 76, L"盘符");
    AddListColumn(1, 72, L"序号");
    AddListColumn(2, 124, L"分区类型");
    AddListColumn(3, 176, L"卷标");
    AddListColumn(4, 112, L"总大小");
    AddListColumn(5, 112, L"可用大小");
    AddListColumn(6, 94, L"文件系统");
    AddListColumn(7, 102, L"类型");

    AddLabel(L"请选择要安装的系统：", 194, 397, 230, 24, g_app.headingFont);
    g_app.image = AddControl(0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        194, 423, 910, 260, IDC_IMAGE);
    HWND format = AddControl(0, L"BUTTON", L"格式化安装分区",
        BS_AUTOCHECKBOX | WS_TABSTOP, 194, 485, 180, 24, IDC_FORMAT_TARGET);
    SendMessageW(format, BM_SETCHECK, BST_CHECKED, 0);
    AddControl(0, L"BUTTON", L"跳过联网并创建本地管理员：",
        BS_AUTOCHECKBOX | WS_TABSTOP, 194, 521, 230, 24, IDC_SKIP_NETWORK);
    g_app.localAdministrator = AddControl(WS_EX_CLIENTEDGE, L"EDIT", L"User",
        ES_AUTOHSCROLL | WS_TABSTOP, 424, 517, 72, 28, IDC_LOCAL_ADMIN);
    SendMessageW(g_app.localAdministrator, EM_SETMARGINS,
                 EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));

    AddLabel(L"区域：", 510, 485, 58, 24);
    g_app.region = AddControl(0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 568, 481, 178, 160, IDC_REGION);
    for (const auto& region : openrcv::config::SupportedRegions()) {
        const std::wstring label = region.displayName + L" (" + region.locale + L")";
        SendMessageW(g_app.region, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(g_app.region, CB_SETCURSEL, 0, 0);

    AddLabel(L"引导分区：", 770, 485, 92, 24);
    g_app.bootVolume = AddControl(0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        862, 481, 242, 220, IDC_BOOT_VOLUME);
    g_app.progress = AddControl(0, PROGRESS_CLASSW, L"",
        PBS_SMOOTH, 194, 567, 490, 22, IDC_PROGRESS);
    SendMessageW(g_app.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    AddButton(L"开始安装", 710, 558, 260, 40, IDC_VALIDATE);

    AddLabel(L"运行状态", 194, 625, 75, 22, g_app.smallFont);
    g_app.log = AddControl(0, L"EDIT",
        L"OpenRCV 已启动。\r\n",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        274, 615, 830, 91, IDC_LOG);
    SetControlFont(g_app.log, g_app.smallFont);

    const struct {
        const wchar_t* text;
        int id;
        bool checked;
    } advancedSettings[] = {
        {L"禁用 Windows Update（计划任务每周续期暂停）", IDC_ADV_DISABLE_UPDATE, false},
        {L"Windows Update 仅通知，不自动下载或安装", IDC_ADV_NOTIFY_UPDATE, false},
        {L"显示已知文件类型的扩展名", IDC_ADV_SHOW_EXTENSIONS, true},
        {L"在桌面显示“此电脑”图标", IDC_ADV_SHOW_THIS_PC, true},
        {L"禁用 Widgets", IDC_ADV_DISABLE_WIDGETS, false},
        {L"禁用 Fast Startup（快速启动）", IDC_ADV_DISABLE_FAST_STARTUP, false},
        {L"禁用 UAC（不推荐，会降低安全性）", IDC_ADV_DISABLE_UAC, false},
        {L"阻止 Windows 自动启用 BitLocker", IDC_ADV_PREVENT_BITLOCKER, false},
        {L"禁止 Windows Update 自动更新驱动", IDC_ADV_EXCLUDE_DRIVERS, false},
    };
    int advancedY = 160;
    for (const auto& setting : advancedSettings) {
        HWND checkbox = AddControl(0, L"BUTTON", setting.text,
            BS_AUTOCHECKBOX | WS_TABSTOP, 220, advancedY, 620, 30, setting.id);
        SendMessageW(checkbox, BM_SETCHECK,
                     setting.checked ? BST_CHECKED : BST_UNCHECKED, 0);
        advancedY += 48;
    }

    SelectPage(0);
    RefreshVolumes();
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawUiText(HDC dc, const wchar_t* text, RECT rect, HFONT font,
                COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, oldFont);
}

void PaintUi(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillSolidRect(dc, client, kBackground);
    FillSolidRect(dc, RECT{0, 0, client.right, 58}, kBlueDark);
    FillSolidRect(dc, RECT{0, 58, 166, client.bottom}, kSidebar);
    FillSolidRect(dc, RECT{166, 58, 167, client.bottom}, kBorder);

    HBRUSH markBrush = CreateSolidBrush(RGB(104, 181, 220));
    HGDIOBJ oldBrush = SelectObject(dc, markBrush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, 20, 13, 52, 45);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(markBrush);
    DrawUiText(dc, L"O", RECT{20, 13, 52, 45}, g_app.headingFont,
               kSurface, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawUiText(dc, L"OpenRCV", RECT{64, 8, 330, 38},
               g_app.brandFont, kSurface, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawUiText(dc, L"v0.1  ·  Windows 安装程序", RECT{65, 33, 330, 53},
               g_app.smallFont, RGB(190, 221, 239),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (g_app.activePage == 0) {
        FillSolidRect(dc, RECT{194, 466, 1104, 467}, kBorder);
        FillSolidRect(dc, RECT{194, 605, 1104, 606}, kBorder);
    } else {
        DrawUiText(dc, L"高级设置", RECT{202, 88, 520, 126},
                   g_app.brandFont, kText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawUiText(dc, L"这些选项将在系统部署后写入离线 Windows。",
                   RECT{203, 124, 720, 150}, g_app.smallFont, kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        FillSolidRect(dc, RECT{202, 150, 1104, 151}, kBorder);
    }
    EndPaint(window, &paint);
}

void DrawButton(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool nav = id == IDC_NAV_INSTALL || id == IDC_NAV_ADVANCED;
    const bool activeNav = (id == IDC_NAV_INSTALL && g_app.activePage == 0) ||
                           (id == IDC_NAV_ADVANCED && g_app.activePage == 1);
    const bool primary = id == IDC_VALIDATE;

    COLORREF background = kSurface;
    COLORREF border = kBorder;
    COLORREF text = kText;
    if (nav) {
        background = activeNav ? RGB(210, 214, 217) : kSidebar;
        border = activeNav ? kBlue : kSidebar;
        text = disabled ? kDisabled : activeNav ? kBlueDark : kMuted;
    } else if (primary) {
        background = pressed ? kBlueDark : kBlue;
        border = background;
        text = kSurface;
    } else if (pressed) {
        background = RGB(224, 240, 249);
        border = kBlue;
    }
    if (disabled && !nav) {
        background = RGB(235, 237, 239);
        border = RGB(211, 214, 217);
        text = kDisabled;
    }

    HBRUSH brush = CreateSolidBrush(background);
    HPEN pen = CreatePen(PS_SOLID, nav && activeNav ? 3 : 1, border);
    HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
    HGDIOBJ oldPen = SelectObject(item.hDC, pen);
    if (primary) {
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
                  item.rcItem.right, item.rcItem.bottom, 4, 4);
    } else {
        Rectangle(item.hDC, item.rcItem.left, item.rcItem.top,
                  item.rcItem.right, item.rcItem.bottom);
    }
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t caption[128]{};
    GetWindowTextW(item.hwndItem, caption, static_cast<int>(std::size(caption)));
    RECT textRect = item.rcItem;
    if (pressed) OffsetRect(&textRect, 0, 1);
    const HFONT font = primary ? g_app.headingFont : g_app.bodyFont;
    if (nav) {
        RECT measured{0, 0, textRect.right - textRect.left,
                      textRect.bottom - textRect.top};
        HGDIOBJ oldFont = SelectObject(item.hDC, font);
        DrawTextW(item.hDC, caption, -1, &measured,
                  DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
        SelectObject(item.hDC, oldFont);

        const int blockHeight = measured.bottom - measured.top;
        const int availableHeight = textRect.bottom - textRect.top;
        textRect.top += std::max(0, (availableHeight - blockHeight) / 2);
        textRect.bottom = textRect.top + blockHeight;
        DrawUiText(item.hDC, caption, textRect, font, text,
                   DT_CENTER | DT_WORDBREAK);
    } else {
        DrawUiText(item.hDC, caption, textRect, font, text,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focusRect = item.rcItem;
        InflateRect(&focusRect, -4, -4);
        DrawFocusRect(item.hDC, &focusRect);
    }
}

void DestroyUiResources() {
    if (g_app.brandFont) DeleteObject(g_app.brandFont);
    if (g_app.headingFont) DeleteObject(g_app.headingFont);
    if (g_app.bodyFont) DeleteObject(g_app.bodyFont);
    if (g_app.smallFont) DeleteObject(g_app.smallFont);
    if (g_app.surfaceBrush) DeleteObject(g_app.surfaceBrush);
    if (g_app.backgroundBrush) DeleteObject(g_app.backgroundBrush);
}

LRESULT DrawVolumeList(NMLVCUSTOMDRAW& draw) {
    if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        const int row = static_cast<int>(draw.nmcd.dwItemSpec);
        if ((ListView_GetItemState(g_app.volumeList, row, LVIS_SELECTED) &
             LVIS_SELECTED) != 0) {
            draw.nmcd.uItemState &= ~CDIS_SELECTED;
            draw.clrText = kSurface;
            draw.clrTextBk = kBlueDark;
            return CDRF_NEWFONT;
        }
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateUi(window);
        return 0;
    case WM_PAINT:
        PaintUi(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        if (reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->CtlType == ODT_BUTTON) {
            DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lParam))
                         ? kText : kDisabled);
        SetBkColor(dc, kBackground);
        return reinterpret_cast<LRESULT>(g_app.backgroundBrush);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kSurface);
        return reinterpret_cast<LRESULT>(g_app.surfaceBrush);
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->idFrom == IDC_VOLUME_LIST && header->code == NM_CUSTOMDRAW) {
            return DrawVolumeList(*reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
        }
        if (header->idFrom == IDC_VOLUME_LIST && header->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((change->uNewState & LVIS_SELECTED) != 0) {
                RecommendBootVolume(change->iItem);
            }
        }
        break;
    }
    case WM_INSTALL_LOG: {
        std::unique_ptr<std::wstring> text(
            reinterpret_cast<std::wstring*>(lParam));
        if (text) AppendLog(*text);
        return 0;
    }
    case WM_INSTALL_PROGRESS:
        SendMessageW(g_app.progress, PBM_SETPOS, wParam, 0);
        return 0;
    case WM_INSTALL_COMPLETE: {
        std::unique_ptr<InstallationResult> result(
            reinterpret_cast<InstallationResult*>(lParam));
        if (g_app.installationThread.joinable()) g_app.installationThread.join();
        g_app.installationRunning.store(false, std::memory_order_relaxed);
        g_app.cancellationRequested.store(false, std::memory_order_relaxed);
        SetInstallationControlsEnabled(true);
        SetWindowTextW(GetDlgItem(g_app.window, IDC_VALIDATE), L"开始安装");
        EnableWindow(GetDlgItem(g_app.window, IDC_VALIDATE), TRUE);
        if (result && !result->temporaryBootRoot.empty()) {
            for (auto& boot : g_app.bootVolumes) {
                if (boot.root == result->temporaryBootRoot) boot.root.clear();
            }
        }
        if (result && result->succeeded) {
            SendMessageW(g_app.progress, PBM_SETPOS, 100, 0);
            MessageBoxW(g_app.window,
                        L"Windows 安装和引导配置已完成。\n\n安装成功，请重启电脑。",
                        L"安装成功", MB_OK | MB_ICONINFORMATION);
        } else if (result && result->cancelled) {
            MessageBoxW(g_app.window, L"安装已取消，请查看运行日志。",
                        L"OpenRCV", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_app.window, L"安装未完成，请查看运行日志。",
                        L"OpenRCV", MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_NAV_INSTALL: SelectPage(0); return 0;
        case IDC_NAV_ADVANCED: SelectPage(1); return 0;
        case IDC_BROWSE: BrowseIso(); return 0;
        case IDC_REFRESH_VOLUMES: RefreshVolumes(); return 0;
        case IDC_VALIDATE: StartInstallation(); return 0;
        case IDC_ADV_DISABLE_UPDATE:
            if (HIWORD(wParam) == BN_CLICKED && IsChecked(IDC_ADV_DISABLE_UPDATE)) {
                SendMessageW(GetDlgItem(window, IDC_ADV_NOTIFY_UPDATE),
                             BM_SETCHECK, BST_UNCHECKED, 0);
            }
            return 0;
        case IDC_ADV_NOTIFY_UPDATE:
            if (HIWORD(wParam) == BN_CLICKED && IsChecked(IDC_ADV_NOTIFY_UPDATE)) {
                SendMessageW(GetDlgItem(window, IDC_ADV_DISABLE_UPDATE),
                             BM_SETCHECK, BST_UNCHECKED, 0);
            }
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (g_app.installationRunning.load(std::memory_order_relaxed)) {
            if (MessageBoxW(window,
                    L"安装仍在进行。是否请求取消？\n\n格式化阶段会等待完成后再停止。",
                    L"OpenRCV", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                g_app.cancellationRequested.store(true, std::memory_order_relaxed);
                SetWindowTextW(GetDlgItem(window, IDC_VALIDATE), L"正在取消…");
                EnableWindow(GetDlgItem(window, IDC_VALIDATE), FALSE);
            }
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_app.cancellationRequested.store(true, std::memory_order_relaxed);
        if (g_app.installationThread.joinable()) g_app.installationThread.join();
        g_app.imageSource.Close();
        DestroyUiResources();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int openrcv::ui::RunApplication(HINSTANCE instance, int showCommand) {
    INITCOMMONCONTROLSEX controls{
        sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                            WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT windowRect{0, 0, 1130, 735};
    AdjustWindowRectEx(&windowRect, style, FALSE, 0);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = workArea.left +
        std::max(0L, (workArea.right - workArea.left - width) / 2);
    const int y = workArea.top +
        std::max(0L, (workArea.bottom - workArea.top - height) / 2);
    HWND window = CreateWindowExW(0, kWindowClass, L"OpenRCV",
        style, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
