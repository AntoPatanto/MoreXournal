/*
 * MoreXournal
 *
 * Tests for the options the floating tool pill shows for each tool
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/ToolPillLogic.h"

using xoj::topbar::ToolGroup;
using namespace xoj::toolpill;

TEST(ToolPillLogicTest, testSectionsOfEachTool) {
    EXPECT_EQ(sectionsFor(ToolGroup::PEN), COLORS | THICKNESS | LINE_STYLE | FILL | HOLD_TO_SHAPE);
    EXPECT_EQ(sectionsFor(ToolGroup::HIGHLIGHTER), COLORS | THICKNESS | FILL);
    EXPECT_EQ(sectionsFor(ToolGroup::ERASER), ERASER_OPTIONS);
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION), SELECTION_MODES);
    EXPECT_EQ(sectionsFor(ToolGroup::SHAPES), SHAPE_TYPES | COLORS | THICKNESS | LINE_STYLE | FILL);
    EXPECT_EQ(sectionsFor(ToolGroup::TEXT), FONT | COLORS | TEXT_ALIGNMENT);
    EXPECT_EQ(sectionsFor(ToolGroup::IMAGE), IMAGE_HINT);
    EXPECT_EQ(sectionsFor(ToolGroup::HAND), HAND_HINT);
}

TEST(ToolPillLogicTest, testOnlyThePenHasAutomaticShapes) {
    for (auto g: {ToolGroup::HIGHLIGHTER, ToolGroup::ERASER, ToolGroup::SELECTION, ToolGroup::SHAPES, ToolGroup::TEXT,
                  ToolGroup::IMAGE, ToolGroup::HAND, ToolGroup::NONE}) {
        EXPECT_EQ(sectionsFor(g, {true, true, true}) & HOLD_TO_SHAPE, 0U) << static_cast<int>(g);
    }
}

TEST(ToolPillLogicTest, testCapabilitiesOnlyMatterForToolsWithoutButton) {
    // The groups of the top bar ignore the capabilities
    EXPECT_EQ(sectionsFor(ToolGroup::ERASER, {true, true, true}), ERASER_OPTIONS);
    EXPECT_EQ(sectionsFor(ToolGroup::HAND, {true, true, true}), HAND_HINT);

    EXPECT_EQ(sectionsFor(ToolGroup::NONE), TOOL_NAME);
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {true, false, false}), TOOL_NAME | COLORS);
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {false, true, false}), TOOL_NAME | THICKNESS);
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {true, true, true}), TOOL_NAME | COLORS | THICKNESS | FILL);
    // Fill lives in the thickness popover: no fill without thickness
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {false, false, true}), TOOL_NAME);
}
