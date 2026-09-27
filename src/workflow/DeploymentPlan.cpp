#include "openrcv/workflow/DeploymentPlan.h"

#include <cwctype>
#include <utility>

namespace openrcv::workflow {
namespace {

bool IsDriveRoot(const std::wstring& path) {
    return path.size() == 3 && std::iswalpha(path[0]) != 0 && path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/');
}

wchar_t NormalizedDriveLetter(const std::wstring& root) {
    return static_cast<wchar_t>(std::towupper(root[0]));
}

bool SameDrive(const std::wstring& left, const std::wstring& right) {
    return IsDriveRoot(left) && IsDriveRoot(right) &&
           NormalizedDriveLetter(left) == NormalizedDriveLetter(right);
}

std::wstring JoinPath(const std::wstring& root, const wchar_t* relative) {
    return root + relative;
}

} // namespace

DeploymentPlan BuildDeploymentPlan(const DeploymentRequest& request) {
    DeploymentPlan plan;
    if (request.imageFile.empty()) plan.errors.emplace_back(L"没有指定 Windows 镜像文件。");
    if (request.imageIndex <= 0) plan.errors.emplace_back(L"镜像索引必须大于零。");
    if (!IsDriveRoot(request.targetRoot)) {
        plan.errors.emplace_back(L"目标必须是明确的驱动器根路径，例如 C:\\。");
    } else if (NormalizedDriveLetter(request.targetRoot) == L'X') {
        plan.errors.emplace_back(L"不能把 WinPE 的 X: RAM 盘作为安装目标。");
    }
    if (!request.systemPartitionRoot.empty() && !IsDriveRoot(request.systemPartitionRoot)) {
        plan.errors.emplace_back(L"系统分区必须是明确的驱动器根路径。");
    }
    if (request.formatTarget && !SameDrive(request.targetRoot, request.confirmedFormatTarget)) {
        plan.errors.emplace_back(L"格式化目标尚未经过逐字确认。");
    }
    if (!plan.IsValid()) return plan;

    if (request.formatTarget) {
        plan.operations.push_back({
            DeploymentOperationType::FormatTarget,
            L"格式化目标分区 " + request.targetRoot,
            L"format.com",
            {request.targetRoot.substr(0, 2), L"/FS:NTFS", L"/Q", L"/Y", L"/V:Windows"},
            true,
        });
    }

    plan.operations.push_back({
        DeploymentOperationType::ApplyImage,
        L"应用 Windows 镜像到 " + request.targetRoot,
        L"dism.exe",
        {L"/Apply-Image", L"/ImageFile:" + request.imageFile,
         L"/Index:" + std::to_wstring(request.imageIndex), L"/ApplyDir:" + request.targetRoot},
        true,
    });
    plan.operations.push_back({
        DeploymentOperationType::WriteUnattend,
        L"写入无人值守配置",
        L"",
        {JoinPath(request.targetRoot, L"Windows\\Panther\\unattend.xml")},
        true,
    });

    std::vector<std::wstring> bootArguments{JoinPath(request.targetRoot, L"Windows")};
    if (!request.systemPartitionRoot.empty()) {
        bootArguments.push_back(L"/s");
        bootArguments.push_back(request.systemPartitionRoot.substr(0, 2));
    }
    bootArguments.push_back(L"/f");
    bootArguments.push_back(L"ALL");
    plan.operations.push_back({
        DeploymentOperationType::ConfigureBoot,
        L"配置 Windows 启动文件",
        L"bcdboot.exe",
        std::move(bootArguments),
        true,
    });
    return plan;
}

} // namespace openrcv::workflow
