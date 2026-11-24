/*
 * MoreXournal
 *
 * Pure geometry, palette and visibility logic for the canvas eraser indicator
 * and pen/highlighter dot cursor.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <cmath>

#include "control/ToolEnums.h"
#include "control/settings/RecolorParameters.h"
#include "control/settings/SettingsEnums.h"
#include "control/tools/CursorSelectionType.h"
#include "gui/inputdevices/InputEvents.h"
#include "util/Color.h"

namespace xoj::cursor {

constexpr double MIN_VISIBLE_SIZE_PX = 12.0;
constexpr double CORNER_BRACKET_BOX_PX = 12.0;
constexpr double CORNER_BRACKET_LEG_PX = 4.0;
constexpr double ROUNDED_SQUARE_RADIUS_PX = 2.0;
constexpr double STROKE_WIDTH_HOVER_PX = 1.5;
constexpr double STROKE_WIDTH_PRESSED_PX = 2.0;
constexpr double HALO_OUTER_PX = 1.0;
constexpr double WHITEOUT_DASH_ON_PX = 3.0;
constexpr double WHITEOUT_DASH_OFF_PX = 2.0;

constexpr double PEN_DOT_MIN_DIAMETER_PX = 4.0;
constexpr double PEN_DOT_MAX_DIAMETER_PX = 90.0;
constexpr double PEN_DOT_HALO_PX = 1.0;

enum class EraserIndicatorShape {
    RoundedSquare,  ///< Standard and delete-stroke modes (matches EraseHandler's PaddedBox)
    Circle,         ///< Whiteout mode (matches StrokeHandler's round-cap whiteout stroke)
};

struct EraserIndicatorGeometry {
    EraserIndicatorShape shape = EraserIndicatorShape::RoundedSquare;
    double trueSizePx = 0.0;          ///< Square side length or circle diameter on screen in logical px
    double cornerRadiusPx = 0.0;      ///< Corner radius for RoundedSquare; 0 for Circle
    bool showCornerBrackets = false;  ///< True when trueSizePx < MIN_VISIBLE_SIZE_PX
    double bracketBoxSizePx = 0.0;    ///< CORNER_BRACKET_BOX_PX when showCornerBrackets is true
    double bracketLegPx = 0.0;        ///< CORNER_BRACKET_LEG_PX when showCornerBrackets is true
    double strokeWidthPx = STROKE_WIDTH_HOVER_PX;
    double paintExtentHalfPx = 0.0;  ///< Half-extent around the pointer needed to paint and invalidate cleanly
};

/**
 * Compute the on-screen eraser indicator geometry matching the true erased area:
 * - Standard and delete-stroke modes erase a square of half-size = thickness (side = 2 * thickness * zoom).
 * - Whiteout mode paints a round-capped stroke of width = thickness (circle of diameter = thickness * zoom).
 * - Whenever trueSizePx < 12 px, 4 px corner brackets on a 12 px box are added so the indicator stays visible.
 */
constexpr auto computeEraserIndicatorGeometry(EraserType mode, double thickness, double zoom, bool pressed)
        -> EraserIndicatorGeometry {
    const double safeThickness = std::max(0.0, thickness);
    const double safeZoom = std::max(0.0, zoom);

    EraserIndicatorGeometry geom{};
    geom.strokeWidthPx = pressed ? STROKE_WIDTH_PRESSED_PX : STROKE_WIDTH_HOVER_PX;

    if (mode == ERASER_TYPE_WHITEOUT) {
        geom.shape = EraserIndicatorShape::Circle;
        geom.trueSizePx = safeThickness * safeZoom;
        geom.cornerRadiusPx = 0.0;
    } else {
        geom.shape = EraserIndicatorShape::RoundedSquare;
        geom.trueSizePx = 2.0 * safeThickness * safeZoom;
        geom.cornerRadiusPx = std::min(ROUNDED_SQUARE_RADIUS_PX, geom.trueSizePx * 0.25);
    }

    geom.showCornerBrackets = geom.trueSizePx < MIN_VISIBLE_SIZE_PX;
    if (geom.showCornerBrackets) {
        geom.bracketBoxSizePx = CORNER_BRACKET_BOX_PX;
        geom.bracketLegPx = CORNER_BRACKET_LEG_PX;
    }

    const double maxHalf = std::max(geom.trueSizePx, geom.bracketBoxSizePx) * 0.5;
    geom.paintExtentHalfPx = std::ceil(maxHalf + geom.strokeWidthPx * 0.5 + HALO_OUTER_PX + 2.0);
    return geom;
}

struct RgbaColor {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;
};

struct EraserIndicatorPalette {
    RgbaColor stroke{};        ///< Rounded square stroke (#1c1f26 @ 85% light, #eceef4 @ 85% dark)
    RgbaColor whiteoutRing{};  ///< Whiteout dashed ring (#1c1f26 light, #eceef4 dark)
    RgbaColor halo{};          ///< 1 px outer halo (white @ 90% light, black @ 60% dark)
    RgbaColor pressedFill{};   ///< Translucent fill while button/pen is down
    RgbaColor whiteoutFill{};  ///< 90% paper-matched fill for whiteout brush preview
    bool darkPage = false;
};

constexpr auto isDarkColor(Color color) -> bool {
    const double luma = (0.2126 * static_cast<double>(color.red) + 0.7152 * static_cast<double>(color.green) +
                         0.0722 * static_cast<double>(color.blue)) /
                        255.0;
    return luma < 0.5;
}

constexpr auto computeEraserIndicatorPalette(bool darkPage, Color whiteoutFillColor = Colors::white)
        -> EraserIndicatorPalette {
    EraserIndicatorPalette p{};
    p.darkPage = darkPage;
    p.whiteoutFill = {
            static_cast<double>(whiteoutFillColor.red) / 255.0,
            static_cast<double>(whiteoutFillColor.green) / 255.0,
            static_cast<double>(whiteoutFillColor.blue) / 255.0,
            0.90,
    };

    if (darkPage) {
        // #eceef4 stroke, rgba(0, 0, 0, 0.6) halo
        p.stroke = {236.0 / 255.0, 238.0 / 255.0, 244.0 / 255.0, 0.85};
        p.whiteoutRing = {236.0 / 255.0, 238.0 / 255.0, 244.0 / 255.0, 1.0};
        p.halo = {0.0, 0.0, 0.0, 0.60};
        p.pressedFill = {236.0 / 255.0, 238.0 / 255.0, 244.0 / 255.0, 0.08};
    } else {
        // #1c1f26 stroke at 85%, white halo at 90%, pressed fill rgba(28, 31, 38, 0.08)
        p.stroke = {28.0 / 255.0, 31.0 / 255.0, 38.0 / 255.0, 0.85};
        p.whiteoutRing = {28.0 / 255.0, 31.0 / 255.0, 38.0 / 255.0, 1.0};
        p.halo = {1.0, 1.0, 1.0, 0.90};
        p.pressedFill = {28.0 / 255.0, 31.0 / 255.0, 38.0 / 255.0, 0.08};
    }
    return p;
}

/// Effective displayed paper colour for a page: PDF pages render on white paper regardless of getBackgroundColor().
inline auto displayedPaperColor(bool isPdfPage, Color backgroundColor, const RecolorParameters& recolorParams)
        -> Color {
    const Color paper = isPdfPage ? Colors::white : backgroundColor;
    return recolorParams.recolorizeMainView ? Color(recolorParams.recolor.convertColor(paper)) : paper;
}

inline auto computeEraserIndicatorPaletteForPage(bool isPdfPage, Color backgroundColor,
                                                 const RecolorParameters& recolorParams) -> EraserIndicatorPalette {
    const Color displayedPaper = displayedPaperColor(isPdfPage, backgroundColor, recolorParams);
    // Whiteout strokes are always painted white, so preview them as white (recolored like the strokes are)
    const Color whiteoutFill =
            recolorParams.recolorizeMainView ? Color(recolorParams.recolor.convertColor(Colors::white)) : Colors::white;
    return computeEraserIndicatorPalette(isDarkColor(displayedPaper), whiteoutFill);
}

struct EraserOverlayInput {
    bool busy = false;
    bool insidePage = false;
    bool pointerInWidget = false;
    CursorSelectionType selectionType = CURSOR_SELECTION_NONE;
    ToolType toolType = TOOL_PEN;
    InputDeviceClass inputDevice = INPUT_DEVICE_MOUSE;
    bool pressed = false;
    EraserVisibility eraserVisibility = ERASER_VISIBILITY_ALWAYS;
};

constexpr auto shouldShowEraserOverlay(const EraserOverlayInput& in) -> bool {
    if (in.busy || in.selectionType != CURSOR_SELECTION_NONE || !in.insidePage || !in.pointerInWidget ||
        in.toolType != TOOL_ERASER) {
        return false;
    }

    // Finger (touchscreen) has no hover: show the indicator only while in contact
    if (in.inputDevice == INPUT_DEVICE_TOUCHSCREEN) {
        return in.pressed;
    }

    if (in.inputDevice == INPUT_DEVICE_PEN || in.inputDevice == INPUT_DEVICE_ERASER) {
        if (in.eraserVisibility == ERASER_VISIBILITY_NEVER ||
            (in.eraserVisibility == ERASER_VISIBILITY_HOVER && in.pressed) ||
            (in.eraserVisibility == ERASER_VISIBILITY_TOUCH && !in.pressed)) {
            return false;
        }
    }

    return true;
}

constexpr auto computePenDotDiameterPx(double thickness, double zoom) -> double {
    const double raw = std::max(0.0, thickness) * std::max(0.0, zoom);
    return std::clamp(raw, PEN_DOT_MIN_DIAMETER_PX, PEN_DOT_MAX_DIAMETER_PX);
}

}  // namespace xoj::cursor
