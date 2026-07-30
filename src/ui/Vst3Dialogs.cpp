#include "ui/Vst3Dialogs.h"

#include <algorithm>

namespace dawhermes::ui {

Vst3PluginManagerComponent::Vst3PluginManagerComponent(
    plugins::Vst3InstrumentHost& host,
    std::function<void()> stopPlayback)
    : host_(host),
      stopPlayback_(std::move(stopPlayback)),
      progress_(progressValue_)
{
    title_.setText(
        "Cached VST3 instruments",
        juce::dontSendNotification);
    title_.setFont(juce::FontOptions(18.0f));
    addAndMakeVisible(title_);

    filter_.setTextToShowWhenEmpty(
        "Filter by instrument or manufacturer",
        juce::Colours::grey);
    filter_.onTextChange = [this]() { refreshFilter(); };
    addAndMakeVisible(filter_);

    list_.setModel(this);
    list_.setRowHeight(28);
    addAndMakeVisible(list_);

    scanButton_.setTooltip(
        "Scan standard VST3 locations and keep unchanged cached entries.");
    rescanButton_.setTooltip(
        "Test every VST3 candidate again.");
    scanButton_.onClick = [this]() { startScan(false); };
    rescanButton_.onClick = [this]() { startScan(true); };
    cancelButton_.onClick = [this]() {
        host_.catalog().requestCancel();
    };
    addAndMakeVisible(scanButton_);
    addAndMakeVisible(rescanButton_);
    addAndMakeVisible(cancelButton_);
    addAndMakeVisible(progress_);

    status_.setJustificationType(
        juce::Justification::centredLeft);
    status_.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(status_);

    all_ = host_.catalog().instruments();
    refreshFilter();
    startTimerHz(12);
}

Vst3PluginManagerComponent::~Vst3PluginManagerComponent()
{
    stopTimer();
    list_.setModel(nullptr);
}

void Vst3PluginManagerComponent::resized()
{
    auto area = getLocalBounds().reduced(12);
    title_.setBounds(area.removeFromTop(30));
    area.removeFromTop(6);
    filter_.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    auto buttons = area.removeFromBottom(34);
    scanButton_.setBounds(buttons.removeFromLeft(90));
    buttons.removeFromLeft(6);
    rescanButton_.setBounds(buttons.removeFromLeft(110));
    buttons.removeFromLeft(6);
    cancelButton_.setBounds(buttons.removeFromLeft(110));
    area.removeFromBottom(6);
    status_.setBounds(area.removeFromBottom(28));
    progress_.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(8);
    list_.setBounds(area);
}

int Vst3PluginManagerComponent::getNumRows()
{
    return static_cast<int>(filtered_.size());
}

void Vst3PluginManagerComponent::paintListBoxItem(
    int row,
    juce::Graphics& graphics,
    int width,
    int height,
    bool selected)
{
    if (row < 0 || row >= static_cast<int>(filtered_.size())) {
        return;
    }
    graphics.fillAll(
        selected
            ? juce::Colour(0xff315f91)
            : (row % 2 == 0
                   ? juce::Colour(0xff252a31)
                   : juce::Colour(0xff20252b)));
    const auto& plugin =
        filtered_[static_cast<std::size_t>(row)];
    auto label = plugin.name;
    if (plugin.manufacturerName.isNotEmpty()) {
        label << "  —  " << plugin.manufacturerName;
    }
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        label,
        8,
        0,
        width - 16,
        height,
        juce::Justification::centredLeft,
        true);
}

void Vst3PluginManagerComponent::timerCallback()
{
    const auto scan = host_.catalog().scanStatus();
    progressValue_ = scan.progress;
    scanButton_.setEnabled(!scan.running);
    rescanButton_.setEnabled(!scan.running);
    cancelButton_.setEnabled(
        scan.running && !scan.cancelRequested);
    juce::String text = scan.summary;
    if (scan.running && scan.currentCandidate.isNotEmpty()) {
        text << "  Current: " << scan.currentCandidate;
    } else if (!scan.running) {
        text << "  Instruments: "
             << static_cast<int>(all_.size());
        if (scan.failedCount > 0) {
            text << "  Failed: "
                 << scan.failedCount;
        }
        if (scan.recoverySkippedCount > 0) {
            text << "  Skipped after crash: "
                 << scan.recoverySkippedCount;
        }
        if (scan.staleRecoveryCount > 0) {
            text << "  Stale recovery entries ignored: "
                 << scan.staleRecoveryCount;
        }
    }
    status_.setText(text, juce::dontSendNotification);
    if (scan.completed && scan.catalogReplaced) {
        auto latest = host_.catalog().instruments();
        if (latest.size() != all_.size()
            || !std::equal(
                latest.begin(),
                latest.end(),
                all_.begin(),
                [](const auto& left, const auto& right) {
                    return plugins::stableVst3Identifier(left)
                        == plugins::stableVst3Identifier(right);
                })) {
            all_ = std::move(latest);
            refreshFilter();
        }
    }
}

void Vst3PluginManagerComponent::refreshFilter()
{
    const auto needle = filter_.getText().trim();
    filtered_.clear();
    for (const auto& plugin : all_) {
        if (needle.isEmpty()
            || plugin.name.containsIgnoreCase(needle)
            || plugin.manufacturerName.containsIgnoreCase(needle)) {
            filtered_.push_back(plugin);
        }
    }
    list_.updateContent();
    list_.repaint();
}

void Vst3PluginManagerComponent::startScan(bool rescan)
{
    if (stopPlayback_) {
        stopPlayback_();
    }
    if (!host_.catalog().startScan(rescan)) {
        status_.setText(
            "A VST3 scan is already running.",
            juce::dontSendNotification);
    }
}

std::optional<juce::PluginDescription>
chooseVst3Instrument(
    juce::Component* parent,
    const plugins::Vst3PluginCatalog& catalog)
{
    const auto instruments = catalog.instruments();
    if (instruments.empty()) {
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::InfoIcon,
            "Select VST3 Instrument",
            "The cached instrument catalog is empty.\n\nUse Plugins -> VST3 Instrument Manager... and run a deliberate scan first.",
            "Close",
            parent);
        return std::nullopt;
    }

    juce::AlertWindow dialog(
        "Select VST3 Instrument",
        "Choose one cached VST3 instrument for the selected MIDI track.",
        juce::AlertWindow::NoIcon);
    juce::StringArray choices;
    for (const auto& instrument : instruments) {
        auto label = instrument.name;
        if (instrument.manufacturerName.isNotEmpty()) {
            label << " — " << instrument.manufacturerName;
        }
        choices.add(label);
    }
    dialog.addComboBox("instrument", choices, "Instrument");
    dialog.addButton(
        "Select",
        1,
        juce::KeyPress(juce::KeyPress::returnKey));
    dialog.addButton(
        "Cancel",
        0,
        juce::KeyPress(juce::KeyPress::escapeKey));
    if (dialog.runModalLoop() != 1) {
        return std::nullopt;
    }
    const auto selected =
        dialog.getComboBoxComponent("instrument")->getSelectedItemIndex();
    return selected >= 0
               && selected < static_cast<int>(instruments.size())
        ? std::optional<juce::PluginDescription> {
              instruments[static_cast<std::size_t>(selected)]
          }
        : std::nullopt;
}

}  // namespace dawhermes::ui
