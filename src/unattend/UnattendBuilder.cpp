#include "openrcv/unattend/UnattendBuilder.h"

#include <string_view>

namespace openrcv::unattend {
namespace {

std::wstring EscapeXml(std::wstring_view value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (const wchar_t character : value) {
        switch (character) {
        case L'&': escaped += L"&amp;"; break;
        case L'<': escaped += L"&lt;"; break;
        case L'>': escaped += L"&gt;"; break;
        case L'\"': escaped += L"&quot;"; break;
        case L'\'': escaped += L"&apos;"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

const wchar_t* ArchitectureName(ProcessorArchitecture architecture) {
    switch (architecture) {
    case ProcessorArchitecture::X86: return L"x86";
    case ProcessorArchitecture::Amd64: return L"amd64";
    case ProcessorArchitecture::Arm64: return L"arm64";
    }
    return L"amd64";
}

void AddElement(std::wstring& xml, const wchar_t* name, std::wstring_view value,
                int indentation) {
    xml.append(static_cast<std::size_t>(indentation), L' ');
    xml += L"<";
    xml += name;
    xml += L">";
    xml += EscapeXml(value);
    xml += L"</";
    xml += name;
    xml += L">\r\n";
}

void AddInternationalComponent(
    std::wstring& xml, std::wstring_view componentName,
    const std::wstring& componentPrefix, const std::wstring& componentSuffix,
    const config::RegionConfiguration& region) {
    xml += componentPrefix + std::wstring(componentName) + componentSuffix;
    AddElement(xml, L"InputLocale", region.inputLocale, 6);
    AddElement(xml, L"SystemLocale", region.locale, 6);
    AddElement(xml, L"UserLocale", region.locale, 6);
    xml += L"    </component>\r\n";
}

} // namespace

std::wstring BuildUnattendXml(const UnattendOptions& options) {
    const std::wstring architecture = ArchitectureName(options.architecture);
    const std::wstring componentPrefix =
        L"    <component name=\"Microsoft-Windows-";
    const std::wstring componentSuffix =
        L"\" processorArchitecture=\"" + architecture +
        L"\" publicKeyToken=\"31bf3856ad364e35\" language=\"neutral\" "
        L"versionScope=\"nonSxS\">\r\n";

    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        L"<unattend xmlns=\"urn:schemas-microsoft-com:unattend\" "
        L"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\">\r\n";

    xml += L"  <settings pass=\"windowsPE\">\r\n";
    AddInternationalComponent(xml, L"International-Core-WinPE",
                              componentPrefix, componentSuffix, options.region);
    xml += componentPrefix + L"Setup" + componentSuffix;
    xml += L"      <DynamicUpdate>\r\n"
           L"        <Enable>false</Enable>\r\n"
           L"        <WillShowUI>Never</WillShowUI>\r\n"
           L"      </DynamicUpdate>\r\n"
           L"    </component>\r\n"
           L"  </settings>\r\n";

    if (options.skipOnlineAccountScreens || options.pauseWindowsUpdate) {
        xml += L"  <settings pass=\"specialize\">\r\n";
        xml += componentPrefix + L"Deployment" + componentSuffix;
        xml += L"      <RunSynchronous>\r\n";
        int order = 1;
        if (options.skipOnlineAccountScreens) {
            xml += L"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
                   L"          <Order>" + std::to_wstring(order++) +
                   L"</Order>\r\n"
                   L"          <Description>Block network during OOBE</Description>\r\n";
            AddElement(xml, L"Path",
                L"netsh.exe advfirewall firewall add rule "
                L"name=\"OpenRCV OOBE Network Block\" dir=out action=block "
                L"enable=yes profile=any",
                10);
            xml += L"        </RunSynchronousCommand>\r\n";
        }
        if (options.pauseWindowsUpdate) {
            xml += L"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
                   L"          <Order>" + std::to_wstring(order++) +
                   L"</Order>\r\n"
                   L"          <Description>Install Windows Update pause task</Description>\r\n";
            AddElement(xml, L"Path",
                L"schtasks.exe /Create /TN PauseWindowsUpdate /XML "
                L"C:\\Windows\\Setup\\Scripts\\PauseWindowsUpdate.xml /F",
                10);
            xml += L"        </RunSynchronousCommand>\r\n"
                   L"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
                   L"          <Order>" + std::to_wstring(order) +
                   L"</Order>\r\n"
                   L"          <Description>Pause Windows Update</Description>\r\n";
            AddElement(xml, L"Path",
                L"schtasks.exe /Run /TN PauseWindowsUpdate", 10);
            xml += L"        </RunSynchronousCommand>\r\n";
        }
        xml += L"      </RunSynchronous>\r\n"
               L"    </component>\r\n"
               L"  </settings>\r\n";
    }

    xml += L"  <settings pass=\"oobeSystem\">\r\n";

    AddInternationalComponent(xml, L"International-Core", componentPrefix,
                              componentSuffix, options.region);

    xml += componentPrefix + L"Shell-Setup" + componentSuffix;
    xml += L"      <OOBE>\r\n"
           L"        <HideEULAPage>true</HideEULAPage>\r\n"
           L"        <ProtectYourPC>3</ProtectYourPC>\r\n";
    if (options.skipOnlineAccountScreens) {
        xml += L"        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>\r\n"
               L"        <HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>\r\n";
    }
    xml += L"      </OOBE>\r\n";

    if (options.createLocalAdministrator && !options.localAdministrator.empty()) {
        xml += L"      <UserAccounts>\r\n"
               L"        <LocalAccounts>\r\n"
               L"          <LocalAccount wcm:action=\"add\">\r\n";
        AddElement(xml, L"Name", options.localAdministrator, 12);
        AddElement(xml, L"DisplayName", options.localAdministrator, 12);
        AddElement(xml, L"Group", L"Administrators", 12);
        xml += L"            <Password>\r\n"
               L"              <Value></Value>\r\n"
               L"              <PlainText>true</PlainText>\r\n"
               L"            </Password>\r\n"
               L"          </LocalAccount>\r\n"
               L"        </LocalAccounts>\r\n"
               L"      </UserAccounts>\r\n";
    }

    xml += L"      <FirstLogonCommands>\r\n";
    if (options.skipOnlineAccountScreens) {
        xml += L"        <SynchronousCommand wcm:action=\"add\">\r\n"
               L"          <Order>1</Order>\r\n"
               L"          <Description>Restore network after OOBE</Description>\r\n";
        AddElement(xml, L"CommandLine",
            L"netsh.exe advfirewall firewall delete rule "
            L"name=\"OpenRCV OOBE Network Block\"",
            10);
        xml += L"        </SynchronousCommand>\r\n";
    }
    xml += L"        <SynchronousCommand wcm:action=\"add\">\r\n"
           L"          <Order>" +
           std::to_wstring(options.skipOnlineAccountScreens ? 2 : 1) +
           L"</Order>\r\n"
           L"          <Description>Set Windows region</Description>\r\n";
    AddElement(xml, L"CommandLine",
        L"powershell.exe -NoLogo -NoProfile -NonInteractive "
        L"-ExecutionPolicy Bypass -Command \"Set-WinHomeLocation -GeoId " +
            std::to_wstring(options.region.geoId) + L"\"",
        10);
    xml += L"        </SynchronousCommand>\r\n"
           L"      </FirstLogonCommands>\r\n";

    xml += L"    </component>\r\n"
           L"  </settings>\r\n"
           L"</unattend>\r\n";
    return xml;
}

std::wstring BuildOobeXml(const config::RegionConfiguration& region) {
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        L"<FirstExperience>\r\n"
        L"  <oobe>\r\n"
        L"    <defaults>\r\n";
    AddElement(xml, L"location", std::to_wstring(region.geoId), 6);
    AddElement(xml, L"keyboard", region.inputLocale, 6);
    xml += L"    </defaults>\r\n"
           L"  </oobe>\r\n"
           L"</FirstExperience>\r\n";
    return xml;
}

} // namespace openrcv::unattend
