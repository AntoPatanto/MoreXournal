/*
 * MoreXournal
 *
 * Unit tests for selection handle sizing, device hit targets, overlap priority,
 * small-selection layout, transient hold-to-shape hit rules and dark-paper palette.
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>  // for any_of
#include <array>      // for array

#include <gtest/gtest.h>

#include "control/tools/SelectionHandleLogic.h"

using namespace xoj::handles;

namespace {
bool hasHandle(const SelectionLayout& layout, CursorSelectionType type) {
    for (size_t i = 0; i < layout.handleCount; i++) {
        if (layout.handles[i].type == type) {
            return true;
        }
    }
    return false;
}

HandlePoint findHandle(const SelectionLayout& layout, CursorSelectionType type) {
    for (size_t i = 0; i < layout.handleCount; i++) {
        if (layout.handles[i].type == type) {
            return layout.handles[i];
        }
    }
    ADD_FAILURE() << "Expected handle type " << static_cast<int>(type) << " to be present";
    return {};
}
}  // namespace

TEST(SelectionHandleLogic, deviceRadiiAndVisualSizes) {
    EXPECT_DOUBLE_EQ(hitDiameterPx(DeviceKind::Fine), 24.0);
    EXPECT_DOUBLE_EQ(hitDiameterPx(DeviceKind::Pen), 32.0);
    EXPECT_DOUBLE_EQ(hitDiameterPx(DeviceKind::Touch), 44.0);

    EXPECT_DOUBLE_EQ(hitRadiusPx(DeviceKind::Fine), 12.0);
    EXPECT_DOUBLE_EQ(hitRadiusPx(DeviceKind::Pen), 16.0);
    EXPECT_DOUBLE_EQ(hitRadiusPx(DeviceKind::Touch), 22.0);

    EXPECT_DOUBLE_EQ(handleVisualDiameterPx(DeviceKind::Fine), 12.0);
    EXPECT_DOUBLE_EQ(handleVisualDiameterPx(DeviceKind::Pen), 12.0);
    EXPECT_DOUBLE_EQ(handleVisualDiameterPx(DeviceKind::Touch), 16.0);

    EXPECT_TRUE(showsHoverFeedback(DeviceKind::Fine));
    EXPECT_TRUE(showsHoverFeedback(DeviceKind::Pen));
    EXPECT_FALSE(showsHoverFeedback(DeviceKind::Touch));

    // Tap-to-select and play-object radii
    EXPECT_DOUBLE_EQ(objectSelectRadiusPx(DeviceKind::Fine), 6.0);
    EXPECT_DOUBLE_EQ(objectSelectRadiusPx(DeviceKind::Pen), 10.0);
    EXPECT_DOUBLE_EQ(objectSelectRadiusPx(DeviceKind::Touch), 20.0);
    EXPECT_DOUBLE_EQ(playObjectRadiusPx(DeviceKind::Fine), 18.0);
    EXPECT_DOUBLE_EQ(playObjectRadiusPx(DeviceKind::Pen), 30.0);
    EXPECT_DOUBLE_EQ(playObjectRadiusPx(DeviceKind::Touch), 60.0);
}

TEST(SelectionHandleLogic, normalSelectionShowsAllNineHandles) {
    const auto layout = computeSelectionLayout(100, 100, 300, 220, DeviceKind::Fine);
    EXPECT_FALSE(layout.tinyBoth);
    EXPECT_TRUE(layout.showTopBottomSides);
    EXPECT_TRUE(layout.showLeftRightSides);
    EXPECT_TRUE(layout.showRotate);
    EXPECT_EQ(layout.handleCount, 9U);
    EXPECT_DOUBLE_EQ(layout.boxMinX, 100.0);
    EXPECT_DOUBLE_EQ(layout.boxMaxX, 300.0);
    EXPECT_DOUBLE_EQ(layout.boxMinY, 100.0);
    EXPECT_DOUBLE_EQ(layout.boxMaxY, 220.0);
    EXPECT_TRUE(hasHandle(layout, CURSOR_SELECTION_ROTATE));
    // Rotation stays on the right side
    const auto rot = findHandle(layout, CURSOR_SELECTION_ROTATE);
    EXPECT_GT(rot.x, layout.boxMaxX);
    EXPECT_DOUBLE_EQ(rot.y, 160.0);
}

TEST(SelectionHandleLogic, smallSelectionThresholds) {
    // One axis < 64 px hides only that axis's side handles; box is at least 40 px
    {
        const auto narrow = computeSelectionLayout(100, 100, 120, 200, DeviceKind::Fine);  // 20 x 100
        EXPECT_FALSE(narrow.tinyBoth);
        EXPECT_DOUBLE_EQ(narrow.boxMaxX - narrow.boxMinX, 40.0);
        EXPECT_DOUBLE_EQ(narrow.boxMaxY - narrow.boxMinY, 100.0);
        EXPECT_FALSE(narrow.showTopBottomSides);
        EXPECT_TRUE(narrow.showLeftRightSides);
        EXPECT_TRUE(narrow.showRotate);
        EXPECT_FALSE(hasHandle(narrow, CURSOR_SELECTION_TOP));
        EXPECT_FALSE(hasHandle(narrow, CURSOR_SELECTION_BOTTOM));
        EXPECT_TRUE(hasHandle(narrow, CURSOR_SELECTION_LEFT));
        EXPECT_TRUE(hasHandle(narrow, CURSOR_SELECTION_RIGHT));
    }

    // Both axes between 28 and 63 px: 4 corners + rotate, no side handles
    {
        const auto mediumSmall = computeSelectionLayout(100, 100, 150, 150, DeviceKind::Fine);  // 50 x 50
        EXPECT_FALSE(mediumSmall.tinyBoth);
        EXPECT_FALSE(mediumSmall.showTopBottomSides);
        EXPECT_FALSE(mediumSmall.showLeftRightSides);
        EXPECT_TRUE(mediumSmall.showRotate);
        EXPECT_EQ(mediumSmall.handleCount, 5U);
    }

    // Both axes < 28 px: 40 x 40 drawn box, only four corner handles pushed outward, whole interior moves
    {
        const auto tiny = computeSelectionLayout(100, 100, 114, 114, DeviceKind::Fine);  // 14 x 14
        EXPECT_TRUE(tiny.tinyBoth);
        EXPECT_DOUBLE_EQ(tiny.boxMaxX - tiny.boxMinX, 40.0);
        EXPECT_DOUBLE_EQ(tiny.boxMaxY - tiny.boxMinY, 40.0);
        EXPECT_FALSE(tiny.showTopBottomSides);
        EXPECT_FALSE(tiny.showLeftRightSides);
        EXPECT_FALSE(tiny.showRotate);
        EXPECT_EQ(tiny.handleCount, 4U);

        const auto tl = findHandle(tiny, CURSOR_SELECTION_TOP_LEFT);
        const auto br = findHandle(tiny, CURSOR_SELECTION_BOTTOM_RIGHT);
        EXPECT_LT(tl.x, tiny.boxMinX);
        EXPECT_LT(tl.y, tiny.boxMinY);
        EXPECT_GT(br.x, tiny.boxMaxX);
        EXPECT_GT(br.y, tiny.boxMaxY);

        // Centre moves even under touch hit radius
        const double cx = (tiny.boxMinX + tiny.boxMaxX) * 0.5;
        const double cy = (tiny.boxMinY + tiny.boxMaxY) * 0.5;
        EXPECT_EQ(hitTestSelection(tiny, cx, cy, DeviceKind::Fine).type, CURSOR_SELECTION_MOVE);
        EXPECT_EQ(hitTestSelection(tiny, cx, cy, DeviceKind::Pen).type, CURSOR_SELECTION_MOVE);
        EXPECT_EQ(hitTestSelection(tiny, cx, cy, DeviceKind::Touch).type, CURSOR_SELECTION_MOVE);
    }
}

TEST(SelectionHandleLogic, hitPriorityAndDeviceRadii) {
    const auto layout = computeSelectionLayout(100, 100, 300, 220, DeviceKind::Fine);

    // Distance 14 px outside top-left corner: misses mouse (r=12), hits pen (r=16) and touch (r=22)
    EXPECT_EQ(hitTestSelection(layout, 86, 100, DeviceKind::Fine).type, CURSOR_SELECTION_NONE);
    EXPECT_EQ(hitTestSelection(layout, 86, 100, DeviceKind::Pen).type, CURSOR_SELECTION_TOP_LEFT);
    EXPECT_EQ(hitTestSelection(layout, 86, 100, DeviceKind::Touch).type, CURSOR_SELECTION_TOP_LEFT);

    // Distance 19 px outside top-left corner: misses pen (r=16), hits touch (r=22)
    EXPECT_EQ(hitTestSelection(layout, 81, 100, DeviceKind::Pen).type, CURSOR_SELECTION_NONE);
    EXPECT_EQ(hitTestSelection(layout, 81, 100, DeviceKind::Touch).type, CURSOR_SELECTION_TOP_LEFT);

    // Closest handle centre wins when hit areas overlap (right-middle at 300 vs rotate at 320)
    EXPECT_EQ(hitTestSelection(layout, 308, 160, DeviceKind::Fine).type, CURSOR_SELECTION_RIGHT);
    EXPECT_EQ(hitTestSelection(layout, 312, 160, DeviceKind::Fine).type, CURSOR_SELECTION_ROTATE);

    // Handles beat edge bands even inside the edge band strip
    const auto cornerHit = hitTestSelection(layout, 108, 102, DeviceKind::Fine);
    EXPECT_EQ(cornerHit.type, CURSOR_SELECTION_TOP_LEFT);
    EXPECT_EQ(cornerHit.handle, CURSOR_SELECTION_TOP_LEFT);

    // Edge band works away from handles and beats body (move)
    const auto topEdgeHit = hitTestSelection(layout, 150, 103, DeviceKind::Fine);
    EXPECT_EQ(topEdgeHit.type, CURSOR_SELECTION_TOP);
    EXPECT_EQ(topEdgeHit.handle, CURSOR_SELECTION_NONE);

    // Body beats page content
    EXPECT_EQ(hitTestSelection(layout, 200, 160, DeviceKind::Fine).type, CURSOR_SELECTION_MOVE);
    EXPECT_EQ(hitTestSelection(layout, 50, 50, DeviceKind::Fine).type, CURSOR_SELECTION_NONE);
}

TEST(SelectionHandleLogic, noDeadGapBetweenRightEdgeAndRotateHandle) {
    // Test both when right-middle handle is visible and when it is hidden (short wide selection)
    const std::array<SelectionLayout, 2> layouts = {
            computeSelectionLayout(100, 100, 300, 220, DeviceKind::Fine),
            computeSelectionLayout(100, 100, 300, 140, DeviceKind::Fine),
    };
    const std::array<DeviceKind, 3> devices = {DeviceKind::Fine, DeviceKind::Pen, DeviceKind::Touch};

    for (const auto& layout: layouts) {
        ASSERT_TRUE(layout.showRotate);
        for (DeviceKind dev: devices) {
            for (double px = layout.boxMaxX; px <= layout.rotateX; px += 1.0) {
                const auto hit = hitTestSelection(layout, px, layout.rotateY, dev);
                EXPECT_NE(hit.type, CURSOR_SELECTION_NONE)
                        << "Dead gap at x=" << px << " for device=" << static_cast<int>(dev);
            }
        }
    }
}

TEST(SelectionHandleLogic, transientSelectionOnlyGrabsHandlesAndStroke) {
    const auto layout = computeSelectionLayout(100, 100, 260, 260, DeviceKind::Fine);

    // Pen-down in the interior of a hold-to-shape box (away from stroke) must NOT grab
    EXPECT_EQ(hitTestSelection(layout, 180, 180, DeviceKind::Fine, /*transient=*/true, /*nearStroke=*/false).type,
              CURSOR_SELECTION_NONE);
    EXPECT_EQ(hitTestSelection(layout, 180, 180, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/false).type,
              CURSOR_SELECTION_NONE);

    // Invisible edge band of transient selection must NOT grab when away from handles and stroke
    EXPECT_EQ(hitTestSelection(layout, 140, 101, DeviceKind::Fine, /*transient=*/true, /*nearStroke=*/false).type,
              CURSOR_SELECTION_NONE);

    // Touching the shape's own stroke grabs to move
    EXPECT_EQ(hitTestSelection(layout, 180, 120, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/true).type,
              CURSOR_SELECTION_MOVE);

    // Handles still grab even when not on the stroke
    EXPECT_EQ(hitTestSelection(layout, 100, 100, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/false).type,
              CURSOR_SELECTION_TOP_LEFT);
    EXPECT_EQ(hitTestSelection(layout, layout.rotateX, layout.rotateY, DeviceKind::Pen, /*transient=*/true,
                               /*nearStroke=*/false)
                      .type,
              CURSOR_SELECTION_ROTATE);
}

TEST(SelectionHandleLogic, shapeHandlesClosestWinsAndStemHasNoDeadGap) {
    const std::array<ShapeHandleTarget, 3> handles = {{
            {100.0, 100.0, 100.0, 100.0, false},
            {120.0, 100.0, 120.0, 100.0, false},
            {110.0, 76.0, 110.0, 100.0, true},
    }};
    // Closest handle centre wins
    EXPECT_EQ(hitTestShapeHandles(handles.data(), handles.size(), 106.0, 100.0, DeviceKind::Fine), 0U);
    EXPECT_EQ(hitTestShapeHandles(handles.data(), handles.size(), 114.0, 100.0, DeviceKind::Fine), 1U);

    // No dead gap along the rotation stem from y=76 to y=100
    for (double py = 76.0; py <= 100.0; py += 2.0) {
        EXPECT_TRUE(hitTestShapeHandles(handles.data(), handles.size(), 110.0, py, DeviceKind::Fine).has_value())
                << "Dead gap on shape rotate stem at y=" << py;
    }

    // Touch radius reaches farther than mouse radius
    EXPECT_FALSE(hitTestShapeHandles(handles.data(), handles.size(), 82.0, 100.0, DeviceKind::Fine).has_value());
    EXPECT_EQ(hitTestShapeHandles(handles.data(), handles.size(), 82.0, 100.0, DeviceKind::Touch), 0U);
}

TEST(SelectionHandleLogic, transientLineDoesNotGrab) {
    // Plain-stroke layout (hold-to-shape / shape recognizer): transient line must never grab handles or stroke
    const auto layout = computeSelectionLayout(100, 100, 260, 180, DeviceKind::Pen);
    EXPECT_EQ(hitTestSelection(layout, 100, 100, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/true,
                               /*isLine=*/true)
                      .type,
              CURSOR_SELECTION_NONE);
    EXPECT_EQ(hitTestSelection(layout, 180, 140, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/true,
                               /*isLine=*/true)
                      .type,
              CURSOR_SELECTION_NONE);
    EXPECT_EQ(hitTestSelection(layout, layout.rotateX, layout.rotateY, DeviceKind::Pen, /*transient=*/true,
                               /*nearStroke=*/false, /*isLine=*/true)
                      .type,
              CURSOR_SELECTION_NONE);

    // Transient non-line (e.g. rectangle) keeps grabbing handles and stroke as today
    EXPECT_EQ(hitTestSelection(layout, 100, 100, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/false,
                               /*isLine=*/false)
                      .type,
              CURSOR_SELECTION_TOP_LEFT);
    EXPECT_EQ(hitTestSelection(layout, 180, 140, DeviceKind::Pen, /*transient=*/true, /*nearStroke=*/true,
                               /*isLine=*/false)
                      .type,
              CURSOR_SELECTION_MOVE);

    // Non-transient line (selected with a select tool) keeps working handles and body
    EXPECT_EQ(hitTestSelection(layout, 100, 100, DeviceKind::Pen, /*transient=*/false, /*nearStroke=*/false,
                               /*isLine=*/true)
                      .type,
              CURSOR_SELECTION_TOP_LEFT);
    EXPECT_EQ(hitTestSelection(layout, 180, 140, DeviceKind::Pen, /*transient=*/false, /*nearStroke=*/false,
                               /*isLine=*/true)
                      .type,
              CURSOR_SELECTION_MOVE);

    // Shape handles (Line/ruler tool): transient line must not grab endpoint handles;
    // transient rectangle and non-transient line keep grabbing handles
    const std::array<ShapeHandleTarget, 2> lineHandles = {{
            {100.0, 100.0, 100.0, 100.0, false},
            {260.0, 180.0, 260.0, 180.0, false},
    }};
    EXPECT_FALSE(hitTestShapeHandles(lineHandles.data(), lineHandles.size(), 260.0, 180.0, DeviceKind::Pen,
                                     /*transient=*/true, /*isLine=*/true)
                         .has_value());
    EXPECT_EQ(hitTestShapeHandles(lineHandles.data(), lineHandles.size(), 260.0, 180.0, DeviceKind::Pen,
                                  /*transient=*/true, /*isLine=*/false),
              1U);
    EXPECT_EQ(hitTestShapeHandles(lineHandles.data(), lineHandles.size(), 260.0, 180.0, DeviceKind::Pen,
                                  /*transient=*/false, /*isLine=*/true),
              1U);
}

TEST(SelectionHandleLogic, paletteFollowsPaperLightness) {
    const auto light = handlePaletteFor(ACCENT_LIGHT_PAPER, false);
    EXPECT_NEAR(light.accent.r, 0x35 / 255.0, 1e-6);
    EXPECT_NEAR(light.accent.g, 0x63 / 255.0, 1e-6);
    EXPECT_NEAR(light.accent.b, 0xe9 / 255.0, 1e-6);
    EXPECT_DOUBLE_EQ(light.fill.r, 1.0);
    EXPECT_DOUBLE_EQ(light.fill.g, 1.0);
    EXPECT_DOUBLE_EQ(light.fill.b, 1.0);
    EXPECT_NEAR(light.hoverFill.r, 0xe3 / 255.0, 1e-6);
    EXPECT_NEAR(light.hoverFill.g, 0xea / 255.0, 1e-6);
    EXPECT_NEAR(light.hoverFill.b, 0xff / 255.0, 1e-6);
    EXPECT_NEAR(light.halo.a, 0.22, 1e-6);

    const auto dark = handlePaletteFor(ACCENT_LIGHT_PAPER, true);
    EXPECT_NEAR(dark.accent.r, 0x86 / 255.0, 1e-6);
    EXPECT_NEAR(dark.accent.g, 0xa2 / 255.0, 1e-6);
    EXPECT_NEAR(dark.accent.b, 0xff / 255.0, 1e-6);
    EXPECT_NEAR(dark.fill.r, 0x2a / 255.0, 1e-6);
    EXPECT_NEAR(dark.fill.g, 0x2d / 255.0, 1e-6);
    EXPECT_NEAR(dark.fill.b, 0x35 / 255.0, 1e-6);
    EXPECT_NEAR(dark.halo.a, 0.55, 1e-6);
}
