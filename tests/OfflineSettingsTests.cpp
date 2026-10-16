#include "TestSupport.h"

#include "openrcv/config/OfflineSettings.h"
#include "openrcv/config/Region.h"

#include <cstdint>
#include <vector>

namespace openrcv::tests {
namespace {

bool ContainsUtf16Text(const std::vector<std::uint8_t>& bytes,
                       const std::wstring& text) {
    if (text.empty()) return true;
    for (std::size_t offset = 0; offset + text.size() * 2 <= bytes.size(); ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < text.size(); ++index) {
            const std::uint16_t character = static_cast<std::uint16_t>(
                bytes[offset + index * 2] | (bytes[offset + index * 2 + 1] << 8));
            if (character != static_cast<std::uint16_t>(text[index])) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

void RegionConfigurationsKeepLocalesAndGeoIdsTogether() {
    const auto& regions = openrcv::config::SupportedRegions();
    Expect(regions.size() == 5, L"RegionConfigurationsListsSupportedRegions");
    if (regions.size() == 5) {
        Expect(regions[0].locale == L"zh-CN" && regions[0].geoId == 45,
               L"RegionConfigurationsMapsChina");
        Expect(regions[1].locale == L"zh-HK" &&
                   regions[1].inputLocale == L"0C04:00000409" &&
                   regions[1].geoId == 104,
               L"RegionConfigurationsMapsHongKong");
        Expect(regions[2].locale == L"ja-JP" && regions[2].geoId == 122,
               L"RegionConfigurationsMapsJapan");
        Expect(regions[3].locale == L"en-SG" &&
                   regions[3].inputLocale == L"4809:00000409" &&
                   regions[3].geoId == 215,
               L"RegionConfigurationsMapsSingapore");
        Expect(regions[4].locale == L"en-US" && regions[4].geoId == 244,
               L"RegionConfigurationsMapsUnitedStates");
    }
}

bool ContainsRegistryChange(
    const std::vector<openrcv::config::RegistryChange>& changes,
    openrcv::config::OfflineHive hive, const std::wstring& subkey,
    const std::wstring& valueName, std::uint32_t value) {
    for (const auto& change : changes) {
        if (change.hive == hive && change.subkey == subkey &&
            change.valueName == valueName && change.value == value) return true;
    }
    return false;
}

void OfflineSettingsMapAdvancedOptionsToRegistry() {
    openrcv::config::AdvancedSettings settings;
    settings.disableWindowsUpdate = true;
    settings.showFileExtensions = true;
    settings.showThisPcOnDesktop = true;
    settings.disableWidgets = true;
    settings.disableFastStartup = true;
    settings.disableUac = true;
    settings.preventBitLocker = true;
    settings.excludeWindowsUpdateDrivers = true;
    const auto changes = openrcv::config::BuildOfflineRegistryChanges(settings, 2);
    Expect(changes.size() == 9, L"OfflineSettingsCreatesEveryRequestedChange");
    Expect(!ContainsRegistryChange(changes, openrcv::config::OfflineHive::Software,
               L"Policies\\Microsoft\\Windows\\WindowsUpdate\\AU",
               L"NoAutoUpdate", 1),
           L"OfflineSettingsDoesNotPermanentlyDisableWindowsUpdate");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::System,
               L"ControlSet002\\Control\\Session Manager\\Power",
               L"HiberbootEnabled", 0),
           L"OfflineSettingsDisablesFastStartupInCurrentControlSet");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::DefaultUser,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
               L"HideFileExt", 0),
           L"OfflineSettingsShowsFileExtensionsForDefaultUser");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::DefaultUser,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel",
               L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", 0),
           L"OfflineSettingsShowsThisPcForNewStartPanel");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::DefaultUser,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu",
               L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", 0),
           L"OfflineSettingsShowsThisPcForClassicStartMenu");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::Software,
               L"Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0),
           L"OfflineSettingsDisablesWidgets");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::Software,
               L"Microsoft\\Windows\\CurrentVersion\\Policies\\System",
               L"EnableLUA", 0),
           L"OfflineSettingsDisablesUac");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::System,
               L"ControlSet002\\Control\\BitLocker",
               L"PreventDeviceEncryption", 1),
           L"OfflineSettingsPreventsAutomaticBitLocker");
    Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::Software,
               L"Policies\\Microsoft\\Windows\\WindowsUpdate",
               L"ExcludeWUDriversInQualityUpdate", 1),
           L"OfflineSettingsExcludesWindowsUpdateDrivers");
    Expect(openrcv::config::BuildOfflineRegistryChanges({}, 1).empty(),
           L"OfflineSettingsLeavesRegistryUntouchedWhenDisabled");

    const std::wstring taskXml =
        openrcv::config::BuildPauseWindowsUpdateTaskXml();
    Expect(taskXml.find(L"encoding=\"UTF-16\"") != std::wstring::npos &&
               taskXml.find(L"<Interval>P1D</Interval>") != std::wstring::npos &&
               taskXml.find(L"AddDays(7)") != std::wstring::npos &&
               taskXml.find(L"PauseUpdatesExpiryTime") != std::wstring::npos,
           L"OfflineSettingsBuildsRecurringWindowsUpdatePauseTask");
}

void OfflineSettingsSkipsWindows11HardwareChecksWhenEnabled() {
    openrcv::config::AdvancedSettings settings;
    Expect(!settings.AnyEnabled(),
           L"OfflineSettingsLeavesHardwareChecksEnabledWhenUnchecked");
    settings.skipWindows11HardwareChecks = true;
    Expect(settings.AnyEnabled(),
           L"OfflineSettingsEnablesOfflineConfigurationForHardwareBypass");
    const auto changes = openrcv::config::BuildOfflineRegistryChanges(settings, 1);
    Expect(changes.size() == 5,
           L"OfflineSettingsCreatesOnlyWindows11HardwareBypassValues");
    for (const wchar_t* valueName : {L"BypassTPMCheck", L"BypassSecureBootCheck",
                                     L"BypassRAMCheck", L"BypassCPUCheck",
                                     L"BypassStorageCheck"}) {
        Expect(ContainsRegistryChange(changes, openrcv::config::OfflineHive::System,
                   L"Setup\\LabConfig", valueName, 1),
               L"OfflineSettingsWritesWindows11HardwareBypassToSystemHive");
    }
    settings.skipWindows11HardwareChecks = false;
    Expect(openrcv::config::BuildOfflineRegistryChanges(settings, 1).empty(),
           L"OfflineSettingsDoesNotBypassHardwareChecksWhenUnchecked");
}

void OfflineSettingsConfiguresUpdateNotificationByEdition() {
    openrcv::config::AdvancedSettings homeSettings;
    homeSettings.notifyBeforeWindowsUpdateDownload = true;
    homeSettings.windowsUpdateNotificationMethod =
        openrcv::config::WindowsUpdateNotificationMethod::Registry;
    const auto homeChanges =
        openrcv::config::BuildOfflineRegistryChanges(homeSettings, 1);
    Expect(homeChanges.size() == 2 && ContainsRegistryChange(homeChanges,
               openrcv::config::OfflineHive::Software,
               L"Policies\\Microsoft\\Windows\\WindowsUpdate\\AU",
               L"NoAutoUpdate", 0) &&
               ContainsRegistryChange(homeChanges,
               openrcv::config::OfflineHive::Software,
               L"Policies\\Microsoft\\Windows\\WindowsUpdate\\AU",
               L"AUOptions", 2),
           L"OfflineSettingsUsesRegistryNotificationForHome");

    openrcv::config::AdvancedSettings proSettings = homeSettings;
    proSettings.windowsUpdateNotificationMethod =
        openrcv::config::WindowsUpdateNotificationMethod::LocalGroupPolicy;
    Expect(openrcv::config::BuildOfflineRegistryChanges(proSettings, 1).empty(),
           L"OfflineSettingsDoesNotWriteProPolicyDirectlyToRegistry");

    const auto policy =
        openrcv::config::BuildWindowsUpdateNotificationPolicyFile();
    const bool hasHeader = policy.size() >= 8 && policy[0] == 'P' &&
        policy[1] == 'R' && policy[2] == 'e' && policy[3] == 'g' &&
        policy[4] == 1 && policy[5] == 0 && policy[6] == 0 && policy[7] == 0;
    const bool hasDwordTwo = policy.size() >= 6 &&
        policy[policy.size() - 6] == 2 && policy[policy.size() - 5] == 0 &&
        policy[policy.size() - 4] == 0 && policy[policy.size() - 3] == 0 &&
        policy[policy.size() - 2] == ']' && policy.back() == 0;
    Expect(hasHeader && hasDwordTwo &&
               ContainsUtf16Text(policy,
                   L"Software\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU") &&
               ContainsUtf16Text(policy, L"NoAutoUpdate") &&
               ContainsUtf16Text(policy, L"AUOptions"),
           L"OfflineSettingsBuildsWindowsUpdateLocalPolicy");
}

} // namespace

void RunOfflineSettingsTests() {
    RegionConfigurationsKeepLocalesAndGeoIdsTogether();
    OfflineSettingsMapAdvancedOptionsToRegistry();
    OfflineSettingsSkipsWindows11HardwareChecksWhenEnabled();
    OfflineSettingsConfiguresUpdateNotificationByEdition();
}

} // namespace openrcv::tests
