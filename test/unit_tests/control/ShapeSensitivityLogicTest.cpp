/*
 * MoreXournal
 *
 * Unit tests for shape sensitivity mapping and clamping
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "control/shaperecognizer/ShapeSensitivityLogic.h"

using namespace xoj::shapes;

TEST(ShapeSensitivityLogicTest, endpointsAndDefaultMatchTableExactly) {
    const auto strict = paramsForSensitivity(0);
    EXPECT_DOUBLE_EQ(strict.lineMaxDet, 0.005);
    EXPECT_DOUBLE_EQ(strict.segmentMaxDet, 0.02);
    EXPECT_DOUBLE_EQ(strict.circleMinDet, 0.985);
    EXPECT_DOUBLE_EQ(strict.circleMaxScore, 0.05);
    EXPECT_DOUBLE_EQ(strict.rectangleAngleToleranceDeg, 6.0);
    EXPECT_DOUBLE_EQ(strict.rectangleLinearTolerance, 0.08);
    EXPECT_DOUBLE_EQ(strict.triangleLinearTolerance, 0.12);
    EXPECT_DOUBLE_EQ(strict.slantToleranceDeg, 0.0);

    const auto def = paramsForSensitivity(50);
    const ShapeRecognizerParams defaults{};
    EXPECT_EQ(def, defaults);
    EXPECT_DOUBLE_EQ(def.lineMaxDet, 0.015);
    EXPECT_DOUBLE_EQ(def.segmentMaxDet, 0.045);
    EXPECT_DOUBLE_EQ(def.circleMinDet, 0.95);
    EXPECT_DOUBLE_EQ(def.circleMaxScore, 0.10);
    EXPECT_DOUBLE_EQ(def.rectangleAngleToleranceDeg, 15.0);
    EXPECT_DOUBLE_EQ(def.rectangleLinearTolerance, 0.20);
    EXPECT_DOUBLE_EQ(def.triangleLinearTolerance, 0.30);
    EXPECT_DOUBLE_EQ(def.slantToleranceDeg, 5.0);

    const auto tolerant = paramsForSensitivity(100);
    EXPECT_DOUBLE_EQ(tolerant.lineMaxDet, 0.045);
    EXPECT_DOUBLE_EQ(tolerant.segmentMaxDet, 0.10);
    EXPECT_DOUBLE_EQ(tolerant.circleMinDet, 0.85);
    EXPECT_DOUBLE_EQ(tolerant.circleMaxScore, 0.20);
    EXPECT_DOUBLE_EQ(tolerant.rectangleAngleToleranceDeg, 30.0);
    EXPECT_DOUBLE_EQ(tolerant.rectangleLinearTolerance, 0.40);
    EXPECT_DOUBLE_EQ(tolerant.triangleLinearTolerance, 0.55);
    EXPECT_DOUBLE_EQ(tolerant.slantToleranceDeg, 15.0);
}

TEST(ShapeSensitivityLogicTest, outOfRangePositionClamps) {
    EXPECT_EQ(paramsForSensitivity(-25), paramsForSensitivity(0));
    EXPECT_EQ(paramsForSensitivity(150), paramsForSensitivity(100));
}

TEST(ShapeSensitivityLogicTest, mappingIsMonotonicAcrossFullRange) {
    ShapeRecognizerParams prev = paramsForSensitivity(0);
    for (int pos = 1; pos <= 100; pos++) {
        const ShapeRecognizerParams cur = paramsForSensitivity(pos);
        EXPECT_GT(cur.lineMaxDet, prev.lineMaxDet) << "pos=" << pos;
        EXPECT_GT(cur.segmentMaxDet, prev.segmentMaxDet) << "pos=" << pos;
        EXPECT_LT(cur.circleMinDet, prev.circleMinDet) << "pos=" << pos;  // lower det is more tolerant
        EXPECT_GT(cur.circleMaxScore, prev.circleMaxScore) << "pos=" << pos;
        EXPECT_GT(cur.rectangleAngleToleranceDeg, prev.rectangleAngleToleranceDeg) << "pos=" << pos;
        EXPECT_GT(cur.rectangleLinearTolerance, prev.rectangleLinearTolerance) << "pos=" << pos;
        EXPECT_GT(cur.triangleLinearTolerance, prev.triangleLinearTolerance) << "pos=" << pos;
        EXPECT_GT(cur.slantToleranceDeg, prev.slantToleranceDeg) << "pos=" << pos;
        prev = cur;
    }
}

TEST(ShapeSensitivityLogicTest, matchesSensitivityDetectsCustomEdits) {
    for (int pos: {0, 25, 50, 75, 100}) {
        const auto mapped = paramsForSensitivity(pos);
        EXPECT_TRUE(matchesSensitivity(mapped, pos));

        auto modified = mapped;
        modified.lineMaxDet += 0.002;
        EXPECT_FALSE(matchesSensitivity(modified, pos));

        auto modifiedSlant = mapped;
        modifiedSlant.slantToleranceDeg += 1.0;
        EXPECT_FALSE(matchesSensitivity(modifiedSlant, pos));
    }
}

TEST(ShapeSensitivityLogicTest, clampHandlesBoundsAndNonFinite) {
    ShapeRecognizerParams extreme;
    extreme.lineMaxDet = -1.0;
    extreme.segmentMaxDet = 10.0;
    extreme.circleMinDet = 0.1;
    extreme.circleMaxScore = std::numeric_limits<double>::quiet_NaN();
    extreme.rectangleAngleToleranceDeg = std::numeric_limits<double>::infinity();
    extreme.rectangleLinearTolerance = 0.0;
    extreme.triangleLinearTolerance = 99.0;
    extreme.slantToleranceDeg = -5.0;

    const auto clamped = clampShapeRecognizerParams(extreme);
    EXPECT_DOUBLE_EQ(clamped.lineMaxDet, LINE_MAX_DET_RANGE.min());
    EXPECT_DOUBLE_EQ(clamped.segmentMaxDet, SEGMENT_MAX_DET_RANGE.max());
    EXPECT_DOUBLE_EQ(clamped.circleMinDet, CIRCLE_MIN_DET_RANGE.min());
    EXPECT_DOUBLE_EQ(clamped.circleMaxScore, CIRCLE_MAX_SCORE_RANGE.def);
    EXPECT_DOUBLE_EQ(clamped.rectangleAngleToleranceDeg, RECTANGLE_ANGLE_TOLERANCE_DEG_RANGE.def);
    EXPECT_DOUBLE_EQ(clamped.rectangleLinearTolerance, RECTANGLE_LINEAR_TOLERANCE_RANGE.min());
    EXPECT_DOUBLE_EQ(clamped.triangleLinearTolerance, TRIANGLE_LINEAR_TOLERANCE_RANGE.max());
    EXPECT_DOUBLE_EQ(clamped.slantToleranceDeg, SLANT_TOLERANCE_DEG_RANGE.min());
}
