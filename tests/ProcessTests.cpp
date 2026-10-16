#include "TestSupport.h"

#include "openrcv/process/ProcessRunner.h"

#include <atomic>

namespace openrcv::tests {
namespace {

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

void DismProgressParserReturnsLatestPercentage() {
    const auto progress = openrcv::process::ParseDismProgressPercentage(
        L"[===== 25.0% ]\r[========== 67.4% ]");
    Expect(progress.has_value() && *progress == 67,
           L"DismProgressParserReturnsLatestPercentage");
    Expect(!openrcv::process::ParseDismProgressPercentage(L"操作成功完成。").has_value(),
           L"DismProgressParserIgnoresOutputWithoutPercentage");
}

void ProcessRunnerStreamsOutput() {
    std::wstring streamed;
    openrcv::process::ProcessRunOptions options;
    options.outputCallback = [&](std::wstring_view chunk) {
        streamed.append(chunk.data(), chunk.size());
    };
    const auto result = openrcv::process::RunHidden(
        L"cmd.exe", {L"/d", L"/c", L"echo OpenRCV-stream-test"}, options);
    if (!result.Succeeded()) {
        std::wcerr << L"ProcessRunner stream error: system=" << result.systemError
                   << L" exit=" << result.exitCode << L"\n";
    }
    Expect(result.Succeeded(), L"ProcessRunnerStreamsOutputCommandSucceeds");
    Expect(streamed.find(L"OpenRCV-stream-test") != std::wstring::npos,
           L"ProcessRunnerStreamsOutputBeforeReturning");
}

void ProcessRunnerCancelsRunningProcess() {
    std::atomic_bool cancellationRequested{};
    openrcv::process::ProcessRunOptions options;
    options.cancellationRequested = &cancellationRequested;
    options.outputCallback = [&](std::wstring_view chunk) {
        if (!chunk.empty()) cancellationRequested.store(true);
    };
    const auto result = openrcv::process::RunHidden(
        L"ping.exe", {L"127.0.0.1", L"-n", L"20"}, options);
    if (!result.cancelled) {
        std::wcerr << L"ProcessRunner cancel error: system=" << result.systemError
                   << L" exit=" << result.exitCode << L"\n";
    }
    Expect(result.cancelled && !result.Succeeded(),
           L"ProcessRunnerCancelsRunningProcess");
}

} // namespace

void RunProcessTests() {
    CommandLineQuotingPreservesSpaces();
    CommandLineQuotingEscapesQuotesAndTrailingSlashes();
    DismProgressParserReturnsLatestPercentage();
    ProcessRunnerStreamsOutput();
    ProcessRunnerCancelsRunningProcess();
}

} // namespace openrcv::tests
