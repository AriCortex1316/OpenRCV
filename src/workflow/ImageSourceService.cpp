#include <windows.h>

#include "openrcv/workflow/ImageSourceService.h"

#include "openrcv/process/ProcessRunner.h"

#include <iterator>

namespace openrcv::workflow {

ImageSourceResult ImageSourceService::LoadIso(const std::wstring& isoPath) {
    ImageSourceResult result;
    const imaging::IsoMountResult mount = mountedIso_.MountReadOnly(isoPath);
    result.messages.push_back(mount.message);
    if (!mount.success) return result;

    result.imageFile = mountedIso_.Root() + L"sources\\install.wim";
    if (GetFileAttributesW(result.imageFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.imageFile = mountedIso_.Root() + L"sources\\install.esd";
    }
    if (GetFileAttributesW(result.imageFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.messages.emplace_back(L"ISO 中没有 sources\\install.wim 或 install.esd。");
        result.imageFile.clear();
        return result;
    }
    result.messages.push_back(L"发现系统镜像：" + result.imageFile);

    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
    if (length == 0 || length >= std::size(systemDirectory)) {
        result.messages.emplace_back(L"无法定位 WinPE 系统目录。");
        return result;
    }

    const std::wstring dism = std::wstring(systemDirectory) + L"\\dism.exe";
    const process::ProcessResult processResult = process::RunHidden(
        dism, {L"/English", L"/Get-ImageInfo", L"/ImageFile:" + result.imageFile});
    if (!processResult.Started()) {
        result.messages.push_back(L"无法启动 DISM，系统错误 " +
                                  std::to_wstring(processResult.systemError));
        return result;
    }
    if (!processResult.Succeeded()) {
        result.messages.push_back(L"DISM 读取镜像失败，退出码 " +
                                  std::to_wstring(processResult.exitCode));
        return result;
    }

    result.images = imaging::ParseDismImageInfo(processResult.output);
    if (result.images.empty()) {
        result.messages.emplace_back(L"DISM 已运行，但没有解析到镜像版本。");
        return result;
    }

    result.messages.push_back(L"已读取 " + std::to_wstring(result.images.size()) +
                              L" 个 Windows 版本。");
    result.success = true;
    return result;
}

void ImageSourceService::Close() noexcept { mountedIso_.Close(); }

} // namespace openrcv::workflow
