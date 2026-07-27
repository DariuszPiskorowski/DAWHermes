#include "ui/TimeRulerView.h"

#include <algorithm>

namespace dawhermes::ui {

TimeRulerView::TimeRulerView()
{
    setOpaque(true);
}

void TimeRulerView::setViewportState(const core::TimelineViewportState& state)
{
    viewportState_ = core::sanitizeTimelineViewportState(state);
    repaint();
}

void TimeRulerView::setTimeSignatureMap(std::vector<core::MidiTimeSignatureEvent> timeSignatureMap)
{
    timeSignatureMap_ = core::sanitizeTimeSignatureMap(timeSignatureMap);
    repaint();
}

void TimeRulerView::setGridDenominator(int denominator)
{
    gridDenominator_ = std::max(1, denominator);
    repaint();
}

void TimeRulerView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff232932));

    if (bounds.getWidth() <= 1 || bounds.getHeight() <= 1) {
        return;
    }

    const auto viewport = core::sanitizeTimelineViewportState(viewportState_);
    const auto visibleStart = viewport.startBeat;
    const auto visibleEnd = viewport.startBeat + viewport.visibleBeats;

    const auto signatures = core::sanitizeTimeSignatureMap(timeSignatureMap_);
    const auto gridBeats = core::buildGridBeatPositions(visibleStart, visibleEnd, gridDenominator_);

    g.setColour(juce::Colour(0xff2f3a46));
    for (const auto beat : gridBeats) {
        const auto x = static_cast<float>(core::timelineBeatToX(beat, bounds.getWidth(), viewport));
        g.drawVerticalLine(static_cast<int>(std::round(x)), 0.0f, static_cast<float>(bounds.getBottom()));
    }

    const auto barStarts = core::buildBarStartBeats(visibleStart, visibleEnd, signatures, 2048);
    g.setColour(juce::Colour(0xff6d8198));

    for (const auto barBeat : barStarts) {
        const auto x = static_cast<float>(core::timelineBeatToX(barBeat, bounds.getWidth(), viewport));
        const auto xi = static_cast<int>(std::round(x));

        g.drawVerticalLine(xi, 0.0f, static_cast<float>(bounds.getBottom()));

        const auto barNumber = core::barNumberAt(barBeat, signatures);
        const auto label = juce::String(barNumber);
        g.setColour(juce::Colour(0xffd7e5f5));
        g.drawText(
            label,
            xi + 3,
            0,
            36,
            bounds.getHeight(),
            juce::Justification::centredLeft,
            false);
        g.setColour(juce::Colour(0xff6d8198));
    }

    g.setColour(juce::Colour(0xff39424d));
    g.drawRect(bounds, 1);
}

}  // namespace dawhermes::ui
