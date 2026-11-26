/*
 * MoreXournal
 *
 * Pure geometry, hit-testing, sizing and palette rules for canvas selection and shape handles (no GTK).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>  // for clamp, max, min
#include <array>      // for array
#include <cmath>      // for abs, hypot
#include <cstddef>    // for size_t
#include <limits>     // for numeric_limits
#include <optional>   // for optional

#include "control/tools/CursorSelectionType.h"  // for CursorSelectionType
#include "util/Color.h"                         // for Color

namespace xoj::handles {

enum class DeviceKind {
    Fine,   ///< Mouse, touchpad, trackpoint, cursor
    Pen,    ///< Stylus pen or stylus eraser
    Touch,  ///< Touchscreen finger
};

/// Minimum drawn box size on screen, in logical pixels
constexpr double MIN_BOX_PX = 40.0;

/// Minimum drawn side length on screen to display side handles (and edge bands) on that axis
constexpr double SIDE_HANDLE_MIN_PX = 64.0;

/// Below this screen size on BOTH axes, show only the four corner handles pushed outward
constexpr double TINY_BOTH_MAX_PX = 28.0;

/// Distance each corner handle is pushed outward from the 40 px box when both sides are < 28 px
constexpr double TINY_CORNER_PUSH_PX = 8.0;

/// Distance from the right edge of a selection box to the rotation handle centre (screen px)
constexpr double ROTATE_STEM_DEFAULT_PX = 20.0;
constexpr double ROTATE_STEM_TOUCH_PX = 24.0;

/// Distance from the top of a shape to its rotation handle centre (screen px)
constexpr double SHAPE_ROTATE_DISTANCE_PX = 24.0;

/// Default selection colour (app accent #3563e9) and its dark-paper counterpart (#86a2ff)
constexpr Color ACCENT_LIGHT_PAPER{0xff3563e9U};
constexpr Color ACCENT_DARK_PAPER{0xff86a2ffU};

/// Hit target radius in screen px (diameters: mouse 24, pen 32, touch 44)
constexpr double hitRadiusPx(DeviceKind dev) {
    switch (dev) {
        case DeviceKind::Fine:
            return 12.0;
        case DeviceKind::Pen:
            return 16.0;
        case DeviceKind::Touch:
            return 22.0;
    }
    return 12.0;
}

/// Half-width of the resize edge bands in screen px
constexpr double edgeBandHalfWidthPx(DeviceKind dev) {
    switch (dev) {
        case DeviceKind::Fine:
            return 6.0;
        case DeviceKind::Pen:
            return 8.0;
        case DeviceKind::Touch:
            return 12.0;
    }
    return 6.0;
}

/// Visual handle diameter in screen px (12 px normally, 16 px after a touchscreen press)
constexpr double handleVisualDiameterPx(DeviceKind lastPressDevice) {
    return lastPressDevice == DeviceKind::Touch ? 16.0 : 12.0;
}

/// Extra diameter added when a mouse or pen hovers over a handle
constexpr double hoverDiameterBonusPx() { return 2.0; }

/// Whether hovering with `dev` should show handle hover feedback
constexpr bool showsHoverFeedback(DeviceKind dev) { return dev != DeviceKind::Touch; }

/// Rotation stem distance from the right edge of a selection box (screen px)
constexpr double rotateStemPx(DeviceKind lastPressDevice) {
    return lastPressDevice == DeviceKind::Touch ? ROTATE_STEM_TOUCH_PX : ROTATE_STEM_DEFAULT_PX;
}

/// Tap-to-select object hit radius in screen px (divide by zoom for page coordinates)
constexpr double objectSelectRadiusPx(DeviceKind dev) {
    switch (dev) {
        case DeviceKind::Fine:
            return 6.0;
        case DeviceKind::Pen:
            return 10.0;
        case DeviceKind::Touch:
            return 20.0;
    }
    return 6.0;
}

/// Play-object hit radius in screen px (keeps the historical 3x ratio to object selection)
constexpr double playObjectRadiusPx(DeviceKind dev) { return 3.0 * objectSelectRadiusPx(dev); }

struct Rgba {
    double r{};
    double g{};
    double b{};
    double a{1.0};
};

struct HandlePalette {
    Rgba accent;       ///< Ring of normal handles, border, stem, fill of active/rotation handles
    Rgba fill;         ///< Fill of normal handles, ring of active/rotation handles
    Rgba hoverFill;    ///< Fill of hovered scale handles
    Rgba halo;         ///< 1 px outer halo around every handle
    Rgba shadow;       ///< Soft drop shadow colour (0 1px 2px)
    Rgba outerBorder;  ///< 1 px outer contrast line around the selection border
};

constexpr Rgba rgbaFromColor(Color c, double alpha = 1.0) {
    return {c.red / 255.0, c.green / 255.0, c.blue / 255.0, alpha};
}

/// Handle colours follow the paper's lightness, not the application chrome theme
constexpr HandlePalette handlePaletteFor(Color selectionColor, bool darkPaper) {
    if (darkPaper) {
        return {
                rgbaFromColor(ACCENT_DARK_PAPER, 1.0),            // #86a2ff
                {42.0 / 255.0, 45.0 / 255.0, 53.0 / 255.0, 1.0},  // #2a2d35
                {56.0 / 255.0, 65.0 / 255.0, 89.0 / 255.0, 1.0},  // subtle tint over #2a2d35
                {0.0, 0.0, 0.0, 0.55},                            // rgba(0, 0, 0, .55)
                {0.0, 0.0, 0.0, 0.45},                            // shadow on dark paper
                {42.0 / 255.0, 45.0 / 255.0, 53.0 / 255.0, 0.9},  // dark outer contrast line
        };
    }
    return {
            rgbaFromColor(selectionColor, 1.0),
            {1.0, 1.0, 1.0, 1.0},                                // #ffffff
            {227.0 / 255.0, 234.0 / 255.0, 255.0 / 255.0, 1.0},  // #e3eaff
            {16.0 / 255.0, 20.0 / 255.0, 32.0 / 255.0, 0.22},    // rgba(16, 20, 32, .22)
            {20.0 / 255.0, 24.0 / 255.0, 40.0 / 255.0, 0.25},    // rgba(20, 24, 40, .25)
            {1.0, 1.0, 1.0, 1.0},                                // 1 px white outer line
    };
}

struct HandlePoint {
    CursorSelectionType type{CURSOR_SELECTION_NONE};
    double x{};
    double y{};
};

/// Screen-space layout of a rectangular EditSelection in its unrotated local frame
struct SelectionLayout {
    double boxMinX{};
    double boxMinY{};
    double boxMaxX{};
    double boxMaxY{};

    /// Logical edges preserving flip when width or height is negative during mirror scaling
    double leftX{};
    double rightX{};
    double topY{};
    double bottomY{};

    bool showTopBottomSides{};
    bool showLeftRightSides{};
    bool showRotate{};
    bool tinyBoth{};

    double rotateAnchorX{};
    double rotateAnchorY{};
    double rotateX{};
    double rotateY{};

    std::array<HandlePoint, 9> handles{};
    size_t handleCount{0};
};

/// Compute the drawn box and visible handle positions in screen px (unrotated selection frame)
inline SelectionLayout computeSelectionLayout(double x1, double y1, double x2, double y2,
                                              DeviceKind lastPressDevice = DeviceKind::Fine) {
    SelectionLayout out{};

    const double rawMinX = std::min(x1, x2);
    const double rawMaxX = std::max(x1, x2);
    const double rawMinY = std::min(y1, y2);
    const double rawMaxY = std::max(y1, y2);
    const double rawW = rawMaxX - rawMinX;
    const double rawH = rawMaxY - rawMinY;
    const double cx = (rawMinX + rawMaxX) * 0.5;
    const double cy = (rawMinY + rawMaxY) * 0.5;

    const double drawW = std::max(rawW, MIN_BOX_PX);
    const double drawH = std::max(rawH, MIN_BOX_PX);

    out.boxMinX = cx - drawW * 0.5;
    out.boxMaxX = cx + drawW * 0.5;
    out.boxMinY = cy - drawH * 0.5;
    out.boxMaxY = cy + drawH * 0.5;

    const bool flippedX = x1 > x2;
    const bool flippedY = y1 > y2;
    out.leftX = flippedX ? out.boxMaxX : out.boxMinX;
    out.rightX = flippedX ? out.boxMinX : out.boxMaxX;
    out.topY = flippedY ? out.boxMaxY : out.boxMinY;
    out.bottomY = flippedY ? out.boxMinY : out.boxMaxY;

    out.tinyBoth = (rawW < TINY_BOTH_MAX_PX && rawH < TINY_BOTH_MAX_PX);
    out.showTopBottomSides = !out.tinyBoth && (rawW >= SIDE_HANDLE_MIN_PX);
    out.showLeftRightSides = !out.tinyBoth && (rawH >= SIDE_HANDLE_MIN_PX);
    out.showRotate = !out.tinyBoth;

    const double pushX = out.tinyBoth ? (flippedX ? TINY_CORNER_PUSH_PX : -TINY_CORNER_PUSH_PX) : 0.0;
    const double pushY = out.tinyBoth ? (flippedY ? TINY_CORNER_PUSH_PX : -TINY_CORNER_PUSH_PX) : 0.0;

    auto addHandle = [&](CursorSelectionType type, double hx, double hy) {
        out.handles[out.handleCount++] = {type, hx, hy};
    };

    // Corners (pushed outward when both sides are < 28 px)
    addHandle(CURSOR_SELECTION_TOP_LEFT, out.leftX + pushX, out.topY + pushY);
    addHandle(CURSOR_SELECTION_TOP_RIGHT, out.rightX - pushX, out.topY + pushY);
    addHandle(CURSOR_SELECTION_BOTTOM_LEFT, out.leftX + pushX, out.bottomY - pushY);
    addHandle(CURSOR_SELECTION_BOTTOM_RIGHT, out.rightX - pushX, out.bottomY - pushY);

    if (out.showTopBottomSides) {
        addHandle(CURSOR_SELECTION_TOP, cx, out.topY);
        addHandle(CURSOR_SELECTION_BOTTOM, cx, out.bottomY);
    }
    if (out.showLeftRightSides) {
        addHandle(CURSOR_SELECTION_LEFT, out.leftX, cy);
        addHandle(CURSOR_SELECTION_RIGHT, out.rightX, cy);
    }

    if (out.showRotate) {
        out.rotateAnchorX = out.boxMaxX;
        out.rotateAnchorY = cy;
        out.rotateX = out.boxMaxX + rotateStemPx(lastPressDevice);
        out.rotateY = cy;
        addHandle(CURSOR_SELECTION_ROTATE, out.rotateX, out.rotateY);
    }

    return out;
}

struct HitResult {
    CursorSelectionType type{CURSOR_SELECTION_NONE};
    /// Which handle dot was directly hit (CURSOR_SELECTION_NONE for edge bands or body)
    CursorSelectionType handle{CURSOR_SELECTION_NONE};
};

inline double distanceToSegment2D(double px, double py, double ax, double ay, double bx, double by) {
    const double abx = bx - ax;
    const double aby = by - ay;
    const double len2 = abx * abx + aby * aby;
    if (len2 <= 0.0) {
        return std::hypot(px - ax, py - ay);
    }
    const double t = std::clamp(((px - ax) * abx + (py - ay) * aby) / len2, 0.0, 1.0);
    return std::hypot(px - (ax + abx * t), py - (ay + aby * t));
}

/// Whether a selection should answer canvas presses/hovers to grab handles or move.
/// Transient selections on lines do not grab so consecutive lines can start at the previous endpoint.
constexpr bool selectionAllowsGrab(bool transient, bool isLine) { return !(transient && isLine); }

/**
 * Resolve which part of a selection (px, py) hits in screen px.
 *
 * Priority:
 *  1. Visible handles (closest handle centre within hitRadiusPx wins).
 *  2. Rotation stem (closes any gap between the right edge and the rotate handle when the right-middle dot is hidden).
 *  3. Edge bands on axes with side handles (skipped for transient selections, which draw no border).
 *  4. Selection body (normal selections: whole drawn box interior; transient selections: only when nearStroke is true).
 *  5. None (page content).
 */
inline HitResult hitTestSelection(const SelectionLayout& layout, double px, double py, DeviceKind device,
                                  bool transient = false, bool nearStroke = false, bool isLine = false) {
    if (!selectionAllowsGrab(transient, isLine)) {
        return {};
    }
    const double radius = hitRadiusPx(device);
    const double edgeHalf = edgeBandHalfWidthPx(device);

    // 1. Visible handles: closest centre within hit radius wins
    CursorSelectionType bestHandle = CURSOR_SELECTION_NONE;
    double bestDist = radius;
    for (size_t i = 0; i < layout.handleCount; i++) {
        const auto& h = layout.handles[i];
        const double d = std::hypot(px - h.x, py - h.y);
        if (d <= bestDist) {
            bestDist = d;
            bestHandle = h.type;
        }
    }
    if (bestHandle != CURSOR_SELECTION_NONE) {
        return {bestHandle, bestHandle};
    }

    // 2. Rotation stem: ensures zero dead gap between the right edge and the rotation hit area
    if (layout.showRotate) {
        const double stemDist =
                distanceToSegment2D(px, py, layout.rotateAnchorX, layout.rotateAnchorY, layout.rotateX, layout.rotateY);
        if (stemDist <= edgeHalf) {
            return {CURSOR_SELECTION_ROTATE, CURSOR_SELECTION_ROTATE};
        }
    }

    // 3. Edge bands (only on non-transient selections and axes where side handles are shown)
    if (!transient) {
        CursorSelectionType bestEdge = CURSOR_SELECTION_NONE;
        double bestEdgeDist = edgeHalf;

        if (layout.showTopBottomSides && px >= layout.boxMinX && px <= layout.boxMaxX) {
            const double dTop = std::abs(py - layout.topY);
            if (dTop <= bestEdgeDist) {
                bestEdgeDist = dTop;
                bestEdge = CURSOR_SELECTION_TOP;
            }
            const double dBot = std::abs(py - layout.bottomY);
            if (dBot <= bestEdgeDist) {
                bestEdgeDist = dBot;
                bestEdge = CURSOR_SELECTION_BOTTOM;
            }
        }
        if (layout.showLeftRightSides && py >= layout.boxMinY && py <= layout.boxMaxY) {
            const double dLeft = std::abs(px - layout.leftX);
            if (dLeft <= bestEdgeDist) {
                bestEdgeDist = dLeft;
                bestEdge = CURSOR_SELECTION_LEFT;
            }
            const double dRight = std::abs(px - layout.rightX);
            if (dRight <= bestEdgeDist) {
                bestEdgeDist = dRight;
                bestEdge = CURSOR_SELECTION_RIGHT;
            }
        }
        if (bestEdge != CURSOR_SELECTION_NONE) {
            return {bestEdge, CURSOR_SELECTION_NONE};
        }
    }

    // 4. Selection body (move)
    if (transient) {
        if (nearStroke) {
            return {CURSOR_SELECTION_MOVE, CURSOR_SELECTION_NONE};
        }
        return {};
    }

    if (px >= layout.boxMinX && px <= layout.boxMaxX && py >= layout.boxMinY && py <= layout.boxMaxY) {
        return {CURSOR_SELECTION_MOVE, CURSOR_SELECTION_NONE};
    }

    return {};
}

/// Shape handle hit test with closest-centre priority and zero-gap rotation stem check (all coordinates in screen px)
struct ShapeHandleTarget {
    double x{};
    double y{};
    double anchorX{};
    double anchorY{};
    bool isRotate{false};
};

inline std::optional<size_t> hitTestShapeHandles(const ShapeHandleTarget* handles, size_t count, double px, double py,
                                                 DeviceKind device, bool transient = false, bool isLine = false) {
    if (!selectionAllowsGrab(transient, isLine)) {
        return std::nullopt;
    }
    const double radius = hitRadiusPx(device);
    std::optional<size_t> best;
    double bestDist = radius;

    for (size_t i = 0; i < count; i++) {
        const double d = std::hypot(px - handles[i].x, py - handles[i].y);
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    if (best) {
        return best;
    }

    // Check rotation stem so there is no dead gap between the shape and the rotate handle
    const double stemHalf = edgeBandHalfWidthPx(device);
    for (size_t i = 0; i < count; i++) {
        if (handles[i].isRotate) {
            if (distanceToSegment2D(px, py, handles[i].anchorX, handles[i].anchorY, handles[i].x, handles[i].y) <=
                stemHalf) {
                return i;
            }
        }
    }
    return std::nullopt;
}

}  // namespace xoj::handles
