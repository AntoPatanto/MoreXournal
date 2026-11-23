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

#include <cmath>   // for cos, sin, hypot, M_PI
#include <memory>  // for unique_ptr
#include <utility>  // for pair
#include <vector>   // for vector

#include <gtest/gtest.h>

#include "control/shaperecognizer/ShapeRecognizer.h"
#include "control/shaperecognizer/ShapeSensitivityLogic.h"
#include "model/Point.h"
#include "model/ShapeInfo.h"
#include "model/Stroke.h"

namespace {
constexpr double MIN_SIZE = 40.0;

/// Deterministic noise in [-amplitude, amplitude]
double noise(size_t i, double amplitude) { return amplitude * std::sin(static_cast<double>(i) * 12.9898); }

/// Build a stroke going through the given corners, sampling a point every `step` units, with some noise
std::unique_ptr<Stroke> makePolyline(const std::vector<Point>& corners, double amplitude, double step = 2.0) {
    auto s = std::make_unique<Stroke>();
    size_t n = 0;
    for (size_t c = 0; c + 1 < corners.size(); c++) {
        const Point& a = corners[c];
        const Point& b = corners[c + 1];
        const auto count = static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) / step);
        for (int k = 0; k < count; k++, n++) {
            const double t = static_cast<double>(k) / count;
            s->addPoint(Point(a.x + t * (b.x - a.x) + noise(n, amplitude),
                              a.y + t * (b.y - a.y) + noise(n + 7, amplitude)));
        }
    }
    s->addPoint(corners.back());
    return s;
}

std::unique_ptr<Stroke> makeCircle(double cx, double cy, double r, double amplitude) {
    auto s = std::make_unique<Stroke>();
    const int count = 120;
    for (int i = 0; i <= count; i++) {
        const double angle = 2 * M_PI * i / count;
        const double rr = r + noise(static_cast<size_t>(i), amplitude);
        s->addPoint(Point(cx + rr * std::cos(angle), cy + rr * std::sin(angle)));
    }
    return s;
}

bool isClosed(const Stroke& s) {
    const auto& pts = s.getPointVector();
    return pts.size() > 2 && pts.front().lineLengthTo(pts.back()) < 1e-9;
}
}  // namespace

TEST(ShapeRecognizerTest, noisyLineGivesTwoPoints) {
    auto stroke = makePolyline({Point(10, 10), Point(210, 120)}, 0.5);
    ShapeRecognizer reco;
    auto result = reco.recognizePatterns(stroke.get(), MIN_SIZE);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->getPointCount(), 2U);
    EXPECT_NEAR(result->getPoint(0).x, 10, 3);
    EXPECT_NEAR(result->getPoint(0).y, 10, 3);
    EXPECT_NEAR(result->getPoint(1).x, 210, 3);
    EXPECT_NEAR(result->getPoint(1).y, 120, 3);
}

TEST(ShapeRecognizerTest, rectangleGivesFiveClosedPoints) {
    auto stroke = makePolyline({Point(0, 0), Point(200, 0), Point(200, 100), Point(0, 100), Point(0, 0)}, 0.5);
    ShapeRecognizer reco;
    auto result = reco.recognizePatterns(stroke.get(), MIN_SIZE);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getPointCount(), 5U);
    EXPECT_TRUE(isClosed(*result));
}

TEST(ShapeRecognizerTest, triangleGivesFourClosedPoints) {
    auto stroke = makePolyline({Point(0, 0), Point(200, 0), Point(100, 150), Point(0, 0)}, 0.5);
    ShapeRecognizer reco;
    auto result = reco.recognizePatterns(stroke.get(), MIN_SIZE);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getPointCount(), 4U);
    EXPECT_TRUE(isClosed(*result));
}

TEST(ShapeRecognizerTest, noisyCircleGivesClosedStroke) {
    auto stroke = makeCircle(100, 100, 60, 1.0);
    ShapeRecognizer reco;
    auto result = reco.recognizePatterns(stroke.get(), MIN_SIZE);
    ASSERT_NE(result, nullptr);
    EXPECT_GT(result->getPointCount(), 5U);
    EXPECT_TRUE(isClosed(*result));
    for (const auto& p: result->getPointVector()) {
        EXPECT_NEAR(std::hypot(p.x - 100, p.y - 100), 60, 3);
    }
}

TEST(ShapeRecognizerTest, scribbleIsNotRecognized) {
    auto stroke = makePolyline({Point(0, 0), Point(80, 60), Point(10, 90), Point(120, 10), Point(30, 30),
                                Point(140, 100), Point(60, 0), Point(100, 120)},
                               2.0);
    ShapeRecognizer reco;
    EXPECT_EQ(reco.recognizePatterns(stroke.get(), MIN_SIZE), nullptr);
}

TEST(ShapeRecognizerTest, strokeBelowMinSizeIsNotRecognized) {
    auto stroke = makePolyline({Point(10, 10), Point(30, 20)}, 0.0, 1.0);
    ShapeRecognizer reco;
    EXPECT_EQ(reco.recognizePatterns(stroke.get(), MIN_SIZE), nullptr);
    // The same line is recognized with a smaller minimum size
    ShapeRecognizer reco2;
    EXPECT_NE(reco2.recognizePatterns(stroke.get(), 10.0), nullptr);
}

TEST(ShapeRecognizerTest, recognizedStrokesCarryTheirShape) {
    ShapeRecognizer reco;

    auto line = reco.recognizePatterns(makePolyline({Point(10, 10), Point(210, 120)}, 0.5).get(), MIN_SIZE);
    ASSERT_NE(line, nullptr);
    ASSERT_TRUE(line->getShape());
    EXPECT_EQ(line->getShape()->type, ShapeType::LINE);

    ShapeRecognizer reco2;
    auto rect = reco2.recognizePatterns(
            makePolyline({Point(0, 0), Point(200, 0), Point(200, 100), Point(0, 100), Point(0, 0)}, 0.5).get(),
            MIN_SIZE);
    ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->getShape());
    EXPECT_EQ(rect->getShape()->type, ShapeType::RECTANGLE);

    ShapeRecognizer reco3;
    auto triangle = reco3.recognizePatterns(
            makePolyline({Point(0, 0), Point(200, 0), Point(100, 150), Point(0, 0)}, 0.5).get(), MIN_SIZE);
    ASSERT_NE(triangle, nullptr);
    ASSERT_TRUE(triangle->getShape());
    EXPECT_EQ(triangle->getShape()->type, ShapeType::POLYGON);
    EXPECT_EQ(triangle->getShape()->pts.size(), 3U);

    ShapeRecognizer reco4;
    auto circle = reco4.recognizePatterns(makeCircle(100, 100, 60, 1.0).get(), MIN_SIZE);
    ASSERT_NE(circle, nullptr);
    ASSERT_TRUE(circle->getShape());
    EXPECT_EQ(circle->getShape()->type, ShapeType::ELLIPSE);

    // The points draw the attached shape
    for (const auto* s: {line.get(), rect.get(), triangle.get(), circle.get()}) {
        EXPECT_TRUE(s->getShape()->matchesBounds(s->getSnappedBounds()));
    }
    for (const auto& [p, q]: {std::pair{rect->getPointVector(), rect->getShape()->generatePoints()},
                              std::pair{triangle->getPointVector(), triangle->getShape()->generatePoints()}}) {
        ASSERT_EQ(p.size(), q.size());
        for (size_t i = 0; i < p.size(); i++) {
            EXPECT_NEAR(p[i].x, q[i].x, 1e-6);
            EXPECT_NEAR(p[i].y, q[i].y, 1e-6);
        }
    }
}

TEST(ShapeRecognizerTest, wobblyLineRespondsToStrictAndTolerantParams) {
    // Gently bowed stroke: curvature det around 0.025 (rejected at strict 0.005, accepted at tolerant 0.045)
    auto stroke = std::make_unique<Stroke>();
    for (int i = 0; i <= 100; i++) {
        const double t = static_cast<double>(i) / 100.0;
        const double x = 200.0 * t;
        const double y = 14.0 * std::sin(M_PI * t);
        stroke->addPoint(Point(x, y));
    }

    ShapeRecognizer strictReco(xoj::shapes::paramsForSensitivity(0));
    ShapeRecognizerDiagnostics strictDiag;
    EXPECT_EQ(strictReco.recognizePatterns(stroke.get(), MIN_SIZE, &strictDiag), nullptr);
    EXPECT_EQ(strictDiag.shape, RecognizedShapeKind::None);
    EXPECT_FALSE(strictDiag.reasons.empty());

    ShapeRecognizer tolerantReco(xoj::shapes::paramsForSensitivity(100));
    ShapeRecognizerDiagnostics tolerantDiag;
    auto recognized = tolerantReco.recognizePatterns(stroke.get(), MIN_SIZE, &tolerantDiag);
    ASSERT_NE(recognized, nullptr);
    EXPECT_EQ(recognized->getPointCount(), 2U);
    EXPECT_EQ(tolerantDiag.shape, RecognizedShapeKind::Line);
}

TEST(ShapeRecognizerTest, imperfectCircleRespondsToStrictAndTolerantParams) {
    // Slightly oval circle (rx=60, ry=48): det is around 0.90 (below strict 0.985, above tolerant 0.85)
    auto stroke = std::make_unique<Stroke>();
    for (int i = 0; i <= 120; i++) {
        const double angle = 2.0 * M_PI * i / 120.0;
        stroke->addPoint(Point(100.0 + 60.0 * std::cos(angle), 100.0 + 48.0 * std::sin(angle)));
    }

    ShapeRecognizer strictReco(xoj::shapes::paramsForSensitivity(0));
    ShapeRecognizerDiagnostics strictDiag;
    EXPECT_EQ(strictReco.recognizePatterns(stroke.get(), MIN_SIZE, &strictDiag), nullptr);
    EXPECT_EQ(strictDiag.shape, RecognizedShapeKind::None);
    EXPECT_FALSE(strictDiag.reasons.empty());

    ShapeRecognizer tolerantReco(xoj::shapes::paramsForSensitivity(100));
    ShapeRecognizerDiagnostics tolerantDiag;
    auto recognized = tolerantReco.recognizePatterns(stroke.get(), MIN_SIZE, &tolerantDiag);
    ASSERT_NE(recognized, nullptr);
    EXPECT_EQ(tolerantDiag.shape, RecognizedShapeKind::Circle);
}

TEST(ShapeRecognizerTest, slantToleranceControlsAxisStraightening) {
    // Line slanted by ~7 degrees from horizontal
    const double angleRad = 7.0 * M_PI / 180.0;
    auto stroke = makePolyline({Point(0, 0), Point(200.0 * std::cos(angleRad), 200.0 * std::sin(angleRad))}, 0.0);

    ShapeRecognizerParams noSlant;
    noSlant.slantToleranceDeg = 0.0;
    ShapeRecognizer recoNoSlant(noSlant);
    auto slanted = recoNoSlant.recognizePatterns(stroke.get(), MIN_SIZE);
    ASSERT_NE(slanted, nullptr);
    ASSERT_EQ(slanted->getPointCount(), 2U);
    EXPECT_GT(std::fabs(slanted->getPoint(1).y - slanted->getPoint(0).y), 10.0);

    ShapeRecognizerParams wideSlant;
    wideSlant.slantToleranceDeg = 10.0;
    ShapeRecognizer recoWideSlant(wideSlant);
    auto straightened = recoWideSlant.recognizePatterns(stroke.get(), MIN_SIZE);
    ASSERT_NE(straightened, nullptr);
    ASSERT_EQ(straightened->getPointCount(), 2U);
    EXPECT_NEAR(straightened->getPoint(1).y, straightened->getPoint(0).y, 1e-6);
}

TEST(ShapeRecognizerTest, diagnosticsReportTooSmallStroke) {
    auto stroke = makePolyline({Point(0, 0), Point(15, 0)}, 0.0, 1.0);
    ShapeRecognizer reco;
    ShapeRecognizerDiagnostics diag;
    EXPECT_EQ(reco.recognizePatterns(stroke.get(), MIN_SIZE, &diag), nullptr);
    EXPECT_EQ(diag.shape, RecognizedShapeKind::None);
    ASSERT_EQ(diag.reasons.size(), 1U);
    EXPECT_NE(diag.reasons[0].find("15"), std::string::npos);
    EXPECT_NE(diag.reasons[0].find("40"), std::string::npos);
}
