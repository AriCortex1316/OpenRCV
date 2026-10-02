#include "openrcv/config/OfflineSettings.h"
#include "openrcv/imaging/WindowsImage.h"
#include "openrcv/process/ProcessRunner.h"
#include "openrcv/unattend/UnattendBuilder.h"
#include "openrcv/workflow/DeploymentPlan.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void ExpectEqual(const std::wstring& actual, const std::wstring& expected,
                 const wchar_t* testName) {
    if (actual == expected) return;
    std::wcerr << L"FAILED: " << testName << L"\n  expected: " << expected
               << L"\n  actual:   " << actual << L'\n';
    ++failures;
}

void Expect(bool condition, const wchar_t* testName) {
    if (condition) return;
    std::wcerr << L"FAILED: " << testName << L'\n';
    ++failures;
}

void CommandLineQuotingPreservesSpaces() {
    ExpectEqual(openrcv::process::QuoteCommandLineArgument(L"C:\\Windows Images\\install.wim"),
                L"\"C:\\Windows Images\\install.wim\"",
                L"CommandLineQuotingPreservesSpaces");
}

void CommandLineQuotingEscapesQuotesAndTrailingSlashes() {
    ExpectEqual(openrcv::process::QuoteCommandLineArgument(L"value \\\"quoted\\\"\\"),
                L"\"value \\\\\\\"quoted\\\\\\\"\\\\\"",
                L"CommandLineQuotingEscapesQuotesAndTrailingSlashes");
}

void DismProgressParserReturnsLatestPercentage() {
    const auto progress = openrcv::process::ParseDismProgressPercentage(
        L"[===== 25.0% ]\r[========== 67.4% ]");
    Expect(progress.has_value() && *progress == 67,
           L"DismProgressParserReturnsLatestPercentage");
    Expect(!openrcv::process::ParseDismProgressPercentage(L"操作成功完成。").has_value(),
           L"DismProgressParserIgnoresOutputWithoutPercentage");
}

void ProcessRunnerStreamsOutput() {
    std::wstring streamed;
    openrcv::process::ProcessRunOptions options;
    options.outputCallback = [&](std::wstring_view chunk) {
        streamed.append(chunk.data(), chunk.size());
    };
    const auto result = openrcv::process::RunHidden(
        L"cmd.exe", {L"/d", L"/c", L"echo OpenRCV-stream-test"}, options);
    if (!result.Succeeded()) {
        std::wcerr << L"ProcessRunner stream error: system=" << result.systemError
                   << L" exit=" << result.exitCode << L"\n";
    }
    Expect(result.Succeeded(), L"ProcessRunnerStreamsOutputCommandSucceeds");
    Expect(streamed.find(L"OpenRCV-stream-test") != std::wstring::npos,
           L"ProcessRunnerStreamsOutputBeforeReturning");
}

void ProcessRunnerCancelsRunningProcess() {
    std::atomic_bool cancellationRequested{};
    openrcv::process::ProcessRunOptions options;
    options.cancellationRequested = &cancellationRequested;
    options.outputCallback = [&](std::wstring_view chunk) {
        if (!chunk.empty()) cancellationRequested.store(true);
    };
    const auto result = openrcv::process::RunHidden(
        L"ping.exe", {L"127.0.0.1", L"-n", L"20"}, options);
    if (!result.cancelled) {
        std::wcerr << L"ProcessRunner cancel error: system=" << result.systemError
                   << L" exit=" << result.exitCode << L"\n";
    }
    Expect(result.cancelled && !result.Succeeded(),
           L"ProcessRunnerCancelsRunningProcess");
}

void WimMetadataParserReturnsImagesAndArchitectures() {
    const std::wstring xml =
        L"<WIM><IMAGE INDEX=\"1\"><NAME>Windows 11 Home</NAME>"
        L"<WINDOWS><ARCH>0</ARCH><EDITIONID>Core</EDITIONID></WINDOWS></IMAGE>"
        L"<IMAGE INDEX=\"2\"><NAME>Windows 11 Pro &amp; Workstation</NAME>"
        L"<WINDOWS><ARCH>9</ARCH><EDITIONID>Professional</EDITIONID></WINDOWS></IMAGE>"
        L"<IMAGE INDEX=\"3\"><NAME>Windows 11 ARM64</NAME>"
        L"<WINDOWS><ARCH>12</ARCH><EDITIONID>Enterprise</EDITIONID></WINDOWS></IMAGE></WIM>";
    const std::vector<openrcv::imaging::WindowsImage> images =
        openrcv::imaging::ParseWimImageInfoXml(xml);
    Expect(images.size() == 3, L"WimMetadataParserReturnsThreeImages");
    if (images.size() == 3) {
        Expect(images[0].index == 1 && images[0].name == L"Windows 11 Home" &&
                   images[0].architecture == openrcv::imaging::ImageArchitecture::X86 &&
                   images[0].editionClass ==
                       openrcv::imaging::ImageEditionClass::Home,
               L"WimMetadataParserReturnsX86Image");
        Expect(images[1].index == 2 &&
                   images[1].name == L"Windows 11 Pro & Workstation" &&
                   images[1].architecture == openrcv::imaging::ImageArchitecture::Amd64 &&
                   images[1].editionClass ==
                       openrcv::imaging::ImageEditionClass::ProOrHigher,
               L"WimMetadataParserReturnsAmd64ImageAndDecodesName");
        Expect(images[2].index == 3 &&
                   images[2].architecture == openrcv::imaging::ImageArchitecture::Arm64,
               L"WimMetadataParserReturnsArm64Image");
    }
}

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

void UnattendBuilderCreatesLocalAdmin() {
    openrcv::unattend::UnattendOptions options;
    options.localAdministrator = L"Installer & Admin";
    const std::wstring xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"<Name>Installer &amp; Admin</Name>") != std::wstring::npos,
           L"UnattendBuilderEscapesLocalAdminName");
    Expect(xml.find(L"<Group>Administrators</Group>") != std::wstring::npos,
           L"UnattendBuilderCreatesLocalAdmin");
    Expect(xml.find(L"processorArchitecture=\"amd64\"") != std::wstring::npos,
           L"UnattendBuilderUsesAmd64Architecture");
}

void UnattendBuilderUsesSelectedLocales() {
    openrcv::unattend::UnattendOptions options;
    options.region = openrcv::config::SupportedRegions()[2];
    const std::wstring xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"<UILanguage>") == std::wstring::npos &&
               xml.find(L"<UILanguageFallback>") == std::wstring::npos,
           L"UnattendBuilderLeavesImageUiLanguageUnchanged");
    Expect(xml.find(L"Microsoft-Windows-International-Core-WinPE") !=
               std::wstring::npos,
           L"UnattendBuilderConfiguresWindowsPeLocales");
    Expect(xml.find(L"<InputLocale>0411:00000411</InputLocale>") !=
               std::wstring::npos,
           L"UnattendBuilderUsesSelectedInputLocale");
    Expect(xml.find(L"<SystemLocale>ja-JP</SystemLocale>") != std::wstring::npos,
           L"UnattendBuilderUsesSelectedSystemLocale");
    Expect(xml.find(L"<UserLocale>ja-JP</UserLocale>") != std::wstring::npos,
           L"UnattendBuilderUsesSelectedRegion");
    Expect(xml.find(L"Set-WinHomeLocation -GeoId 122") != std::wstring::npos,
           L"UnattendBuilderSetsSelectedHomeLocationAtFirstLogon");
    const std::wstring oobe = openrcv::unattend::BuildOobeXml(options.region);
    Expect(oobe.find(L"<location>122</location>") != std::wstring::npos,
           L"OobeBuilderUsesSelectedDeviceRegion");
    Expect(oobe.find(L"<keyboard>0411:00000411</keyboard>") !=
               std::wstring::npos,
           L"OobeBuilderUsesSelectedKeyboard");
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

void UnattendBuilderUsesSelectedArchitecture() {
    openrcv::unattend::UnattendOptions options;
    options.architecture = openrcv::unattend::ProcessorArchitecture::X86;
    std::wstring xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"processorArchitecture=\"x86\"") != std::wstring::npos,
           L"UnattendBuilderUsesX86Architecture");
    options.architecture = openrcv::unattend::ProcessorArchitecture::Arm64;
    xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"processorArchitecture=\"arm64\"") != std::wstring::npos,
           L"UnattendBuilderUsesArm64Architecture");
}

void UnattendBuilderDisablesSetupDynamicUpdate() {
    const std::wstring xml =
        openrcv::unattend::BuildUnattendXml(openrcv::unattend::UnattendOptions{});
    Expect(xml.find(L"<settings pass=\"windowsPE\">") != std::wstring::npos,
           L"UnattendBuilderAddsWindowsPeSettings");
    Expect(xml.find(L"<DynamicUpdate>\r\n"
                    L"        <Enable>false</Enable>\r\n"
                    L"        <WillShowUI>Never</WillShowUI>") !=
               std::wstring::npos,
           L"UnattendBuilderDisablesSetupDynamicUpdate");
}

void UnattendBuilderBlocksNetworkOnlyDuringSkippedOobe() {
    openrcv::unattend::UnattendOptions options;
    options.skipOnlineAccountScreens = true;
    std::wstring xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"<settings pass=\"specialize\">") != std::wstring::npos,
           L"UnattendBuilderAddsSpecializeNetworkBlock");
    Expect(xml.find(L"HideWirelessSetupInOOBE>true") != std::wstring::npos,
           L"UnattendBuilderHidesOobeNetworkPage");
    Expect(xml.find(L"advfirewall firewall add rule") != std::wstring::npos,
           L"UnattendBuilderBlocksOobeOutboundNetwork");
    Expect(xml.find(L"advfirewall firewall delete rule") != std::wstring::npos,
           L"UnattendBuilderRestoresNetworkAtFirstLogon");

    options.skipOnlineAccountScreens = false;
    xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"OpenRCV OOBE Network Block") == std::wstring::npos &&
               xml.find(L"HideWirelessSetupInOOBE") == std::wstring::npos,
           L"UnattendBuilderKeepsNetworkWhenOobeSkipIsDisabled");
}

void UnattendBuilderInstallsWindowsUpdatePauseTask() {
    openrcv::unattend::UnattendOptions options;
    options.skipOnlineAccountScreens = false;
    options.pauseWindowsUpdate = true;
    const std::wstring xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"schtasks.exe /Create /TN PauseWindowsUpdate") !=
               std::wstring::npos &&
               xml.find(L"schtasks.exe /Run /TN PauseWindowsUpdate") !=
               std::wstring::npos,
           L"UnattendBuilderRegistersAndStartsWindowsUpdatePauseTask");
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

void DeploymentPlanRejectsUnconfirmedFormat() {
    openrcv::workflow::DeploymentRequest request;
    request.imageFile = L"D:\\sources\\install.wim";
    request.imageIndex = 1;
    request.targetRoot = L"C:\\";
    request.formatTarget = true;
    const openrcv::workflow::DeploymentPlan plan =
        openrcv::workflow::BuildDeploymentPlan(request);
    Expect(!plan.IsValid(), L"DeploymentPlanRejectsUnconfirmedFormat");
}

void DeploymentPlanUsesDiskPartForConfirmedFormat() {
    openrcv::workflow::DeploymentRequest request;
    request.imageFile = L"D:\\sources\\install.wim";
    request.imageIndex = 1;
    request.targetRoot = L"C:\\";
    request.formatTarget = true;
    request.confirmedFormatTarget = L"C:\\";
    const auto plan = openrcv::workflow::BuildDeploymentPlan(request);
    Expect(plan.IsValid() && !plan.operations.empty(),
           L"DeploymentPlanAcceptsConfirmedFormat");
    if (!plan.operations.empty()) {
        Expect(plan.operations.front().type ==
                   openrcv::workflow::DeploymentOperationType::FormatTarget &&
               plan.operations.front().executable == L"diskpart.exe" &&
               plan.operations.front().arguments == std::vector<std::wstring>{L"C:\\"},
               L"DeploymentPlanUsesDiskPartForFormat");
    }
}

void DeploymentPlanCreatesApplyAndBootOperations() {
    openrcv::workflow::DeploymentRequest request;
    request.imageFile = L"D:\\sources\\install.wim";
    request.imageIndex = 3;
    request.targetRoot = L"C:\\";
    request.systemPartitionRoot = L"S:\\";
    const openrcv::workflow::DeploymentPlan plan =
        openrcv::workflow::BuildDeploymentPlan(request);
    Expect(plan.IsValid(), L"DeploymentPlanAcceptsSafeRequest");
    Expect(plan.operations.size() == 3, L"DeploymentPlanCreatesThreeOperations");
    if (plan.operations.size() == 3) {
        Expect(plan.operations[0].type == openrcv::workflow::DeploymentOperationType::ApplyImage,
               L"DeploymentPlanAppliesImageFirst");
        Expect(plan.operations[0].arguments.back() == L"/CheckIntegrity",
               L"DeploymentPlanChecksImageIntegrity");
        Expect(plan.operations[1].arguments.size() == 2 &&
                   plan.operations[1].arguments[1] ==
                       L"C:\\Windows\\System32\\Oobe\\Info\\Oobe.xml",
               L"DeploymentPlanWritesOobeRegionConfiguration");
        Expect(plan.operations[2].type == openrcv::workflow::DeploymentOperationType::ConfigureBoot,
               L"DeploymentPlanConfiguresBootLast");
    }
}

void DeploymentPlanSelectsBcdBootFirmwareAndLocale() {
    openrcv::workflow::DeploymentRequest request;
    request.imageFile = L"D:\\sources\\install.wim";
    request.imageIndex = 1;
    request.targetRoot = L"C:\\";
    request.systemPartitionRoot = L"S:\\";
    request.bootFirmware = openrcv::workflow::BootFirmware::Uefi;
    request.bootLocale = L"zh-CN";
    auto plan = openrcv::workflow::BuildDeploymentPlan(request);
    const std::vector<std::wstring> expectedUefi{
        L"C:\\Windows", L"/l", L"zh-CN", L"/s", L"S:", L"/f", L"UEFI"};
    Expect(plan.IsValid() && plan.operations.back().arguments == expectedUefi,
           L"DeploymentPlanUsesUefiBcdBootAndSelectedLocale");

    request.bootFirmware = openrcv::workflow::BootFirmware::Bios;
    request.bootLocale = L"en-US";
    plan = openrcv::workflow::BuildDeploymentPlan(request);
    const std::vector<std::wstring> expectedBios{
        L"C:\\Windows", L"/l", L"en-US", L"/s", L"S:", L"/f", L"BIOS"};
    Expect(plan.IsValid() && plan.operations.back().arguments == expectedBios,
           L"DeploymentPlanUsesBiosBcdBootAndSelectedLocale");

    request.bootLocale.clear();
    plan = openrcv::workflow::BuildDeploymentPlan(request);
    const auto& defaultArguments = plan.operations.back().arguments;
    Expect(std::find(defaultArguments.begin(), defaultArguments.end(), L"/l") ==
               defaultArguments.end(),
           L"DeploymentPlanDoesNotForceDefaultBootLanguage");
}

void DeploymentPlanAddsOfflineSettingsWhenRequested() {
    openrcv::workflow::DeploymentRequest request;
    request.imageFile = L"D:\\sources\\install.wim";
    request.imageIndex = 1;
    request.targetRoot = L"C:\\";
    request.systemPartitionRoot = L"S:\\";
    request.configureOfflineSettings = true;
    const auto plan = openrcv::workflow::BuildDeploymentPlan(request);
    Expect(plan.IsValid() && plan.operations.size() == 4,
           L"DeploymentPlanAddsOfflineSettingsOperation");
    if (plan.operations.size() == 4) {
        Expect(plan.operations[2].type ==
                   openrcv::workflow::DeploymentOperationType::ConfigureOfflineSettings,
               L"DeploymentPlanConfiguresOfflineSettingsBeforeBoot");
    }
}

} // namespace

int wmain() {
    CommandLineQuotingPreservesSpaces();
    CommandLineQuotingEscapesQuotesAndTrailingSlashes();
    DismProgressParserReturnsLatestPercentage();
    ProcessRunnerStreamsOutput();
    ProcessRunnerCancelsRunningProcess();
    WimMetadataParserReturnsImagesAndArchitectures();
    UnattendBuilderCreatesLocalAdmin();
    UnattendBuilderUsesSelectedLocales();
    RegionConfigurationsKeepLocalesAndGeoIdsTogether();
    UnattendBuilderUsesSelectedArchitecture();
    UnattendBuilderDisablesSetupDynamicUpdate();
    UnattendBuilderBlocksNetworkOnlyDuringSkippedOobe();
    UnattendBuilderInstallsWindowsUpdatePauseTask();
    OfflineSettingsMapAdvancedOptionsToRegistry();
    OfflineSettingsConfiguresUpdateNotificationByEdition();
    DeploymentPlanRejectsUnconfirmedFormat();
    DeploymentPlanUsesDiskPartForConfirmedFormat();
    DeploymentPlanCreatesApplyAndBootOperations();
    DeploymentPlanSelectsBcdBootFirmwareAndLocale();
    DeploymentPlanAddsOfflineSettingsWhenRequested();
    if (failures == 0) std::wcout << L"All OpenRCV core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
