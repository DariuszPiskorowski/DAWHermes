#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dawhermes::core {

// Project-model paths are stored as UTF-8. Convert only at the filesystem
// boundary so Windows paths never pass through the active narrow code page.
inline std::filesystem::path pathFromUtf8(std::string_view utf8Path)
{
    if (utf8Path.empty()) {
        return {};
    }
    const auto* begin = reinterpret_cast<const char8_t*>(utf8Path.data());
    return std::filesystem::path(
        std::u8string(begin, begin + utf8Path.size()));
}

inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string(encoded.begin(), encoded.end());
}

inline std::string absolutePathToUtf8(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return pathToUtf8(error ? path : absolute);
}

inline std::string filenameFromUtf8Path(std::string_view utf8Path)
{
    if (utf8Path.empty()) {
        return {};
    }
    return pathToUtf8(pathFromUtf8(utf8Path).filename());
}

}  // namespace dawhermes::core
