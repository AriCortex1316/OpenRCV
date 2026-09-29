#pragma once

#include <string>

#include "openrcv/config/Region.h"

namespace openrcv::unattend {

enum class ProcessorArchitecture {
    X86,
    Amd64,
    Arm64,
};

struct UnattendOptions {
    ProcessorArchitecture architecture{ProcessorArchitecture::Amd64};
    config::RegionConfiguration region{config::DefaultRegion()};
    std::wstring localAdministrator{L"User"};
    bool createLocalAdministrator{true};
    bool skipOnlineAccountScreens{true};
    bool pauseWindowsUpdate{};
};

std::wstring BuildUnattendXml(const UnattendOptions& options);
std::wstring BuildOobeXml(const config::RegionConfiguration& region);

} // namespace openrcv::unattend
