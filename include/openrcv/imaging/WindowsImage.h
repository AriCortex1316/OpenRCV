#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace openrcv::imaging {

struct WindowsImage {
    int index{};
    std::wstring name;
};

std::vector<WindowsImage> ParseDismImageInfo(std::wstring_view output);

} // namespace openrcv::imaging
