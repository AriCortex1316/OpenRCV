add_executable(OpenRCVCoreTests
    CoreTests.cpp
)

target_link_libraries(OpenRCVCoreTests PRIVATE openrcv_core)

if(MSVC)
    set_property(TARGET OpenRCVCoreTests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(OpenRCVCoreTests PRIVATE /W4 /permissive- /utf-8)
endif()

add_test(NAME OpenRCVCoreTests COMMAND OpenRCVCoreTests)
