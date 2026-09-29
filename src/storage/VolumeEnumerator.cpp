#include <windows.h>
#include <winioctl.h>

#include "openrcv/storage/VolumeEnumerator.h"

#include <iterator>
#include <vector>

namespace openrcv::storage {

namespace {

constexpr GUID kEfiSystemPartitionType = {
    0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
};

std::wstring DevicePathFromRoot(const wchar_t* root) {
    if (!root || root[0] == L'\0' || root[1] != L':') return {};
    return std::wstring(L"\\\\.\\") + root[0] + L":";
}

std::wstring BusTypeName(STORAGE_BUS_TYPE busType) {
    switch (busType) {
    case BusTypeScsi: return L"SCSI";
    case BusTypeAta: return L"ATA";
    case BusTypeSata: return L"SATA";
    case BusTypeUsb: return L"USB";
    case BusTypeNvme: return L"NVMe";
    case BusTypeSas: return L"SAS";
    case BusTypeVirtual: return L"虚拟";
    case BusTypeFileBackedVirtual: return L"虚拟";
    default: return L"未知";
    }
}

void ReadDeviceDetails(const std::wstring& path, VolumeInfo& volume) {
    HANDLE device = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) return;

    DWORD returned{};
    STORAGE_DEVICE_NUMBER number{};
    if (DeviceIoControl(device, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
                        &number, sizeof(number), &returned, nullptr)) {
        volume.diskNumber = number.DeviceNumber;
        volume.partitionNumber = number.PartitionNumber;
    }

    PARTITION_INFORMATION_EX partition{};
    if (DeviceIoControl(device, IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0,
                        &partition, sizeof(partition), &returned, nullptr)) {
        volume.partitionStyle = partition.PartitionStyle == PARTITION_STYLE_GPT
            ? L"GPT" : partition.PartitionStyle == PARTITION_STYLE_MBR ? L"MBR" : L"RAW";
        volume.partitionNumber = partition.PartitionNumber;
        if (partition.PartitionStyle == PARTITION_STYLE_GPT) {
            volume.efiSystemPartition =
                IsEqualGUID(partition.Gpt.PartitionType,
                            kEfiSystemPartitionType) != FALSE;
        } else if (partition.PartitionStyle == PARTITION_STYLE_MBR) {
            volume.activePartition = partition.Mbr.BootIndicator != FALSE;
        }
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    STORAGE_DEVICE_DESCRIPTOR descriptor{};
    if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        &descriptor, sizeof(descriptor), &returned, nullptr)) {
        volume.busType = BusTypeName(descriptor.BusType);
    }
    CloseHandle(device);
}

std::wstring VolumeHandlePath(const std::wstring& volumePath) {
    if (!volumePath.empty() && volumePath.back() == L'\\') {
        return volumePath.substr(0, volumePath.size() - 1);
    }
    return volumePath;
}

std::wstring FirstDriveRoot(const std::wstring& volumePath) {
    DWORD required{};
    GetVolumePathNamesForVolumeNameW(volumePath.c_str(), nullptr, 0, &required);
    if (required == 0) return {};
    std::vector<wchar_t> paths(required);
    if (!GetVolumePathNamesForVolumeNameW(volumePath.c_str(), paths.data(),
                                           required, &required)) return {};
    for (const wchar_t* path = paths.data(); *path; path += wcslen(path) + 1) {
        if (path[0] && path[1] == L':' && path[2] == L'\\' && path[3] == L'\0') {
            return path;
        }
    }
    return {};
}

bool ReadVolume(const std::wstring& volumePath, VolumeInfo& volume) {
    volume.volumePath = volumePath;
    volume.root = FirstDriveRoot(volumePath);
    wchar_t fileSystem[32]{};
    wchar_t label[MAX_PATH]{};
    GetVolumeInformationW(volumePath.c_str(), label, MAX_PATH, nullptr, nullptr,
                          nullptr, fileSystem,
                          static_cast<DWORD>(std::size(fileSystem)));
    volume.label = label;
    volume.fileSystem = fileSystem;
    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    if (GetDiskFreeSpaceExW(volumePath.c_str(), &freeBytes, &totalBytes, nullptr)) {
        volume.totalBytes = totalBytes.QuadPart;
        volume.freeBytes = freeBytes.QuadPart;
    }
    ReadDeviceDetails(VolumeHandlePath(volumePath), volume);
    return volume.diskNumber != static_cast<unsigned long>(-1);
}

} // namespace

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

        VolumeInfo volume;
        volume.root = root;
        wchar_t volumePath[MAX_PATH]{};
        if (GetVolumeNameForVolumeMountPointW(root, volumePath,
                                              static_cast<DWORD>(std::size(volumePath)))) {
            volume.volumePath = volumePath;
        }
        volume.label = label;
        volume.fileSystem = fileSystem;
        volume.totalBytes = totalBytes.QuadPart;
        volume.freeBytes = freeBytes.QuadPart;
        ReadDeviceDetails(DevicePathFromRoot(root), volume);
        volumes.push_back(std::move(volume));
    }
    return volumes;
}

std::vector<VolumeInfo> EnumerateBootVolumes() {
    std::vector<VolumeInfo> volumes;
    wchar_t volumePath[MAX_PATH]{};
    HANDLE search = FindFirstVolumeW(volumePath,
        static_cast<DWORD>(std::size(volumePath)));
    if (search == INVALID_HANDLE_VALUE) return volumes;

    do {
        VolumeInfo volume;
        if (ReadVolume(volumePath, volume) &&
            (volume.efiSystemPartition || volume.activePartition)) {
            volumes.push_back(std::move(volume));
        }
    } while (FindNextVolumeW(search, volumePath,
                             static_cast<DWORD>(std::size(volumePath))));
    FindVolumeClose(search);
    return volumes;
}

bool MountVolumeWithTemporaryLetter(VolumeInfo& volume, std::wstring& error) {
    if (!volume.root.empty()) return true;
    if (volume.volumePath.empty()) {
        error = L"引导分区没有可用的卷 GUID 路径。";
        return false;
    }
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'S'; letter >= L'D'; --letter) {
        const DWORD mask = 1u << (letter - L'A');
        if ((drives & mask) != 0 || letter == L'X') continue;
        std::wstring root;
        root.push_back(letter);
        root += L":\\";
        if (SetVolumeMountPointW(root.c_str(), volume.volumePath.c_str())) {
            volume.root = root;
            return true;
        }
    }
    error = L"无法为引导分区分配临时盘符，系统错误 " +
            std::to_wstring(GetLastError());
    return false;
}

void RemoveTemporaryMount(const std::wstring& root) noexcept {
    if (!root.empty()) DeleteVolumeMountPointW(root.c_str());
}

} // namespace openrcv::storage
