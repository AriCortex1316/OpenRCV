#include <windows.h>

#include "openrcv/process/ProcessRunner.h"

#include <algorithm>
#include <cwctype>
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

std::wstring ResolveExecutable(const std::wstring& executable) {
    if (executable.find_first_of(L"\\/") != std::wstring::npos) return executable;
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = SearchPathW(nullptr, executable.c_str(), nullptr,
                                     static_cast<DWORD>(path.size()),
                                     path.data(), nullptr);
    if (length == 0 || length >= path.size()) return {};
    return std::wstring(path.data(), length);
}

std::wstring DecodeOem(const char* bytes, std::size_t size) {
    if (size == 0) return {};
    const int length = MultiByteToWideChar(CP_OEMCP, 0, bytes,
        static_cast<int>(size), nullptr, 0);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, bytes, static_cast<int>(size),
                        text.data(), length);
    return text;
}

void AppendOutput(ProcessResult& result, const ProcessRunOptions& options,
                  const char* bytes, std::size_t size) {
    std::wstring text = DecodeOem(bytes, size);
    if (text.empty()) return;
    result.output += text;
    if (options.outputCallback) options.outputCallback(text);
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
    return RunHidden(executable, arguments, ProcessRunOptions{});
}

ProcessResult RunHidden(const std::wstring& executable,
                        const std::vector<std::wstring>& arguments,
                        const ProcessRunOptions& options) {
    ProcessResult result;
    const std::wstring resolvedExecutable = ResolveExecutable(executable);
    if (resolvedExecutable.empty()) {
        result.systemError = GetLastError();
        if (result.systemError == 0) result.systemError = ERROR_FILE_NOT_FOUND;
        return result;
    }
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

    ScopedHandle jobHandle(CreateJobObjectW(nullptr, nullptr));
    bool jobReady = false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInformation{};
    jobInformation.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (jobHandle.Get()) {
        jobReady = SetInformationJobObject(
            jobHandle.Get(), JobObjectExtendedLimitInformation,
            &jobInformation, sizeof(jobInformation)) != FALSE;
    }

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe.Get();
    startup.hStdError = writePipe.Get();
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring command = BuildCommandLine(resolvedExecutable, arguments);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(resolvedExecutable.c_str(), mutableCommand.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                        &startup, &process)) {
        result.systemError = GetLastError();
        return result;
    }

    ScopedHandle processHandle(process.hProcess);
    ScopedHandle threadHandle(process.hThread);
    const bool processInJob = jobReady &&
        AssignProcessToJobObject(jobHandle.Get(), processHandle.Get()) != FALSE;
    if (ResumeThread(threadHandle.Get()) == static_cast<DWORD>(-1)) {
        result.systemError = GetLastError();
        if (processInJob) {
            TerminateJobObject(jobHandle.Get(), result.systemError);
        } else {
            TerminateProcess(processHandle.Get(), result.systemError);
        }
        WaitForSingleObject(processHandle.Get(), INFINITE);
        return result;
    }
    writePipe = ScopedHandle{};

    char buffer[4096];
    bool processExited = false;
    while (!processExited) {
        processExited = WaitForSingleObject(processHandle.Get(), 0) == WAIT_OBJECT_0;
        if (processExited) break;
        if (options.cancellationRequested &&
            options.cancellationRequested->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            if (processInJob) {
                TerminateJobObject(jobHandle.Get(), ERROR_CANCELLED);
            } else {
                TerminateProcess(processHandle.Get(), ERROR_CANCELLED);
            }
        }

        DWORD available = 0;
        if (!PeekNamedPipe(readPipe.Get(), nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) result.systemError = GetLastError();
            break;
        }
        while (available != 0) {
            DWORD count = 0;
            const DWORD requested = std::min<DWORD>(available, sizeof(buffer));
            if (!ReadFile(readPipe.Get(), buffer, requested, &count, nullptr) || count == 0) {
                available = 0;
                break;
            }
            AppendOutput(result, options, buffer, count);
            available -= std::min(available, count);
        }

        processExited = WaitForSingleObject(processHandle.Get(), 50) == WAIT_OBJECT_0;
    }

    WaitForSingleObject(processHandle.Get(), INFINITE);

    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe.Get(), nullptr, 0, nullptr, &available, nullptr) ||
            available == 0) break;
        DWORD count = 0;
        if (!ReadFile(readPipe.Get(), buffer,
                      std::min<DWORD>(available, sizeof(buffer)), &count, nullptr) ||
            count == 0) break;
        AppendOutput(result, options, buffer, count);
    }

    DWORD exitCode = ERROR_GEN_FAILURE;
    GetExitCodeProcess(processHandle.Get(), &exitCode);
    result.exitCode = exitCode;
    return result;
}

std::optional<int> ParseDismProgressPercentage(std::wstring_view output) {
    std::optional<int> progress;
    std::size_t percent = output.find(L'%');
    while (percent != std::wstring_view::npos) {
        std::size_t begin = percent;
        while (begin > 0 && (std::iswdigit(output[begin - 1]) != 0 ||
                             output[begin - 1] == L'.')) {
            --begin;
        }
        if (begin < percent) {
            const std::wstring number(output.substr(begin, percent - begin));
            wchar_t* end = nullptr;
            const double value = std::wcstod(number.c_str(), &end);
            if (end == number.c_str() + number.size() &&
                value >= 0.0 && value <= 100.0) {
                progress = static_cast<int>(value + 0.5);
            }
        }
        percent = output.find(L'%', percent + 1);
    }
    return progress;
}

} // namespace openrcv::process
