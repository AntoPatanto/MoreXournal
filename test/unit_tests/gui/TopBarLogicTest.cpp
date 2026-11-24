/*
 * MoreXournal
 *
 * Tests for the tool button that the top bar shows as active
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/TopBarLogic.h"

using xoj::topbar::isShapeDrawingType;
using xoj::topbar::ToolGroup;
using xoj::topbar::toolGroupFor;

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
