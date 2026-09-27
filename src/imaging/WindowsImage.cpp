#include "openrcv/imaging/WindowsImage.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace openrcv::imaging {
namespace {

std::wstring Trim(std::wstring text) {
    const auto isSpace = [](wchar_t character) { return std::iswspace(character) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), isSpace));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), isSpace).base(), text.end());
    return text;
}

} // namespace

std::vector<WindowsImage> ParseDismImageInfo(std::wstring_view output) {
    std::vector<WindowsImage> images;
    std::size_t cursor = 0;
    int currentIndex = 0;
    while (cursor < output.size()) {
        const std::size_t end = output.find_first_of(L"\r\n", cursor);
        const std::size_t count = end == std::wstring_view::npos ? output.size() - cursor
                                                                 : end - cursor;
        const std::wstring line = Trim(std::wstring(output.substr(cursor, count)));
        if (line.rfind(L"Index :", 0) == 0) {
            currentIndex = std::wcstol(Trim(line.substr(7)).c_str(), nullptr, 10);
        } else if (line.rfind(L"Name :", 0) == 0 && currentIndex > 0) {
            images.push_back({currentIndex, Trim(line.substr(6))});
        }
        if (end == std::wstring_view::npos) break;
        cursor = output.find_first_not_of(L"\r\n", end);
        if (cursor == std::wstring_view::npos) break;
    }
    return images;
}

} // namespace openrcv::imaging
