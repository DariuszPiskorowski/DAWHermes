#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace dawhermes::hermes {

std::filesystem::path hermesCacheRoot();
std::filesystem::path createHermesJobDirectory(const std::string& operationName, std::string& error);
std::size_t clearHermesCache(std::string& error);
std::size_t cleanupStaleHermesCache(std::chrono::hours olderThan, std::string& error);

}  // namespace dawhermes::hermes
