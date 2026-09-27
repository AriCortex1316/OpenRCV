#include <windows.h>

#include "openrcv/process/ProcessRunner.h"

#include <utility>
#include <vector>

namespace openrcv::process {
namespace {

class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedHandle() { Reset(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    HANDLE Get() const noexcept { return handle_; }
    HANDLE* Put() noexcept {
        Reset();
        return &handle_;
    }
    HANDLE Release() noexcept { return std::exchange(handle_, nullptr); }

private:
    void Reset() noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = nullptr;
    }

    HANDLE handle_{};
};

std::wstring BuildCommandLine(const std::wstring& executable,
                              const std::vector<std::wstring>& arguments) {
    std::wstring command = QuoteCommandLineArgument(executable);
    for (const std::wstring& argument : arguments) {
        command.push_back(L' ');
        command += QuoteCommandLineArgument(argument);
    }
    return command;
}

} // namespace

std::wstring QuoteCommandLineArgument(std::wstring_view argument) {
    if (argument.empty()) return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }

    std::wstring quoted(1, L'\"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

ProcessResult RunHidden(const std::wstring& executable,
                        const std::vector<std::wstring>& arguments) {
    ProcessResult result;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    ScopedHandle readPipe;
    ScopedHandle writePipe;
    if (!CreatePipe(readPipe.Put(), writePipe.Put(), &security, 0)) {
        result.systemError = GetLastError();
        return result;
    }
    if (!SetHandleInformation(readPipe.Get(), HANDLE_FLAG_INHERIT, 0)) {
        result.systemError = GetLastError();
        return result;
    }

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe.Get();
    startup.hStdError = writePipe.Get();
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring command = BuildCommandLine(executable, arguments);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        result.systemError = GetLastError();
        return result;
    }

    ScopedHandle processHandle(process.hProcess);
    ScopedHandle threadHandle(process.hThread);
    writePipe = ScopedHandle{};

    std::string bytes;
    char buffer[4096];
    DWORD count = 0;
    while (ReadFile(readPipe.Get(), buffer, sizeof(buffer), &count, nullptr) && count != 0) {
        bytes.append(buffer, count);
    }

    WaitForSingleObject(processHandle.Get(), INFINITE);
    DWORD exitCode = ERROR_GEN_FAILURE;
    GetExitCodeProcess(processHandle.Get(), &exitCode);
    result.exitCode = exitCode;

    if (!bytes.empty()) {
        const int length = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(),
                                                static_cast<int>(bytes.size()), nullptr, 0);
        if (length > 0) {
            result.output.resize(static_cast<std::size_t>(length));
            MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()),
                                result.output.data(), length);
        }
    }
    return result;
}

} // namespace openrcv::process
