#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/Track.h"

namespace dawhermes::core {

class MidiNoteSelectionState {
public:
    void clear()
    {
        activeTrackId_.reset();
        selectedNoteIds_.clear();
        primarySelectedNoteId_.reset();
    }

    void clearSelection()
    {
        selectedNoteIds_.clear();
        primarySelectedNoteId_.reset();
    }

    void setActiveTrack(std::optional<std::uint64_t> trackId)
    {
        if (activeTrackId_ == trackId) {
            return;
        }

        activeTrackId_ = trackId;
        clearSelection();
    }

    std::optional<std::uint64_t> activeTrackId() const { return activeTrackId_; }

    void setSelection(
        std::uint64_t trackId,
        std::vector<std::uint64_t> selectedNoteIds,
        std::optional<std::uint64_t> primarySelectedNoteId)
    {
        setActiveTrack(trackId);

        selectedNoteIds_.clear();
        for (const auto noteId : selectedNoteIds) {
            if (noteId == 0) {
                continue;
            }

            if (std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId) == selectedNoteIds_.end()) {
                selectedNoteIds_.push_back(noteId);
            }
        }

        if (primarySelectedNoteId.has_value() && isSelected(primarySelectedNoteId.value())) {
            primarySelectedNoteId_ = primarySelectedNoteId.value();
        } else if (!selectedNoteIds_.empty()) {
            primarySelectedNoteId_ = selectedNoteIds_.back();
        } else {
            primarySelectedNoteId_.reset();
        }
    }

    void selectSingle(std::uint64_t trackId, std::uint64_t noteId)
    {
        setActiveTrack(trackId);
        selectedNoteIds_.clear();
        if (noteId != 0) {
            selectedNoteIds_.push_back(noteId);
            primarySelectedNoteId_ = noteId;
            return;
        }

        primarySelectedNoteId_.reset();
    }

    void toggleNote(std::uint64_t trackId, std::uint64_t noteId)
    {
        if (noteId == 0) {
            return;
        }

        setActiveTrack(trackId);

        const auto existingIt = std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId);
        if (existingIt != selectedNoteIds_.end()) {
            selectedNoteIds_.erase(existingIt);
            if (primarySelectedNoteId_.has_value() && primarySelectedNoteId_.value() == noteId) {
                if (!selectedNoteIds_.empty()) {
                    primarySelectedNoteId_ = selectedNoteIds_.back();
                } else {
                    primarySelectedNoteId_.reset();
                }
            }
            return;
        }

        selectedNoteIds_.push_back(noteId);
        primarySelectedNoteId_ = noteId;
    }

    bool hasSelection() const { return !selectedNoteIds_.empty(); }

    bool isSelected(std::uint64_t noteId) const
    {
        return std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId) != selectedNoteIds_.end();
    }

    std::size_t selectionCount() const { return selectedNoteIds_.size(); }

    const std::vector<std::uint64_t>& selectedNoteIds() const { return selectedNoteIds_; }

    std::optional<std::uint64_t> primarySelectedNoteId() const
    {
        if (!primarySelectedNoteId_.has_value() || !isSelected(primarySelectedNoteId_.value())) {
            return std::nullopt;
        }

        return primarySelectedNoteId_;
    }

    void removeDeletedNotes(const std::vector<MidiNote>& notes)
    {
        if (selectedNoteIds_.empty()) {
            return;
        }

        std::vector<std::uint64_t> liveIds;
        liveIds.reserve(notes.size());
        for (const auto& note : notes) {
            if (note.id != 0) {
                liveIds.push_back(note.id);
            }
        }

        selectedNoteIds_.erase(
            std::remove_if(
                selectedNoteIds_.begin(),
                selectedNoteIds_.end(),
                [&liveIds](const std::uint64_t noteId) {
                    return std::find(liveIds.begin(), liveIds.end(), noteId) == liveIds.end();
                }),
            selectedNoteIds_.end());

        if (!primarySelectedNoteId_.has_value()) {
            return;
        }

        if (!isSelected(primarySelectedNoteId_.value())) {
            if (!selectedNoteIds_.empty()) {
                primarySelectedNoteId_ = selectedNoteIds_.back();
            } else {
                primarySelectedNoteId_.reset();
            }
        }
    }

private:
    std::optional<std::uint64_t> activeTrackId_;
    std::vector<std::uint64_t> selectedNoteIds_;
    std::optional<std::uint64_t> primarySelectedNoteId_;
};

}  // namespace dawhermes::core
