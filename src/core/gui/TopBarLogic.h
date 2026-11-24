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

/// Minimum window width allowed (half-tiling on 1280/1366 px screens)
constexpr int MIN_WINDOW_WIDTH = 600;

/// Responsive breakpoints on the actual usable width of the top bar box (header bar minus CSD window buttons and
/// padding, ~126 px with CSD on). Sized from the measured widths of start, center and end in each density so items
/// move into the overflow menu before any group collides.
constexpr int COMPACT_USABLE_OVERFLOW = 880;  ///< Full bar needs ~770 px usable (~996 px window with CSD)
constexpr int COMPACT_USABLE_NO_TITLE = 740;  ///< Title + arrows + overflow need ~729 px usable (~866 px window)
constexpr int COMPACT_USABLE_NARROW = 625;    ///< Sidebar + compact page + overflow need ~611 px usable (~751 px)

constexpr int TOUCH_USABLE_OVERFLOW = 910;    ///< Full touch bar needs ~883 px usable (~1036 px window with CSD)
constexpr int TOUCH_USABLE_NO_TITLE = 850;    ///< Touch title + arrows + overflow need ~839 px usable (~976 px)
constexpr int TOUCH_USABLE_NARROW = 710;      ///< Touch sidebar + compact page + overflow need ~695 px usable (~836 px)
constexpr int TOUCH_USABLE_TIGHT = 550;       ///< 12 touch buttons (44 px) need ~540 px usable; below this use 38 px hit

/// Which optional chrome pieces are shown directly in the top bar at a given usable bar width and density.
/// The tool buttons, Undo and Redo never leave the bar.
struct BarLayout {
    bool showTitle = true;            ///< Document title + status beside the menu button
    bool showTitleInMenu = false;     ///< Document title + status header in the main menu
    bool showPageArrows = true;       ///< Full `< 3 / 12 >` control
    bool showPageCompactBtn = false;  ///< Compact `3 / 12 v` popover button
    bool showSidebarBtn = true;       ///< Sidebar toggle in the bar
    bool showNewPageBtn = true;       ///< New page button in the bar
    bool showFullscreenBtn = true;    ///< Fullscreen toggle in the bar
    bool showOverflowBtn = false;     ///< Overflow `...` menu button
    bool showSeparators = true;       ///< Vertical separators between groups
    bool compactBarButtons = false;   ///< Touch-only fallback (< 550 px usable): tighten bar buttons to 34/38 px hit

    bool operator==(const BarLayout&) const = default;
};

constexpr BarLayout barLayoutForWidth(int usableWidth, bool touch = false) {
    BarLayout l{};
    const int overflowAt = touch ? TOUCH_USABLE_OVERFLOW : COMPACT_USABLE_OVERFLOW;
    const int noTitleAt = touch ? TOUCH_USABLE_NO_TITLE : COMPACT_USABLE_NO_TITLE;
    const int narrowAt = touch ? TOUCH_USABLE_NARROW : COMPACT_USABLE_NARROW;

    if (usableWidth < overflowAt) {
        l.showNewPageBtn = false;
        l.showFullscreenBtn = false;
        l.showOverflowBtn = true;
    }
    if (usableWidth < noTitleAt) {
        l.showTitle = false;
        l.showTitleInMenu = true;
        l.showPageArrows = false;
        l.showPageCompactBtn = true;
    }
    if (usableWidth < narrowAt) {
        l.showPageCompactBtn = false;
        l.showSidebarBtn = false;
        l.showSeparators = false;
    }
    if (touch && usableWidth < TOUCH_USABLE_TIGHT) {
        l.compactBarButtons = true;
    }
    return l;
}

}  // namespace xoj::topbar
