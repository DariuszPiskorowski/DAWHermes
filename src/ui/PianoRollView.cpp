#include "ui/PianoRollView.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dawhermes::ui {

namespace {

juce::String noteNameForPitch(int pitch)
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const auto clamped = std::clamp(pitch, 0, 127);
    const auto pitchClass = clamped % 12;
    const auto octave = (clamped / 12) - 1;
    return juce::String(names[static_cast<std::size_t>(pitchClass)]) + juce::String(octave);
}

}  // namespace

PianoRollView::PianoRollView()
{
    setOpaque(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PianoRollView::setViewportState(const core::TimelineViewportState& state)
{
    viewportState_ = core::sanitizeTimelineViewportState(state);
    repaint();
}

void PianoRollView::setPitchViewportState(const core::PitchViewportState& state)
{
    pitchViewportState_ = core::sanitizePitchViewportState(state);
    repaint();
}

void PianoRollView::setTimeSignatureMap(std::vector<core::MidiTimeSignatureEvent> timeSignatureMap)
{
    timeSignatureMap_ = core::sanitizeTimeSignatureMap(timeSignatureMap);
    repaint();
}

void PianoRollView::setGridDenominator(int denominator)
{
    gridDenominator_ = std::max(1, denominator);
    repaint();
}

void PianoRollView::setPrimaryNotes(std::vector<core::MidiNote> notes)
{
    primaryNotes_ = std::move(notes);
    repaint();
}

void PianoRollView::setComparisonNotes(
    std::vector<core::MidiNote> candidateNotes,
    core::MidiComparisonResult comparisonResult,
    bool enabled)
{
    candidateNotes_ = std::move(candidateNotes);
    comparisonResult_ = std::move(comparisonResult);
    comparisonEnabled_ = enabled;
    repaint();
}

juce::Colour PianoRollView::colourForCategory(core::MidiComparisonCategory category)
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

juce::String PianoRollView::categoryLabel(core::MidiComparisonCategory category)
{
    switch (category) {
    case core::MidiComparisonCategory::unchanged:
        return "Unchanged";
    case core::MidiComparisonCategory::timingAdjusted:
        return "Timing adjusted";
    case core::MidiComparisonCategory::velocityAdjusted:
        return "Velocity adjusted";
    case core::MidiComparisonCategory::pitchChanged:
        return "Pitch changed";
    case core::MidiComparisonCategory::added:
        return "Added";
    case core::MidiComparisonCategory::removed:
        return "Removed";
    default:
        return "Unknown";
    }
}

void PianoRollView::rebuildRenderedNotes()
{
    renderedNotes_.clear();

    const auto bounds = getLocalBounds();
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0) {
        return;
    }

    const auto viewport = core::sanitizeTimelineViewportState(viewportState_);
    const auto pitchViewport = core::sanitizePitchViewportState(pitchViewportState_);

    const auto primaryGeometry = core::computeVisibleNoteGeometry(
        primaryNotes_,
        bounds.getWidth(),
        bounds.getHeight(),
        viewport,
        pitchViewport,
        20000);

    for (const auto& geom : primaryGeometry) {
        const auto& note = primaryNotes_[geom.noteIndex];
        RenderedNote rendered;
        rendered.bounds = juce::Rectangle<float>(
            static_cast<float>(geom.x),
            static_cast<float>(geom.y),
            static_cast<float>(geom.width),
            static_cast<float>(geom.height));
        rendered.note = note;
        rendered.isCandidate = false;
        rendered.category = core::MidiComparisonCategory::unchanged;
        renderedNotes_.push_back(rendered);
    }

    if (!comparisonEnabled_) {
        return;
    }

    std::vector<core::MidiComparisonCategory> candidateCategories(
        candidateNotes_.size(),
        core::MidiComparisonCategory::added);

    for (const auto& match : comparisonResult_.matches) {
        if (match.targetIndex < candidateCategories.size()) {
            candidateCategories[match.targetIndex] = match.category;
        }
    }

    const auto candidateGeometry = core::computeVisibleNoteGeometry(
        candidateNotes_,
        bounds.getWidth(),
        bounds.getHeight(),
        viewport,
        pitchViewport,
        20000);

    for (const auto& geom : candidateGeometry) {
        if (geom.noteIndex >= candidateNotes_.size()) {
            continue;
        }

        RenderedNote rendered;
        rendered.bounds = juce::Rectangle<float>(
            static_cast<float>(geom.x),
            static_cast<float>(geom.y),
            static_cast<float>(geom.width),
            static_cast<float>(geom.height));
        rendered.note = candidateNotes_[geom.noteIndex];
        rendered.isCandidate = true;
        rendered.category = candidateCategories[geom.noteIndex];
        renderedNotes_.push_back(rendered);
    }

    for (const auto sourceIndex : comparisonResult_.sourceOnlyIndices) {
        if (sourceIndex >= primaryNotes_.size()) {
            continue;
        }

        const auto geometry = core::computeVisibleNoteGeometry(
            std::vector<core::MidiNote> { primaryNotes_[sourceIndex] },
            bounds.getWidth(),
            bounds.getHeight(),
            viewport,
            pitchViewport,
            1);

        if (geometry.empty()) {
            continue;
        }

        RenderedNote rendered;
        rendered.bounds = juce::Rectangle<float>(
            static_cast<float>(geometry.front().x),
            static_cast<float>(geometry.front().y),
            static_cast<float>(geometry.front().width),
            static_cast<float>(geometry.front().height));
        rendered.note = primaryNotes_[sourceIndex];
        rendered.isCandidate = false;
        rendered.category = core::MidiComparisonCategory::removed;
        renderedNotes_.push_back(rendered);
    }
}

void PianoRollView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff171c23));

    if (bounds.getWidth() <= 1 || bounds.getHeight() <= 1) {
        return;
    }

    const auto viewport = core::sanitizeTimelineViewportState(viewportState_);
    const auto pitchViewport = core::sanitizePitchViewportState(pitchViewportState_);

    const auto visibleStart = viewport.startBeat;
    const auto visibleEnd = viewport.startBeat + viewport.visibleBeats;

    for (int pitch = 0; pitch <= 127; ++pitch) {
        const auto y = static_cast<int>(std::round(core::pitchToY(static_cast<double>(pitch), bounds.getHeight(), pitchViewport)));
        if (y < 0 || y >= bounds.getBottom()) {
            continue;
        }

        const auto black = ((pitch % 12) == 1) || ((pitch % 12) == 3)
            || ((pitch % 12) == 6) || ((pitch % 12) == 8) || ((pitch % 12) == 10);

        g.setColour(black ? juce::Colour(0xff1d232c) : juce::Colour(0xff1a2028));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getRight()));
    }

    const auto gridBeats = core::buildGridBeatPositions(visibleStart, visibleEnd, gridDenominator_);
    g.setColour(juce::Colour(0xff2a3340));
    for (const auto beat : gridBeats) {
        const auto x = static_cast<int>(std::round(core::timelineBeatToX(beat, bounds.getWidth(), viewport)));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getBottom()));
    }

    const auto signatures = core::sanitizeTimeSignatureMap(timeSignatureMap_);
    const auto bars = core::buildBarStartBeats(visibleStart, visibleEnd, signatures, 4096);
    g.setColour(juce::Colour(0xff4a5a6f));
    for (const auto beat : bars) {
        const auto x = static_cast<int>(std::round(core::timelineBeatToX(beat, bounds.getWidth(), viewport)));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getBottom()));
    }

    rebuildRenderedNotes();

    for (const auto& note : renderedNotes_) {
        if (!note.isCandidate && note.category != core::MidiComparisonCategory::removed) {
            const auto colour = comparisonEnabled_ ? juce::Colour(0xff3f5f86) : juce::Colour(0xff6ba4f6);
            g.setColour(colour);
            g.fillRect(note.bounds);
            continue;
        }

        if (note.isCandidate) {
            g.setColour(colourForCategory(note.category));
            g.fillRect(note.bounds);
            continue;
        }

        if (note.category == core::MidiComparisonCategory::removed) {
            g.setColour(colourForCategory(core::MidiComparisonCategory::removed));
            g.drawRect(note.bounds, 1.2f);
        }
    }

    if (hoveredNoteIndex_.has_value() && hoveredNoteIndex_.value() < renderedNotes_.size()) {
        const auto& hovered = renderedNotes_[hoveredNoteIndex_.value()];
        g.setColour(juce::Colour(0xfffff2c0));
        g.drawRect(hovered.bounds.expanded(1.5f), 1.5f);

        juce::String tooltip;
        tooltip << (hovered.isCandidate ? "Candidate" : "Reference")
                << " | " << noteNameForPitch(hovered.note.pitch)
                << " | vel " << hovered.note.velocity
                << " | start " << juce::String(hovered.note.startBeat, 3)
                << " | dur " << juce::String(hovered.note.durationBeats, 3)
                << " | ch " << hovered.note.channel;

        if (comparisonEnabled_) {
            tooltip << " | " << categoryLabel(hovered.category);
        }

        g.setFont(12.0f);
        const auto estimatedTextWidth = (tooltip.length() * 7) + 14;
        const auto textWidth = std::max(240, static_cast<int>(estimatedTextWidth));
        const auto textHeight = 20;

        auto tooltipRect = juce::Rectangle<int>(
            hoverPosition_.x + 12,
            hoverPosition_.y - textHeight - 8,
            textWidth,
            textHeight);
        tooltipRect = tooltipRect.constrainedWithin(bounds.reduced(2));

        g.setColour(juce::Colour(0xee0f1319));
        g.fillRoundedRectangle(tooltipRect.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xfff0f6ff));
        g.drawRoundedRectangle(tooltipRect.toFloat(), 4.0f, 1.0f);
        g.drawText(tooltip, tooltipRect.reduced(6, 0), juce::Justification::centredLeft, false);
    }

    g.setColour(juce::Colour(0xff313b47));
    g.drawRect(bounds, 1);
}

void PianoRollView::mouseMove(const juce::MouseEvent& event)
{
    hoverPosition_ = event.getPosition();
    hoveredNoteIndex_.reset();

    const auto point = event.position;
    for (std::size_t index = renderedNotes_.size(); index-- > 0;) {
        if (renderedNotes_[index].bounds.contains(point)) {
            hoveredNoteIndex_ = index;
            break;
        }
    }

    repaint();
}

void PianoRollView::mouseExit(const juce::MouseEvent&)
{
    hoveredNoteIndex_.reset();
    repaint();
}

}  // namespace dawhermes::ui
