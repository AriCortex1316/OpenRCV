#pragma once

#include <iostream>
#include <string>

namespace openrcv::tests {

inline int failures = 0;

inline void ExpectEqual(const std::wstring& actual, const std::wstring& expected,
                        const wchar_t* testName) {
    if (actual == expected) return;
    std::wcerr << L"FAILED: " << testName << L"\n  expected: " << expected
               << L"\n  actual:   " << actual << L'\n';
    ++failures;
}

inline void Expect(bool condition, const wchar_t* testName) {
    if (condition) return;
    std::wcerr << L"FAILED: " << testName << L'\n';
    ++failures;
}

void RunProcessTests();
void RunImagingTests();
void RunUnattendTests();
void RunOfflineSettingsTests();
void RunDeploymentPlanTests();

} // namespace openrcv::tests
