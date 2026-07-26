#pragma once

#include <cstdint>
#include <optional>

namespace dawhermes::core {

class SelectionState {
public:
    void selectTrack(std::uint64_t trackId) { selectedTrackId_ = trackId; }
    void clear() { selectedTrackId_.reset(); }

    bool hasSelection() const { return selectedTrackId_.has_value(); }
    bool isSelected(std::uint64_t trackId) const
    {
        return selectedTrackId_.has_value() && selectedTrackId_.value() == trackId;
    }

    std::optional<std::uint64_t> selectedTrackId() const { return selectedTrackId_; }

private:
    std::optional<std::uint64_t> selectedTrackId_;
};

}  // namespace dawhermes::core
