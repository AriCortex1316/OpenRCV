#pragma once

#include <memory>
#include <string>

namespace openrcv::imaging {

struct IsoMountResult {
    bool success{};
    std::wstring message;
};

class IsoMount {
public:
    IsoMount();
    ~IsoMount();

    IsoMount(const IsoMount&) = delete;
    IsoMount& operator=(const IsoMount&) = delete;

    IsoMountResult MountReadOnly(const std::wstring& path);
    void Close() noexcept;
    const std::wstring& Root() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openrcv::imaging
