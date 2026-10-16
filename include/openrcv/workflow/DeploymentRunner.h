#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "openrcv/config/OfflineSettings.h"
#include "openrcv/unattend/UnattendBuilder.h"
#include "openrcv/workflow/DeploymentPlan.h"

namespace openrcv::workflow {

struct DeploymentTask {
    DeploymentPlan plan;
    unattend::UnattendOptions unattendOptions;
    config::AdvancedSettings advancedSettings;
    std::wstring targetRoot;
    std::wstring temporaryBootRoot;
};

struct DeploymentResult {
    bool succeeded{};
    bool cancelled{};
    std::wstring temporaryBootRoot;
};

struct DeploymentCallbacks {
    std::function<void(const std::wstring&)> log;
    std::function<void(int)> progress;
};

// The caller must validate the plan, check target safety and obtain explicit
// user confirmation before invoking this executor.
// Runs synchronously; callbacks execute on the calling thread. Formatting is
// allowed to finish before cancellation is observed. Takes ownership of any
// temporary boot mount and removes it before returning its root in the result.
DeploymentResult RunDeployment(
    DeploymentTask task, const std::atomic_bool& cancellationRequested,
    const DeploymentCallbacks& callbacks = {});

} // namespace openrcv::workflow
