/*
 * MoreXournal
 *
 * Pure mapping between the global shape sensitivity slider (0..100) and the recognizer thresholds
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "ShapeRecognizerParams.h"

namespace xoj::shapes {

/// Clamp a finite value to [range.min(), range.max()], falling back to range.def if non-finite.
inline double clampParam(double value, const ParamRange& range) {
    if (!std::isfinite(value)) {
        return range.def;
    }
    return std::clamp(value, range.min(), range.max());
}

/// Piecewise linear interpolation: u = 0 -> strict, u = 0.5 -> default, u = 1 -> tolerant.
constexpr double lerpThreshold(const ParamRange& range, double u) {
    if (u <= 0.0) {
        return range.strict;
    }
    if (u >= 1.0) {
        return range.tolerant;
    }
    if (u == 0.5) {
        return range.def;
    }
    if (u < 0.5) {
        return range.strict + (range.def - range.strict) * (u * 2.0);
    }
    return range.def + (range.tolerant - range.def) * ((u - 0.5) * 2.0);
}

/// Map a sensitivity slider position in [0, 100] (50 = default) to all 8 recognizer thresholds.
constexpr ShapeRecognizerParams paramsForSensitivity(int position) {
    const int clamped = position < SHAPE_SENSITIVITY_MIN ? SHAPE_SENSITIVITY_MIN :
                        (position > SHAPE_SENSITIVITY_MAX ? SHAPE_SENSITIVITY_MAX : position);
    const double u = static_cast<double>(clamped - SHAPE_SENSITIVITY_MIN) /
                     static_cast<double>(SHAPE_SENSITIVITY_MAX - SHAPE_SENSITIVITY_MIN);

    ShapeRecognizerParams p;
    p.lineMaxDet = lerpThreshold(LINE_MAX_DET_RANGE, u);
    p.segmentMaxDet = lerpThreshold(SEGMENT_MAX_DET_RANGE, u);
    p.circleMinDet = lerpThreshold(CIRCLE_MIN_DET_RANGE, u);
    p.circleMaxScore = lerpThreshold(CIRCLE_MAX_SCORE_RANGE, u);
    p.rectangleAngleToleranceDeg = lerpThreshold(RECTANGLE_ANGLE_TOLERANCE_DEG_RANGE, u);
    p.rectangleLinearTolerance = lerpThreshold(RECTANGLE_LINEAR_TOLERANCE_RANGE, u);
    p.triangleLinearTolerance = lerpThreshold(TRIANGLE_LINEAR_TOLERANCE_RANGE, u);
    p.slantToleranceDeg = lerpThreshold(SLANT_TOLERANCE_DEG_RANGE, u);
    return p;
}

/// Clamp every field of ShapeRecognizerParams to its valid range, replacing NaN/Inf with defaults.
inline ShapeRecognizerParams clampShapeRecognizerParams(ShapeRecognizerParams p) {
    p.lineMaxDet = clampParam(p.lineMaxDet, LINE_MAX_DET_RANGE);
    p.segmentMaxDet = clampParam(p.segmentMaxDet, SEGMENT_MAX_DET_RANGE);
    p.circleMinDet = clampParam(p.circleMinDet, CIRCLE_MIN_DET_RANGE);
    p.circleMaxScore = clampParam(p.circleMaxScore, CIRCLE_MAX_SCORE_RANGE);
    p.rectangleAngleToleranceDeg = clampParam(p.rectangleAngleToleranceDeg, RECTANGLE_ANGLE_TOLERANCE_DEG_RANGE);
    p.rectangleLinearTolerance = clampParam(p.rectangleLinearTolerance, RECTANGLE_LINEAR_TOLERANCE_RANGE);
    p.triangleLinearTolerance = clampParam(p.triangleLinearTolerance, TRIANGLE_LINEAR_TOLERANCE_RANGE);
    p.slantToleranceDeg = clampParam(p.slantToleranceDeg, SLANT_TOLERANCE_DEG_RANGE);
    return p;
}

/// Return true if params matches paramsForSensitivity(position) within eps on every field.
inline bool matchesSensitivity(const ShapeRecognizerParams& params, int position, double eps = 1e-5) {
    const ShapeRecognizerParams expected = paramsForSensitivity(position);
    return std::fabs(params.lineMaxDet - expected.lineMaxDet) <= eps &&
           std::fabs(params.segmentMaxDet - expected.segmentMaxDet) <= eps &&
           std::fabs(params.circleMinDet - expected.circleMinDet) <= eps &&
           std::fabs(params.circleMaxScore - expected.circleMaxScore) <= eps &&
           std::fabs(params.rectangleAngleToleranceDeg - expected.rectangleAngleToleranceDeg) <= eps &&
           std::fabs(params.rectangleLinearTolerance - expected.rectangleLinearTolerance) <= eps &&
           std::fabs(params.triangleLinearTolerance - expected.triangleLinearTolerance) <= eps &&
           std::fabs(params.slantToleranceDeg - expected.slantToleranceDeg) <= eps;
}

/// Format a floating-point number with a fixed number of decimal places, avoiding "-0".
inline std::string formatFixed(double value, int decimals) {
    if (std::fabs(value) < 1e-12) {
        value = 0.0;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return buf;
}

}  // namespace xoj::shapes
