#include <windows.h>

#include "openrcv/storage/VolumeEnumerator.h"

#include <iterator>

namespace openrcv::storage {

std::vector<VolumeInfo> EnumerateInstallVolumes() {
    std::vector<VolumeInfo> volumes;
    wchar_t buffer[1024]{};
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buffer)), buffer);
    if (length == 0 || length >= std::size(buffer)) return volumes;

    for (const wchar_t* root = buffer; *root; root += wcslen(root) + 1) {
        if (root[0] == L'X' || root[0] == L'x') continue;
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;

        wchar_t fileSystem[32]{};
        wchar_t label[MAX_PATH]{};
        GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr,
                              fileSystem, static_cast<DWORD>(std::size(fileSystem)));
        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root, &freeBytes, &totalBytes, nullptr)) continue;

        volumes.push_back({root, label, fileSystem, totalBytes.QuadPart, freeBytes.QuadPart});
    }
    return volumes;
}

} // namespace openrcv::storage
