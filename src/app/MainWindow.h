#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dawhermes::ui {
class MainComponent;
}

namespace dawhermes::app {

class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(
        juce::String name,
        juce::ApplicationProperties& applicationProperties);

    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    juce::ApplicationProperties& applicationProperties_;
};

}  // namespace dawhermes::app
