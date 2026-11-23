/*
 * MoreXournal
 *
 * Tests for the options the floating tool pill shows for each tool
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/ToolHandler.h"
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
        EXPECT_EQ(sectionsFor(g, {true, true, true, true}) & HOLD_TO_SHAPE, 0U) << static_cast<int>(g);
    }
}

TEST(ToolPillLogicTest, testSelectionActionsBitIsAddedOnlyBySelectionState) {
    for (auto g: {ToolGroup::PEN, ToolGroup::HIGHLIGHTER, ToolGroup::ERASER, ToolGroup::SELECTION, ToolGroup::SHAPES,
                  ToolGroup::TEXT, ToolGroup::IMAGE, ToolGroup::HAND, ToolGroup::NONE}) {
        EXPECT_EQ(sectionsFor(g, {true, true, true, true}) & SELECTION_ACTIONS, 0U) << static_cast<int>(g);
    }
    EXPECT_GT(static_cast<uint32_t>(SELECTION_ACTIONS), static_cast<uint32_t>(HAND_HINT));
}

TEST(ToolPillLogicTest, testCapabilitiesForSelectionAndToolsWithoutButton) {
    // Fixed top-bar groups other than selection ignore capabilities
    EXPECT_EQ(sectionsFor(ToolGroup::ERASER, {true, true, true, true}), ERASER_OPTIONS);
    EXPECT_EQ(sectionsFor(ToolGroup::HAND, {true, true, true, true}), HAND_HINT);

    // Selection with no editable elements
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION), SELECTION_MODES);
    // Text selection: color only
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION, {true, false, false, false}), SELECTION_MODES | COLORS);
    // Whiteout/eraser stroke selection: thickness only
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION, {false, true, false, false}), SELECTION_MODES | THICKNESS);
    // Highlighter selection: color, thickness, fill (no line style)
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION, {true, true, true, false}),
              SELECTION_MODES | COLORS | THICKNESS | FILL);
    // Pen stroke selection: color, thickness, line style, fill
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION, {true, true, true, true}),
              SELECTION_MODES | COLORS | THICKNESS | LINE_STYLE | FILL);
    // Fill and line style live in the thickness popover: hidden without size capability
    EXPECT_EQ(sectionsFor(ToolGroup::SELECTION, {false, false, true, true}), SELECTION_MODES);

    EXPECT_EQ(sectionsFor(ToolGroup::NONE), TOOL_NAME);
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {true, false, false}), TOOL_NAME | COLORS);
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {false, true, false}), TOOL_NAME | THICKNESS);
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {true, true, true}), TOOL_NAME | COLORS | THICKNESS | FILL);
    // Fill lives in the thickness popover: no fill without thickness
    EXPECT_EQ(sectionsFor(ToolGroup::NONE, {false, false, true}), TOOL_NAME);
}

TEST(ToolPillLogicTest, testThicknessTableToolAlwaysHasTable) {
    ToolHandler th(nullptr, nullptr);

    // Legacy shape tools have no thickness table of their own: SHAPES must use the pen's table
    for (ToolType legacyShape: {TOOL_DRAW_RECT, TOOL_DRAW_ELLIPSE, TOOL_DRAW_ARROW, TOOL_DRAW_DOUBLE_ARROW,
                                TOOL_DRAW_COORDINATE_SYSTEM, TOOL_DRAW_SPLINE}) {
        EXPECT_FALSE(hasOwnThicknessTable(legacyShape));
        EXPECT_EQ(thicknessTableTool(legacyShape, ToolGroup::SHAPES), TOOL_PEN);
    }

    // Selection tools also have no thickness table: editable selections use the pen's table
    for (ToolType sel: {TOOL_SELECT_RECT, TOOL_SELECT_REGION, TOOL_SELECT_MULTILAYER_RECT,
                        TOOL_SELECT_MULTILAYER_REGION, TOOL_SELECT_OBJECT}) {
        EXPECT_FALSE(hasOwnThicknessTable(sel));
        EXPECT_EQ(thicknessTableTool(sel, ToolGroup::SELECTION), TOOL_PEN);
    }

    // Tools with their own tables keep them in their groups
    EXPECT_EQ(thicknessTableTool(TOOL_PEN, ToolGroup::PEN), TOOL_PEN);
    EXPECT_EQ(thicknessTableTool(TOOL_PEN, ToolGroup::SHAPES), TOOL_PEN);
    EXPECT_EQ(thicknessTableTool(TOOL_HIGHLIGHTER, ToolGroup::HIGHLIGHTER), TOOL_HIGHLIGHTER);
    EXPECT_EQ(thicknessTableTool(TOOL_LASER_POINTER_PEN, ToolGroup::NONE), TOOL_LASER_POINTER_PEN);
    EXPECT_EQ(thicknessTableTool(TOOL_LASER_POINTER_HIGHLIGHTER, ToolGroup::NONE), TOOL_LASER_POINTER_HIGHLIGHTER);

    // Every (tool, group) pair resolves to a tool with a valid thickness table in ToolHandler
    for (int t = TOOL_NONE; t < TOOL_END_ENTRY; t++) {
        const auto tool = static_cast<ToolType>(t);
        if (tool >= TOOL_PEN) {
            if (hasOwnThicknessTable(tool)) {
                EXPECT_NO_THROW(th.getToolThickness(tool)) << "tool " << t;
            } else {
                EXPECT_THROW(th.getToolThickness(tool), std::bad_optional_access) << "tool " << t;
            }
        }
        for (auto g: {ToolGroup::PEN, ToolGroup::HIGHLIGHTER, ToolGroup::ERASER, ToolGroup::SELECTION,
                      ToolGroup::SHAPES, ToolGroup::TEXT, ToolGroup::IMAGE, ToolGroup::HAND, ToolGroup::NONE}) {
            const ToolType resolved = thicknessTableTool(tool, g);
            EXPECT_TRUE(hasOwnThicknessTable(resolved)) << "tool " << t << ", group " << static_cast<int>(g);
            EXPECT_NO_THROW(th.getToolThickness(resolved)) << "tool " << t << ", group " << static_cast<int>(g);
        }
    }
}
