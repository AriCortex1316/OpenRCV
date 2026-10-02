#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openrcv::imaging {

enum class ImageArchitecture {
    Unknown,
    X86,
    Amd64,
    Arm64,
};

enum class ImageEditionClass {
    Unknown,
    Home,
    ProOrHigher,
};

struct WindowsImage {
    int index{};
    std::wstring name;
    ImageArchitecture architecture{ImageArchitecture::Unknown};
    std::wstring editionId;
    ImageEditionClass editionClass{ImageEditionClass::Unknown};
};

ImageEditionClass ClassifyEditionId(std::wstring_view editionId);
std::vector<WindowsImage> ParseWimImageInfoXml(std::wstring_view xml);
bool LoadWindowsImages(const std::wstring& imageFile,
                       std::vector<WindowsImage>& images,
                       std::uint32_t& error);

} // namespace openrcv::imaging
