#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/AudioTrackImport.h"

namespace dawhermes::audio {

struct SkippedAudioImport {
    std::filesystem::path sourcePath;
    std::string reason;
};

struct AudioTrackImportPreparation {
    std::vector<core::PreparedAudioTrackImport> validTracks;
    std::vector<SkippedAudioImport> skippedFiles;
};

AudioTrackImportPreparation prepareAudioTrackImports(
    const std::vector<std::filesystem::path>& sourcePaths);

}  // namespace dawhermes::audio
