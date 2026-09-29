#include <windows.h>

#include "openrcv/imaging/WindowsImage.h"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <utility>

namespace openrcv::imaging {
namespace {

std::wstring Trim(std::wstring text) {
    const auto isSpace = [](wchar_t character) { return std::iswspace(character) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), isSpace));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), isSpace).base(), text.end());
    return text;
}

class ScopedModule {
public:
    explicit ScopedModule(HMODULE module) : module_(module) {}
    ~ScopedModule() {
        if (module_) FreeLibrary(module_);
    }

    ScopedModule(const ScopedModule&) = delete;
    ScopedModule& operator=(const ScopedModule&) = delete;

    HMODULE Get() const noexcept { return module_; }

private:
    HMODULE module_{};
};

class ScopedWimHandle {
public:
    using CloseFunction = BOOL(WINAPI*)(HANDLE);

    ScopedWimHandle(HANDLE handle, CloseFunction close)
        : handle_(handle), close_(close) {}
    ~ScopedWimHandle() {
        if (handle_) close_(handle_);
    }

    ScopedWimHandle(const ScopedWimHandle&) = delete;
    ScopedWimHandle& operator=(const ScopedWimHandle&) = delete;

    HANDLE Get() const noexcept { return handle_; }

private:
    HANDLE handle_{};
    CloseFunction close_{};
};

class ScopedLocalMemory {
public:
    explicit ScopedLocalMemory(void* memory) : memory_(memory) {}
    ~ScopedLocalMemory() {
        if (memory_) LocalFree(memory_);
    }

    ScopedLocalMemory(const ScopedLocalMemory&) = delete;
    ScopedLocalMemory& operator=(const ScopedLocalMemory&) = delete;

private:
    void* memory_{};
};

bool IsTagBoundary(wchar_t character) {
    return character == L'>' || std::iswspace(character) != 0;
}

std::size_t FindOpeningTag(std::wstring_view xml, std::wstring_view tag,
                           std::size_t start) {
    const std::wstring prefix = L"<" + std::wstring(tag);
    std::size_t position = xml.find(prefix, start);
    while (position != std::wstring_view::npos) {
        const std::size_t boundary = position + prefix.size();
        if (boundary < xml.size() && IsTagBoundary(xml[boundary])) return position;
        position = xml.find(prefix, position + 1);
    }
    return std::wstring_view::npos;
}

std::wstring_view ElementText(std::wstring_view xml, std::wstring_view tag) {
    const std::wstring opening = L"<" + std::wstring(tag) + L">";
    const std::wstring closing = L"</" + std::wstring(tag) + L">";
    const std::size_t begin = xml.find(opening);
    if (begin == std::wstring_view::npos) return {};
    const std::size_t textBegin = begin + opening.size();
    const std::size_t end = xml.find(closing, textBegin);
    if (end == std::wstring_view::npos) return {};
    return xml.substr(textBegin, end - textBegin);
}

std::wstring_view AttributeValue(std::wstring_view tag,
                                 std::wstring_view attribute) {
    std::size_t cursor = attribute.size();
    while ((cursor = tag.find(attribute, cursor)) != std::wstring_view::npos) {
        if (cursor != 0 && std::iswspace(tag[cursor - 1]) == 0) {
            cursor += attribute.size();
            continue;
        }
        std::size_t equals = cursor + attribute.size();
        while (equals < tag.size() && std::iswspace(tag[equals]) != 0) ++equals;
        if (equals >= tag.size() || tag[equals] != L'=') {
            cursor += attribute.size();
            continue;
        }
        std::size_t quote = equals + 1;
        while (quote < tag.size() && std::iswspace(tag[quote]) != 0) ++quote;
        if (quote >= tag.size() || (tag[quote] != L'\"' && tag[quote] != L'\'')) {
            return {};
        }
        const wchar_t delimiter = tag[quote];
        const std::size_t end = tag.find(delimiter, quote + 1);
        if (end == std::wstring_view::npos) return {};
        return tag.substr(quote + 1, end - quote - 1);
    }
    return {};
}

bool ParseUnsigned(std::wstring_view text, std::uint32_t& value) {
    const std::wstring trimmed = Trim(std::wstring(text));
    if (trimmed.empty()) return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 10);
    if (end != trimmed.c_str() + trimmed.size() ||
        parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

void AppendCodePoint(std::wstring& output, std::uint32_t codePoint) {
    if (codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) return;
    if (codePoint <= 0xffff) {
        output.push_back(static_cast<wchar_t>(codePoint));
        return;
    }
    codePoint -= 0x10000;
    output.push_back(static_cast<wchar_t>(0xd800 + (codePoint >> 10)));
    output.push_back(static_cast<wchar_t>(0xdc00 + (codePoint & 0x3ff)));
}

std::wstring DecodeXmlText(std::wstring_view text) {
    std::wstring decoded;
    decoded.reserve(text.size());
    for (std::size_t cursor = 0; cursor < text.size();) {
        if (text[cursor] != L'&') {
            decoded.push_back(text[cursor++]);
            continue;
        }
        const std::size_t semicolon = text.find(L';', cursor + 1);
        if (semicolon == std::wstring_view::npos) {
            decoded.push_back(text[cursor++]);
            continue;
        }
        const std::wstring_view entity =
            text.substr(cursor + 1, semicolon - cursor - 1);
        if (entity == L"amp") decoded.push_back(L'&');
        else if (entity == L"lt") decoded.push_back(L'<');
        else if (entity == L"gt") decoded.push_back(L'>');
        else if (entity == L"quot") decoded.push_back(L'\"');
        else if (entity == L"apos") decoded.push_back(L'\'');
        else if (!entity.empty() && entity.front() == L'#') {
            const bool hexadecimal = entity.size() > 1 &&
                (entity[1] == L'x' || entity[1] == L'X');
            const std::wstring digits(entity.substr(hexadecimal ? 2 : 1));
            wchar_t* end = nullptr;
            const unsigned long codePoint =
                std::wcstoul(digits.c_str(), &end, hexadecimal ? 16 : 10);
            if (!digits.empty() && end == digits.c_str() + digits.size()) {
                AppendCodePoint(decoded, static_cast<std::uint32_t>(codePoint));
            } else {
                decoded.append(text.substr(cursor, semicolon - cursor + 1));
            }
        } else {
            decoded.append(text.substr(cursor, semicolon - cursor + 1));
        }
        cursor = semicolon + 1;
    }
    return Trim(std::move(decoded));
}

ImageArchitecture ParseArchitecture(std::wstring_view value) {
    std::uint32_t architecture = 0;
    if (!ParseUnsigned(value, architecture)) return ImageArchitecture::Unknown;
    switch (architecture) {
    case 0: return ImageArchitecture::X86;
    case 9: return ImageArchitecture::Amd64;
    case 12: return ImageArchitecture::Arm64;
    default: return ImageArchitecture::Unknown;
    }
}

} // namespace

std::vector<WindowsImage> ParseWimImageInfoXml(std::wstring_view xml) {
    std::vector<WindowsImage> images;
    std::size_t cursor = 0;
    while ((cursor = FindOpeningTag(xml, L"IMAGE", cursor)) !=
           std::wstring_view::npos) {
        const std::size_t openingEnd = xml.find(L'>', cursor);
        if (openingEnd == std::wstring_view::npos) break;
        const std::size_t closing = xml.find(L"</IMAGE>", openingEnd + 1);
        if (closing == std::wstring_view::npos) break;

        WindowsImage image;
        std::uint32_t index = 0;
        const std::wstring_view openingTag =
            xml.substr(cursor, openingEnd - cursor + 1);
        if (ParseUnsigned(AttributeValue(openingTag, L"INDEX"), index) &&
            index <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            image.index = static_cast<int>(index);
        }

        const std::wstring_view body =
            xml.substr(openingEnd + 1, closing - openingEnd - 1);
        image.name = DecodeXmlText(ElementText(body, L"NAME"));
        const std::wstring_view windows = ElementText(body, L"WINDOWS");
        image.architecture = ParseArchitecture(ElementText(windows, L"ARCH"));
        if (image.index > 0 && !image.name.empty()) images.push_back(std::move(image));

        cursor = closing + 8;
    }
    return images;
}

bool LoadWindowsImages(const std::wstring& imageFile,
                       std::vector<WindowsImage>& images,
                       std::uint32_t& error) {
    images.clear();
    error = ERROR_SUCCESS;

    ScopedModule module(LoadLibraryExW(
        L"wimgapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!module.Get()) {
        error = GetLastError();
        return false;
    }

    using CreateFileFunction = HANDLE(WINAPI*)(PWSTR, DWORD, DWORD, DWORD,
                                                DWORD, PDWORD);
    using GetInformationFunction = BOOL(WINAPI*)(HANDLE, PVOID*, PDWORD);
    using CloseFunction = BOOL(WINAPI*)(HANDLE);
    const auto createFile = reinterpret_cast<CreateFileFunction>(
        GetProcAddress(module.Get(), "WIMCreateFile"));
    const auto getInformation = reinterpret_cast<GetInformationFunction>(
        GetProcAddress(module.Get(), "WIMGetImageInformation"));
    const auto closeHandle = reinterpret_cast<CloseFunction>(
        GetProcAddress(module.Get(), "WIMCloseHandle"));
    if (!createFile || !getInformation || !closeHandle) {
        error = ERROR_PROC_NOT_FOUND;
        return false;
    }

    DWORD creationResult = 0;
    ScopedWimHandle wim(createFile(const_cast<PWSTR>(imageFile.c_str()), 0,
                                   OPEN_EXISTING, 0, 0, &creationResult),
                        closeHandle);
    if (!wim.Get()) {
        error = GetLastError();
        return false;
    }

    void* information = nullptr;
    DWORD informationSize = 0;
    if (!getInformation(wim.Get(), &information, &informationSize)) {
        error = GetLastError();
        return false;
    }
    ScopedLocalMemory informationOwner(information);
    if (!information || informationSize < sizeof(wchar_t) ||
        informationSize % sizeof(wchar_t) != 0) {
        error = ERROR_INVALID_DATA;
        return false;
    }

    std::wstring_view xml(static_cast<const wchar_t*>(information),
                          informationSize / sizeof(wchar_t));
    while (!xml.empty() && (xml.back() == L'\0' || xml.back() == L'\ufeff')) {
        xml.remove_suffix(1);
    }
    if (!xml.empty() && xml.front() == L'\ufeff') xml.remove_prefix(1);
    images = ParseWimImageInfoXml(xml);
    if (images.empty()) {
        error = ERROR_INVALID_DATA;
        return false;
    }
    return true;
}

} // namespace openrcv::imaging
