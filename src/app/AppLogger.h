#pragma once

#include <juce_core/juce_core.h>

namespace dawhermes::app {

class AppLogger {
public:
    static void initialise(const juce::String& appVersion);
    static void shutdown();
    static void log(const juce::String& message);
    static juce::File getLogFile();

private:
    static juce::File getLogDirectory();
};

}  // namespace dawhermes::app
