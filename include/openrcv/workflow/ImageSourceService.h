#pragma once

#include <string>
#include <vector>

#include "openrcv/imaging/IsoMount.h"
#include "openrcv/imaging/WindowsImage.h"

namespace openrcv::workflow {

struct ImageSourceResult {
    bool success{};
    std::wstring imageFile;
    std::vector<imaging::WindowsImage> images;
    std::vector<std::wstring> messages;
};

class ImageSourceService {
public:
    ImageSourceResult LoadIso(const std::wstring& isoPath);
    void Close() noexcept;

private:
    imaging::IsoMount mountedIso_;
};

} // namespace openrcv::workflow
