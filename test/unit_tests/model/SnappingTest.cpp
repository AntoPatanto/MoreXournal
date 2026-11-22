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

#include <cmath>  // for atan2, cos, sin, hypot, M_PI

#include <gtest/gtest.h>

#include "model/Point.h"
#include "model/Snapping.h"

namespace {
double angleDeg(const Point& p, const Point& center) {
    return std::atan2(p.y - center.y, p.x - center.x) * 180.0 / M_PI;
}

Point pointAtAngle(const Point& center, double deg, double length) {
    const double rad = deg * M_PI / 180.0;
    return Point(center.x + length * std::cos(rad), center.y + length * std::sin(rad));
}
}  // namespace

TEST(SnappingTest, snapRotationSnapsToFifteenDegreeSteps) {
    const Point center(10, 20);
    // The tolerance is a fraction of half a step (7.5 degrees): 0.5 -> 3.75 degrees
    const Point snapped = Snapping::snapRotation(pointAtAngle(center, 43.0, 100), center, 0.5);
    EXPECT_NEAR(angleDeg(snapped, center), 45.0, 1e-9);
    EXPECT_NEAR(std::hypot(snapped.x - center.x, snapped.y - center.y), 100, 1e-9);

    const Point snapped2 = Snapping::snapRotation(pointAtAngle(center, 32.0, 50), center, 0.5);
    EXPECT_NEAR(angleDeg(snapped2, center), 30.0, 1e-9);
}

TEST(SnappingTest, snapRotationKeepsAnglesOutsideTolerance) {
    const Point center(0, 0);
    // 39 degrees is 6 degrees away from 45 and 30 degrees is 9 away: outside a 3.75 degrees tolerance
    const Point p = pointAtAngle(center, 39.0, 100);
    const Point snapped = Snapping::snapRotation(p, center, 0.5);
    EXPECT_NEAR(angleDeg(snapped, center), 39.0, 1e-9);
}
