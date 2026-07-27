#include "ui/MidiComparisonLegend.h"

#include <array>

namespace dawhermes::ui {

MidiComparisonLegend::MidiComparisonLegend()
{
    setOpaque(true);
}

void MidiComparisonLegend::setComparisonEnabled(bool enabled)
{
    comparisonEnabled_ = enabled;
    repaint();
}

void MidiComparisonLegend::setComparisonSummary(const core::MidiComparisonSummary& summary)
{
    summary_ = summary;
    repaint();
}

void MidiComparisonLegend::setTrackLabels(juce::String referenceTrackName, juce::String candidateTrackName)
{
    referenceTrackName_ = std::move(referenceTrackName);
    candidateTrackName_ = std::move(candidateTrackName);
    repaint();
}

juce::Colour MidiComparisonLegend::colourForCategory(core::MidiComparisonCategory category)
{
    switch (category) {
    case core::MidiComparisonCategory::unchanged:
        return juce::Colour(0xff56c271);
    case core::MidiComparisonCategory::timingAdjusted:
        return juce::Colour(0xfff1cc55);
    case core::MidiComparisonCategory::velocityAdjusted:
        return juce::Colour(0xff67d1e8);
    case core::MidiComparisonCategory::pitchChanged:
        return juce::Colour(0xfff2836f);
    case core::MidiComparisonCategory::added:
        return juce::Colour(0xffa883f2);
    case core::MidiComparisonCategory::removed:
        return juce::Colour(0xffdf8f53);
    default:
        return juce::Colours::grey;
    }
}

void MidiComparisonLegend::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff1f252d));

    if (!comparisonEnabled_) {
        g.setColour(juce::Colour(0xffa9b3bf));
        g.drawText(
            "MIDI comparison: off (select two MIDI tracks and enable in View menu)",
            bounds.reduced(8, 0),
            juce::Justification::centredLeft,
            true);
        return;
    }

    g.setColour(juce::Colour(0xffd4dbe4));
    const auto header = "Compare: "
        + (referenceTrackName_.isEmpty() ? juce::String("(reference)") : referenceTrackName_)
        + " vs "
        + (candidateTrackName_.isEmpty() ? juce::String("(candidate)") : candidateTrackName_);

    g.drawText(header, bounds.removeFromTop(16).reduced(8, 0), juce::Justification::centredLeft, true);

    struct LegendEntry {
        core::MidiComparisonCategory category;
        juce::String label;
        std::size_t count;
    };

    const std::array<LegendEntry, 6> entries {
        LegendEntry { core::MidiComparisonCategory::unchanged, "Unchanged", summary_.unchangedCount },
        LegendEntry { core::MidiComparisonCategory::timingAdjusted, "Timing", summary_.timingAdjustedCount },
        LegendEntry { core::MidiComparisonCategory::velocityAdjusted, "Velocity", summary_.velocityAdjustedCount },
        LegendEntry { core::MidiComparisonCategory::pitchChanged, "Pitch", summary_.pitchChangedCount },
        LegendEntry { core::MidiComparisonCategory::added, "Added", summary_.addedCount },
        LegendEntry { core::MidiComparisonCategory::removed, "Removed", summary_.removedCount },
    };

    auto row = bounds.reduced(8, 2);
    const auto itemWidth = std::max(90, row.getWidth() / static_cast<int>(entries.size()));

    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto item = row.removeFromLeft(itemWidth);
        const auto& entry = entries[index];

        g.setColour(colourForCategory(entry.category));
        g.fillRect(item.getX(), item.getY() + 4, 10, 10);

        g.setColour(juce::Colour(0xffd4dbe4));
        g.drawText(
            entry.label + ": " + juce::String(static_cast<int>(entry.count)),
            item.withTrimmedLeft(14),
            juce::Justification::centredLeft,
            false);
    }
}

}  // namespace dawhermes::ui
