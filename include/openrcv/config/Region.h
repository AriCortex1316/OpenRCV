#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openrcv::config {

struct RegionConfiguration {
    std::wstring displayName;
    std::wstring locale;
    std::wstring inputLocale;
    std::uint32_t geoId{};
};

const std::vector<RegionConfiguration>& SupportedRegions();
const RegionConfiguration& DefaultRegion();

} // namespace openrcv::config
