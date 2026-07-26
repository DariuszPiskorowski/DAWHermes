#pragma once

namespace dawhermes::core {

struct IntRect {
    int x { 0 };
    int y { 0 };
    int width { 0 };
    int height { 0 };
};

struct MainLayoutGeometry {
    IntRect menuBar;
    IntRect transportBar;
    IntRect tracksHeader;
    IntRect trackList;
    IntRect timeline;
    IntRect aiAssistant;
    IntRect midiEditor;
    IntRect statusBar;
};

MainLayoutGeometry computeMainLayoutGeometry(int width, int height);

}  // namespace dawhermes::core
