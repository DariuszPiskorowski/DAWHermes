#include "ui/PianoKeyboardView.h"

#include <algorithm>
#include <cmath>

namespace dawhermes::ui {

PianoKeyboardView::PianoKeyboardView()
{
    setOpaque(true);
}

void PianoKeyboardView::setPitchViewportState(const core::PitchViewportState& state)
{
    pitchViewportState_ = core::sanitizePitchViewportState(state);
    repaint();
}

bool PianoKeyboardView::isBlackKey(int midiNoteNumber)
{
    const auto keyClass = ((midiNoteNumber % 12) + 12) % 12;
    switch (keyClass) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

void PianoKeyboardView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff1b1f25));

    if (bounds.getWidth() <= 1 || bounds.getHeight() <= 1) {
        return;
    }

    const auto viewport = core::sanitizePitchViewportState(pitchViewportState_);

    const auto highest = static_cast<int>(std::ceil(viewport.highestVisiblePitch));
    const auto lowest = static_cast<int>(std::floor(viewport.highestVisiblePitch - viewport.visiblePitchSpan));

    for (int pitch = highest; pitch >= lowest; --pitch) {
        const auto yTop = core::pitchToY(static_cast<double>(pitch + 1), bounds.getHeight(), viewport);
        const auto yBottom = core::pitchToY(static_cast<double>(pitch), bounds.getHeight(), viewport);

        const auto top = static_cast<int>(std::floor(std::min(yTop, yBottom)));
        const auto bottom = static_cast<int>(std::ceil(std::max(yTop, yBottom)));
        const auto height = std::max(1, bottom - top);

        const auto keyRect = juce::Rectangle<int>(bounds.getX(), top, bounds.getWidth(), height).getIntersection(bounds);
        if (keyRect.isEmpty()) {
            continue;
        }

        const auto black = isBlackKey(pitch);
        g.setColour(black ? juce::Colour(0xff2b313a) : juce::Colour(0xffe8ecf0));
        g.fillRect(keyRect);

        g.setColour(black ? juce::Colour(0xff5f6a79) : juce::Colour(0xff9aa6b5));
        g.drawLine(
            static_cast<float>(keyRect.getX()),
            static_cast<float>(keyRect.getY()),
            static_cast<float>(keyRect.getRight()),
            static_cast<float>(keyRect.getY()));

        if (height >= 10 && !black && pitch % 12 == 0) {
            const auto octave = (pitch / 12) - 1;
            g.setColour(juce::Colour(0xff2c3440));
            g.setFont(10.0f);
            g.drawText(
                "C" + juce::String(octave),
                keyRect.reduced(2, 0),
                juce::Justification::centredLeft,
                false);
        }
    }
}

}  // namespace dawhermes::ui
