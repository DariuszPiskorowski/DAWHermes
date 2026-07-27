#include "app/MainWindow.h"

#include <memory>

#include "ui/MainComponent.h"

namespace dawhermes::app {

MainWindow::MainWindow(
    juce::String name,
    juce::ApplicationProperties& applicationProperties)
    : juce::DocumentWindow(
          std::move(name),
          juce::Colours::black,
          juce::DocumentWindow::allButtons),
      applicationProperties_(applicationProperties)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(1024, 640, 3840, 2160);

    setContentOwned(new ui::MainComponent(applicationProperties_), true);

    if (auto* settings = applicationProperties_.getUserSettings()) {
        const auto state = settings->getValue("windowState");
        if (state.isNotEmpty()) {
            restoreWindowStateFromString(state);
        }
    }

    if (getWidth() == 0 || getHeight() == 0) {
        centreWithSize(1400, 850);
    }

    setVisible(true);
}

MainWindow::~MainWindow()
{
    if (auto* settings = applicationProperties_.getUserSettings()) {
        settings->setValue("windowState", getWindowStateAsString());
        settings->saveIfNeeded();
    }
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

}  // namespace dawhermes::app
