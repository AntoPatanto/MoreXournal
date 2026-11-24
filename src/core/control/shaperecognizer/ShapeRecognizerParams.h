/*
 * MoreXournal
 *
 * Runtime thresholds and diagnostics for the shape recognizer
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace xoj::shapes {

/// Strict, default (original Xournal++ value), and tolerant bounds for a numeric parameter.
struct ParamRange {
    double strict;
    double def;
    double tolerant;

    constexpr double min() const { return strict < tolerant ? strict : tolerant; }
    constexpr double max() const { return strict > tolerant ? strict : tolerant; }
};

inline constexpr ParamRange LINE_MAX_DET_RANGE{0.005, 0.015, 0.045};
inline constexpr ParamRange SEGMENT_MAX_DET_RANGE{0.02, 0.045, 0.10};
inline constexpr ParamRange CIRCLE_MIN_DET_RANGE{0.985, 0.95, 0.85};
inline constexpr ParamRange CIRCLE_MAX_SCORE_RANGE{0.05, 0.10, 0.20};
inline constexpr ParamRange RECTANGLE_ANGLE_TOLERANCE_DEG_RANGE{6.0, 15.0, 30.0};
inline constexpr ParamRange RECTANGLE_LINEAR_TOLERANCE_RANGE{0.08, 0.20, 0.40};
inline constexpr ParamRange TRIANGLE_LINEAR_TOLERANCE_RANGE{0.12, 0.30, 0.55};
inline constexpr ParamRange SLANT_TOLERANCE_DEG_RANGE{0.0, 5.0, 15.0};

inline constexpr ParamRange HOLD_TOLERANCE_PX_RANGE{3.0, 8.0, 20.0};
inline constexpr ParamRange STROKE_MIN_SIZE_RANGE{10.0, 40.0, 150.0};

inline constexpr int SHAPE_SENSITIVITY_MIN = 0;
inline constexpr int SHAPE_SENSITIVITY_DEFAULT = 50;
inline constexpr int SHAPE_SENSITIVITY_MAX = 100;

}  // namespace xoj::shapes

/**
 * Runtime thresholds for ShapeRecognizer and CircleRecognizer.
 * Angles are stored in degrees (matching persisted settings) and converted to radians when needed.
 * Default values match the original compile-time macros in ShapeRecognizerConfig.h.
 */
struct ShapeRecognizerParams {
    double lineMaxDet = xoj::shapes::LINE_MAX_DET_RANGE.def;
    double segmentMaxDet = xoj::shapes::SEGMENT_MAX_DET_RANGE.def;
    double circleMinDet = xoj::shapes::CIRCLE_MIN_DET_RANGE.def;
    double circleMaxScore = xoj::shapes::CIRCLE_MAX_SCORE_RANGE.def;
    double rectangleAngleToleranceDeg = xoj::shapes::RECTANGLE_ANGLE_TOLERANCE_DEG_RANGE.def;
    double rectangleLinearTolerance = xoj::shapes::RECTANGLE_LINEAR_TOLERANCE_RANGE.def;
    double triangleLinearTolerance = xoj::shapes::TRIANGLE_LINEAR_TOLERANCE_RANGE.def;
    double slantToleranceDeg = xoj::shapes::SLANT_TOLERANCE_DEG_RANGE.def;

    constexpr double rectangleAngleToleranceRad() const { return rectangleAngleToleranceDeg * M_PI / 180.0; }
    constexpr double slantToleranceRad() const { return slantToleranceDeg * M_PI / 180.0; }

    bool operator==(const ShapeRecognizerParams&) const = default;
};

enum class RecognizedShapeKind { None, Line, Triangle, Rectangle, Circle };

/**
 * Optional diagnostic output from ShapeRecognizer::recognizePatterns.
 * Passing nullptr skips all string formatting during normal drawing.
 */
struct ShapeRecognizerDiagnostics {
    RecognizedShapeKind shape = RecognizedShapeKind::None;
    std::vector<std::string> reasons;
};
