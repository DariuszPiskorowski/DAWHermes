#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace dawhermes::core {

class SelectionState {
public:
    void selectTrack(std::uint64_t trackId)
    {
        selectedTrackIds_.clear();
        selectedTrackIds_.push_back(trackId);
        primarySelectedTrackId_ = trackId;
    }

    void toggleTrack(std::uint64_t trackId)
    {
        const auto it = std::find(selectedTrackIds_.begin(), selectedTrackIds_.end(), trackId);
        if (it != selectedTrackIds_.end()) {
            selectedTrackIds_.erase(it);
            if (primarySelectedTrackId_.has_value() && primarySelectedTrackId_.value() == trackId) {
                if (!selectedTrackIds_.empty()) {
                    primarySelectedTrackId_ = selectedTrackIds_.back();
                } else {
                    primarySelectedTrackId_.reset();
                }
            }

            return;
        }

        selectedTrackIds_.push_back(trackId);
        primarySelectedTrackId_ = trackId;
    }

    void deselectTrack(std::uint64_t trackId)
    {
        const auto it = std::find(selectedTrackIds_.begin(), selectedTrackIds_.end(), trackId);
        if (it == selectedTrackIds_.end()) {
            return;
        }

        selectedTrackIds_.erase(it);
        if (primarySelectedTrackId_.has_value() && primarySelectedTrackId_.value() == trackId) {
            if (!selectedTrackIds_.empty()) {
                primarySelectedTrackId_ = selectedTrackIds_.back();
            } else {
                primarySelectedTrackId_.reset();
            }
        }
    }

    void clear()
    {
        selectedTrackIds_.clear();
        primarySelectedTrackId_.reset();
    }

    bool hasSelection() const { return !selectedTrackIds_.empty(); }

    std::size_t selectionCount() const { return selectedTrackIds_.size(); }

    bool isSelected(std::uint64_t trackId) const
    {
        return std::find(selectedTrackIds_.begin(), selectedTrackIds_.end(), trackId) != selectedTrackIds_.end();
    }

    std::optional<std::uint64_t> selectedTrackId() const { return primarySelectedTrackId_; }

    const std::vector<std::uint64_t>& selectedTrackIds() const { return selectedTrackIds_; }

private:
    std::vector<std::uint64_t> selectedTrackIds_;
    std::optional<std::uint64_t> primarySelectedTrackId_;
};

}  // namespace dawhermes::core
