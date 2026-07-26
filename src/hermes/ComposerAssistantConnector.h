#pragma once

#include <string>

namespace dawhermes::hermes {

struct ComposerAssistantSettings {
    bool enabled { false };
    std::string host { "100.126.75.32" };
    int port { 3456 };
    int timeoutMs { 800 };
    bool requireLoopbackHost { false };
};

struct ComposerAssistantProbeResult {
    bool ok { false };
    std::string message;
};

class ComposerAssistantConnector {
public:
    ComposerAssistantSettings defaultSettings() const;
    ComposerAssistantProbeResult validateSettings(const ComposerAssistantSettings& settings) const;
    ComposerAssistantProbeResult probe(const ComposerAssistantSettings& settings) const;
};

}  // namespace dawhermes::hermes
