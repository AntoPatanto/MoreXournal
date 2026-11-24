/*
 * MoreXournal
 *
 * Which button of the top bar shows a tool as active
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "control/ToolEnums.h"  // for ToolType, DrawingType

namespace xoj::topbar {

/// The tool buttons in the centre of the top bar, in display order
enum class ToolGroup { PEN, HIGHLIGHTER, ERASER, SELECTION, SHAPES, TEXT, IMAGE, HAND, NONE };

/// True for the drawing types offered by the Shapes button
constexpr bool isShapeDrawingType(DrawingType dt) {
    switch (dt) {
        case DRAWING_TYPE_LINE:
        case DRAWING_TYPE_RECTANGLE:
        case DRAWING_TYPE_ELLIPSE:
        case DRAWING_TYPE_ARROW:
        case DRAWING_TYPE_DOUBLE_ARROW:
        case DRAWING_TYPE_COORDINATE_SYSTEM:
        case DRAWING_TYPE_SPLINE:
            return true;
        default:
            return false;
    }
}

/**
 * The button that shows the tool as active. Pen and Shapes are both the pen tool and differ by the drawing type.
 * Tools without a button (laser pointers, PDF text selection, links, TeX) give NONE.
 */
constexpr ToolGroup toolGroupFor(ToolType tool, DrawingType dt) {
    switch (tool) {
        case TOOL_PEN:
            return isShapeDrawingType(dt) ? ToolGroup::SHAPES : ToolGroup::PEN;
        case TOOL_HIGHLIGHTER:
            return ToolGroup::HIGHLIGHTER;
        case TOOL_ERASER:
            return ToolGroup::ERASER;
        case TOOL_SELECT_RECT:
        case TOOL_SELECT_REGION:
        case TOOL_SELECT_MULTILAYER_RECT:
        case TOOL_SELECT_MULTILAYER_REGION:
        case TOOL_SELECT_OBJECT:
        case TOOL_PLAY_OBJECT:
            return ToolGroup::SELECTION;
        case TOOL_DRAW_RECT:
        case TOOL_DRAW_ELLIPSE:
        case TOOL_DRAW_ARROW:
        case TOOL_DRAW_DOUBLE_ARROW:
        case TOOL_DRAW_COORDINATE_SYSTEM:
        case TOOL_DRAW_SPLINE:
            return ToolGroup::SHAPES;
        case TOOL_TEXT:
            return ToolGroup::TEXT;
        case TOOL_IMAGE:
            return ToolGroup::IMAGE;
        case TOOL_HAND:
            return ToolGroup::HAND;
        default:
            return ToolGroup::NONE;
    }
}

/// Responsive breakpoints for the top bar (window width in logical pixels)
constexpr int BREAKPOINT_OVERFLOW = 1000;
constexpr int BREAKPOINT_NO_TITLE = 860;
constexpr int BREAKPOINT_NARROW = 700;
constexpr int MIN_WINDOW_WIDTH = 600;

/// Which optional chrome pieces are shown directly in the top bar at a given window width.
/// The tool buttons, Undo and Redo never leave the bar.
struct BarLayout {
    bool showTitle = true;            ///< Document title + status beside the menu button (>= 860)
    bool showTitleInMenu = false;     ///< Document title + status header in the main menu (< 860)
    bool showPageArrows = true;       ///< Full `< 3 / 12 >` control (>= 860)
    bool showPageCompactBtn = false;  ///< Compact `3 / 12 v` popover button (700..859)
    bool showSidebarBtn = true;       ///< Sidebar toggle in the bar (>= 700)
    bool showNewPageBtn = true;       ///< New page button in the bar (>= 1000)
    bool showFullscreenBtn = true;    ///< Fullscreen toggle in the bar (>= 1000)
    bool showOverflowBtn = false;     ///< Overflow `...` menu button (< 1000)
    bool showSeparators = true;       ///< Vertical separators between groups (>= 700)

    bool operator==(const BarLayout&) const = default;
};

constexpr BarLayout barLayoutForWidth(int width) {
    BarLayout l{};
    if (width < BREAKPOINT_OVERFLOW) {
        l.showNewPageBtn = false;
        l.showFullscreenBtn = false;
        l.showOverflowBtn = true;
    }
    if (width < BREAKPOINT_NO_TITLE) {
        l.showTitle = false;
        l.showTitleInMenu = true;
        l.showPageArrows = false;
        l.showPageCompactBtn = true;
    }
    if (width < BREAKPOINT_NARROW) {
        l.showPageCompactBtn = false;
        l.showSidebarBtn = false;
        l.showSeparators = false;
    }
    return l;
}

}  // namespace xoj::topbar
