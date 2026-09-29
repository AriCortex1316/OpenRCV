#include "openrcv/config/Region.h"

namespace openrcv::config {

const std::vector<RegionConfiguration>& SupportedRegions() {
    static const std::vector<RegionConfiguration> regions{
        {L"中国", L"zh-CN", L"0804:00000804", 45},
        {L"香港", L"zh-HK", L"0C04:00000409", 104},
        {L"日本", L"ja-JP", L"0411:00000411", 122},
        {L"新加坡", L"en-SG", L"4809:00000409", 215},
        {L"美国", L"en-US", L"0409:00000409", 244},
    };
    return regions;
}

const RegionConfiguration& DefaultRegion() {
    return SupportedRegions().front();
}

} // namespace openrcv::config
