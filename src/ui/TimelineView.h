#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiTimeMap.h"
#include "core/TimelineGeometry.h"
#include "core/TimelineViewport.h"
#include "core/Track.h"

namespace dawhermes::ui {

class TimelineView final : public juce::Component {
public:
    TimelineView();

    void setTracks(std::vector<core::Track> tracks);
    void setSelectedTrackIds(std::vector<std::uint64_t> selectedTrackIds);
    void setViewportState(const core::TimelineViewportState& state);
    void setGridDenominator(int denominator);
    void setTempoMap(std::vector<core::MidiTempoEvent> tempoMap);
    void setTrackRowHeight(int rowHeight);
    void setVerticalScrollPixels(int scrollPixels);

    void paint(juce::Graphics& g) override;

private:
    juce::AudioThumbnail* getOrCreateThumbnail(const std::string& path);
    bool isTrackSelected(std::uint64_t trackId) const;
    double beatsPerSecond() const;

    std::vector<core::Track> tracks_;
    std::vector<std::uint64_t> selectedTrackIds_;
    core::TimelineViewportState viewportState_;
    std::vector<core::MidiTempoEvent> tempoMap_;

    int gridDenominator_ { 16 };
    int rowHeight_ { 30 };
    int verticalScrollPixels_ { 0 };

    juce::AudioFormatManager audioFormatManager_;
    juce::AudioThumbnailCache thumbnailCache_ { 32 };
    std::unordered_map<std::string, std::unique_ptr<juce::AudioThumbnail>> thumbnailsByPath_;
};

}  // namespace dawhermes::ui
