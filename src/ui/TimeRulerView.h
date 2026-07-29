#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiTimeMap.h"
#include "core/TimelineLoop.h"
#include "core/TimelineViewport.h"

namespace dawhermes::ui {

class TimeRulerView final : public juce::Component {
public:
    TimeRulerView();

    void setViewportState(const core::TimelineViewportState& state);
    void setTimeSignatureMap(std::vector<core::MidiTimeSignatureEvent> timeSignatureMap);
    void setGridDenominator(int denominator);
    void setSnapEnabled(bool enabled);
    void setProjectEndBeat(double endBeat);
    void setLoopRange(
        std::optional<core::TimelineLoopRange> range);

    std::function<void(double)> onSeekRequested;
    std::function<void(std::optional<core::TimelineLoopRange>)>
        onLoopRangeChanged;
    std::function<void()> onClearLoopRequested;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class DragMode {
        none,
        create,
        resizeStart,
        resizeEnd,
        move
    };

    double beatAtX(int x) const noexcept;
    int xAtBeat(double beat) const noexcept;

    core::TimelineViewportState viewportState_;
    std::vector<core::MidiTimeSignatureEvent> timeSignatureMap_;
    std::optional<core::TimelineLoopRange> loopRange_;
    std::optional<core::TimelineLoopRange> dragStartRange_;
    int gridDenominator_ { 16 };
    double projectEndBeat_ { 0.0 };
    double dragAnchorBeat_ { 0.0 };
    bool snapEnabled_ { true };
    bool dragThresholdPassed_ { false };
    DragMode dragMode_ { DragMode::none };
};

}  // namespace dawhermes::ui
