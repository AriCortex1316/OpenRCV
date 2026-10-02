# Development progress

Last updated: 2026-09-30

## Current objective

Build a native C++17 Windows deployment application for WinPE. The primary
target is x64 WinPE, with a separate Win32 build retained for legacy x86 WinPE.

The installation page now follows an EasyRCV-inspired layout: blue title bar,
left-side navigation, image selection, a detailed partition table, Windows
edition selection, installation options, explicit boot partition selection,
and a central start-install button.

## Completed

- Created a native Unicode Win32 application with no third-party runtime.
- Configured the Release build to use the static MSVC runtime.
- Added both CMake and Visual Studio/MSBuild build definitions.
- Reduced `src/main.cpp` to the Windows application entry point.
- Split implementation into `ui`, `imaging`, `process`, `storage`, `unattend`,
  and `workflow` modules.
- Implemented read-only ISO mounting through the Virtual Disk API.
- Implemented discovery of `sources\install.wim` and `install.esd`.
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
- Reworked the native Win32 installation page in an EasyRCV-inspired style.
- Replaced the target-volume combo box with a report-style partition list.
- Added disk/partition number, GPT/MBR, filesystem, size, free-space, and storage
  bus information to fixed-volume enumeration and the UI.
- Added an explicit boot-partition selector and kept unavailable features visibly
  disabled rather than presenting them as working actions.
- Added independent EFI/MBR boot-partition enumeration using volume GUID paths.
  The UI recommends an EFI system partition or active MBR partition only when it
  is on the same physical disk as the selected Windows target.
- Added temporary drive-letter mounting for EFI partitions that normally have no
  drive letter; the mount is removed after BCDBoot completes.
- Connected the start-install action to the validated deployment plan. After an
  explicit target confirmation it can format the selected target, apply the
  selected image with DISM, write unattend.xml, and configure boot files with
  BCDBoot. Commands, output, and exit codes are shown in the run log.
- Added live DISM progress reporting and cooperative installation cancellation;
  formatting is allowed to finish before cancellation takes effect.
- Added offline Windows registry configuration for the advanced settings page,
  including file extensions, Widgets, Fast Startup, UAC, automatic BitLocker,
  and Windows Update driver behavior.
- Reworked the advanced “Disable Windows Update” option so it no longer writes
  the permanent `NoAutoUpdate` policy. It now writes a `PauseWindowsUpdate`
  scheduled-task definition, registers and starts the task during the
  `specialize` pass, and refreshes the feature/quality update pause deadline to
  seven days in the future at boot and once per day. Users can temporarily use
  “Resume updates” in Settings, or permanently restore updates by disabling or
  deleting the task.
- Fixed scheduled-task registration after VM diagnosis showed that the task XML
  was written but rejected by `schtasks.exe` with “The task XML is malformed /
  cannot switch encoding.” Task XML is now emitted as UTF-16LE with a BOM and
  declares `encoding="UTF-16"`.
- Added a default-enabled advanced option that shows the “This PC” desktop icon
  for newly created users. Both the `NewStartPanel` and `ClassicStartMenu`
  registry branches are written to the offline default-user hive.
- Added a successful-installation dialog that tells the user to restart the
  computer after image application and boot configuration finish successfully.
- Strengthened target-partition confirmation: a stale gray selection is not
  accepted. The user must click the target partition so it is actively blue,
  then click Start Installation directly; otherwise the app displays
  `请选择分区。` and does not proceed.
- Added an independent advanced option that configures Windows Update to notify
  before downloading and installing. WIM metadata parsing now reads
  `WINDOWS/EDITIONID`: Home editions write Microsoft's documented
  `NoAutoUpdate=0` and `AUOptions=2` values to the offline SOFTWARE hive, while
  Pro and higher editions persist the same settings in the local computer
  policy `Machine/Registry.pol` and update `gpt.ini`. Existing valid policy
  records are preserved, unknown editions are rejected instead of guessed, and
  the option is mutually exclusive with the existing update-pause feature.
- Added host-safe tests for edition classification, the Home registry mapping,
  and the Pro-or-higher `Registry.pol` binary payload.

## Verified builds

- Visual Studio/MSBuild Release x64: passed.
- Visual Studio/MSBuild Release Win32: passed.
- CMake Release x64: passed.
- CTest: all current tests passed.
- Windows Update pause-task generation and unattend registration have unit-test
  coverage; the UTF-16 encoding fix builds successfully on x64 and Win32.
- The final x64/Win32 builds and CTest pass for the Windows Update notification
  feature succeeded after the policy payload was tightened to include
  `NoAutoUpdate=0` alongside `AUOptions=2`.

Generated executables are under `build/` and are excluded from Git.

## Not implemented yet

- Physical-disk identity confirmation and removable/host-disk safeguards.
- WinPE integration tests in a disposable virtual machine.

## Next implementation sequence

1. Extract and unit-test the current boot-partition recommendation logic.
2. Add physical-disk identity confirmation and removable/host-disk safeguards
   beyond the current exact volume confirmation.
3. Add WinPE integration coverage in a disposable virtual machine.

## Safety constraints

- Never run format, DISM apply, or BCDBoot against the development host.
- Destructive storage tests must run only in a disposable virtual machine.
- Every destructive command must be logged with its exit code.
- Formatting requires explicit confirmation of the exact target.
- Destructive execution must remain gated by an explicit confirmation that names
  the exact target and boot partitions.
