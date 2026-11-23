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
        case TOOL_VERTICAL_SPACE:
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

}  // namespace xoj::topbar
