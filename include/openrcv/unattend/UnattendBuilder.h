#pragma once

#include <string>

namespace openrcv::unattend {

enum class ProcessorArchitecture {
    X86,
    Amd64,
};

struct UnattendOptions {
    ProcessorArchitecture architecture{ProcessorArchitecture::Amd64};
    std::wstring uiLanguage{L"zh-CN"};
    std::wstring inputLocale{L"0804:00000804"};
    std::wstring systemLocale{L"zh-CN"};
    std::wstring userLocale{L"zh-CN"};
    std::wstring localAdministrator{L"User"};
    bool createLocalAdministrator{true};
    bool skipOnlineAccountScreens{true};
};

std::wstring BuildUnattendXml(const UnattendOptions& options);

} // namespace openrcv::unattend
