#include <windows.h>

#include "openrcv/workflow/ImageSourceService.h"

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

    std::uint32_t imageError = ERROR_SUCCESS;
    if (!imaging::LoadWindowsImages(result.imageFile, result.images, imageError)) {
        result.messages.push_back(L"WIMGAPI 读取镜像信息失败，系统错误 " +
                                  std::to_wstring(imageError));
        return result;
    }
    for (const imaging::WindowsImage& image : result.images) {
        if (image.architecture == imaging::ImageArchitecture::Unknown) {
            result.messages.push_back(
                L"WIMGAPI 元数据中没有可识别的镜像索引 " +
                std::to_wstring(image.index) + L" 处理器架构。");
            return result;
        }
    }

    result.messages.push_back(L"已读取 " + std::to_wstring(result.images.size()) +
                              L" 个 Windows 版本。");
    result.success = true;
    return result;
}

void ImageSourceService::Close() noexcept { mountedIso_.Close(); }

} // namespace openrcv::workflow
