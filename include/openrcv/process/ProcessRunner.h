#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openrcv::process {

struct ProcessResult {
    std::uint32_t exitCode{UINT32_MAX};
    std::uint32_t systemError{};
    std::wstring output;
    bool cancelled{};

    bool Started() const noexcept { return systemError == 0; }
    bool Succeeded() const noexcept { return Started() && !cancelled && exitCode == 0; }
};

struct ProcessRunOptions {
    std::function<void(std::wstring_view)> outputCallback;
    const std::atomic_bool* cancellationRequested{};
};

std::wstring QuoteCommandLineArgument(std::wstring_view argument);

ProcessResult RunHidden(const std::wstring& executable,
                        const std::vector<std::wstring>& arguments);

ProcessResult RunHidden(const std::wstring& executable,
                        const std::vector<std::wstring>& arguments,
                        const ProcessRunOptions& options);

std::optional<int> ParseDismProgressPercentage(std::wstring_view output);

} // namespace openrcv::process
