/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/tools/HoldDetector.h"
#include "util/Point.h"

using xoj::util::Point;

TEST(HoldDetectorTest, startArmsAndAnchors) {
    HoldDetector detector(5.0);
    EXPECT_FALSE(detector.isArmed());
    detector.start({10.0, 20.0});
    EXPECT_TRUE(detector.isArmed());
    EXPECT_EQ(detector.getAnchor(), Point(10.0, 20.0));
}

TEST(HoldDetectorTest, jitterBelowToleranceDoesNotRestart) {
    HoldDetector detector(5.0);
    detector.start({100.0, 100.0});
    // 4 screen pixels at zoom 1
    EXPECT_FALSE(detector.update({104.0, 100.0}, 1.0));
    EXPECT_FALSE(detector.update({100.0, 96.0}, 1.0));
    EXPECT_FALSE(detector.update({102.0, 102.0}, 1.0));
    EXPECT_EQ(detector.getAnchor(), Point(100.0, 100.0));
    EXPECT_TRUE(detector.isArmed());
}

TEST(HoldDetectorTest, toleranceIsInScreenPixels) {
    HoldDetector detector(5.0);
    detector.start({100.0, 100.0});
    // 1.5 page units = 4.5 screen pixels at zoom 3: still jitter
    EXPECT_FALSE(detector.update({101.5, 100.0}, 3.0));
    // 2 page units = 6 screen pixels at zoom 3: a real movement
    EXPECT_TRUE(detector.update({102.0, 100.0}, 3.0));
    EXPECT_EQ(detector.getAnchor(), Point(102.0, 100.0));
}

TEST(HoldDetectorTest, movementAboveToleranceReanchors) {
    HoldDetector detector(5.0);
    detector.start({0.0, 0.0});
    EXPECT_TRUE(detector.update({6.0, 0.0}, 1.0));
    EXPECT_EQ(detector.getAnchor(), Point(6.0, 0.0));
    // Distances are now measured from the new anchor
    EXPECT_FALSE(detector.update({10.0, 0.0}, 1.0));
    EXPECT_TRUE(detector.update({12.0, 0.0}, 1.0));
    EXPECT_EQ(detector.getAnchor(), Point(12.0, 0.0));
}

TEST(HoldDetectorTest, slowDriftAccumulates) {
    HoldDetector detector(5.0);
    detector.start({0.0, 0.0});
    for (int i = 1; i <= 5; i++) {
        EXPECT_FALSE(detector.update({static_cast<double>(i), 0.0}, 1.0)) << "step " << i;
    }
    EXPECT_TRUE(detector.update({6.0, 0.0}, 1.0));
}

TEST(HoldDetectorTest, zoomChangeDuringHold) {
    HoldDetector detector(5.0);
    detector.start({0.0, 0.0});
    EXPECT_FALSE(detector.update({3.0, 0.0}, 1.0));
    // Same page position, but zooming in makes the distance 6 screen pixels
    EXPECT_TRUE(detector.update({3.0, 0.0}, 2.0));
}

TEST(HoldDetectorTest, disarmUntilMovement) {
    HoldDetector detector(5.0);
    detector.start({0.0, 0.0});
    detector.disarm();
    EXPECT_FALSE(detector.isArmed());
    // Jitter does not re-arm
    EXPECT_FALSE(detector.update({2.0, 2.0}, 1.0));
    EXPECT_FALSE(detector.isArmed());
    // A real movement does
    EXPECT_TRUE(detector.update({10.0, 0.0}, 1.0));
    EXPECT_TRUE(detector.isArmed());
}

TEST(HoldDetectorTest, defaultToleranceAcceptsSevenPixelJitter) {
    HoldDetector detector;
    detector.start({100.0, 100.0});
    EXPECT_FALSE(detector.update({107.0, 100.0}, 1.0));
    EXPECT_EQ(detector.getAnchor(), Point(100.0, 100.0));
    EXPECT_TRUE(detector.update({109.0, 100.0}, 1.0));
    EXPECT_EQ(detector.getAnchor(), Point(109.0, 100.0));
}
