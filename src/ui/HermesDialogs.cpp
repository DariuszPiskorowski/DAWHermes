#include "ui/HermesDialogs.h"

#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>

#include "hermes/HermesValidation.h"
#include "ui/DrumMappingDialog.h"

namespace dawhermes::ui {

namespace {

std::optional<int> parseIntStrict(const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    if (!trimmed.containsOnly("+-0123456789")) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const auto value = std::stoi(trimmed.toStdString(), &consumed, 10);
        if (consumed != static_cast<std::size_t>(trimmed.getNumBytesAsUTF8())) {
            return std::nullopt;
        }

        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDoubleStrict(const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const auto value = std::stod(trimmed.toStdString(), &consumed);
        if (consumed != static_cast<std::size_t>(trimmed.getNumBytesAsUTF8())) {
            return std::nullopt;
        }

        return value;
    } catch (...) {
        return std::nullopt;
    }
}

juce::String contextLine(const hermes::HermesTrackContext& context)
{
    const auto type = context.trackType == core::TrackType::audio ? "Audio" : "MIDI";
    return "Selected track: " + juce::String(context.trackName) + " (" + type + ")";
}

juce::String boolToOnOff(bool value)
{
    return value ? "Enabled" : "Disabled";
}

}  // namespace

void showHermesOperationMessage(
    juce::Component*,
    const juce::String& title,
    const juce::String& message,
    juce::AlertWindow::AlertIconType icon)
{
    juce::AlertWindow::showMessageBox(icon, title, message);
}

void showMilestoneNotIntegratedMessage(juce::Component*, const juce::String& details)
{
    auto message = juce::String("This Hermes command is not yet integrated in Milestone 1.");
    if (details.isNotEmpty()) {
        message << "\n\n" << details;
    }

    showHermesOperationMessage(nullptr, "DAWHermes", message, juce::AlertWindow::InfoIcon);
}

void showValidationError(juce::Component*, const juce::String& message)
{
    showHermesOperationMessage(nullptr, "Validation error", message, juce::AlertWindow::WarningIcon);
}

std::optional<hermes::ComposerAssistantSettings> showComposerAssistantSettingsDialog(
    juce::Component*,
    const hermes::ComposerAssistantSettings& initialSettings)
{
    while (true) {
        juce::AlertWindow dialog(
            "Composer Assistant Connector",
            "Configure optional compatibility connector for legacy Composer Assistant XML-RPC service.",
            juce::AlertWindow::NoIcon);

        dialog.addTextEditor("host", juce::String(initialSettings.host), "Host");
        dialog.addTextEditor("port", juce::String(initialSettings.port), "Port");
        dialog.addTextEditor("timeout", juce::String(initialSettings.timeoutMs), "Timeout (ms)");

        auto enabledToggle = std::make_unique<juce::ToggleButton>(
            juce::String("Connector status: ") + boolToOnOff(initialSettings.enabled));
        enabledToggle->setToggleState(initialSettings.enabled, juce::dontSendNotification);
        enabledToggle->onClick = [button = enabledToggle.get()] {
            button->setButtonText(juce::String("Connector status: ") + boolToOnOff(button->getToggleState()));
        };

        auto loopbackToggle = std::make_unique<juce::ToggleButton>(
            juce::String("Safety mode (loopback only): ") + boolToOnOff(initialSettings.requireLoopbackHost));
        loopbackToggle->setToggleState(initialSettings.requireLoopbackHost, juce::dontSendNotification);
        loopbackToggle->onClick = [button = loopbackToggle.get()] {
            button->setButtonText(
                juce::String("Safety mode (loopback only): ") + boolToOnOff(button->getToggleState()));
        };

        dialog.addCustomComponent(enabledToggle.get());
        dialog.addCustomComponent(loopbackToggle.get());

        dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog.addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));

        if (dialog.runModalLoop() != 1) {
            return std::nullopt;
        }

        const auto parsedPort = parseIntStrict(dialog.getTextEditorContents("port"));
        if (!parsedPort.has_value()) {
            showValidationError(nullptr, "Port must be an integer in range 1..65535.");
            continue;
        }

        const auto parsedTimeout = parseIntStrict(dialog.getTextEditorContents("timeout"));
        if (!parsedTimeout.has_value()) {
            showValidationError(nullptr, "Timeout must be an integer in milliseconds.");
            continue;
        }

        hermes::ComposerAssistantSettings settings;
        settings.enabled = enabledToggle->getToggleState();
        settings.requireLoopbackHost = loopbackToggle->getToggleState();
        settings.host = dialog.getTextEditorContents("host").trim().toStdString();
        settings.port = parsedPort.value();
        settings.timeoutMs = parsedTimeout.value();

        return settings;
    }
}

std::optional<hermes::HermesDrumsOptions> showDrumsMakeMidiDialog(juce::Component*)
{
    while (true) {
        juce::AlertWindow dialog(
            "Hermes - Drums - Make MIDI from WAV",
            "Configure drum options.",
            juce::AlertWindow::NoIcon);

        dialog.addComboBox(
            "resultLayout",
            { "Separate MIDI tracks", "One grouped multitrack", "One single drum track" });
        dialog.getComboBoxComponent("resultLayout")->setSelectedItemIndex(0);

        dialog.addComboBox(
            "profile",
            { "Conservative", "Balanced", "Sensitive" });
        dialog.getComboBoxComponent("profile")->setSelectedItemIndex(0);

        dialog.addComboBox(
            "detectionMode",
            { "Multi-detector", "Global" });
        dialog.getComboBoxComponent("detectionMode")->setSelectedItemIndex(0);

        dialog.addComboBox(
            "targetMapping",
            { "UJAM Kandy", "General MIDI", "Sitala", "Custom" });
        dialog.getComboBoxComponent("targetMapping")->setSelectedItemIndex(0);

        dialog.addTextEditor("c1Midi", "36", "C1 MIDI note (0..127)");
        auto createEmptyToggle = std::make_unique<juce::ToggleButton>("Create empty enabled layers");
        createEmptyToggle->setToggleState(false, juce::dontSendNotification);
        dialog.addCustomComponent(createEmptyToggle.get());

        dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog.addButton("Create MIDI", 1, juce::KeyPress(juce::KeyPress::returnKey));

        if (dialog.runModalLoop() != 1) {
            return std::nullopt;
        }

        hermes::HermesDrumsOptions options;

        switch (dialog.getComboBoxComponent("resultLayout")->getSelectedItemIndex()) {
        case 0:
            options.resultLayout = hermes::HermesResultLayout::separateMidiTracks;
            break;
        case 1:
            options.resultLayout = hermes::HermesResultLayout::groupedMultitrack;
            break;
        case 2:
            options.resultLayout = hermes::HermesResultLayout::singleDrumTrack;
            break;
        default:
            options.resultLayout = static_cast<hermes::HermesResultLayout>(-1);
            break;
        }

        switch (dialog.getComboBoxComponent("profile")->getSelectedItemIndex()) {
        case 0:
            options.profile = hermes::HermesDrumsProfile::conservative;
            break;
        case 1:
            options.profile = hermes::HermesDrumsProfile::balanced;
            break;
        case 2:
            options.profile = hermes::HermesDrumsProfile::sensitive;
            break;
        default:
            options.profile = static_cast<hermes::HermesDrumsProfile>(-1);
            break;
        }

        switch (dialog.getComboBoxComponent("detectionMode")->getSelectedItemIndex()) {
        case 0:
            options.detectionMode = hermes::HermesDetectionMode::multiDetector;
            break;
        case 1:
            options.detectionMode = hermes::HermesDetectionMode::global;
            break;
        default:
            options.detectionMode = static_cast<hermes::HermesDetectionMode>(-1);
            break;
        }

        switch (dialog.getComboBoxComponent("targetMapping")->getSelectedItemIndex()) {
        case 0:
            options.targetMapping = hermes::HermesTargetMapping::ujamKandy;
            break;
        case 1:
            options.targetMapping = hermes::HermesTargetMapping::generalMidi;
            break;
        case 2:
            options.targetMapping = hermes::HermesTargetMapping::sitala;
            break;
        case 3:
            options.targetMapping = hermes::HermesTargetMapping::custom;
            break;
        default:
            options.targetMapping = static_cast<hermes::HermesTargetMapping>(-1);
            break;
        }

        options.createEmptyEnabledLayers = createEmptyToggle->getToggleState();

        const auto parsedC1 = parseIntStrict(dialog.getTextEditorContents("c1Midi"));
        if (!parsedC1.has_value()) {
            showValidationError(nullptr, "C1 MIDI note must be an integer in range 0..127.");
            continue;
        }

        options.c1MidiNote = parsedC1.value();

        const auto validation = hermes::validateDrumsOptions(options);
        if (!validation.ok) {
            showValidationError(nullptr, validation.message);
            continue;
        }

        return options;
    }
}

std::optional<hermes::HermesBpmOptions> showSetFixBpmDialog(
    juce::Component*,
    const hermes::HermesTrackContext& context)
{
    while (true) {
        juce::AlertWindow dialog(
            "Hermes - Set / Fix BPM",
            contextLine(context),
            juce::AlertWindow::NoIcon);

        dialog.addTextEditor("bpmValue", "120", "BPM");
        dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog.addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));

        if (dialog.runModalLoop() != 1) {
            return std::nullopt;
        }

        const auto parsed = parseDoubleStrict(dialog.getTextEditorContents("bpmValue"));
        if (!parsed.has_value()) {
            showValidationError(nullptr, "BPM must be a positive finite number.");
            continue;
        }

        hermes::HermesBpmOptions options;
        options.bpm = parsed.value();

        const auto validation = hermes::validateBpmOptions(options);
        if (!validation.ok) {
            showValidationError(nullptr, validation.message);
            continue;
        }

        return options;
    }
}

bool showBassRepairDialog(juce::Component*, const hermes::HermesTrackContext& context)
{
    juce::AlertWindow dialog(
        "Hermes - Bass - Make / Repair MIDI from WAV",
        contextLine(context),
        juce::AlertWindow::NoIcon);

    dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog.addButton("Run", 1, juce::KeyPress(juce::KeyPress::returnKey));

    return dialog.runModalLoop() == 1;
}

bool showSynchronizeDialog(juce::Component*, const hermes::HermesTrackContext& context)
{
    juce::AlertWindow dialog(
        "Hermes - Synchronize MIDI with WAV",
        contextLine(context),
        juce::AlertWindow::NoIcon);

    dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog.addButton("Run", 1, juce::KeyPress(juce::KeyPress::returnKey));

    return dialog.runModalLoop() == 1;
}

bool showDrumMappingDialog(juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Hermes - Drum Mapping";
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.dialogBackgroundColour = juce::Colour(0xff22262d);
    options.componentToCentreAround = parent;
    options.content.setOwned(new DrumMappingDialog());

    return options.runModal() == 1;
}

}  // namespace dawhermes::ui
