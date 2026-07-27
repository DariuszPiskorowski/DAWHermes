#include "hermes/HermesCache.h"

#include <cstdlib>
#include <random>
#include <system_error>

namespace dawhermes::hermes {

namespace {

std::string getEnvironmentValue(const char* variableName)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, variableName) != 0 || value == nullptr) {
        return {};
    }

    std::string output(value);
    std::free(value);
    return output;
#else
    const auto* value = std::getenv(variableName);
    return value == nullptr ? std::string {} : std::string(value);
#endif
}

std::filesystem::path normalizedPath(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }

    return absolute.lexically_normal();
}

bool isSafeChildPath(const std::filesystem::path& root, const std::filesystem::path& child)
{
    const auto normalizedRoot = normalizedPath(root);
    const auto normalizedChild = normalizedPath(child);
    auto rootIt = normalizedRoot.begin();
    auto childIt = normalizedChild.begin();

    for (; rootIt != normalizedRoot.end() && childIt != normalizedChild.end(); ++rootIt, ++childIt) {
        if (*rootIt != *childIt) {
            return false;
        }
    }

    return rootIt == normalizedRoot.end();
}

std::string makeJobDirectoryName(const std::string& operationName)
{
    static thread_local std::mt19937_64 rng(std::random_device {}());
    const auto randomValue = static_cast<unsigned long long>(rng());
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(millis) + "_" + operationName + "_" + std::to_string(randomValue);
}

}  // namespace

std::filesystem::path hermesCacheRoot()
{
    const auto localAppData = getEnvironmentValue("LOCALAPPDATA");
    if (!localAppData.empty()) {
        return normalizedPath(std::filesystem::path(localAppData) / "DAWHermes" / "cache" / "hermes");
    }

    std::error_code ec;
    const auto tempRoot = std::filesystem::temp_directory_path(ec);
    if (!ec) {
        return normalizedPath(tempRoot / "DAWHermes" / "cache" / "hermes");
    }

    return normalizedPath(std::filesystem::path(".") / "cache" / "hermes");
}

std::filesystem::path createHermesJobDirectory(const std::string& operationName, std::string& error)
{
    error.clear();

    std::error_code ec;
    const auto root = hermesCacheRoot();
    std::filesystem::create_directories(root, ec);
    if (ec) {
        error = "Unable to create Hermes cache root: " + root.string();
        return {};
    }

    auto jobDirectory = root / makeJobDirectoryName(operationName.empty() ? "job" : operationName);
    std::filesystem::create_directories(jobDirectory, ec);
    if (ec) {
        error = "Unable to create Hermes job directory: " + jobDirectory.string();
        return {};
    }

    return normalizedPath(jobDirectory);
}

std::size_t clearHermesCache(std::string& error)
{
    error.clear();

    std::error_code ec;
    const auto root = hermesCacheRoot();
    if (!std::filesystem::exists(root, ec) || ec) {
        return 0;
    }

    std::size_t removedCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            error = "Failed while iterating Hermes cache directory.";
            return removedCount;
        }

        if (!entry.is_directory()) {
            continue;
        }

        const auto candidate = entry.path();
        if (!isSafeChildPath(root, candidate)) {
            continue;
        }

        std::filesystem::remove_all(candidate, ec);
        if (!ec) {
            ++removedCount;
        }
    }

    return removedCount;
}

std::size_t cleanupStaleHermesCache(std::chrono::hours olderThan, std::string& error)
{
    error.clear();

    std::error_code ec;
    const auto root = hermesCacheRoot();
    if (!std::filesystem::exists(root, ec) || ec) {
        return 0;
    }

    const auto now = std::chrono::file_clock::now();
    std::size_t removedCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            error = "Failed while iterating Hermes cache for stale cleanup.";
            return removedCount;
        }

        if (!entry.is_directory()) {
            continue;
        }

        const auto successMarker = entry.path() / ".success";
        if (!std::filesystem::exists(successMarker, ec) || ec) {
            ec.clear();
            continue;
        }

        const auto writeTime = std::filesystem::last_write_time(successMarker, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const auto age = now - writeTime;
        if (age < olderThan) {
            continue;
        }

        if (!isSafeChildPath(root, entry.path())) {
            continue;
        }

        std::filesystem::remove_all(entry.path(), ec);
        if (!ec) {
            ++removedCount;
        }
    }

    return removedCount;
}

}  // namespace dawhermes::hermes
