#include "app/AppLogger.h"

#include <memory>

namespace dawhermes::app {

namespace {
std::unique_ptr<juce::FileLogger> gLogger;

juce::File getLocalAppDataRoot()
{
    const auto localAppData = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (localAppData.isNotEmpty()) {
        return juce::File(localAppData).getChildFile("DAWHermes");
    }

    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DAWHermes");
}
}

juce::File AppLogger::getLogDirectory()
{
    return getLocalAppDataRoot().getChildFile("logs");
}

juce::File AppLogger::getLogFile()
{
    return getLogDirectory().getChildFile("DAWHermes.log");
}

void AppLogger::initialise(const juce::String& appVersion)
{
    const auto logDirectory = getLogDirectory();
    if (!logDirectory.exists()) {
        logDirectory.createDirectory();
    }

    gLogger = std::make_unique<juce::FileLogger>(
        getLogFile(),
        "DAWHermes logging started",
        1 * 1024 * 1024);

    juce::Logger::setCurrentLogger(gLogger.get());
    log("Application start");
    log("Application version: " + appVersion);
}

void AppLogger::shutdown()
{
    log("Application shutdown");
    juce::Logger::setCurrentLogger(nullptr);
    gLogger.reset();
}

void AppLogger::log(const juce::String& message)
{
    juce::Logger::writeToLog("[DAWHermes] " + message);
}

}  // namespace dawhermes::app
