# OpenRCV

OpenRCV is a native Win32 Windows deployment tool designed for WinPE.
The current milestone implements image selection and configuration validation;
image application and boot configuration are not connected yet.

Implemented:

- native Unicode Win32 GUI;
- Install and Settings workspace navigation;
- ISO selection;
- ISO attachment through the Windows Virtual Disk API;
- discovery of `sources\\install.wim` or `sources\\install.esd`;
- image list retrieval through `dism.exe /Get-ImageInfo /English`;
- mounted-volume enumeration;
- unattended-setup XML generation;
- human-readable first-version settings.

## Project layout

- `src/ui`: Win32 presentation and event handling;
- `src/imaging`: ISO lifetime management and image metadata parsing;
- `src/storage`: target-volume discovery;
- `src/process`: child-process execution and Windows command-line quoting;
- `src/unattend`: unattended-setup document generation;
- `src/workflow`: validated deployment plans and image-source orchestration;
- `include/openrcv`: module interfaces;
- `tests`: host-safe unit tests for pure logic.

## Build and test

Use a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release
ctest --test-dir build/x64 -C Release --output-on-failure
```

The checked-in Visual Studio project remains available for direct MSBuild use:

```powershell
msbuild OpenRCV.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild OpenRCV.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The x64 Release build is the primary target for current WinPE environments.
The Win32 build remains available for legacy x86 WinPE images.

The executable uses the static MSVC runtime (`/MT`) and has no third-party
runtime dependencies. The x86 build must run in an x86 WinPE image; a standard
x64 WinPE image should use the x64 build.

The ISO path is opened read-only. Some WinPE images do not include a working
ISO virtual-disk provider even when `virtdisk.dll` is present; the program
reports that condition instead of falling back to PowerShell or a bundled
archive library.
