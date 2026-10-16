#include "TestSupport.h"

#include "openrcv/imaging/WindowsImage.h"

#include <vector>

namespace openrcv::tests {
namespace {

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

} // namespace

void RunImagingTests() {
    WimMetadataParserReturnsImagesAndArchitectures();
}

} // namespace openrcv::tests
