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
    setWantsKeyboardFocus(true);
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

void PianoRollView::setSnapEnabled(bool enabled)
{
    snapEnabled_ = enabled;
}

void PianoRollView::setPrimaryNotes(std::vector<core::MidiNote> notes)
{
    if (noteEditGesture_ == NoteEditGesture::moving || noteEditGesture_ == NoteEditGesture::resizing) {
        return;
    }

    primaryNotes_ = std::move(notes);
    repaint();
}

void PianoRollView::setSelectedNoteIds(std::vector<std::uint64_t> selectedNoteIds)
{
    selectedNoteIds_ = std::move(selectedNoteIds);
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

std::optional<std::size_t> PianoRollView::findRenderedNoteAt(juce::Point<float> point, bool primaryOnly) const
{
    for (std::size_t index = renderedNotes_.size(); index-- > 0;) {
        const auto& rendered = renderedNotes_[index];
        if (primaryOnly && (rendered.isCandidate || rendered.category == core::MidiComparisonCategory::removed)) {
            continue;
        }

        if (rendered.bounds.contains(point)) {
            return index;
        }
    }

    return std::nullopt;
}

bool PianoRollView::isRightEdgeHit(const RenderedNote& note, juce::Point<float> point) const
{
    return !note.isCandidate
        && note.category != core::MidiComparisonCategory::removed
        && note.bounds.contains(point)
        && core::isMidiNoteRightEdgeHit(note.bounds.getX(), note.bounds.getWidth(), point.x);
}

double PianoRollView::beatForX(float x) const
{
    return core::timelineXToBeat(
        static_cast<double>(x),
        getLocalBounds().getWidth(),
        core::sanitizeTimelineViewportState(viewportState_));
}

double PianoRollView::pitchForY(float y) const
{
    return core::yToPitch(
        static_cast<double>(y),
        getLocalBounds().getHeight(),
        core::sanitizePitchViewportState(pitchViewportState_));
}

void PianoRollView::beginNoteEditGesture(const RenderedNote& note, bool resize)
{
    const auto clickedSelected = note.note.id != 0
        && std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), note.note.id) != selectedNoteIds_.end();

    if (!clickedSelected) {
        selectedNoteIds_.clear();
        selectedNoteIds_.push_back(note.note.id);
        if (onPrimaryNoteClicked != nullptr) {
            onPrimaryNoteClicked(note.note.id, false);
        }
    }

    noteEditSelectedIds_ = selectedNoteIds_;
    if (noteEditSelectedIds_.empty() && note.note.id != 0) {
        noteEditSelectedIds_.push_back(note.note.id);
    }

    noteEditOriginalNotes_ = primaryNotes_;
    noteEditAnchorNoteId_ = note.note.id;
    noteEditChanged_ = false;
    noteEditRequestedDeltaBeats_ = 0.0;
    noteEditRequestedDeltaSemitones_ = 0;
    noteEditRequestedAnchorEndBeat_ = note.note.startBeat + note.note.durationBeats;
    noteEditGesture_ = resize ? NoteEditGesture::resizing : NoteEditGesture::moving;
    setMouseCursor(resize ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void PianoRollView::updateNoteEditGesture(juce::Point<float> currentPosition)
{
    if (noteEditGesture_ != NoteEditGesture::moving && noteEditGesture_ != NoteEditGesture::resizing) {
        return;
    }

    noteEditCurrentPosition_ = currentPosition;
    primaryNotes_ = noteEditOriginalNotes_;

    if (noteEditGesture_ == NoteEditGesture::moving) {
        noteEditRequestedDeltaBeats_ = beatForX(noteEditCurrentPosition_.x) - beatForX(noteEditStartPosition_.x);
        noteEditRequestedDeltaSemitones_ = static_cast<int>(std::round(
            pitchForY(noteEditCurrentPosition_.y) - pitchForY(noteEditStartPosition_.y)));

        core::MoveSelectedNotesRequest request;
        request.selectedNoteIds = noteEditSelectedIds_;
        request.requestedDeltaBeats = noteEditRequestedDeltaBeats_;
        request.requestedDeltaSemitones = noteEditRequestedDeltaSemitones_;
        request.snapEnabled = snapEnabled_;
        request.gridStepBeats = core::gridStepBeats(gridDenominator_);

        const auto result = core::moveSelectedNotes(primaryNotes_, request);
        noteEditChanged_ = result.changed;
    } else {
        noteEditRequestedAnchorEndBeat_ = beatForX(noteEditCurrentPosition_.x);

        core::ResizeSelectedNotesRequest request;
        request.selectedNoteIds = noteEditSelectedIds_;
        request.anchorNoteId = noteEditAnchorNoteId_;
        request.requestedAnchorEndBeat = noteEditRequestedAnchorEndBeat_;
        request.snapEnabled = snapEnabled_;
        request.gridStepBeats = core::gridStepBeats(gridDenominator_);

        const auto result = core::resizeSelectedNotes(primaryNotes_, request);
        noteEditChanged_ = result.changed;
    }

    repaint();
}

void PianoRollView::finishNoteEditGesture()
{
    if (noteEditGesture_ != NoteEditGesture::moving && noteEditGesture_ != NoteEditGesture::resizing) {
        noteEditGesture_ = NoteEditGesture::none;
        return;
    }

    const auto gesture = noteEditGesture_;
    const auto changed = noteEditChanged_;
    const auto selectedIds = noteEditSelectedIds_;
    const auto deltaBeats = noteEditRequestedDeltaBeats_;
    const auto deltaSemitones = noteEditRequestedDeltaSemitones_;
    const auto anchorId = noteEditAnchorNoteId_;
    const auto anchorEndBeat = noteEditRequestedAnchorEndBeat_;
    const auto originalNotes = noteEditOriginalNotes_;

    noteEditGesture_ = NoteEditGesture::none;
    noteEditOriginalNotes_.clear();
    noteEditSelectedIds_.clear();
    noteEditAnchorNoteId_ = 0;
    noteEditChanged_ = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    if (!changed) {
        primaryNotes_ = originalNotes;
        repaint();
        return;
    }

    if (gesture == NoteEditGesture::moving && onMoveNotesRequested != nullptr) {
        onMoveNotesRequested(selectedIds, deltaBeats, deltaSemitones);
    } else if (gesture == NoteEditGesture::resizing && onResizeNotesRequested != nullptr) {
        onResizeNotesRequested(anchorId, selectedIds, anchorEndBeat);
    }
}

void PianoRollView::cancelNoteEditGesture()
{
    if (noteEditGesture_ == NoteEditGesture::none) {
        return;
    }

    primaryNotes_ = noteEditOriginalNotes_;
    noteEditOriginalNotes_.clear();
    noteEditSelectedIds_.clear();
    noteEditAnchorNoteId_ = 0;
    noteEditGesture_ = NoteEditGesture::none;
    noteEditChanged_ = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    repaint();
}

core::MidiNoteMarqueeSelectionRequest PianoRollView::marqueeRequestFor(
    juce::Point<float> start,
    juce::Point<float> end) const
{
    const auto bounds = getLocalBounds();
    const auto viewport = core::sanitizeTimelineViewportState(viewportState_);
    const auto pitchViewport = core::sanitizePitchViewportState(pitchViewportState_);

    const auto startBeat = core::timelineXToBeat(start.x, bounds.getWidth(), viewport);
    const auto endBeat = core::timelineXToBeat(end.x, bounds.getWidth(), viewport);
    const auto startPitch = core::yToPitch(start.y, bounds.getHeight(), pitchViewport);
    const auto endPitch = core::yToPitch(end.y, bounds.getHeight(), pitchViewport);

    core::MidiNoteMarqueeSelectionRequest request;
    request.startBeat = startBeat;
    request.endBeat = endBeat;
    request.lowPitch = std::floor(std::min(startPitch, endPitch));
    request.highPitch = std::ceil(std::max(startPitch, endPitch));
    return request;
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
            const auto selected = note.note.id != 0
                && std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), note.note.id) != selectedNoteIds_.end();
            if (selected) {
                g.setColour(juce::Colour(0xfffff2c0));
                g.drawRect(note.bounds.expanded(1.5f), 2.0f);
                g.setColour(juce::Colour(0x55304050));
                g.fillRect(note.bounds.reduced(1.0f));
            }
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

    if (marqueeActive_ && marqueeStartPosition_.has_value()) {
        const auto marquee = juce::Rectangle<float>::leftTopRightBottom(
            std::min(marqueeStartPosition_->x, marqueeCurrentPosition_.x),
            std::min(marqueeStartPosition_->y, marqueeCurrentPosition_.y),
            std::max(marqueeStartPosition_->x, marqueeCurrentPosition_.x),
            std::max(marqueeStartPosition_->y, marqueeCurrentPosition_.y));
        g.setColour(juce::Colour(0x334f96d8));
        g.fillRect(marquee);
        g.setColour(juce::Colour(0xff9bc8ff));
        g.drawRect(marquee, 1.25f);
    }

    g.setColour(juce::Colour(0xff313b47));
    g.drawRect(bounds, 1);
}

bool PianoRollView::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) {
        if (noteEditGesture_ != NoteEditGesture::none) {
            cancelNoteEditGesture();
            return true;
        }

        if (marqueeStartPosition_.has_value() || marqueeActive_) {
            marqueeStartPosition_.reset();
            marqueeActive_ = false;
            repaint();
            return true;
        }

        return false;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (onDeleteRequested != nullptr) {
            onDeleteRequested();
            return true;
        }
    }

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey
        || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
        if (onNudgeRequested != nullptr) {
            const auto gridStep = core::gridStepBeats(gridDenominator_);
            if (key == juce::KeyPress::leftKey) {
                onNudgeRequested(0, -gridStep);
            } else if (key == juce::KeyPress::rightKey) {
                onNudgeRequested(0, gridStep);
            } else {
                const auto octave = key.getModifiers().isShiftDown() ? 12 : 1;
                onNudgeRequested(key == juce::KeyPress::upKey ? octave : -octave, 0.0);
            }
            return true;
        }
    }

    return false;
}

void PianoRollView::mouseMove(const juce::MouseEvent& event)
{
    hoverPosition_ = event.getPosition();
    hoveredNoteIndex_.reset();

    rebuildRenderedNotes();
    const auto point = event.position;
    hoveredNoteIndex_ = findRenderedNoteAt(point, false);
    if (hoveredNoteIndex_.has_value() && isRightEdgeHit(renderedNotes_[hoveredNoteIndex_.value()], point)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    repaint();
}

void PianoRollView::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (!event.mods.isLeftButtonDown()) {
        return;
    }

    rebuildRenderedNotes();

    const auto additive = event.mods.isCtrlDown() || event.mods.isCommandDown();
    const auto primaryNoteIndex = findRenderedNoteAt(event.position, true);
    const auto anyNoteIndex = findRenderedNoteAt(event.position, false);

    marqueeStartPosition_.reset();
    marqueeActive_ = false;
    marqueeAdditive_ = additive;
    noteEditGesture_ = NoteEditGesture::none;

    if (event.getNumberOfClicks() >= 2) {
        if (!anyNoteIndex.has_value() && onCreateNoteRequested != nullptr) {
            const auto beat = core::timelineXToBeat(
                event.position.x,
                getLocalBounds().getWidth(),
                core::sanitizeTimelineViewportState(viewportState_));
            const auto pitch = static_cast<int>(std::floor(core::yToPitch(
                event.position.y,
                getLocalBounds().getHeight(),
                core::sanitizePitchViewportState(pitchViewportState_))));
            onCreateNoteRequested(beat, pitch);
        }
        return;
    }

    if (primaryNoteIndex.has_value()) {
        noteEditAnchorNoteId_ = renderedNotes_[primaryNoteIndex.value()].note.id;
        noteEditStartPosition_ = event.position;
        noteEditCurrentPosition_ = event.position;
        noteEditGesture_ = isRightEdgeHit(renderedNotes_[primaryNoteIndex.value()], event.position)
            ? NoteEditGesture::pendingResize
            : NoteEditGesture::pendingMove;
        noteEditOriginalNotes_ = primaryNotes_;
        return;
    }

    if (onEmptySpaceClicked != nullptr) {
        onEmptySpaceClicked(additive);
    }

    marqueeStartPosition_ = event.position;
    marqueeCurrentPosition_ = event.position;
}

void PianoRollView::mouseDrag(const juce::MouseEvent& event)
{
    if (noteEditGesture_ == NoteEditGesture::pendingMove || noteEditGesture_ == NoteEditGesture::pendingResize) {
        if (!core::isMeaningfulMarqueeDrag(
                event.position.x - noteEditStartPosition_.x,
                event.position.y - noteEditStartPosition_.y)) {
            return;
        }

        rebuildRenderedNotes();
        const auto noteIndex = findRenderedNoteAt(noteEditStartPosition_, true);
        if (!noteIndex.has_value()) {
            cancelNoteEditGesture();
            return;
        }

        beginNoteEditGesture(
            renderedNotes_[noteIndex.value()],
            noteEditGesture_ == NoteEditGesture::pendingResize);
        updateNoteEditGesture(event.position);
        return;
    }

    if (noteEditGesture_ == NoteEditGesture::moving || noteEditGesture_ == NoteEditGesture::resizing) {
        updateNoteEditGesture(event.position);
        return;
    }

    if (!marqueeStartPosition_.has_value()) {
        return;
    }

    marqueeCurrentPosition_ = event.position;
    marqueeActive_ = core::isMeaningfulMarqueeDrag(
        marqueeCurrentPosition_.x - marqueeStartPosition_->x,
        marqueeCurrentPosition_.y - marqueeStartPosition_->y);
    repaint();
}

void PianoRollView::mouseUp(const juce::MouseEvent& event)
{
    if (noteEditGesture_ == NoteEditGesture::moving || noteEditGesture_ == NoteEditGesture::resizing) {
        updateNoteEditGesture(event.position);
        finishNoteEditGesture();
        return;
    }

    if (noteEditGesture_ == NoteEditGesture::pendingMove || noteEditGesture_ == NoteEditGesture::pendingResize) {
        rebuildRenderedNotes();
        const auto noteIndex = findRenderedNoteAt(noteEditStartPosition_, true);
        if (noteIndex.has_value() && onPrimaryNoteClicked != nullptr) {
            const auto additive = event.mods.isCtrlDown() || event.mods.isCommandDown();
            onPrimaryNoteClicked(renderedNotes_[noteIndex.value()].note.id, additive);
        }
        noteEditGesture_ = NoteEditGesture::none;
        noteEditOriginalNotes_.clear();
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }

    if (!marqueeStartPosition_.has_value()) {
        return;
    }

    marqueeCurrentPosition_ = event.position;
    const auto shouldApply = marqueeActive_
        && core::isMeaningfulMarqueeDrag(
            marqueeCurrentPosition_.x - marqueeStartPosition_->x,
            marqueeCurrentPosition_.y - marqueeStartPosition_->y);

    if (shouldApply && onMarqueeSelectionFinished != nullptr) {
        onMarqueeSelectionFinished(
            core::findMidiNotesIntersectingRange(primaryNotes_, marqueeRequestFor(*marqueeStartPosition_, marqueeCurrentPosition_)),
            marqueeAdditive_);
    }

    marqueeStartPosition_.reset();
    marqueeActive_ = false;
    repaint();
}

void PianoRollView::mouseExit(const juce::MouseEvent&)
{
    hoveredNoteIndex_.reset();
    if (noteEditGesture_ == NoteEditGesture::none) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    repaint();
}

}  // namespace dawhermes::ui
