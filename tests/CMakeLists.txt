add_executable(OpenRCVCoreTests
    CoreTests.cpp
    DeploymentPlanTests.cpp
    ImagingTests.cpp
    OfflineSettingsTests.cpp
    ProcessTests.cpp
    UnattendTests.cpp
)

target_link_libraries(OpenRCVCoreTests PRIVATE openrcv_core)

add_test(NAME OpenRCVCoreTests COMMAND OpenRCVCoreTests)
