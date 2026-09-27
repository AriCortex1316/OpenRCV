#include "openrcv/imaging/WindowsImage.h"
#include "openrcv/process/ProcessRunner.h"
#include "openrcv/unattend/UnattendBuilder.h"
#include "openrcv/workflow/DeploymentPlan.h"

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

void DismParserReturnsImageIndexesAndNames() {
    const std::wstring output =
        L"Deployment Image Servicing and Management tool\r\n"
        L"Index : 1\r\nName : Windows 11 Pro\r\n"
        L"Index : 2\r\nName : Windows 11 Enterprise\r\n";
    const std::vector<openrcv::imaging::WindowsImage> images =
        openrcv::imaging::ParseDismImageInfo(output);
    Expect(images.size() == 2, L"DismParserReturnsTwoImages");
    if (images.size() == 2) {
        Expect(images[0].index == 1 && images[0].name == L"Windows 11 Pro",
               L"DismParserReturnsFirstImage");
        Expect(images[1].index == 2 && images[1].name == L"Windows 11 Enterprise",
               L"DismParserReturnsSecondImage");
    }
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
        Expect(plan.operations[2].type == openrcv::workflow::DeploymentOperationType::ConfigureBoot,
               L"DeploymentPlanConfiguresBootLast");
    }
}

} // namespace

int wmain() {
    CommandLineQuotingPreservesSpaces();
    CommandLineQuotingEscapesQuotesAndTrailingSlashes();
    DismParserReturnsImageIndexesAndNames();
    UnattendBuilderCreatesLocalAdmin();
    DeploymentPlanRejectsUnconfirmedFormat();
    DeploymentPlanCreatesApplyAndBootOperations();
    if (failures == 0) std::wcout << L"All OpenRCV core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
