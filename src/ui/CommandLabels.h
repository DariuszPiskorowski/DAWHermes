#pragma once

#include <array>
#include <string_view>

namespace dawhermes::ui::command_labels {

inline constexpr std::string_view newProject = "New Project";
inline constexpr std::string_view importAudioAsTrack = "Import Audio as Track...";
inline constexpr std::string_view importMidiAsTrack = "Import MIDI as Track...";
inline constexpr std::string_view exportSelectedMidiTrack = "Export Selected MIDI Track...";
inline constexpr std::string_view exit = "Exit";
inline constexpr std::string_view addMidiTrack = "Add MIDI Track";
inline constexpr std::string_view deleteSelectedTrack = "Delete Selected Track";

inline constexpr std::array fileMenu {
    newProject,
    importAudioAsTrack,
    importMidiAsTrack,
    exportSelectedMidiTrack,
    exit,
};

inline constexpr std::array trackMenu {
    addMidiTrack,
    deleteSelectedTrack,
};

}  // namespace dawhermes::ui::command_labels
