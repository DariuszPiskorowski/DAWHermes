#pragma once

#include <cstdint>
#include <string>

namespace dawhermes::core {

enum class TrackType {
    audio,
    midi
};

struct Track {
    std::uint64_t id{};
    std::string name;
    TrackType type { TrackType::audio };
};

}  // namespace dawhermes::core
