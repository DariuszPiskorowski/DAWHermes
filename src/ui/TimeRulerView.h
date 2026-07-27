#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiTimeMap.h"
#include "core/TimelineViewport.h"

namespace dawhermes::ui {

class TimeRulerView final : public juce::Component {
public:
    TimeRulerView();

    void setViewportState(const core::TimelineViewportState& state);
    void setTimeSignatureMap(std::vector<core::MidiTimeSignatureEvent> timeSignatureMap);
    void setGridDenominator(int denominator);

    void paint(juce::Graphics& g) override;

private:
    core::TimelineViewportState viewportState_;
    std::vector<core::MidiTimeSignatureEvent> timeSignatureMap_;
    int gridDenominator_ { 16 };
};

}  // namespace dawhermes::ui
