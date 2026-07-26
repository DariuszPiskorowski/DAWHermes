#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "hermes/HermesTypes.h"

namespace dawhermes::ui {

std::optional<hermes::HermesDrumsOptions> showDrumsMakeMidiDialog(juce::Component* parent);
std::optional<hermes::HermesBpmOptions> showSetFixBpmDialog(
    juce::Component* parent,
    const hermes::HermesTrackContext& context);

bool showBassRepairDialog(juce::Component* parent, const hermes::HermesTrackContext& context);
bool showSynchronizeDialog(juce::Component* parent, const hermes::HermesTrackContext& context);
bool showDrumMappingDialog(juce::Component* parent);

void showMilestoneNotIntegratedMessage(juce::Component* parent, const juce::String& details = {});
void showValidationError(juce::Component* parent, const juce::String& message);

}  // namespace dawhermes::ui
