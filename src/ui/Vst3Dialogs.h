#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugins/Vst3InstrumentHost.h"

namespace dawhermes::ui {

class Vst3PluginManagerComponent final
    : public juce::Component,
      private juce::ListBoxModel,
      private juce::Timer {
public:
    Vst3PluginManagerComponent(
        plugins::Vst3InstrumentHost& host,
        std::function<void()> stopPlayback);
    ~Vst3PluginManagerComponent() override;

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(
        int row,
        juce::Graphics& graphics,
        int width,
        int height,
        bool selected) override;
    void timerCallback() override;
    void refreshFilter();
    void startScan(bool rescan);

    plugins::Vst3InstrumentHost& host_;
    std::function<void()> stopPlayback_;
    std::vector<juce::PluginDescription> all_;
    std::vector<juce::PluginDescription> filtered_;
    juce::Label title_;
    juce::TextEditor filter_;
    juce::ListBox list_;
    juce::TextButton scanButton_ { "Scan" };
    juce::TextButton rescanButton_ { "Rescan All" };
    juce::TextButton cancelButton_ { "Cancel Scan" };
    juce::ProgressBar progress_;
    juce::Label status_;
    double progressValue_ { 0.0 };
};

std::optional<juce::PluginDescription>
chooseVst3Instrument(
    juce::Component* parent,
    const plugins::Vst3PluginCatalog& catalog);

}  // namespace dawhermes::ui
