cmake_minimum_required(VERSION 3.20)

project(OpenRCV VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OpenRCV requires Windows and the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /sdl)
endif()

option(OPENRCV_BUILD_TESTS "Build OpenRCV unit tests" ON)

add_library(openrcv_core STATIC
    src/config/OfflineSettings.cpp
    src/config/Region.cpp
    src/imaging/IsoMount.cpp
    src/imaging/WindowsImage.cpp
    src/process/ProcessRunner.cpp
    src/storage/VolumeEnumerator.cpp
    src/unattend/UnattendBuilder.cpp
    src/workflow/DeploymentPlan.cpp
    src/workflow/DeploymentRunner.cpp
    src/workflow/ImageSourceService.cpp
)

target_compile_features(openrcv_core PUBLIC cxx_std_17)
target_include_directories(openrcv_core PUBLIC include)
target_link_libraries(openrcv_core PUBLIC advapi32)

add_executable(OpenRCV WIN32
    src/main.cpp
    src/ui/Application.cpp
)

target_link_libraries(OpenRCV PRIVATE openrcv_core comctl32 comdlg32)

if(OPENRCV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
