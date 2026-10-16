#include "TestSupport.h"

#include "openrcv/config/OfflineSettings.h"
#include "openrcv/unattend/UnattendBuilder.h"

namespace openrcv::tests {
namespace {

void UnattendBuilderCreatesLocalAdmin() {
    openrcv::unattend::UnattendOptions options;
    options.localAdministrator = L"Installer & Admin";
    const std::wstring xml = openrcv::unattend::BuildUnattendXml(options);
    Expect(xml.find(L"<Name>Installer &amp; Admin</Name>") != std::wstring::npos,
           L"UnattendBuilderEscapesLocalAdminName");
    Expect(xml.find(L"<Group>Administrators</Group>") != std::wstring::npos,
           L"UnattendBuilderCreatesLocalAdmin");
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

} // namespace

void RunUnattendTests() {
    UnattendBuilderCreatesLocalAdmin();
    UnattendBuilderUsesSelectedLocales();
    UnattendBuilderUsesSelectedArchitecture();
    UnattendBuilderDisablesSetupDynamicUpdate();
    UnattendBuilderBlocksNetworkOnlyDuringSkippedOobe();
    UnattendBuilderInstallsWindowsUpdatePauseTask();
}

} // namespace openrcv::tests
