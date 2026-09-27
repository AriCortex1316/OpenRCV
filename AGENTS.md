# Repository Guidelines

## Project Structure & Module Organization

OpenRCV is a native C++ Windows installer intended for WinPE. Keep production code under `src/`, public interfaces under `include/`, Windows resources under `resources/`, and automated tests under `tests/`. Separate platform concerns into focused modules such as `ui`, `imaging`, `storage`, `unattend`, and `process`. Generated files and compiler output belong in `build/` or `out/` and must not be committed.

## Build, Test, and Development Commands

Use a Visual Studio Developer PowerShell with the Desktop development with C++ workload installed.

```powershell
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release
ctest --test-dir build/x64 -C Release --output-on-failure
```

Replace `x64` with `x86` and use `-A Win32` for the 32-bit build. The Release configuration should use the static MSVC runtime so the executable does not require the Visual C++ Redistributable in WinPE.

## Coding Style & Naming Conventions

Use C++17, Unicode Win32 APIs, four-space indentation, and braces on the same line. Prefer `PascalCase` for types and functions, `camelCase` for local variables, and `kPascalCase` for constants. Use the `W` variants of Windows APIs and `std::wstring` for paths. Keep headers self-contained and avoid MFC, ATL, exceptions across module boundaries, and unnecessary third-party dependencies. Run the repository's formatter before submitting changes when a formatting configuration is present.

## Testing Guidelines

Unit-test XML generation, command-line quoting, DISM output parsing, and disk-selection safeguards. Name tests after observable behavior, for example `UnattendBuilderCreatesLocalAdmin`. Destructive storage tests must run only in disposable virtual machines and must never target the host's physical disks. Verify both x86 and x64 builds; perform WinPE integration testing separately from unit tests.

## Commit & Pull Request Guidelines

Use short, imperative commit subjects such as `Add WIM index enumeration`. Keep each commit focused. Pull requests should explain user-visible behavior, list tested architectures and Windows/WinPE versions, include GUI screenshots for visual changes, and call out any operation that formats disks or changes boot configuration. Link relevant issues and include exact reproduction or verification steps.

## Safety & Configuration

Treat formatting, DISM application, and BCDBoot as destructive operations. Require explicit target confirmation, log commands and exit codes, and keep dry-run behavior available during development. Never commit ISO images, WIM/ESD files, credentials, generated answer files, or machine-specific paths.
