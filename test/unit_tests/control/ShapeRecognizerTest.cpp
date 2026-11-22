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
#include <vector>  // for vector

#include <gtest/gtest.h>

#include "control/shaperecognizer/ShapeRecognizer.h"
#include "model/Point.h"
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
