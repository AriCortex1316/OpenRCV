#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openrcv::config {

enum class WindowsUpdateNotificationMethod {
    Registry,
    LocalGroupPolicy,
};

struct AdvancedSettings {
    bool disableWindowsUpdate{};
    bool notifyBeforeWindowsUpdateDownload{};
    WindowsUpdateNotificationMethod windowsUpdateNotificationMethod{
        WindowsUpdateNotificationMethod::Registry};
    bool showFileExtensions{};
    bool showThisPcOnDesktop{};
    bool disableWidgets{};
    bool disableFastStartup{};
    bool disableUac{};
    bool preventBitLocker{};
    bool excludeWindowsUpdateDrivers{};

    bool AnyEnabled() const noexcept;
};

enum class OfflineHive {
    Software,
    System,
    DefaultUser,
};

struct RegistryChange {
    OfflineHive hive{};
    std::wstring subkey;
    std::wstring valueName;
    std::uint32_t value{};
};

struct OfflineSettingsResult {
    bool success{};
    std::wstring error;
};

std::vector<RegistryChange> BuildOfflineRegistryChanges(
    const AdvancedSettings& settings, unsigned long currentControlSet);

std::wstring BuildPauseWindowsUpdateTaskXml();

std::vector<std::uint8_t> BuildWindowsUpdateNotificationPolicyFile();

OfflineSettingsResult ApplyOfflineSettings(
    const std::wstring& targetRoot, const AdvancedSettings& settings);

OfflineSettingsResult ApplyOfflineDeviceRegion(
    const std::wstring& targetRoot, std::uint32_t geoId);

} // namespace openrcv::config
