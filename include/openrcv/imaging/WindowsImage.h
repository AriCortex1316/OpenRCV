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

struct WindowsImage {
    int index{};
    std::wstring name;
    ImageArchitecture architecture{ImageArchitecture::Unknown};
};

std::vector<WindowsImage> ParseWimImageInfoXml(std::wstring_view xml);
bool LoadWindowsImages(const std::wstring& imageFile,
                       std::vector<WindowsImage>& images,
                       std::uint32_t& error);

} // namespace openrcv::imaging
