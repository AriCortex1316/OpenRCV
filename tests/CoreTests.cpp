#include "TestSupport.h"

int wmain() {
    openrcv::tests::RunProcessTests();
    openrcv::tests::RunImagingTests();
    openrcv::tests::RunUnattendTests();
    openrcv::tests::RunOfflineSettingsTests();
    openrcv::tests::RunDeploymentPlanTests();
    if (openrcv::tests::failures == 0) {
        std::wcout << L"All OpenRCV core tests passed.\n";
    }
    return openrcv::tests::failures == 0 ? 0 : 1;
}
