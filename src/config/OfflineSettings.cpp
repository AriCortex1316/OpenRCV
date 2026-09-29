#include <windows.h>

#include "openrcv/config/OfflineSettings.h"

#include <utility>

namespace openrcv::config {
namespace {

class ScopedRegistryKey {
public:
    ScopedRegistryKey() = default;
    explicit ScopedRegistryKey(HKEY key) : key_(key) {}
    ~ScopedRegistryKey() {
        if (key_) RegCloseKey(key_);
    }

    ScopedRegistryKey(const ScopedRegistryKey&) = delete;
    ScopedRegistryKey& operator=(const ScopedRegistryKey&) = delete;

    ScopedRegistryKey(ScopedRegistryKey&& other) noexcept
        : key_(std::exchange(other.key_, nullptr)) {}
    ScopedRegistryKey& operator=(ScopedRegistryKey&& other) noexcept {
        if (this != &other) {
            if (key_) RegCloseKey(key_);
            key_ = std::exchange(other.key_, nullptr);
        }
        return *this;
    }

    HKEY Get() const noexcept { return key_; }
    HKEY* Put() noexcept { return &key_; }

private:
    HKEY key_{};
};

class ScopedLoadedHive {
public:
    ScopedLoadedHive() = default;
    ~ScopedLoadedHive() { Reset(); }

    ScopedLoadedHive(const ScopedLoadedHive&) = delete;
    ScopedLoadedHive& operator=(const ScopedLoadedHive&) = delete;

    HKEY Get() const noexcept { return key_.Get(); }

    LONG Load(const std::wstring& path, const wchar_t* label) {
        Reset();
        mountName_ = L"OpenRCV_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
            std::to_wstring(GetTickCount64()) + L"_" + label;
        LONG status = RegLoadKeyW(HKEY_LOCAL_MACHINE, mountName_.c_str(), path.c_str());
        if (status != ERROR_SUCCESS) {
            mountName_.clear();
            return status;
        }
        status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, mountName_.c_str(), 0,
                               KEY_READ | KEY_WRITE, key_.Put());
        if (status != ERROR_SUCCESS) {
            RegUnLoadKeyW(HKEY_LOCAL_MACHINE, mountName_.c_str());
            mountName_.clear();
        }
        return status;
    }

private:
    void Reset() noexcept {
        key_ = ScopedRegistryKey{};
        if (!mountName_.empty()) {
            RegUnLoadKeyW(HKEY_LOCAL_MACHINE, mountName_.c_str());
            mountName_.clear();
        }
    }

    ScopedRegistryKey key_;
    std::wstring mountName_;
};

std::wstring JoinPath(const std::wstring& root, const wchar_t* relative) {
    return root + relative;
}

const wchar_t* HiveName(OfflineHive hive) {
    switch (hive) {
    case OfflineHive::Software: return L"SOFTWARE";
    case OfflineHive::System: return L"SYSTEM";
    case OfflineHive::DefaultUser: return L"默认用户";
    }
    return L"未知";
}

bool EnablePrivilege(const wchar_t* name, DWORD& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token)) {
        error = GetLastError();
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, name, &luid)) {
        error = GetLastError();
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    const BOOL adjusted = AdjustTokenPrivileges(
        token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    error = GetLastError();
    CloseHandle(token);
    return adjusted != FALSE && error == ERROR_SUCCESS;
}

HKEY SelectHive(const RegistryChange& change, const ScopedLoadedHive& software,
                const ScopedLoadedHive& system, const ScopedLoadedHive& defaultUser) {
    switch (change.hive) {
    case OfflineHive::Software: return software.Get();
    case OfflineHive::System: return system.Get();
    case OfflineHive::DefaultUser: return defaultUser.Get();
    }
    return nullptr;
}

bool HasChangesFor(const std::vector<RegistryChange>& changes, OfflineHive hive) {
    for (const auto& change : changes) {
        if (change.hive == hive) return true;
    }
    return false;
}

OfflineSettingsResult LoadRequiredHive(const std::wstring& path, OfflineHive hive,
                                       const std::vector<RegistryChange>& changes,
                                       ScopedLoadedHive& key) {
    if (!HasChangesFor(changes, hive)) return {true, {}};
    const LONG status = key.Load(path, HiveName(hive));
    if (status != ERROR_SUCCESS) {
        return {false, L"无法加载离线 " + std::wstring(HiveName(hive)) +
                       L" 注册表，错误 " + std::to_wstring(status)};
    }
    return {true, {}};
}

OfflineSettingsResult WriteUtf16File(const std::wstring& path,
                                     const std::wstring& contents) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {false, L"无法创建文件 " + path + L"，系统错误 " +
                       std::to_wstring(GetLastError())};
    }
    std::wstring encoded;
    encoded.reserve(contents.size() + 1);
    encoded.push_back(static_cast<wchar_t>(0xFEFF));
    encoded += contents;
    const DWORD byteCount =
        static_cast<DWORD>(encoded.size() * sizeof(wchar_t));
    DWORD written = 0;
    const BOOL succeeded = WriteFile(
        file, encoded.data(), byteCount, &written, nullptr);
    const DWORD error = !succeeded ? GetLastError()
        : written != byteCount ? ERROR_WRITE_FAULT : ERROR_SUCCESS;
    CloseHandle(file);
    if (error != ERROR_SUCCESS) {
        return {false, L"无法写入文件 " + path + L"，系统错误 " +
                       std::to_wstring(error)};
    }
    return {true, {}};
}

OfflineSettingsResult WritePauseWindowsUpdateTask(const std::wstring& targetRoot) {
    const std::wstring setupDirectory = JoinPath(targetRoot, L"Windows\\Setup");
    const std::wstring scriptsDirectory = setupDirectory + L"\\Scripts";
    for (const auto& directory : {setupDirectory, scriptsDirectory}) {
        if (!CreateDirectoryW(directory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return {false, L"无法创建目录 " + directory + L"，系统错误 " +
                           std::to_wstring(GetLastError())};
        }
    }
    return WriteUtf16File(scriptsDirectory + L"\\PauseWindowsUpdate.xml",
                          BuildPauseWindowsUpdateTaskXml());
}

} // namespace

bool AdvancedSettings::AnyEnabled() const noexcept {
    return disableWindowsUpdate || showFileExtensions || showThisPcOnDesktop ||
           disableWidgets || disableFastStartup || disableUac ||
           preventBitLocker || excludeWindowsUpdateDrivers;
}

std::vector<RegistryChange> BuildOfflineRegistryChanges(
    const AdvancedSettings& settings, unsigned long currentControlSet) {
    std::vector<RegistryChange> changes;
    if (settings.showFileExtensions) {
        changes.push_back({OfflineHive::DefaultUser,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
            L"HideFileExt", 0});
    }
    if (settings.showThisPcOnDesktop) {
        constexpr wchar_t kThisPcClassId[] =
            L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
        changes.push_back({OfflineHive::DefaultUser,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel",
            kThisPcClassId, 0});
        changes.push_back({OfflineHive::DefaultUser,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu",
            kThisPcClassId, 0});
    }
    if (settings.disableWidgets) {
        changes.push_back({OfflineHive::Software,
            L"Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0});
        changes.push_back({OfflineHive::Software,
            L"Policies\\Microsoft\\Windows\\Windows Feeds", L"EnableFeeds", 0});
    }
    const std::wstring controlSet = std::wstring(L"ControlSet") +
        (currentControlSet < 10 ? L"00" : currentControlSet < 100 ? L"0" : L"") +
        std::to_wstring(currentControlSet);
    if (settings.disableFastStartup) {
        changes.push_back({OfflineHive::System,
            controlSet + L"\\Control\\Session Manager\\Power", L"HiberbootEnabled", 0});
    }
    if (settings.disableUac) {
        changes.push_back({OfflineHive::Software,
            L"Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", 0});
    }
    if (settings.preventBitLocker) {
        changes.push_back({OfflineHive::System,
            controlSet + L"\\Control\\BitLocker", L"PreventDeviceEncryption", 1});
    }
    if (settings.excludeWindowsUpdateDrivers) {
        changes.push_back({OfflineHive::Software,
            L"Policies\\Microsoft\\Windows\\WindowsUpdate",
            L"ExcludeWUDriversInQualityUpdate", 1});
    }
    return changes;
}

std::wstring BuildPauseWindowsUpdateTaskXml() {
    return
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <Triggers>\r\n"
        L"    <BootTrigger>\r\n"
        L"      <Repetition>\r\n"
        L"        <Interval>P1D</Interval>\r\n"
        L"        <StopAtDurationEnd>false</StopAtDurationEnd>\r\n"
        L"      </Repetition>\r\n"
        L"      <Enabled>true</Enabled>\r\n"
        L"    </BootTrigger>\r\n"
        L"  </Triggers>\r\n"
        L"  <Principals>\r\n"
        L"    <Principal id=\"Author\">\r\n"
        L"      <UserId>S-1-5-18</UserId>\r\n"
        L"      <RunLevel>HighestAvailable</RunLevel>\r\n"
        L"    </Principal>\r\n"
        L"  </Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <StartWhenAvailable>true</StartWhenAvailable>\r\n"
        L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
        L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
        L"    <Enabled>true</Enabled>\r\n"
        L"    <Hidden>false</Hidden>\r\n"
        L"    <ExecutionTimeLimit>PT1H</ExecutionTimeLimit>\r\n"
        L"    <Priority>7</Priority>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\">\r\n"
        L"    <Exec>\r\n"
        L"      <Command>C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe</Command>\r\n"
        L"      <Arguments>-WindowStyle Hidden -NoLogo -NoProfile -NonInteractive -Command \"$format='yyyy-MM-ddTHH\\:mm\\:ssK';$now=[datetime]::UtcNow;$start=$now.ToString($format);$end=$now.AddDays(7).ToString($format);$params=@{LiteralPath='Registry::HKLM\\Software\\Microsoft\\WindowsUpdate\\UX\\Settings';Type='String';Force=$true};'PauseFeatureUpdatesStartTime','PauseQualityUpdatesStartTime','PauseUpdatesStartTime'|ForEach-Object{Set-ItemProperty @params -Name $_ -Value $start};'PauseFeatureUpdatesEndTime','PauseQualityUpdatesEndTime','PauseUpdatesExpiryTime'|ForEach-Object{Set-ItemProperty @params -Name $_ -Value $end}\"</Arguments>\r\n"
        L"    </Exec>\r\n"
        L"  </Actions>\r\n"
        L"</Task>\r\n";
}

OfflineSettingsResult ApplyOfflineSettings(
    const std::wstring& targetRoot, const AdvancedSettings& settings) {
    if (!settings.AnyEnabled()) return {true, {}};

    DWORD privilegeError = ERROR_SUCCESS;
    if (!EnablePrivilege(SE_BACKUP_NAME, privilegeError)) {
        return {false, L"无法启用注册表备份权限，错误 " +
                       std::to_wstring(privilegeError)};
    }
    if (!EnablePrivilege(SE_RESTORE_NAME, privilegeError)) {
        return {false, L"无法启用注册表还原权限，错误 " +
                       std::to_wstring(privilegeError)};
    }

    ScopedLoadedHive system;
    DWORD currentControlSet = 1;
    if (settings.disableFastStartup || settings.preventBitLocker) {
        const LONG systemStatus = system.Load(
            JoinPath(targetRoot, L"Windows\\System32\\config\\SYSTEM"), L"SYSTEM");
        if (systemStatus != ERROR_SUCCESS) {
            return {false, L"无法加载离线 SYSTEM 注册表，错误 " +
                           std::to_wstring(systemStatus)};
        }

        DWORD size = sizeof(currentControlSet);
        const LONG selectStatus = RegGetValueW(system.Get(), L"Select", L"Current",
            RRF_RT_REG_DWORD, nullptr, &currentControlSet, &size);
        if (selectStatus != ERROR_SUCCESS || currentControlSet == 0) {
            return {false, L"无法读取离线 SYSTEM\\Select\\Current，错误 " +
                           std::to_wstring(selectStatus)};
        }
    }

    const auto changes = BuildOfflineRegistryChanges(settings, currentControlSet);
    ScopedLoadedHive software;
    ScopedLoadedHive defaultUser;
    auto result = LoadRequiredHive(
        JoinPath(targetRoot, L"Windows\\System32\\config\\SOFTWARE"),
        OfflineHive::Software, changes, software);
    if (!result.success) return result;
    result = LoadRequiredHive(JoinPath(targetRoot, L"Users\\Default\\NTUSER.DAT"),
        OfflineHive::DefaultUser, changes, defaultUser);
    if (!result.success) return result;

    for (const auto& change : changes) {
        HKEY hive = SelectHive(change, software, system, defaultUser);
        ScopedRegistryKey key;
        DWORD disposition = 0;
        const LONG createStatus = RegCreateKeyExW(hive, change.subkey.c_str(), 0,
            nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, key.Put(),
            &disposition);
        if (createStatus != ERROR_SUCCESS) {
            return {false, L"无法创建离线注册表项 " + change.subkey +
                           L"，错误 " + std::to_wstring(createStatus)};
        }
        const DWORD value = change.value;
        const LONG setStatus = RegSetValueExW(key.Get(), change.valueName.c_str(), 0,
            REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        if (setStatus != ERROR_SUCCESS) {
            return {false, L"无法写入离线注册表值 " + change.valueName +
                           L"，错误 " + std::to_wstring(setStatus)};
        }
    }

    if (software.Get()) RegFlushKey(software.Get());
    if (system.Get()) RegFlushKey(system.Get());
    if (defaultUser.Get()) RegFlushKey(defaultUser.Get());
    if (settings.disableWindowsUpdate) {
        const auto taskResult = WritePauseWindowsUpdateTask(targetRoot);
        if (!taskResult.success) return taskResult;
    }
    return {true, {}};
}

OfflineSettingsResult ApplyOfflineDeviceRegion(
    const std::wstring& targetRoot, std::uint32_t geoId) {
    if (geoId == 0) return {false, L"设备区域 GeoID 不能为零。"};

    DWORD privilegeError = ERROR_SUCCESS;
    if (!EnablePrivilege(SE_BACKUP_NAME, privilegeError) ||
        !EnablePrivilege(SE_RESTORE_NAME, privilegeError)) {
        return {false, L"无法启用离线注册表权限，错误 " +
                       std::to_wstring(privilegeError)};
    }

    ScopedLoadedHive software;
    const LONG loadStatus = software.Load(
        JoinPath(targetRoot, L"Windows\\System32\\config\\SOFTWARE"),
        L"SOFTWARE");
    if (loadStatus != ERROR_SUCCESS) {
        return {false, L"无法加载离线 SOFTWARE 注册表，错误 " +
                       std::to_wstring(loadStatus)};
    }

    ScopedRegistryKey key;
    DWORD disposition = 0;
    const LONG createStatus = RegCreateKeyExW(software.Get(),
        L"Microsoft\\Windows\\CurrentVersion\\Control Panel\\DeviceRegion",
        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, key.Put(),
        &disposition);
    if (createStatus != ERROR_SUCCESS) {
        return {false, L"无法创建离线设备区域注册表项，错误 " +
                       std::to_wstring(createStatus)};
    }

    const DWORD value = geoId;
    const LONG setStatus = RegSetValueExW(key.Get(), L"DeviceRegion", 0,
        REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    if (setStatus != ERROR_SUCCESS) {
        return {false, L"无法写入离线设备区域，错误 " +
                       std::to_wstring(setStatus)};
    }
    RegFlushKey(software.Get());
    return {true, {}};
}

} // namespace openrcv::config
