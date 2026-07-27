#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TimelineGeometry.h"

namespace dawhermes::ui {

class PianoKeyboardView final : public juce::Component {
public:
    PianoKeyboardView();

    void setPitchViewportState(const core::PitchViewportState& state);

    void paint(juce::Graphics& g) override;

private:
    static bool isBlackKey(int midiNoteNumber);

    core::PitchViewportState pitchViewportState_;
};

}  // namespace dawhermes::ui
