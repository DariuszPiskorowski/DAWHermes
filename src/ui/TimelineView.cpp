#include "ui/TimelineView.h"

#include <algorithm>
#include <cmath>

namespace dawhermes::ui {

namespace {

juce::Colour rowColourForIndex(int rowIndex)
{
    return (rowIndex % 2 == 0) ? juce::Colour(0xff262d37) : juce::Colour(0xff20262f);
}

}  // namespace

TimelineView::TimelineView()
{
    setOpaque(true);
    audioFormatManager_.registerBasicFormats();
}

void TimelineView::setTracks(std::vector<core::Track> tracks)
{
    tracks_ = std::move(tracks);
    repaint();
}

void TimelineView::setSelectedTrackIds(std::vector<std::uint64_t> selectedTrackIds)
{
    selectedTrackIds_ = std::move(selectedTrackIds);
    repaint();
}

void TimelineView::setViewportState(const core::TimelineViewportState& state)
{
    viewportState_ = core::sanitizeTimelineViewportState(state);
    repaint();
}

void TimelineView::setGridDenominator(int denominator)
{
    gridDenominator_ = std::max(1, denominator);
    repaint();
}

void TimelineView::setTempoMap(std::vector<core::MidiTempoEvent> tempoMap)
{
    tempoMap_ = core::sanitizeTempoMap(tempoMap);
    repaint();
}

void TimelineView::setTrackRowHeight(int rowHeight)
{
    rowHeight_ = std::max(12, rowHeight);
    repaint();
}

void TimelineView::setVerticalScrollPixels(int scrollPixels)
{
    verticalScrollPixels_ = std::max(0, scrollPixels);
    repaint();
}

void TimelineView::setPlayheadBeat(std::optional<double> beat)
{
    playheadBeat_ = beat;
    repaint();
}

juce::AudioThumbnail* TimelineView::getOrCreateThumbnail(const std::string& path)
{
    if (path.empty()) {
        return nullptr;
    }

    auto it = thumbnailsByPath_.find(path);
    if (it != thumbnailsByPath_.end()) {
        return it->second.get();
    }

    auto thumbnail = std::make_unique<juce::AudioThumbnail>(512, audioFormatManager_, thumbnailCache_);
    const juce::File audioFile(path);
    if (!audioFile.existsAsFile()) {
        return nullptr;
    }

    thumbnail->setSource(new juce::FileInputSource(audioFile));
    auto* raw = thumbnail.get();
    thumbnailsByPath_.emplace(path, std::move(thumbnail));
    return raw;
}

bool TimelineView::isTrackSelected(std::uint64_t trackId) const
{
    return std::find(selectedTrackIds_.begin(), selectedTrackIds_.end(), trackId) != selectedTrackIds_.end();
}

double TimelineView::beatsPerSecond() const
{
    const auto tempoMap = core::sanitizeTempoMap(tempoMap_);
    const auto microsecondsPerQuarter = static_cast<double>(tempoMap.front().microsecondsPerQuarterNote);
    const auto quarterPerSecond = 1000000.0 / std::max(1.0, microsecondsPerQuarter);
    return std::max(0.1, quarterPerSecond);
}

void TimelineView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff1c222b));

    if (bounds.getWidth() <= 1 || bounds.getHeight() <= 1) {
        return;
    }

    const auto viewport = core::sanitizeTimelineViewportState(viewportState_);
    const auto visibleStart = viewport.startBeat;
    const auto visibleEnd = viewport.startBeat + viewport.visibleBeats;

    const auto gridBeats = core::buildGridBeatPositions(visibleStart, visibleEnd, gridDenominator_);
    g.setColour(juce::Colour(0xff2a333f));
    for (const auto beat : gridBeats) {
        const auto x = static_cast<int>(std::round(core::timelineBeatToX(beat, bounds.getWidth(), viewport)));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getBottom()));
    }

    const auto lanes = core::buildTimelineLaneGeometry(tracks_, rowHeight_, -verticalScrollPixels_);
    const auto bps = beatsPerSecond();

    for (const auto& lane : lanes) {
        juce::Rectangle<int> rowRect(0, lane.y, bounds.getWidth(), lane.height);
        if (!rowRect.intersects(bounds)) {
            continue;
        }

        auto rowFill = rowColourForIndex(lane.rowIndex);
        if (isTrackSelected(lane.trackId)) {
            rowFill = rowFill.brighter(0.35f);
        }

        g.setColour(rowFill);
        g.fillRect(rowRect);

        const auto track = std::find_if(tracks_.begin(), tracks_.end(), [lane](const auto& item) {
            return item.id == lane.trackId;
        });

        if (track != tracks_.end()) {
            if (track->type == core::TrackType::audio) {
                auto* thumbnail = getOrCreateThumbnail(track->audioSourcePath);
                if (thumbnail != nullptr && thumbnail->getTotalLength() > 0.0) {
                    const auto durationBeats = std::max(0.01, thumbnail->getTotalLength() * bps);
                    const auto beatA = std::max(0.0, visibleStart);
                    const auto beatB = std::min(durationBeats, visibleEnd);

                    if (beatB > beatA) {
                        const auto x1 = static_cast<int>(std::floor(core::timelineBeatToX(beatA, bounds.getWidth(), viewport)));
                        const auto x2 = static_cast<int>(std::ceil(core::timelineBeatToX(beatB, bounds.getWidth(), viewport)));

                        juce::Rectangle<int> waveformRect(
                            x1,
                            rowRect.getY() + 3,
                            std::max(2, x2 - x1),
                            std::max(2, rowRect.getHeight() - 6));
                        waveformRect = waveformRect.getIntersection(bounds);

                        const auto t1 = (beatA / durationBeats) * thumbnail->getTotalLength();
                        const auto t2 = (beatB / durationBeats) * thumbnail->getTotalLength();

                        g.setColour(juce::Colour(0xff6f8ba9));
                        thumbnail->drawChannels(g, waveformRect, t1, t2, 0.85f);
                    }
                }
            }

            if (track->type == core::TrackType::midi && !track->midiNotes.empty()) {
                g.setColour(juce::Colour(0xff8ad8f0));
                for (const auto& note : track->midiNotes) {
                    const auto noteStart = note.startBeat;
                    const auto noteEnd = note.startBeat + std::max(1.0 / 960.0, note.durationBeats);
                    if (noteEnd < visibleStart || noteStart > visibleEnd) {
                        continue;
                    }

                    const auto x1 = static_cast<int>(std::floor(core::timelineBeatToX(noteStart, bounds.getWidth(), viewport)));
                    const auto x2 = static_cast<int>(std::ceil(core::timelineBeatToX(noteEnd, bounds.getWidth(), viewport)));
                    const auto laneY = rowRect.getY() + 2;
                    const auto laneHeight = std::max(2, rowRect.getHeight() - 4);
                    const auto pitchRatio = 1.0 - (static_cast<double>(std::clamp(note.pitch, 0, 127)) / 127.0);
                    const auto y = laneY + static_cast<int>(std::round(pitchRatio * static_cast<double>(laneHeight - 3)));

                    g.fillRect(x1, y, std::max(1, x2 - x1), 3);
                }
            }
        }

        g.setColour(juce::Colour(0xff313b47));
        g.drawHorizontalLine(rowRect.getBottom() - 1, 0.0f, static_cast<float>(bounds.getRight()));
    }

    if (playheadBeat_.has_value()) {
        const auto x = static_cast<int>(std::round(
            core::timelineBeatToX(playheadBeat_.value(), bounds.getWidth(), viewport)));
        if (x >= bounds.getX() && x <= bounds.getRight()) {
            g.setColour(juce::Colour(0xffff704d));
            g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getBottom()));
        }
    }
}

}  // namespace dawhermes::ui
