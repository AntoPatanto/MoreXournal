/*
 * MoreXournal
 *
 * Unit tests for VerticalScrollbarRevealState.
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/scroll/VerticalScrollbarRevealLogic.h"

using xoj::scroll::VerticalScrollbarMode;
using xoj::scroll::VerticalScrollbarRevealState;

namespace {

constexpr double UPPER = 5000.0;
constexpr double PAGE_SIZE = 800.0;

auto makeScrollOnlyState(int64_t initMs = 1000) -> VerticalScrollbarRevealState {
    VerticalScrollbarRevealState state;
    state.configure(false, false);
    state.onRangeChanged(0.0, UPPER, PAGE_SIZE, initMs);
    return state;
}

}  // namespace

TEST(VerticalScrollbarRevealLogicTest, startsHiddenInScrollOnlyAndRespectsPreferences) {
    VerticalScrollbarRevealState state;

    auto a = state.configure(false, false);
    EXPECT_EQ(state.getMode(), VerticalScrollbarMode::ScrollOnly);
    EXPECT_FALSE(a.visible);
    EXPECT_TRUE(a.cancelHideTimer);
    EXPECT_EQ(VerticalScrollbarRevealState::HIDE_DELAY_MS, 1000);

    a = state.configure(false, true);
    EXPECT_EQ(state.getMode(), VerticalScrollbarMode::AlwaysVisible);
    EXPECT_TRUE(a.visible);

    a = state.configure(true, false);
    EXPECT_EQ(state.getMode(), VerticalScrollbarMode::Hidden);
    EXPECT_FALSE(a.visible);

    // Hidden preference wins even when fadeout is disabled
    a = state.configure(true, true);
    EXPECT_EQ(state.getMode(), VerticalScrollbarMode::Hidden);
    EXPECT_FALSE(a.visible);
}

TEST(VerticalScrollbarRevealLogicTest, ignoresSmallMovementsAndWobble) {
    auto state = makeScrollOnlyState(1000);
    const int64_t t0 = 1500;

    // Small single step below threshold
    auto a = state.onValueChanged(60.0, UPPER, PAGE_SIZE, false, t0);
    EXPECT_FALSE(a.visible);

    // Back-and-forth wobble should never accumulate to activation
    a = state.onValueChanged(20.0, UPPER, PAGE_SIZE, false, t0 + 50);
    EXPECT_FALSE(a.visible);
    a = state.onValueChanged(70.0, UPPER, PAGE_SIZE, false, t0 + 100);
    EXPECT_FALSE(a.visible);
    a = state.onValueChanged(25.0, UPPER, PAGE_SIZE, false, t0 + 150);
    EXPECT_FALSE(a.visible);
    a = state.onValueChanged(80.0, UPPER, PAGE_SIZE, false, t0 + 200);
    EXPECT_FALSE(a.visible);
}

TEST(VerticalScrollbarRevealLogicTest, resetsAccumulationAfterPause) {
    auto state = makeScrollOnlyState(1000);

    auto a = state.onValueChanged(80.0, UPPER, PAGE_SIZE, false, 1500);
    EXPECT_FALSE(a.visible);

    // Pause longer than ACCUMULATION_WINDOW_MS (450 ms)
    a = state.onValueChanged(160.0, UPPER, PAGE_SIZE, false, 2100);
    EXPECT_FALSE(a.visible);
}

TEST(VerticalScrollbarRevealLogicTest, activatesOnSustainedScrollAndHidesAfterTimeout) {
    auto state = makeScrollOnlyState(1000);

    // Four continuous 30 px steps = 120 px -> activates
    EXPECT_FALSE(state.onValueChanged(30.0, UPPER, PAGE_SIZE, false, 1500).visible);
    EXPECT_FALSE(state.onValueChanged(60.0, UPPER, PAGE_SIZE, false, 1600).visible);
    EXPECT_FALSE(state.onValueChanged(90.0, UPPER, PAGE_SIZE, false, 1700).visible);

    auto a = state.onValueChanged(120.0, UPPER, PAGE_SIZE, false, 1800);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.armHideTimer);

    // Continued scrolling refreshes the hide timer
    a = state.onValueChanged(135.0, UPPER, PAGE_SIZE, false, 2000);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.armHideTimer);

    // Timeout hides the scrollbar again
    a = state.onHideTimeout();
    EXPECT_FALSE(a.visible);
    EXPECT_FALSE(state.isVisible());

    // After hiding, another small movement stays hidden
    a = state.onValueChanged(175.0, UPPER, PAGE_SIZE, false, 3200);
    EXPECT_FALSE(a.visible);
}

TEST(VerticalScrollbarRevealLogicTest, activatesOnUpwardScrollAfterSmallDownwardNudge) {
    auto state = makeScrollOnlyState(1000);
    // Start at y = 500
    state.onRangeChanged(500.0, UPPER, PAGE_SIZE, 1000);

    EXPECT_FALSE(state.onValueChanged(550.0, UPPER, PAGE_SIZE, false, 1500).visible);
    // Reverse direction upward by 130 px -> resets accumulator on reversal and triggers
    auto a = state.onValueChanged(420.0, UPPER, PAGE_SIZE, false, 1550);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.armHideTimer);
}

TEST(VerticalScrollbarRevealLogicTest, ignoresZoomAndRangeChangeCooldown) {
    auto state = makeScrollOnlyState(1000);

    // Large adjustment jump right after range change (e.g. document open or zoom recalculation)
    auto a = state.onValueChanged(600.0, UPPER, PAGE_SIZE, false, 1100);
    EXPECT_FALSE(a.visible);

    // Large jump while pinch-zoom sequence is active
    a = state.onValueChanged(1200.0, UPPER, PAGE_SIZE, true, 2000);
    EXPECT_FALSE(a.visible);

    // After zoom ends, genuine scrolling activates normally
    a = state.onValueChanged(1350.0, UPPER, PAGE_SIZE, false, 2100);
    EXPECT_TRUE(a.visible);
}

TEST(VerticalScrollbarRevealLogicTest, staysVisibleDuringScrollbarDragAndRearmsOnRelease) {
    auto state = makeScrollOnlyState(1000);

    // Reveal scrollbar via scroll
    auto a = state.onValueChanged(200.0, UPPER, PAGE_SIZE, false, 1500);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.armHideTimer);

    // User grabs the scrollbar -> cancels hide timer
    a = state.setScrollbarInteracting(true);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.cancelHideTimer);
    EXPECT_FALSE(a.armHideTimer);

    // Dragging the scrollbar keeps it visible without arming timer
    a = state.onValueChanged(2500.0, UPPER, PAGE_SIZE, false, 2000);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.cancelHideTimer);
    EXPECT_FALSE(a.armHideTimer);

    // Even if a timeout somehow fires while interacting, it must not hide
    a = state.onHideTimeout();
    EXPECT_TRUE(a.visible);

    // Releasing the scrollbar re-arms the 1 s hide timer
    a = state.setScrollbarInteracting(false);
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.armHideTimer);

    // Subsequent timeout hides it
    a = state.onHideTimeout();
    EXPECT_FALSE(a.visible);
}

TEST(VerticalScrollbarRevealLogicTest, hidesImmediatelyWhenContentFitsViewport) {
    auto state = makeScrollOnlyState(1000);

    EXPECT_TRUE(state.onValueChanged(200.0, UPPER, PAGE_SIZE, false, 1500).visible);

    auto a = state.onRangeChanged(0.0, PAGE_SIZE, PAGE_SIZE, 1600);
    EXPECT_FALSE(a.visible);
    EXPECT_TRUE(a.cancelHideTimer);
}

TEST(VerticalScrollbarRevealLogicTest, ignoresInteractionStartWhileHidden) {
    auto state = makeScrollOnlyState(1000);

    auto a = state.setScrollbarInteracting(true);
    EXPECT_FALSE(a.visible);
    EXPECT_FALSE(state.isInteracting());

    // Subsequent micro-movement must still stay below the 120 px threshold
    a = state.onValueChanged(10.0, UPPER, PAGE_SIZE, false, 1500);
    EXPECT_FALSE(a.visible);
}
