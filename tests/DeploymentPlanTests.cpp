#include "TestSupport.h"

#include "openrcv/workflow/DeploymentPlan.h"

#include <algorithm>
#include <vector>

namespace openrcv::tests {
namespace {

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

void RunDeploymentPlanTests() {
    DeploymentPlanRejectsUnconfirmedFormat();
    DeploymentPlanUsesDiskPartForConfirmedFormat();
    DeploymentPlanCreatesApplyAndBootOperations();
    DeploymentPlanSelectsBcdBootFirmwareAndLocale();
    DeploymentPlanAddsOfflineSettingsWhenRequested();
}

} // namespace openrcv::tests
