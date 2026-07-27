#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/MainWindow.h"

namespace dawhermes::app {

class MainApplication final : public juce::JUCEApplication {
public:
    MainApplication() = default;
    ~MainApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;

    void systemRequestedQuit() override;
    void anotherInstanceStarted(const juce::String& commandLine) override;

private:
    juce::ApplicationProperties appProperties_;
    std::unique_ptr<MainWindow> mainWindow_;
};

}  // namespace dawhermes::app
