#pragma once

#include <string>

namespace dawhermes::core {

struct IntRect {
    int x { 0 };
    int y { 0 };
    int width { 0 };
    int height { 0 };
};

struct MainPanelLayoutState {
    double leftColumnRatio { 0.22 };
    double rightColumnRatio { 0.24 };
    double topRowRatio { 0.62 };
};

struct MainLayoutGeometry {
    IntRect menuBar;
    IntRect transportBar;
    IntRect tracksHeader;
    IntRect trackList;
    IntRect leftVerticalSplitter;
    IntRect timeline;
    IntRect rightVerticalSplitter;
    IntRect aiAssistant;
    IntRect horizontalSplitter;
    IntRect midiEditor;
    IntRect statusBar;
};

MainPanelLayoutState defaultMainPanelLayoutState();
MainPanelLayoutState sanitizeMainPanelLayoutState(const MainPanelLayoutState& state);
std::string serializeMainPanelLayoutState(const MainPanelLayoutState& state);
bool deserializeMainPanelLayoutState(const std::string& serialized, MainPanelLayoutState& state);

MainLayoutGeometry computeMainLayoutGeometry(
    int width,
    int height,
    const MainPanelLayoutState& state = defaultMainPanelLayoutState());

}  // namespace dawhermes::core
