#pragma once

#include <optional>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiComparisonModel.h"
#include "core/MidiNoteEditing.h"
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
    void setSnapEnabled(bool enabled);

    void setPrimaryNotes(std::vector<core::MidiNote> notes);
    void setSelectedNoteIds(std::vector<std::uint64_t> selectedNoteIds);
    void setComparisonNotes(
        std::vector<core::MidiNote> candidateNotes,
        core::MidiComparisonResult comparisonResult,
        bool enabled);

    std::function<void(std::uint64_t noteId, bool additive)> onPrimaryNoteClicked;
    std::function<void(bool additive)> onEmptySpaceClicked;
    std::function<void(std::vector<std::uint64_t> noteIds, bool additive)> onMarqueeSelectionFinished;
    std::function<void(double beat, int pitch)> onCreateNoteRequested;
    std::function<void()> onDeleteRequested;
    std::function<void(std::vector<std::uint64_t> selectedNoteIds, double deltaBeats, int deltaSemitones)> onMoveNotesRequested;
    std::function<void(std::uint64_t anchorNoteId, std::vector<std::uint64_t> selectedNoteIds, double requestedAnchorEndBeat)> onResizeNotesRequested;
    std::function<void(int deltaSemitones, double deltaBeats)> onNudgeRequested;

    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
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
    std::optional<std::size_t> findRenderedNoteAt(juce::Point<float> point, bool primaryOnly) const;
    bool isRightEdgeHit(const RenderedNote& note, juce::Point<float> point) const;
    core::MidiNoteMarqueeSelectionRequest marqueeRequestFor(juce::Point<float> start, juce::Point<float> end) const;
    void beginNoteEditGesture(const RenderedNote& note, bool resize);
    void updateNoteEditGesture(juce::Point<float> currentPosition);
    void finishNoteEditGesture();
    void cancelNoteEditGesture();
    double beatForX(float x) const;
    double pitchForY(float y) const;

    core::TimelineViewportState viewportState_;
    core::PitchViewportState pitchViewportState_;
    std::vector<core::MidiTimeSignatureEvent> timeSignatureMap_;
    int gridDenominator_ { 16 };
    bool snapEnabled_ { true };

    std::vector<core::MidiNote> primaryNotes_;
    std::vector<std::uint64_t> selectedNoteIds_;
    std::vector<core::MidiNote> candidateNotes_;
    core::MidiComparisonResult comparisonResult_;
    bool comparisonEnabled_ { false };

    std::vector<RenderedNote> renderedNotes_;
    std::optional<std::size_t> hoveredNoteIndex_;
    juce::Point<int> hoverPosition_;
    std::optional<juce::Point<float>> marqueeStartPosition_;
    juce::Point<float> marqueeCurrentPosition_;
    bool marqueeActive_ { false };
    bool marqueeAdditive_ { false };

    enum class NoteEditGesture {
        none,
        pendingMove,
        pendingResize,
        moving,
        resizing
    };

    NoteEditGesture noteEditGesture_ { NoteEditGesture::none };
    juce::Point<float> noteEditStartPosition_;
    juce::Point<float> noteEditCurrentPosition_;
    std::uint64_t noteEditAnchorNoteId_ { 0 };
    std::vector<std::uint64_t> noteEditSelectedIds_;
    std::vector<core::MidiNote> noteEditOriginalNotes_;
    bool noteEditChanged_ { false };
    double noteEditRequestedDeltaBeats_ { 0.0 };
    int noteEditRequestedDeltaSemitones_ { 0 };
    double noteEditRequestedAnchorEndBeat_ { 0.0 };
};

}  // namespace dawhermes::ui
