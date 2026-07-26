#include "ui/DrumMappingDialog.h"

#include <array>

namespace dawhermes::ui {

namespace {
constexpr int presetUjamKandy = 1;
constexpr int presetGeneralMidi = 2;
constexpr int presetSitala = 3;
constexpr int presetCustom = 4;
}

DrumMappingDialog::DrumMappingDialog()
{
    setOpaque(true);

    titleLabel_.setText("Drum Mapping (Milestone 1 shell)", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    presetCombo_.addItem("UJAM Kandy", presetUjamKandy);
    presetCombo_.addItem("General MIDI", presetGeneralMidi);
    presetCombo_.addItem("Sitala", presetSitala);
    presetCombo_.addItem("Custom", presetCustom);
    presetCombo_.setSelectedId(presetUjamKandy);
    presetCombo_.onChange = [this]() { loadPresetRows(); };
    addAndMakeVisible(presetCombo_);

    table_.setModel(this);
    table_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff20242a));
    table_.setColour(juce::ListBox::textColourId, juce::Colours::white);
    table_.getHeader().addColumn("Semantic layer", 1, 190);
    table_.getHeader().addColumn("Enabled", 2, 90);
    table_.getHeader().addColumn("Track name", 3, 210);
    table_.getHeader().addColumn("MIDI note", 4, 100);
    addAndMakeVisible(table_);

    loadJsonButton_.setEnabled(false);
    loadJsonButton_.setTooltip("JSON load is planned for a later milestone.");
    addAndMakeVisible(loadJsonButton_);

    saveJsonButton_.setEnabled(false);
    saveJsonButton_.setTooltip("JSON save is planned for a later milestone.");
    addAndMakeVisible(saveJsonButton_);

    restoreDefaultButton_.onClick = [this]() { loadPresetRows(); };
    addAndMakeVisible(restoreDefaultButton_);

    cancelButton_.onClick = [this]() {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
            dialog->exitModalState(0);
        }
    };
    addAndMakeVisible(cancelButton_);

    applyButton_.onClick = [this]() {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
            dialog->exitModalState(1);
        }
    };
    addAndMakeVisible(applyButton_);

    loadPresetRows();
    setSize(780, 430);
}

int DrumMappingDialog::getNumRows()
{
    return static_cast<int>(rows_.size());
}

void DrumMappingDialog::paintRowBackground(
    juce::Graphics& g,
    int rowNumber,
    int,
    int,
    bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= getNumRows()) {
        return;
    }

    const auto baseColour = rowIsSelected ? juce::Colour(0xff315f99)
                                          : ((rowNumber % 2 == 0) ? juce::Colour(0xff262b33)
                                                                  : juce::Colour(0xff20242a));
    g.fillAll(baseColour);
}

void DrumMappingDialog::paintCell(
    juce::Graphics& g,
    int rowNumber,
    int columnId,
    int width,
    int height,
    bool)
{
    if (rowNumber < 0 || rowNumber >= getNumRows()) {
        return;
    }

    const auto& row = rows_.at(static_cast<std::size_t>(rowNumber));
    juce::String value;

    switch (columnId) {
    case 1:
        value = row.semanticLayer;
        break;
    case 2:
        value = row.enabled ? "Yes" : "No";
        break;
    case 3:
        value = row.trackName;
        break;
    case 4:
        value = juce::String(row.midiNote);
        break;
    default:
        break;
    }

    g.setColour(juce::Colours::white);
    g.drawText(value, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void DrumMappingDialog::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto top = area.removeFromTop(34);
    titleLabel_.setBounds(top.removeFromLeft(350));
    presetCombo_.setBounds(top.removeFromLeft(220));

    area.removeFromTop(8);

    auto buttons = area.removeFromBottom(38);
    table_.setBounds(area);

    loadJsonButton_.setBounds(buttons.removeFromLeft(110));
    buttons.removeFromLeft(8);
    saveJsonButton_.setBounds(buttons.removeFromLeft(110));
    buttons.removeFromLeft(8);
    restoreDefaultButton_.setBounds(buttons.removeFromLeft(130));

    buttons.removeFromLeft(20);
    cancelButton_.setBounds(buttons.removeFromRight(120));
    buttons.removeFromRight(8);
    applyButton_.setBounds(buttons.removeFromRight(120));
}

void DrumMappingDialog::loadPresetRows()
{
    rows_ = createPresetRows(presetCombo_.getSelectedId());
    table_.updateContent();
    table_.repaint();
}

std::vector<DrumMappingDialog::MappingRow> DrumMappingDialog::createPresetRows(int presetId) const
{
    if (presetId == presetSitala) {
        return {
            { "Kick", true, "kick", 36 },
            { "Snare", true, "snare", 38 },
            { "Closed Hat", true, "hat_closed", 42 },
            { "Open Hat", true, "hat_open", 46 },
            { "Tom", true, "tom_mid", 47 },
            { "Crash", true, "crash", 49 }
        };
    }

    if (presetId == presetGeneralMidi || presetId == presetCustom) {
        return {
            { "Kick", true, "Kick", 36 },
            { "Snare", true, "Snare", 38 },
            { "Closed Hat", true, "Closed Hat", 42 },
            { "Open Hat", true, "Open Hat", 46 },
            { "Low Tom", true, "Low Tom", 45 },
            { "Crash", true, "Crash", 49 }
        };
    }

    return {
        { "Kick", true, "Kandy Kick", 36 },
        { "Snare", true, "Kandy Snare", 38 },
        { "Closed Hat", true, "Kandy Hat Closed", 42 },
        { "Open Hat", true, "Kandy Hat Open", 46 },
        { "Tom", true, "Kandy Tom", 47 },
        { "Crash", true, "Kandy Crash", 49 }
    };
}

}  // namespace dawhermes::ui
