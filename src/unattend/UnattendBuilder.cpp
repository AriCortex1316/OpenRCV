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
    return architecture == ProcessorArchitecture::X86 ? L"x86" : L"amd64";
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
        L"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\">\r\n"
        L"  <settings pass=\"oobeSystem\">\r\n";

    xml += componentPrefix + L"International-Core" + componentSuffix;
    AddElement(xml, L"InputLocale", options.inputLocale, 6);
    AddElement(xml, L"SystemLocale", options.systemLocale, 6);
    AddElement(xml, L"UILanguage", options.uiLanguage, 6);
    AddElement(xml, L"UserLocale", options.userLocale, 6);
    xml += L"    </component>\r\n";

    xml += componentPrefix + L"Shell-Setup" + componentSuffix;
    xml += L"      <OOBE>\r\n"
           L"        <HideEULAPage>true</HideEULAPage>\r\n"
           L"        <ProtectYourPC>3</ProtectYourPC>\r\n";
    if (options.skipOnlineAccountScreens) {
        xml += L"        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>\r\n";
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

    xml += L"    </component>\r\n"
           L"  </settings>\r\n"
           L"</unattend>\r\n";
    return xml;
}

} // namespace openrcv::unattend
