#include "ui/TimeRulerView.h"

#include <algorithm>
#include <cmath>

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

void TimeRulerView::setSnapEnabled(bool enabled)
{
    snapEnabled_ = enabled;
}

void TimeRulerView::setProjectEndBeat(double endBeat)
{
    projectEndBeat_ = std::isfinite(endBeat)
        ? std::max(0.0, endBeat)
        : 0.0;
}

void TimeRulerView::setLoopRange(
    std::optional<core::TimelineLoopRange> range)
{
    loopRange_ = range;
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

    if (loopRange_.has_value() && loopRange_->isValid()) {
        const auto x1 = xAtBeat(loopRange_->startBeat);
        const auto x2 = xAtBeat(loopRange_->endBeat);
        g.setColour(juce::Colour(0x385cc8ff));
        g.fillRect(
            std::min(x1, x2),
            0,
            std::abs(x2 - x1),
            bounds.getHeight());
    }

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

    if (loopRange_.has_value() && loopRange_->isValid()) {
        const auto x1 = xAtBeat(loopRange_->startBeat);
        const auto x2 = xAtBeat(loopRange_->endBeat);
        g.setColour(juce::Colour(0xff79c9ff));
        g.drawVerticalLine(x1, 0.0f, static_cast<float>(bounds.getBottom()));
        g.drawVerticalLine(x2, 0.0f, static_cast<float>(bounds.getBottom()));
        g.fillRect(x1 - 3, 0, 7, 6);
        g.fillRect(x2 - 3, 0, 7, 6);
    }

    g.setColour(juce::Colour(0xff39424d));
    g.drawRect(bounds, 1);
}

double TimeRulerView::beatAtX(int x) const noexcept
{
    return core::timelineXToBeat(
        static_cast<double>(x),
        std::max(1, getWidth()),
        viewportState_);
}

int TimeRulerView::xAtBeat(double beat) const noexcept
{
    return static_cast<int>(std::round(core::timelineBeatToX(
        beat,
        std::max(1, getWidth()),
        viewportState_)));
}

void TimeRulerView::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(
            1,
            "Clear Loop Range",
            loopRange_.has_value());
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
            [safe = juce::Component::SafePointer<TimeRulerView>(this)](
                int result) {
                if (safe != nullptr
                    && result == 1
                    && safe->onClearLoopRequested) {
                    safe->onClearLoopRequested();
                }
            });
        return;
    }
    if (!event.mods.isLeftButtonDown()) {
        return;
    }

    dragAnchorBeat_ = beatAtX(event.x);
    dragStartRange_ = loopRange_;
    dragThresholdPassed_ = false;
    dragMode_ = DragMode::create;
    if (loopRange_.has_value()) {
        constexpr int edgeTolerance = 7;
        if (std::abs(event.x - xAtBeat(loopRange_->startBeat))
            <= edgeTolerance) {
            dragMode_ = DragMode::resizeStart;
        } else if (std::abs(
                       event.x - xAtBeat(loopRange_->endBeat))
                   <= edgeTolerance) {
            dragMode_ = DragMode::resizeEnd;
        } else if (core::timelineLoopContains(
                       loopRange_.value(),
                       dragAnchorBeat_)) {
            dragMode_ = DragMode::move;
        }
    }
}

void TimeRulerView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::none) {
        return;
    }
    if (!dragThresholdPassed_
        && event.getDistanceFromDragStart() < 3) {
        return;
    }
    dragThresholdPassed_ = true;
    const auto currentBeat = beatAtX(event.x);
    std::optional<core::TimelineLoopRange> updated;
    if (dragMode_ == DragMode::create) {
        updated = core::createTimelineLoopRange(
            dragAnchorBeat_,
            currentBeat,
            projectEndBeat_,
            snapEnabled_,
            gridDenominator_);
    } else if (dragStartRange_.has_value()
               && dragMode_ == DragMode::move) {
        updated = core::moveTimelineLoopRange(
            dragStartRange_.value(),
            currentBeat - dragAnchorBeat_,
            projectEndBeat_,
            snapEnabled_,
            gridDenominator_);
    } else if (dragStartRange_.has_value()) {
        updated = core::resizeTimelineLoopRange(
            dragStartRange_.value(),
            dragMode_ == DragMode::resizeStart
                ? core::TimelineLoopEdge::start
                : core::TimelineLoopEdge::end,
            currentBeat,
            projectEndBeat_,
            snapEnabled_,
            gridDenominator_);
    }
    if (updated.has_value()) {
        loopRange_ = updated;
        repaint();
        if (onLoopRangeChanged) {
            onLoopRangeChanged(updated);
        }
    }
}

void TimeRulerView::mouseUp(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::none) {
        return;
    }
    if (!dragThresholdPassed_ && onSeekRequested) {
        onSeekRequested(std::clamp(
            beatAtX(event.x),
            0.0,
            projectEndBeat_));
    }
    dragMode_ = DragMode::none;
    dragStartRange_.reset();
}

}  // namespace dawhermes::ui
