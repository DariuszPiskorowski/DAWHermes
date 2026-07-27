#pragma once

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiComparisonModel.h"
#include "core/MidiTimeMap.h"
#include "core/TimelineGeometry.h"
#include "core/TimelineViewport.h"
#include "core/Track.h"

namespace dawhermes::ui {

class PianoRollView final : public juce::Component {
public:
    PianoRollView();

    void setViewportState(const core::TimelineViewportState& state);
    void setPitchViewportState(const core::PitchViewportState& state);
    void setTimeSignatureMap(std::vector<core::MidiTimeSignatureEvent> timeSignatureMap);
    void setGridDenominator(int denominator);

    void setPrimaryNotes(std::vector<core::MidiNote> notes);
    void setComparisonNotes(
        std::vector<core::MidiNote> candidateNotes,
        core::MidiComparisonResult comparisonResult,
        bool enabled);

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    struct RenderedNote {
        juce::Rectangle<float> bounds;
        core::MidiNote note;
        bool isCandidate { false };
        core::MidiComparisonCategory category { core::MidiComparisonCategory::unchanged };
    };

    static juce::Colour colourForCategory(core::MidiComparisonCategory category);
    static juce::String categoryLabel(core::MidiComparisonCategory category);

    void rebuildRenderedNotes();

    core::TimelineViewportState viewportState_;
    core::PitchViewportState pitchViewportState_;
    std::vector<core::MidiTimeSignatureEvent> timeSignatureMap_;
    int gridDenominator_ { 16 };

    std::vector<core::MidiNote> primaryNotes_;
    std::vector<core::MidiNote> candidateNotes_;
    core::MidiComparisonResult comparisonResult_;
    bool comparisonEnabled_ { false };

    std::vector<RenderedNote> renderedNotes_;
    std::optional<std::size_t> hoveredNoteIndex_;
    juce::Point<int> hoverPosition_;
};

}  // namespace dawhermes::ui
