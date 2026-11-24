/*
 * MoreXournal
 *
 * Tests for the tool button that the top bar shows as active
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/TopBarLogic.h"

using xoj::topbar::barLayoutForWidth;
using xoj::topbar::COMPACT_USABLE_NARROW;
using xoj::topbar::COMPACT_USABLE_NO_TITLE;
using xoj::topbar::COMPACT_USABLE_OVERFLOW;
using xoj::topbar::isShapeDrawingType;
using xoj::topbar::MIN_WINDOW_WIDTH;
using xoj::topbar::ToolGroup;
using xoj::topbar::toolGroupFor;
using xoj::topbar::TOUCH_USABLE_NARROW;
using xoj::topbar::TOUCH_USABLE_NO_TITLE;
using xoj::topbar::TOUCH_USABLE_OVERFLOW;
using xoj::topbar::TOUCH_USABLE_TIGHT;

namespace {
constexpr DrawingType ALL_DRAWING_TYPES[] = {
        DRAWING_TYPE_DONT_CHANGE,      DRAWING_TYPE_DEFAULT, DRAWING_TYPE_LINE,         DRAWING_TYPE_RECTANGLE,
        DRAWING_TYPE_ELLIPSE,          DRAWING_TYPE_ARROW,   DRAWING_TYPE_DOUBLE_ARROW, DRAWING_TYPE_COORDINATE_SYSTEM,
        DRAWING_TYPE_SHAPE_RECOGNIZER, DRAWING_TYPE_SPLINE};

/// Expected group for every tool except the pen, whose group depends on the drawing type
ToolGroup expectedGroupIgnoringDrawingType(ToolType tool) {
    switch (tool) {
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
        case TOOL_NONE:
        case TOOL_FLOATING_TOOLBOX:
        case TOOL_VERTICAL_SPACE:
        case TOOL_SELECT_PDF_TEXT_LINEAR:
        case TOOL_SELECT_PDF_TEXT_RECT:
        case TOOL_LASER_POINTER_PEN:
        case TOOL_LASER_POINTER_HIGHLIGHTER:
        case TOOL_LINK:
        case TOOL_LATEX:
            return ToolGroup::NONE;
        default:
            ADD_FAILURE() << "ToolType " << tool << " is missing from the expected table";
            return ToolGroup::NONE;
    }
}
}  // namespace

TEST(TopBarLogicTest, testShapeDrawingTypes) {
    EXPECT_FALSE(isShapeDrawingType(DRAWING_TYPE_DONT_CHANGE));
    EXPECT_FALSE(isShapeDrawingType(DRAWING_TYPE_DEFAULT));
    EXPECT_FALSE(isShapeDrawingType(DRAWING_TYPE_SHAPE_RECOGNIZER));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_LINE));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_RECTANGLE));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_ELLIPSE));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_ARROW));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_DOUBLE_ARROW));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_COORDINATE_SYSTEM));
    EXPECT_TRUE(isShapeDrawingType(DRAWING_TYPE_SPLINE));
}

TEST(TopBarLogicTest, testPenAndShapesShareThePenTool) {
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_DEFAULT), ToolGroup::PEN);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_DONT_CHANGE), ToolGroup::PEN);
    // Shape recognition draws freehand, so it stays on the Pen button
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_SHAPE_RECOGNIZER), ToolGroup::PEN);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_RECTANGLE), ToolGroup::SHAPES);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_ELLIPSE), ToolGroup::SHAPES);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_ARROW), ToolGroup::SHAPES);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_DOUBLE_ARROW), ToolGroup::SHAPES);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_LINE), ToolGroup::SHAPES);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_COORDINATE_SYSTEM), ToolGroup::SHAPES);
    EXPECT_EQ(toolGroupFor(TOOL_PEN, DRAWING_TYPE_SPLINE), ToolGroup::SHAPES);
}

TEST(TopBarLogicTest, testFullTable) {
    for (int t = TOOL_NONE; t < TOOL_END_ENTRY; t++) {
        auto tool = static_cast<ToolType>(t);
        for (DrawingType dt: ALL_DRAWING_TYPES) {
            ToolGroup expected = tool == TOOL_PEN ? (isShapeDrawingType(dt) ? ToolGroup::SHAPES : ToolGroup::PEN) :
                                                    expectedGroupIgnoringDrawingType(tool);
            EXPECT_EQ(toolGroupFor(tool, dt), expected) << "tool " << t << ", drawing type " << dt;
        }
    }
}

TEST(TopBarLogicTest, testNarrowWindowBreakpointsCompact) {
    EXPECT_EQ(MIN_WINDOW_WIDTH, 600);

    // Wide usable bar (>= 880 px, ~1006+ px window with CSD): everything visible, no overflow menu
    for (int w: {1154, COMPACT_USABLE_OVERFLOW}) {
        const auto l = barLayoutForWidth(w, false);
        EXPECT_TRUE(l.showTitle) << w;
        EXPECT_FALSE(l.showTitleInMenu) << w;
        EXPECT_TRUE(l.showPageArrows) << w;
        EXPECT_FALSE(l.showPageCompactBtn) << w;
        EXPECT_TRUE(l.showSidebarBtn) << w;
        EXPECT_TRUE(l.showNewPageBtn) << w;
        EXPECT_TRUE(l.showFullscreenBtn) << w;
        EXPECT_FALSE(l.showOverflowBtn) << w;
        EXPECT_TRUE(l.showSeparators) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // 740..879 px usable (~866..1005 px window with CSD): new page and fullscreen move into overflow
    for (int w: {COMPACT_USABLE_OVERFLOW - 1, 774, COMPACT_USABLE_NO_TITLE}) {
        const auto l = barLayoutForWidth(w, false);
        EXPECT_TRUE(l.showTitle) << w;
        EXPECT_FALSE(l.showTitleInMenu) << w;
        EXPECT_TRUE(l.showPageArrows) << w;
        EXPECT_FALSE(l.showPageCompactBtn) << w;
        EXPECT_TRUE(l.showSidebarBtn) << w;
        EXPECT_FALSE(l.showNewPageBtn) << w;
        EXPECT_FALSE(l.showFullscreenBtn) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_TRUE(l.showSeparators) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // 625..739 px usable (~751..865 px window with CSD): title moves into main menu, page becomes compact button
    for (int w: {COMPACT_USABLE_NO_TITLE - 1, 634, COMPACT_USABLE_NARROW}) {
        const auto l = barLayoutForWidth(w, false);
        EXPECT_FALSE(l.showTitle) << w;
        EXPECT_TRUE(l.showTitleInMenu) << w;
        EXPECT_FALSE(l.showPageArrows) << w;
        EXPECT_TRUE(l.showPageCompactBtn) << w;
        EXPECT_TRUE(l.showSidebarBtn) << w;
        EXPECT_FALSE(l.showNewPageBtn) << w;
        EXPECT_FALSE(l.showFullscreenBtn) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_TRUE(l.showSeparators) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // < 625 px usable (<= 750 px window with CSD, incl. 700 and 600 px): sidebar & page move into overflow
    for (int w: {COMPACT_USABLE_NARROW - 1, 574, 474}) {
        const auto l = barLayoutForWidth(w, false);
        EXPECT_FALSE(l.showTitle) << w;
        EXPECT_TRUE(l.showTitleInMenu) << w;
        EXPECT_FALSE(l.showPageArrows) << w;
        EXPECT_FALSE(l.showPageCompactBtn) << w;
        EXPECT_FALSE(l.showSidebarBtn) << w;
        EXPECT_FALSE(l.showNewPageBtn) << w;
        EXPECT_FALSE(l.showFullscreenBtn) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_FALSE(l.showSeparators) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }
}

TEST(TopBarLogicTest, testNarrowWindowBreakpointsTouch) {
    // >= 910 px usable (~1036+ px window with CSD): full touch bar
    for (int w: {1154, TOUCH_USABLE_OVERFLOW}) {
        const auto l = barLayoutForWidth(w, true);
        EXPECT_TRUE(l.showTitle) << w;
        EXPECT_TRUE(l.showPageArrows) << w;
        EXPECT_TRUE(l.showNewPageBtn) << w;
        EXPECT_TRUE(l.showFullscreenBtn) << w;
        EXPECT_FALSE(l.showOverflowBtn) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // 850..909 px usable: overflow only, title + arrows still fit
    for (int w: {TOUCH_USABLE_OVERFLOW - 1, TOUCH_USABLE_NO_TITLE}) {
        const auto l = barLayoutForWidth(w, true);
        EXPECT_TRUE(l.showTitle) << w;
        EXPECT_TRUE(l.showPageArrows) << w;
        EXPECT_FALSE(l.showNewPageBtn) << w;
        EXPECT_FALSE(l.showFullscreenBtn) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // 710..849 px usable (~836..975 px window, incl. 860 and 900 px touch): title in menu, compact page button
    for (int w: {TOUCH_USABLE_NO_TITLE - 1, 774, 734, TOUCH_USABLE_NARROW}) {
        const auto l = barLayoutForWidth(w, true);
        EXPECT_FALSE(l.showTitle) << w;
        EXPECT_TRUE(l.showTitleInMenu) << w;
        EXPECT_FALSE(l.showPageArrows) << w;
        EXPECT_TRUE(l.showPageCompactBtn) << w;
        EXPECT_TRUE(l.showSidebarBtn) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_TRUE(l.showSeparators) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // 550..709 px usable (~676..835 px window, incl. 700 and 760 px touch): narrow layout, full 44 px buttons
    for (int w: {TOUCH_USABLE_NARROW - 1, 634, 574, TOUCH_USABLE_TIGHT}) {
        const auto l = barLayoutForWidth(w, true);
        EXPECT_FALSE(l.showTitle) << w;
        EXPECT_FALSE(l.showPageCompactBtn) << w;
        EXPECT_FALSE(l.showSidebarBtn) << w;
        EXPECT_FALSE(l.showSeparators) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_FALSE(l.compactBarButtons) << w;
    }

    // < 550 px usable (< ~676 px window, incl. 600 px touch): narrow layout + tightened 38 px bar buttons
    for (int w: {TOUCH_USABLE_TIGHT - 1, 474}) {
        const auto l = barLayoutForWidth(w, true);
        EXPECT_FALSE(l.showTitle) << w;
        EXPECT_FALSE(l.showPageCompactBtn) << w;
        EXPECT_FALSE(l.showSidebarBtn) << w;
        EXPECT_FALSE(l.showSeparators) << w;
        EXPECT_TRUE(l.showOverflowBtn) << w;
        EXPECT_TRUE(l.compactBarButtons) << w;
    }
}
