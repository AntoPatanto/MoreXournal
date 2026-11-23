/*
 * MoreXournal
 *
 * Which options the floating tool pill shows for each tool
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>  // for uint32_t

#include "TopBarLogic.h"  // for ToolGroup

namespace xoj::toolpill {

/// The parts of the pill, as bits. The pill shows them in the order of the bits.
enum Section : uint32_t {
    TOOL_NAME = 1U << 0,        ///< Name of a tool without a top bar button
    SHAPE_TYPES = 1U << 1,      ///< Rectangle, ellipse, arrow... of the pen
    SELECTION_MODES = 1U << 2,  ///< Lasso, rectangle, object, the other selection tools and vertical space
    ERASER_OPTIONS = 1U << 3,   ///< Eraser type and size
    FONT = 1U << 4,             ///< Font of the text tool
    COLORS = 1U << 5,           ///< Palette colors and the color chooser
    THICKNESS = 1U << 6,        ///< Button with the thickness popover
    LINE_STYLE = 1U << 7,       ///< Line styles, in the thickness popover
    FILL = 1U << 8,             ///< Fill and fill opacity, in the thickness popover
    TEXT_ALIGNMENT = 1U << 9,   ///< Left, centre, right, justify
    HOLD_TO_SHAPE = 1U << 10,   ///< "Automatic shapes" switch
    IMAGE_HINT = 1U << 11,      ///< How to insert an image, and Paste
    HAND_HINT = 1U << 12,       ///< How to move the page
};

/// What the active tool supports, from ToolHandler::hasCapability(). Only used for tools without a top bar button.
struct Capabilities {
    bool color = false;
    bool size = false;
    bool fill = false;
};

/// The sections of the pill for a tool group
constexpr uint32_t sectionsFor(xoj::topbar::ToolGroup group, Capabilities caps = {}) {
    using xoj::topbar::ToolGroup;
    switch (group) {
        case ToolGroup::PEN:
            return COLORS | THICKNESS | LINE_STYLE | FILL | HOLD_TO_SHAPE;
        case ToolGroup::HIGHLIGHTER:
            return COLORS | THICKNESS | FILL;
        case ToolGroup::ERASER:
            return ERASER_OPTIONS;
        case ToolGroup::SELECTION:
            return SELECTION_MODES;
        case ToolGroup::SHAPES:
            // Shapes are drawn with the pen tool: same color, thickness and line style as the pen
            return SHAPE_TYPES | COLORS | THICKNESS | LINE_STYLE | FILL;
        case ToolGroup::TEXT:
            return FONT | COLORS | TEXT_ALIGNMENT;
        case ToolGroup::IMAGE:
            return IMAGE_HINT;
        case ToolGroup::HAND:
            return HAND_HINT;
        case ToolGroup::NONE:
            break;
    }
    return TOOL_NAME | (caps.color ? COLORS : 0U) | (caps.size ? THICKNESS : 0U) | (caps.size && caps.fill ? FILL : 0U);
}

}  // namespace xoj::toolpill
