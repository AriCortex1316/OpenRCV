# OpenRCV development progress

Last updated: 2026-09-28

## Current objective

Build a native C++17 Windows deployment application for WinPE. The primary
target is x64 WinPE, with a separate Win32 build retained for legacy x86 WinPE.

The next UI revision should follow the EasyRCV reference supplied in the
conversation: blue title bar, left-side navigation, image selection, a detailed
partition table, Windows edition selection, installation options, explicit boot
partition selection, and a central start-install button.

## Completed

- Created a native Unicode Win32 application with no third-party runtime.
- Configured the Release build to use the static MSVC runtime.
- Added both CMake and Visual Studio/MSBuild build definitions.
- Reduced `src/main.cpp` to the Windows application entry point.
- Split implementation into `ui`, `imaging`, `process`, `storage`, `unattend`,
  and `workflow` modules.
- Implemented read-only ISO mounting through the Virtual Disk API.
- Implemented discovery of `sources\\install.wim` and `install.esd`.
- Implemented hidden child-process execution and Windows argument quoting.
- Implemented DISM image-list parsing.
- Implemented fixed-volume enumeration.
- Implemented unattended-setup XML generation.
- Implemented a validated deployment-plan model for format, DISM apply,
  unattend-file placement, and BCDBoot operations.
- Added safeguards that reject the WinPE `X:` RAM disk and reject formatting
  until the exact target root has been confirmed.
- Added host-safe tests for command-line quoting, DISM parsing, unattended XML,
  and deployment-plan validation.

## Verified builds

- Visual Studio/MSBuild Release x64: passed.
- Visual Studio/MSBuild Release Win32: passed.
- CMake Release x64: passed.
- CTest: all current tests passed.

Generated executables are under `build/` and are excluded from Git.

## Not implemented yet

- The EasyRCV-style UI from the latest reference image.
- A detailed partition list with disk number, partition number, GPT/MBR,
  filesystem, label, total size, free space, and storage bus type.
- A boot-partition selector in the UI.
- Automatic boot-partition recommendation.
- Enumeration or temporary mounting of EFI partitions that have no drive letter.
- A destructive deployment executor.
- Progress reporting and cancellation for DISM and BCDBoot.
- Physical-disk identity confirmation and removable/host-disk safeguards.
- WinPE integration tests in a disposable virtual machine.

## Next implementation sequence

1. Extend `storage::VolumeInfo` with disk number, partition number, partition
   scheme, filesystem, bus type, and MBR active-partition state.
2. Implement and unit-test `storage::RecommendBootPartition`.
   - GPT: prefer a FAT32 EFI-sized partition on the same physical disk.
   - MBR: prefer the active partition on the same physical disk.
   - Do not guess across physical disks; leave selection empty if confidence is
     insufficient.
3. Replace the current target-volume combo box with a report-style Win32 list
   view matching the supplied UI reference.
4. Add an explicit boot-partition combo box and an automatic-selection action.
5. Rework the full installation page to match the reference layout.
6. Connect the UI to the deployment plan in dry-run mode.
7. Add a separately gated executor only after disk identity and confirmation
   safeguards are covered by tests.

## Safety constraints

- Never run format, DISM apply, or BCDBoot against the development host.
- Destructive storage tests must run only in a disposable virtual machine.
- Every destructive command must be logged with its exit code.
- Formatting requires explicit confirmation of the exact target.
- Dry-run remains the default until WinPE integration testing is complete.

## Repository state

- Git branch: `main`
- Baseline commit before this progress file: `349c935`
- Local-only Git identity: `OpenRCV Local <openrcv@local.invalid>`
- The empty `prototype/` directory may remain visible while Sublime Text holds
  its former `README.md` path open; it contains no project files.
