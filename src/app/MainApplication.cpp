#include "app/MainApplication.h"

#include <exception>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppLogger.h"
#include "app/MainWindow.h"
#include "hermes/StubHermesEngine.h"

namespace dawhermes::app {

MainApplication::~MainApplication() = default;

const juce::String MainApplication::getApplicationName()
{
    return "DAWHermes";
}

const juce::String MainApplication::getApplicationVersion()
{
    return "0.1.0";
}

bool MainApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void MainApplication::initialise(const juce::String&)
{
    juce::PropertiesFile::Options options;
    options.applicationName = getApplicationName();
    options.filenameSuffix = "settings";
    options.folderName = "DAWHermes";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    appProperties_.setStorageParameters(options);

    AppLogger::initialise(getApplicationVersion());

    try {
        hermesEngine_ = std::make_unique<hermes::StubHermesEngine>();
        mainWindow_ = std::make_unique<MainWindow>(
            getApplicationName(),
            appProperties_,
            *hermesEngine_);
    } catch (const std::exception& ex) {
        AppLogger::log("Window creation failure: " + juce::String(ex.what()));
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::WarningIcon,
            "DAWHermes",
            "Failed to create main window.");
        quit();
    } catch (...) {
        AppLogger::log("Window creation failure: unknown exception");
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::WarningIcon,
            "DAWHermes",
            "Failed to create main window.");
        quit();
    }
}

void MainApplication::shutdown()
{
    mainWindow_.reset();
    hermesEngine_.reset();

    AppLogger::shutdown();
}

void MainApplication::systemRequestedQuit()
{
    quit();
}

void MainApplication::anotherInstanceStarted(const juce::String&)
{
}

}  // namespace dawhermes::app
