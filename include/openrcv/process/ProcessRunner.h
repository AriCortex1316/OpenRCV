#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openrcv::process {

struct ProcessResult {
    std::uint32_t exitCode{UINT32_MAX};
    std::uint32_t systemError{};
    std::wstring output;

    bool Started() const noexcept { return systemError == 0; }
    bool Succeeded() const noexcept { return Started() && exitCode == 0; }
};

std::wstring QuoteCommandLineArgument(std::wstring_view argument);

ProcessResult RunHidden(const std::wstring& executable,
                        const std::vector<std::wstring>& arguments);

} // namespace openrcv::process
