#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiComparisonModel.h"

namespace dawhermes::ui {

class MidiComparisonLegend final : public juce::Component {
public:
    MidiComparisonLegend();

    void setComparisonEnabled(bool enabled);
    void setComparisonSummary(const core::MidiComparisonSummary& summary);
    void setTrackLabels(juce::String referenceTrackName, juce::String candidateTrackName);

    void paint(juce::Graphics& g) override;

    static juce::Colour colourForCategory(core::MidiComparisonCategory category);

private:
    bool comparisonEnabled_ { false };
    core::MidiComparisonSummary summary_;
    juce::String referenceTrackName_;
    juce::String candidateTrackName_;
};

}  // namespace dawhermes::ui
