#include "openrcv/workflow/DeploymentRunner.h"

#include <windows.h>

#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include "openrcv/process/ProcessRunner.h"
#include "openrcv/storage/VolumeEnumerator.h"

namespace openrcv::workflow {
namespace {

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

} // namespace

DeploymentResult RunDeployment(
    DeploymentTask task, const std::atomic_bool& cancellationRequested,
    const DeploymentCallbacks& callbacks) {
    bool succeeded = true;
    bool cancelled = false;
    const auto log = [&](const std::wstring& text) {
        if (callbacks.log) callbacks.log(text);
    };

    for (const auto& operation : task.plan.operations) {
        if (cancellationRequested.load(std::memory_order_relaxed)) {
            cancelled = true;
            succeeded = false;
            break;
        }

        log(L"开始：" + operation.description);
        if (operation.type == DeploymentOperationType::WriteUnattend) {
            if (operation.arguments.size() < 2 ||
                !WriteUnattendFile(operation.arguments[0],
                    unattend::BuildUnattendXml(task.unattendOptions)) ||
                !WriteUnattendFile(operation.arguments[1],
                    unattend::BuildOobeXml(task.unattendOptions.region))) {
                log(L"失败：无法写入无人值守或 OOBE 配置，系统错误 " +
                    std::to_wstring(GetLastError()));
                succeeded = false;
                break;
            }
            const auto regionResult = config::ApplyOfflineDeviceRegion(
                task.targetRoot, task.unattendOptions.region.geoId);
            if (!regionResult.success) {
                log(L"失败：" + regionResult.error);
                succeeded = false;
                break;
            }
            log(L"完成：无人值守、OOBE 和离线设备区域配置已写入。");
            continue;
        }
        if (operation.type == DeploymentOperationType::ConfigureOfflineSettings) {
            const auto settingsResult = config::ApplyOfflineSettings(
                operation.arguments.front(), task.advancedSettings);
            if (!settingsResult.success) {
                log(L"失败：" + settingsResult.error);
                succeeded = false;
                break;
            }
            log(L"完成：高级设置已写入离线 Windows。");
            continue;
        }

        std::wstring formatScript;
        std::vector<std::wstring> formatArguments;
        if (operation.type == DeploymentOperationType::FormatTarget) {
            std::wstring scriptError;
            if (!CreateFormatScript(operation.arguments.front(),
                                    formatScript, scriptError)) {
                log(L"失败：" + scriptError);
                succeeded = false;
                break;
            }
            formatArguments = {L"/s", formatScript};
        }
        const auto& arguments = formatScript.empty()
            ? operation.arguments : formatArguments;

        std::wstring command = operation.executable;
        for (const auto& argument : arguments) {
            command += L" " + process::QuoteCommandLineArgument(argument);
        }
        log(L"执行：" + command);

        const auto publishOutput = [&](const std::wstring& line) {
            log(line);
            if (operation.type == DeploymentOperationType::ApplyImage &&
                callbacks.progress) {
                const auto progress = process::ParseDismProgressPercentage(line);
                if (progress) callbacks.progress(*progress);
            }
        };
        std::wstring pendingOutput;
        const auto flushOutput = [&](bool flushAll) {
            for (;;) {
                const std::size_t end = pendingOutput.find_first_of(L"\r\n");
                if (end == std::wstring::npos) break;
                if (end != 0) publishOutput(pendingOutput.substr(0, end));
                std::size_t next = end + 1;
                while (next < pendingOutput.size() &&
                       (pendingOutput[next] == L'\r' || pendingOutput[next] == L'\n')) {
                    ++next;
                }
                pendingOutput.erase(0, next);
            }
            if (flushAll && !pendingOutput.empty()) {
                publishOutput(pendingOutput);
                pendingOutput.clear();
            }
        };

        process::ProcessRunOptions runOptions;
        runOptions.outputCallback = [&](std::wstring_view chunk) {
            pendingOutput.append(chunk.data(), chunk.size());
            flushOutput(false);
        };
        if (operation.type != DeploymentOperationType::FormatTarget) {
            runOptions.cancellationRequested = &cancellationRequested;
        }
        const auto result = process::RunHidden(
            operation.executable, arguments, runOptions);
        if (!formatScript.empty()) DeleteFileW(formatScript.c_str());
        flushOutput(true);
        if (result.cancelled) {
            log(L"命令已取消。");
            cancelled = true;
            succeeded = false;
            break;
        }
        if (!result.Succeeded()) {
            log(L"命令失败，退出码 " + std::to_wstring(result.exitCode) +
                L"，系统错误 " + std::to_wstring(result.systemError));
            succeeded = false;
            break;
        }
        log(L"完成，退出码 0。");
    }

    if (!task.temporaryBootRoot.empty()) {
        storage::RemoveTemporaryMount(task.temporaryBootRoot);
        log(L"已移除引导分区临时盘符 " + task.temporaryBootRoot);
    }
    if (cancelled) log(L"安装已由用户取消。");

    return {succeeded, cancelled, std::move(task.temporaryBootRoot)};
}

} // namespace openrcv::workflow
