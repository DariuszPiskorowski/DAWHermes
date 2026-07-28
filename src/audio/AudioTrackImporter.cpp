#include "audio/AudioTrackImporter.h"

#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

namespace dawhermes::audio {

namespace {

std::optional<core::PreparedAudioTrackImport> prepareAudioTrack(
    const std::filesystem::path& sourcePath,
    std::string& error)
{
    error.clear();
    const juce::File sourceFile(sourcePath.string());
    if (!sourceFile.existsAsFile()) {
        error = "file does not exist";
        return std::nullopt;
    }
    if (!sourceFile.hasFileExtension("wav")) {
        error = "not a WAV file";
        return std::nullopt;
    }

    juce::WavAudioFormat wavFormat;
    auto inputStream = sourceFile.createInputStream();
    if (inputStream == nullptr) {
        error = "file could not be opened";
        return std::nullopt;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(inputStream.release(), true));
    if (reader == nullptr
        || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0
        || reader->numChannels < 1
        || reader->numChannels > 2) {
        error = "unreadable mono/stereo WAV";
        return std::nullopt;
    }

    core::AudioSourceMetadata metadata;
    metadata.sampleRate = reader->sampleRate;
    metadata.channelCount = static_cast<int>(reader->numChannels);
    metadata.durationSeconds =
        static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    metadata.frameCount = static_cast<std::uint64_t>(reader->lengthInSamples);
    metadata.bitsPerSample = reader->bitsPerSample;
    metadata.fileSizeBytes = static_cast<std::uint64_t>(sourceFile.getSize());

    core::PreparedAudioTrackImport prepared;
    prepared.trackName = sourceFile.getFileNameWithoutExtension().toStdString();
    prepared.sourcePath = sourceFile.getFullPathName().toStdString();
    prepared.metadata = metadata;
    return prepared;
}

}  // namespace

AudioTrackImportPreparation prepareAudioTrackImports(
    const std::vector<std::filesystem::path>& sourcePaths)
{
    AudioTrackImportPreparation result;
    result.validTracks.reserve(sourcePaths.size());
    result.skippedFiles.reserve(sourcePaths.size());

    for (const auto& sourcePath : sourcePaths) {
        std::string error;
        auto prepared = prepareAudioTrack(sourcePath, error);
        if (prepared.has_value()) {
            result.validTracks.push_back(std::move(prepared.value()));
        } else {
            result.skippedFiles.push_back({ sourcePath, std::move(error) });
        }
    }

    return result;
}

}  // namespace dawhermes::audio
