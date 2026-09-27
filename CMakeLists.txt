cmake_minimum_required(VERSION 3.20)

project(OpenRCV VERSION 0.1.0 LANGUAGES CXX)

option(OPENRCV_BUILD_TESTS "Build OpenRCV unit tests" ON)

add_library(openrcv_core STATIC
    src/imaging/IsoMount.cpp
    src/imaging/WindowsImage.cpp
    src/process/ProcessRunner.cpp
    src/storage/VolumeEnumerator.cpp
    src/unattend/UnattendBuilder.cpp
    src/workflow/DeploymentPlan.cpp
    src/workflow/ImageSourceService.cpp
)

target_compile_features(openrcv_core PUBLIC cxx_std_17)
target_compile_definitions(openrcv_core PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_include_directories(openrcv_core PUBLIC include)

add_executable(OpenRCV WIN32
    src/main.cpp
    src/ui/Application.cpp
)

target_compile_features(OpenRCV PRIVATE cxx_std_17)
target_compile_definitions(OpenRCV PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(OpenRCV PRIVATE include)
target_link_libraries(OpenRCV PRIVATE openrcv_core comctl32 comdlg32)

if(MSVC)
    set_property(TARGET openrcv_core OpenRCV PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(openrcv_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(OpenRCV PRIVATE /W4 /permissive- /utf-8)
endif()

if(OPENRCV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
