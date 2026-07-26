#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace dawhermes::ui {

class DrumMappingDialog final : public juce::Component,
                                private juce::TableListBoxModel {
public:
    DrumMappingDialog();

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

    void resized() override;

private:
    struct MappingRow {
        juce::String semanticLayer;
        bool enabled { true };
        juce::String trackName;
        int midiNote { 36 };
    };

    void loadPresetRows();
    std::vector<MappingRow> createPresetRows(int presetId) const;

    juce::Label titleLabel_;
    juce::ComboBox presetCombo_;
    juce::TableListBox table_;

    juce::TextButton loadJsonButton_ { "Load JSON" };
    juce::TextButton saveJsonButton_ { "Save JSON" };
    juce::TextButton restoreDefaultButton_ { "Restore Default" };
    juce::TextButton cancelButton_ { "Cancel" };
    juce::TextButton applyButton_ { "Apply" };

    std::vector<MappingRow> rows_;
};

}  // namespace dawhermes::ui
