#include "core/MainLayoutGeometry.h"

#include <algorithm>

namespace dawhermes::core {

namespace {

struct MutableRect {
    int x;
    int y;
    int width;
    int height;
};

int clampNonNegative(int value)
{
    return std::max(value, 0);
}

MutableRect reduced(const MutableRect& rect, int amount)
{
    const auto twice = std::max(0, amount * 2);
    const auto width = std::max(0, rect.width - twice);
    const auto height = std::max(0, rect.height - twice);
    return MutableRect { rect.x + amount, rect.y + amount, width, height };
}

MutableRect removeFromTop(MutableRect& rect, int amount)
{
    const auto take = std::clamp(amount, 0, rect.height);
    MutableRect top { rect.x, rect.y, rect.width, take };
    rect.y += take;
    rect.height -= take;
    return top;
}

MutableRect removeFromBottom(MutableRect& rect, int amount)
{
    const auto take = std::clamp(amount, 0, rect.height);
    MutableRect bottom { rect.x, rect.y + rect.height - take, rect.width, take };
    rect.height -= take;
    return bottom;
}

MutableRect removeFromLeft(MutableRect& rect, int amount)
{
    const auto take = std::clamp(amount, 0, rect.width);
    MutableRect left { rect.x, rect.y, take, rect.height };
    rect.x += take;
    rect.width -= take;
    return left;
}

MutableRect removeFromRight(MutableRect& rect, int amount)
{
    const auto take = std::clamp(amount, 0, rect.width);
    MutableRect right { rect.x + rect.width - take, rect.y, take, rect.height };
    rect.width -= take;
    return right;
}

IntRect toIntRect(const MutableRect& rect)
{
    return IntRect { rect.x, rect.y, clampNonNegative(rect.width), clampNonNegative(rect.height) };
}

}  // namespace

MainLayoutGeometry computeMainLayoutGeometry(int width, int height)
{
    MutableRect area { 0, 0, clampNonNegative(width), clampNonNegative(height) };
    area = reduced(area, 6);

    MainLayoutGeometry geometry;

    geometry.menuBar = toIntRect(removeFromTop(area, 28));

    auto transportRow = removeFromTop(area, 40);
    transportRow = reduced(transportRow, 4);
    geometry.transportBar = toIntRect(transportRow);

    geometry.statusBar = toIntRect(removeFromBottom(area, 24));

    const auto rowGap = 8;
    const auto topRowTarget = std::max(140, (area.height * 62) / 100);
    const auto topRowHeight = std::min(topRowTarget, std::max(0, area.height - rowGap - 80));
    auto topRow = removeFromTop(area, topRowHeight);
    if (area.height > 0) {
        removeFromTop(area, std::min(rowGap, area.height));
    }
    auto bottomRow = area;

    const auto columnGap = 8;
    const auto totalGap = columnGap * 2;
    const auto usableWidth = std::max(0, topRow.width - totalGap);

    int leftWidth = (usableWidth * 22) / 100;
    int rightWidth = (usableWidth * 24) / 100;
    leftWidth = std::clamp(leftWidth, 200, std::max(200, usableWidth));
    rightWidth = std::clamp(rightWidth, 240, std::max(240, usableWidth));

    if (leftWidth + rightWidth > usableWidth) {
        leftWidth = std::max(0, usableWidth / 2);
        rightWidth = usableWidth - leftWidth;
    }

    auto leftColumn = removeFromLeft(topRow, leftWidth);
    if (topRow.width > 0) {
        removeFromLeft(topRow, std::min(columnGap, topRow.width));
    }

    auto rightColumn = removeFromRight(topRow, rightWidth);
    if (topRow.width > 0) {
        removeFromRight(topRow, std::min(columnGap, topRow.width));
    }

    auto centerColumn = topRow;

    leftColumn = reduced(leftColumn, 4);
    centerColumn = reduced(centerColumn, 4);
    rightColumn = reduced(rightColumn, 4);
    bottomRow = reduced(bottomRow, 4);

    auto tracksHeader = removeFromTop(leftColumn, 28);
    geometry.tracksHeader = toIntRect(tracksHeader);
    geometry.trackList = toIntRect(leftColumn);

    geometry.timeline = toIntRect(centerColumn);
    geometry.aiAssistant = toIntRect(rightColumn);
    geometry.midiEditor = toIntRect(bottomRow);

    return geometry;
}

}  // namespace dawhermes::core
