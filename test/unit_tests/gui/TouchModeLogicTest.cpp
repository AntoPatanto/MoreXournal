/*
 * MoreXournal
 *
 * Tests for the touch-mode chrome switching rules and swatch contrast checks
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/ChromeUtil.h"
#include "gui/TouchModeLogic.h"

using namespace xoj::chrome;

TEST(TouchModeLogicTest, testClassifyGdkSource) {
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_TOUCHSCREEN), PointerKind::Touch);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_PEN), PointerKind::Pen);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_ERASER), PointerKind::Pen);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_MOUSE), PointerKind::Mouse);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_TOUCHPAD), PointerKind::Mouse);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_TRACKPOINT), PointerKind::Mouse);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_CURSOR), PointerKind::Mouse);
    EXPECT_EQ(classifyGdkSource(GDK_SOURCE_KEYBOARD), PointerKind::Other);
}

TEST(TouchModeLogicTest, testAutoSwitchesOnTouchAndMouseOnly) {
    // Touch turns touch layout on
    EXPECT_TRUE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Touch, false));
    EXPECT_TRUE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Touch, true));

    // Mouse switches back to compact
    EXPECT_FALSE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Mouse, true));
    EXPECT_FALSE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Mouse, false));

    // Pen never changes the current layout
    EXPECT_FALSE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Pen, false));
    EXPECT_TRUE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Pen, true));

    // Keyboard / unknown never changes the current layout
    EXPECT_FALSE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Other, false));
    EXPECT_TRUE(resolveTouchLayout(TouchMode::AUTO, PointerKind::Other, true));
}

TEST(TouchModeLogicTest, testForcedModesIgnoreInputDevice) {
    for (PointerKind k: {PointerKind::Mouse, PointerKind::Pen, PointerKind::Touch, PointerKind::Other}) {
        EXPECT_TRUE(resolveTouchLayout(TouchMode::ON, k, false));
        EXPECT_TRUE(resolveTouchLayout(TouchMode::ON, k, true));
        EXPECT_FALSE(resolveTouchLayout(TouchMode::OFF, k, false));
        EXPECT_FALSE(resolveTouchLayout(TouchMode::OFF, k, true));
    }
}

TEST(TouchModeLogicTest, testMenuTouchRowsRule) {
    // Auto uses 44 px rows only when opened from touch
    EXPECT_TRUE(isTouchMenuLayout(TouchMode::AUTO, true));
    EXPECT_FALSE(isTouchMenuLayout(TouchMode::AUTO, false));

    EXPECT_TRUE(isTouchMenuLayout(TouchMode::ON, false));
    EXPECT_TRUE(isTouchMenuLayout(TouchMode::ON, true));
    EXPECT_FALSE(isTouchMenuLayout(TouchMode::OFF, false));
    EXPECT_FALSE(isTouchMenuLayout(TouchMode::OFF, true));
}

TEST(TouchModeLogicTest, testDensitySizes) {
    EXPECT_EQ(chromeIconSize(false), 18);
    EXPECT_EQ(chromeIconSize(true), 20);
    EXPECT_EQ(chromeSwatchDotSize(false), 20);
    EXPECT_EQ(chromeSwatchDotSize(true), 24);
}

TEST(TouchModeLogicTest, testSwatchContrastRingThreshold) {
    const Color darkSurface{0xff2a2d35U};
    const Color lightSurface{0xffffffffU};
    const Color black{0xff000000U};
    const Color yellow{0xffffff00U};
    const Color blue{0xff3333ccU};

    // Black in dark mode is ~1.53:1 -> needs ring; on light surface it is 21:1 -> no ring
    EXPECT_TRUE(needsContrastRing(black, darkSurface));
    EXPECT_FALSE(needsContrastRing(black, lightSurface));

    // Yellow on white is ~1.07:1 -> needs ring; on dark surface it is >13:1 -> no ring
    EXPECT_TRUE(needsContrastRing(yellow, lightSurface));
    EXPECT_FALSE(needsContrastRing(yellow, darkSurface));

    // Blue on white is ~8.9:1 -> no ring
    EXPECT_FALSE(needsContrastRing(blue, lightSurface));
}
