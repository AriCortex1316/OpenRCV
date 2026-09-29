#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openrcv::storage {

struct VolumeInfo {
    std::wstring root;
    std::wstring volumePath;
    std::wstring label;
    std::wstring fileSystem;
    std::wstring partitionStyle;
    std::wstring busType;
    unsigned long diskNumber{static_cast<unsigned long>(-1)};
    unsigned long partitionNumber{};
    bool efiSystemPartition{};
    bool activePartition{};
    std::uint64_t totalBytes{};
    std::uint64_t freeBytes{};
};

std::vector<VolumeInfo> EnumerateInstallVolumes();
std::vector<VolumeInfo> EnumerateBootVolumes();

bool MountVolumeWithTemporaryLetter(VolumeInfo& volume, std::wstring& error);
void RemoveTemporaryMount(const std::wstring& root) noexcept;

} // namespace openrcv::storage
