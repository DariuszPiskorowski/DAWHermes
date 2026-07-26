#include "core/MainLayoutGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace dawhermes::core {

namespace {

constexpr int kOuterPadding = 6;
constexpr int kTransportPadding = 4;
constexpr int kPanelPadding = 4;
constexpr int kMenuBarHeight = 28;
constexpr int kTransportHeight = 40;
constexpr int kStatusHeight = 24;
constexpr int kTracksHeaderHeight = 28;
constexpr int kSplitterThickness = 8;

constexpr int kMinLeftPanelWidth = 200;
constexpr int kMinCenterPanelWidth = 280;
constexpr int kMinRightPanelWidth = 240;
constexpr int kMinTopRowHeight = 140;
constexpr int kMinBottomRowHeight = 80;

constexpr double kMinLeftRatio = 0.12;
constexpr double kMaxLeftRatio = 0.55;
constexpr double kMinRightRatio = 0.12;
constexpr double kMaxRightRatio = 0.55;
constexpr double kMinCenterRatio = 0.20;
constexpr double kMinTopRatio = 0.35;
constexpr double kMaxTopRatio = 0.85;

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

double clampFinite(double value, double lower, double upper)
{
    if (!std::isfinite(value)) {
        return lower;
    }

    return std::clamp(value, lower, upper);
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

std::array<int, 2> splitRows(int usableHeight, double topRatio)
{
    if (usableHeight <= 0) {
        return { 0, 0 };
    }

    int top = static_cast<int>(std::lround(static_cast<double>(usableHeight) * topRatio));
    top = std::clamp(top, 0, usableHeight);
    int bottom = usableHeight - top;

    const auto minRequired = kMinTopRowHeight + kMinBottomRowHeight;
    if (usableHeight >= minRequired) {
        if (top < kMinTopRowHeight) {
            top = kMinTopRowHeight;
            bottom = usableHeight - top;
        }

        if (bottom < kMinBottomRowHeight) {
            bottom = kMinBottomRowHeight;
            top = usableHeight - bottom;
        }
    }

    return { std::max(0, top), std::max(0, bottom) };
}

std::array<int, 3> splitColumns(int usableWidth, double leftRatio, double rightRatio)
{
    if (usableWidth <= 0) {
        return { 0, 0, 0 };
    }

    int left = static_cast<int>(std::lround(static_cast<double>(usableWidth) * leftRatio));
    left = std::clamp(left, 0, usableWidth);

    int right = static_cast<int>(std::lround(static_cast<double>(usableWidth) * rightRatio));
    right = std::clamp(right, 0, std::max(0, usableWidth - left));

    int center = usableWidth - left - right;

    const auto minRequired = kMinLeftPanelWidth + kMinCenterPanelWidth + kMinRightPanelWidth;
    if (usableWidth < minRequired) {
        return { std::max(0, left), std::max(0, center), std::max(0, right) };
    }

    if (left < kMinLeftPanelWidth) {
        const auto delta = kMinLeftPanelWidth - left;
        left += delta;
        center -= delta;
    }

    if (right < kMinRightPanelWidth) {
        const auto delta = kMinRightPanelWidth - right;
        right += delta;
        center -= delta;
    }

    if (center < kMinCenterPanelWidth) {
        auto missing = kMinCenterPanelWidth - center;

        const auto leftSurplus = std::max(0, left - kMinLeftPanelWidth);
        const auto takeLeft = std::min(leftSurplus, missing);
        left -= takeLeft;
        center += takeLeft;
        missing -= takeLeft;

        const auto rightSurplus = std::max(0, right - kMinRightPanelWidth);
        const auto takeRight = std::min(rightSurplus, missing);
        right -= takeRight;
        center += takeRight;
    }

    center = std::max(0, usableWidth - left - right);
    return { std::max(0, left), std::max(0, center), std::max(0, right) };
}

MainPanelLayoutState sanitizedStateInternal(const MainPanelLayoutState& state)
{
    MainPanelLayoutState output;
    output.leftColumnRatio = clampFinite(state.leftColumnRatio, kMinLeftRatio, kMaxLeftRatio);
    output.rightColumnRatio = clampFinite(state.rightColumnRatio, kMinRightRatio, kMaxRightRatio);

    const auto maxCombined = 1.0 - kMinCenterRatio;
    const auto combined = output.leftColumnRatio + output.rightColumnRatio;
    if (combined > maxCombined) {
        if (combined <= 0.0) {
            output.leftColumnRatio = maxCombined / 2.0;
            output.rightColumnRatio = maxCombined / 2.0;
        } else {
            const auto scale = maxCombined / combined;
            output.leftColumnRatio *= scale;
            output.rightColumnRatio *= scale;
        }
    }

    output.topRowRatio = clampFinite(state.topRowRatio, kMinTopRatio, kMaxTopRatio);
    return output;
}

bool parseDouble(const std::string& token, double& value)
{
    std::stringstream parser(token);
    parser >> value;
    return !parser.fail() && parser.eof();
}

}  // namespace

MainPanelLayoutState defaultMainPanelLayoutState()
{
    return MainPanelLayoutState {};
}

MainPanelLayoutState sanitizeMainPanelLayoutState(const MainPanelLayoutState& state)
{
    return sanitizedStateInternal(state);
}

std::string serializeMainPanelLayoutState(const MainPanelLayoutState& state)
{
    const auto normalized = sanitizedStateInternal(state);
    std::ostringstream builder;
    builder.setf(std::ios::fixed);
    builder.precision(6);
    builder << normalized.leftColumnRatio << ',' << normalized.rightColumnRatio << ','
            << normalized.topRowRatio;
    return builder.str();
}

bool deserializeMainPanelLayoutState(const std::string& serialized, MainPanelLayoutState& state)
{
    std::stringstream stream(serialized);
    std::string leftToken;
    std::string rightToken;
    std::string topToken;

    if (!std::getline(stream, leftToken, ',')) {
        return false;
    }

    if (!std::getline(stream, rightToken, ',')) {
        return false;
    }

    if (!std::getline(stream, topToken, ',')) {
        return false;
    }

    if (stream.rdbuf()->in_avail() != 0) {
        return false;
    }

    MainPanelLayoutState parsed;
    if (!parseDouble(leftToken, parsed.leftColumnRatio)
        || !parseDouble(rightToken, parsed.rightColumnRatio)
        || !parseDouble(topToken, parsed.topRowRatio)) {
        return false;
    }

    state = sanitizedStateInternal(parsed);
    return true;
}

MainLayoutGeometry computeMainLayoutGeometry(int width, int height, const MainPanelLayoutState& state)
{
    const auto normalizedState = sanitizedStateInternal(state);

    MutableRect area { 0, 0, clampNonNegative(width), clampNonNegative(height) };
    area = reduced(area, kOuterPadding);

    MainLayoutGeometry geometry;

    geometry.menuBar = toIntRect(removeFromTop(area, kMenuBarHeight));

    auto transportRow = removeFromTop(area, kTransportHeight);
    transportRow = reduced(transportRow, kTransportPadding);
    geometry.transportBar = toIntRect(transportRow);

    geometry.statusBar = toIntRect(removeFromBottom(area, kStatusHeight));

    const auto rowHeights = splitRows(
        std::max(0, area.height - kSplitterThickness),
        normalizedState.topRowRatio);

    auto topRow = removeFromTop(area, rowHeights[0]);
    geometry.horizontalSplitter = toIntRect(removeFromTop(area, std::min(kSplitterThickness, area.height)));
    auto bottomRow = area;

    const auto columnWidths = splitColumns(
        std::max(0, topRow.width - (kSplitterThickness * 2)),
        normalizedState.leftColumnRatio,
        normalizedState.rightColumnRatio);

    auto leftColumn = removeFromLeft(topRow, columnWidths[0]);
    geometry.leftVerticalSplitter = toIntRect(removeFromLeft(topRow, std::min(kSplitterThickness, topRow.width)));
    auto centerColumn = removeFromLeft(topRow, columnWidths[1]);
    geometry.rightVerticalSplitter = toIntRect(removeFromLeft(topRow, std::min(kSplitterThickness, topRow.width)));
    auto rightColumn = topRow;

    leftColumn = reduced(leftColumn, kPanelPadding);
    centerColumn = reduced(centerColumn, kPanelPadding);
    rightColumn = reduced(rightColumn, kPanelPadding);
    bottomRow = reduced(bottomRow, kPanelPadding);

    auto tracksHeader = removeFromTop(leftColumn, kTracksHeaderHeight);
    geometry.tracksHeader = toIntRect(tracksHeader);
    geometry.trackList = toIntRect(leftColumn);

    geometry.timeline = toIntRect(centerColumn);
    geometry.aiAssistant = toIntRect(rightColumn);
    geometry.midiEditor = toIntRect(bottomRow);

    return geometry;
}

}  // namespace dawhermes::core
