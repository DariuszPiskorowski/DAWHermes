#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "hermes/ComposerAssistantConnector.h"
#include "hermes/HermesTypes.h"

namespace dawhermes::ui {

std::optional<hermes::HermesDrumsOptions> showDrumsMakeMidiDialog(juce::Component* parent);
std::optional<hermes::HermesBpmOptions> showSetFixBpmDialog(
    juce::Component* parent,
    const hermes::HermesTrackContext& context);

std::optional<hermes::HermesBassOptions> showBassRepairDialog(
    juce::Component* parent,
    const hermes::HermesAudioMidiPairContext& context);
std::optional<hermes::HermesSyncOptions> showSynchronizeDialog(
    juce::Component* parent,
    const hermes::HermesAudioMidiPairContext& context);
bool showDrumMappingDialog(juce::Component* parent);
std::optional<hermes::ComposerAssistantSettings> showComposerAssistantSettingsDialog(
    juce::Component* parent,
    const hermes::ComposerAssistantSettings& initialSettings);

void showMilestoneNotIntegratedMessage(juce::Component* parent, const juce::String& details = {});
void showValidationError(juce::Component* parent, const juce::String& message);
void showHermesOperationMessage(
    juce::Component* parent,
    const juce::String& title,
    const juce::String& message,
    juce::AlertWindow::AlertIconType icon = juce::AlertWindow::InfoIcon);

}  // namespace dawhermes::ui
