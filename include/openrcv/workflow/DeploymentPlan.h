#pragma once

#include <string>
#include <vector>

namespace openrcv::workflow {

enum class DeploymentOperationType {
    FormatTarget,
    ApplyImage,
    WriteUnattend,
    ConfigureBoot,
};

struct DeploymentRequest {
    std::wstring imageFile;
    int imageIndex{};
    std::wstring targetRoot;
    std::wstring systemPartitionRoot;
    bool formatTarget{};
    std::wstring confirmedFormatTarget;
};

struct DeploymentOperation {
    DeploymentOperationType type{};
    std::wstring description;
    std::wstring executable;
    std::vector<std::wstring> arguments;
    bool destructive{};
};

struct DeploymentPlan {
    std::vector<DeploymentOperation> operations;
    std::vector<std::wstring> errors;

    bool IsValid() const noexcept { return errors.empty(); }
};

DeploymentPlan BuildDeploymentPlan(const DeploymentRequest& request);

} // namespace openrcv::workflow
