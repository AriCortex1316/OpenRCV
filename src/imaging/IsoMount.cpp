#include <windows.h>
#include <virtdisk.h>

#include "openrcv/imaging/IsoMount.h"

#include <string>

namespace openrcv::imaging {
namespace {

constexpr GUID kMicrosoftVirtualDiskVendor = {
    0xEC984AEC, 0xA0F9, 0x47E9, {0x90, 0x1F, 0x71, 0x41, 0x5A, 0x66, 0x34, 0x5B}
};

std::wstring FormatError(DWORD error) {
    wchar_t* message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = message ? message : L"未知错误";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result + L" (" + std::to_wstring(error) + L")";
}

std::wstring FindNewCdRoot(DWORD before) {
    for (int attempt = 0; attempt != 30; ++attempt) {
        const DWORD added = GetLogicalDrives() & ~before;
        for (int index = 0; index != 26; ++index) {
            if ((added & (1u << index)) == 0) continue;
            wchar_t root[] = L"A:\\";
            root[0] = static_cast<wchar_t>(L'A' + index);
            if (GetDriveTypeW(root) == DRIVE_CDROM) return root;
        }
        Sleep(100);
    }
    return {};
}

} // namespace

struct IsoMount::Impl {
    using DetachFn = DWORD(WINAPI*)(HANDLE, DETACH_VIRTUAL_DISK_FLAG, ULONG);

    HMODULE module{};
    HANDLE disk{INVALID_HANDLE_VALUE};
    DetachFn detach{};
    std::wstring root;
};

IsoMount::IsoMount() : impl_(std::make_unique<Impl>()) {}
IsoMount::~IsoMount() { Close(); }

void IsoMount::Close() noexcept {
    if (impl_->disk != INVALID_HANDLE_VALUE) {
        if (impl_->detach) impl_->detach(impl_->disk, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
        CloseHandle(impl_->disk);
    }
    if (impl_->module) FreeLibrary(impl_->module);
    *impl_ = {};
    impl_->disk = INVALID_HANDLE_VALUE;
}

const std::wstring& IsoMount::Root() const noexcept { return impl_->root; }

IsoMountResult IsoMount::MountReadOnly(const std::wstring& path) {
    Close();
    const DWORD before = GetLogicalDrives();
    impl_->module = LoadLibraryW(L"virtdisk.dll");
    if (!impl_->module) {
        return {false, L"当前 WinPE 没有可用的 virtdisk.dll：" + FormatError(GetLastError())};
    }

    using OpenFn = DWORD(WINAPI*)(PVIRTUAL_STORAGE_TYPE, PCWSTR, VIRTUAL_DISK_ACCESS_MASK,
                                  OPEN_VIRTUAL_DISK_FLAG, POPEN_VIRTUAL_DISK_PARAMETERS, PHANDLE);
    using AttachFn = DWORD(WINAPI*)(HANDLE, PSECURITY_DESCRIPTOR, ATTACH_VIRTUAL_DISK_FLAG,
                                    ULONG, PATTACH_VIRTUAL_DISK_PARAMETERS, LPOVERLAPPED);
    auto open = reinterpret_cast<OpenFn>(GetProcAddress(impl_->module, "OpenVirtualDisk"));
    auto attach = reinterpret_cast<AttachFn>(GetProcAddress(impl_->module, "AttachVirtualDisk"));
    impl_->detach = reinterpret_cast<Impl::DetachFn>(GetProcAddress(impl_->module, "DetachVirtualDisk"));
    if (!open || !attach || !impl_->detach) {
        Close();
        return {false, L"当前 virtdisk.dll 缺少 ISO 挂载接口。"};
    }

    VIRTUAL_STORAGE_TYPE storage{VIRTUAL_STORAGE_TYPE_DEVICE_ISO, kMicrosoftVirtualDiskVendor};
    OPEN_VIRTUAL_DISK_PARAMETERS openParameters{};
    openParameters.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    openParameters.Version1.RWDepth = 1;
    DWORD status = open(&storage, path.c_str(), VIRTUAL_DISK_ACCESS_ATTACH_RO,
                        OPEN_VIRTUAL_DISK_FLAG_NONE, &openParameters, &impl_->disk);
    if (status != ERROR_SUCCESS) {
        const std::wstring message = L"无法打开 ISO：" + FormatError(status);
        Close();
        return {false, message};
    }

    ATTACH_VIRTUAL_DISK_PARAMETERS attachParameters{};
    attachParameters.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
    status = attach(impl_->disk, nullptr, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY,
                    0, &attachParameters, nullptr);
    if (status != ERROR_SUCCESS) {
        const std::wstring message = L"无法挂载 ISO：" + FormatError(status);
        Close();
        return {false, message};
    }

    impl_->root = FindNewCdRoot(before);
    if (impl_->root.empty()) {
        Close();
        return {false, L"ISO 已挂载，但没有发现新光驱盘符。"};
    }
    return {true, L"ISO 已只读挂载到 " + impl_->root};
}

} // namespace openrcv::imaging
