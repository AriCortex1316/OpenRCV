#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openrcv::storage {

struct VolumeInfo {
    std::wstring root;
    std::wstring label;
    std::wstring fileSystem;
    std::uint64_t totalBytes{};
    std::uint64_t freeBytes{};
};

std::vector<VolumeInfo> EnumerateInstallVolumes();

} // namespace openrcv::storage
