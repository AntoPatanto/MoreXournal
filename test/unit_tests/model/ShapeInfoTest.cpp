/*
 * MoreXournal
 *
 * Tests of the shape metadata of strokes
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>   // for cos, sin, hypot, M_PI, NAN
#include <limits>  // for numeric_limits
#include <memory>  // for unique_ptr
#include <vector>  // for vector

#include <gtest/gtest.h>

#include "model/PathParameter.h"
#include "model/Point.h"
#include "model/ShapeInfo.h"
#include "model/Stroke.h"
#include "util/Color.h"
#include "util/Matrix.h"
#include "util/Rectangle.h"

using xoj::util::Matrix;
using Vec = xoj::util::Point<double>;

namespace {
constexpr double EPS = 1e-9;

ShapeInfo rotatedRectangle() {
    // A 40 x 20 rectangle rotated by 30 degrees
    const Vec o{100, 50};
    const Vec u{40 * std::cos(M_PI / 6), 40 * std::sin(M_PI / 6)};
    const Vec v{-20 * std::sin(M_PI / 6), 20 * std::cos(M_PI / 6)};
    return ShapeInfo{ShapeType::RECTANGLE, {o, o + u, o + v}};
}

ShapeInfo tiltedEllipse() {
    const Vec c{200, 150};
    return ShapeInfo{ShapeType::ELLIPSE, {c, c + Vec{30, 10}, c + Vec{-5, 15}}};
}

/// Distance from p to the ellipse C + a cos(t) + b sin(t), measured in the frame (a, b): 0 on the ellipse
double ellipseResidual(const ShapeInfo& e, const Point& p) {
    const Vec a = e.pts[1] - e.pts[0];
    const Vec b = e.pts[2] - e.pts[0];
    const Vec d = Vec{p.x, p.y} - e.pts[0];
    const double det = a.x * b.y - a.y * b.x;
    // Solve d = a * c + b * s
    const double c = (d.x * b.y - d.y * b.x) / det;
    const double s = (a.x * d.y - a.y * d.x) / det;
    return std::abs(std::hypot(c, s) - 1.0);
}

void expectSamePoints(const std::vector<Point>& actual, const std::vector<Point>& expected, double tol = EPS) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); i++) {
        EXPECT_NEAR(actual[i].x, expected[i].x, tol) << "point " << i;
        EXPECT_NEAR(actual[i].y, expected[i].y, tol) << "point " << i;
    }
}

/// Check that the transformed shape generates the transformed points
void expectTransformConsistent(const ShapeInfo& info, const Matrix& m) {
    ShapeInfo transformed = info;
    transformed.transform(m);
    ASSERT_TRUE(transformed.isValid());
    std::vector<Point> mapped;
    for (const auto& p: info.generatePoints()) {
        const Vec q = m * p.getPosition();
        mapped.emplace_back(q.x, q.y);
    }
    if (info.type == ShapeType::ELLIPSE) {
        // The resolution depends on the size, so compare with the curve rather than point by point
        for (const auto& p: mapped) {
            EXPECT_NEAR(ellipseResidual(transformed, p), 0.0, 1e-9);
        }
        for (const auto& p: transformed.generatePoints()) {
            EXPECT_NEAR(ellipseResidual(transformed, p), 0.0, 1e-9);
        }
    } else {
        expectSamePoints(mapped, transformed.generatePoints());
    }
}

Matrix scaleAboutRotated(double x0, double y0, double fx, double fy, double rotation) {
    // The matrix Stroke::scale() uses
    return Matrix::TRANSLATION(x0, y0).rotate(rotation).scale(fx, fy).rotate(-rotation).translate(-x0, -y0);
}

std::unique_ptr<Stroke> shapeStroke(ShapeInfo info) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(2);
    s->setShape(std::move(info));
    return s;
}
}  // namespace

TEST(ShapeInfo, generatePointsCountAndClosure) {
    const ShapeInfo line{ShapeType::LINE, {{0, 0}, {10, 5}}};
    const auto linePts = line.generatePoints();
    ASSERT_EQ(linePts.size(), 2U);
    EXPECT_TRUE(linePts[1].equalsPos(Point(10, 5)));

    const ShapeInfo triangle{ShapeType::POLYGON, {{0, 0}, {10, 0}, {5, 8}}};
    const auto triPts = triangle.generatePoints();
    ASSERT_EQ(triPts.size(), 4U);
    EXPECT_TRUE(triPts.front().equalsPos(triPts.back()));

    const ShapeInfo rect{ShapeType::RECTANGLE, {{0, 0}, {10, 0}, {0, 4}}};
    expectSamePoints(rect.generatePoints(), {{0, 0}, {10, 0}, {10, 4}, {0, 4}, {0, 0}});

    // Same count as EllipseHandler: 4 * ceil(5 + 0.3 * (|a| + |b|)) + 1
    const ShapeInfo ellipse{ShapeType::ELLIPSE, {{50, 50}, {60, 50}, {50, 65}}};
    const auto ePts = ellipse.generatePoints();
    ASSERT_EQ(ePts.size(), 4U * 13U + 1U);
    EXPECT_TRUE(ePts.front().equalsPos(ePts.back()));
    EXPECT_TRUE(ePts.front().equalsPos(Point(60, 50)));
    for (const auto& p: ePts) {
        EXPECT_NEAR(ellipseResidual(ellipse, p), 0.0, EPS);
    }

    for (const auto& p: ePts) {
        EXPECT_EQ(p.z, Point::NO_PRESSURE);
    }
}

TEST(ShapeInfo, validity) {
    EXPECT_TRUE((ShapeInfo{ShapeType::LINE, {{0, 0}, {1, 0}}}.isValid()));
    EXPECT_FALSE((ShapeInfo{ShapeType::LINE, {{0, 0}, {0, 0}}}.isValid()));
    EXPECT_FALSE((ShapeInfo{ShapeType::LINE, {{0, 0}, {1, 0}, {2, 0}}}.isValid()));
    EXPECT_FALSE((ShapeInfo{ShapeType::POLYGON, {{0, 0}, {1, 0}}}.isValid()));
    EXPECT_FALSE((ShapeInfo{ShapeType::POLYGON, std::vector<Vec>(9, Vec{1, 2})}.isValid()));
    EXPECT_FALSE((ShapeInfo{ShapeType::RECTANGLE, {{0, 0}, {1, 0}, {2, 0}}}.isValid()));  // flat
    EXPECT_FALSE((ShapeInfo{ShapeType::ELLIPSE, {{0, 0}, {NAN, 0}, {0, 1}}}.isValid()));
    EXPECT_FALSE((
            ShapeInfo{ShapeType::RECTANGLE, {{0, 0}, {std::numeric_limits<double>::infinity(), 0}, {0, 1}}}.isValid()));

    EXPECT_TRUE(ShapeInfo::fromDoubles(ShapeType::LINE, {0, 0, 3, 4}));
    EXPECT_FALSE(ShapeInfo::fromDoubles(ShapeType::LINE, {0, 0, 3}));
    EXPECT_FALSE(ShapeInfo::fromDoubles(ShapeType::ELLIPSE, {0, 0, 3, 4}));
    const auto info = ShapeInfo::fromDoubles(ShapeType::POLYGON, {0, 0, 3, 4, 5, 0});
    ASSERT_TRUE(info);
    EXPECT_EQ(info->toDoubles(), (std::vector<double>{0, 0, 3, 4, 5, 0}));
}

TEST(ShapeInfo, outlineMatchesBounds) {
    const auto e = tiltedEllipse();
    Range rg;
    for (const auto& p: e.generatePoints()) {
        rg.addPoint(p.x, p.y);
    }
    const Range outline = e.outlineRange();
    EXPECT_NEAR(outline.minX, rg.minX, 0.05);
    EXPECT_NEAR(outline.maxY, rg.maxY, 0.05);
    EXPECT_TRUE(e.matchesBounds(xoj::util::Rectangle<double>(rg.minX, rg.minY, rg.getWidth(), rg.getHeight())));
    EXPECT_FALSE(e.matchesBounds(xoj::util::Rectangle<double>(rg.minX + 5, rg.minY, rg.getWidth(), rg.getHeight())));
}

TEST(ShapeInfo, transformKeepsShapeAndPointsConsistent) {
    const ShapeInfo line{ShapeType::LINE, {{10, 10}, {50, 30}}};
    const ShapeInfo triangle{ShapeType::POLYGON, {{0, 0}, {40, 0}, {10, 30}}};
    const std::vector<ShapeInfo> shapes = {line, triangle, rotatedRectangle(), tiltedEllipse()};

    const Matrix rotation = Matrix::TRANSLATION(120, 80).rotate(0.7).translate(-120, -80);
    const Matrix uniformScale = scaleAboutRotated(10, 20, 2.5, 2.5, 0);
    // Non-uniform scale of a rotated selection: a rectangle becomes a parallelogram
    const Matrix skewingScale = scaleAboutRotated(10, 20, 2, 0.5, 0.4);
    const Matrix reflection = scaleAboutRotated(100, 100, -1, 1, 0);

    for (const auto& shape: shapes) {
        SCOPED_TRACE(static_cast<int>(shape.type));
        expectTransformConsistent(shape, rotation);
        expectTransformConsistent(shape, uniformScale);
        expectTransformConsistent(shape, skewingScale);
        expectTransformConsistent(shape, reflection);
    }
}

TEST(ShapeInfo, strokeAffineOperationsTransformTheShape) {
    for (const auto& info: {rotatedRectangle(), tiltedEllipse()}) {
        auto s = shapeStroke(info);
        const size_t count = s->getPointCount();
        ASSERT_EQ(count, info.generatePoints().size());

        s->move(12, -7);
        s->rotate(150, 100, 0.3);
        s->scale(150, 100, 1.7, 0.6, 0.25, false);

        ASSERT_TRUE(s->getShape());
        EXPECT_EQ(s->getShape()->type, info.type);
        // The points are transformed, not regenerated: the count stays, so that undo stays exact
        EXPECT_EQ(s->getPointCount(), count);
        if (info.type == ShapeType::ELLIPSE) {
            for (const auto& p: s->getPointVector()) {
                EXPECT_NEAR(ellipseResidual(*s->getShape(), p), 0.0, 1e-9);
            }
        } else {
            expectSamePoints(s->getPointVector(), s->getShape()->generatePoints());
        }
        EXPECT_TRUE(s->getShape()->matchesBounds(s->getSnappedBounds()));
    }
}

TEST(ShapeInfo, pointEditsDropTheShape) {
    const ShapeInfo rect = rotatedRectangle();

    auto s = shapeStroke(rect);
    s->addPoint(Point(0, 0));
    EXPECT_FALSE(s->getShape());

    s = shapeStroke(rect);
    s->setPointVector(std::vector<Point>{{0, 0}, {1, 1}});
    EXPECT_FALSE(s->getShape());

    s = shapeStroke(rect);
    const std::vector<Point> pts = {{0, 0}, {1, 1}};
    s->setPointVector(pts);
    EXPECT_FALSE(s->getShape());

    s = shapeStroke(rect);
    s->deletePointsFrom(2);
    EXPECT_FALSE(s->getShape());

    s = shapeStroke(rect);
    s->setFillColor(Color(0xff00ff00));
    // Eraser splits
    auto section = s->cloneSection(PathParameter(0, 0.5), PathParameter(2, 0.5));
    EXPECT_FALSE(section->getShape());
    EXPECT_EQ(section->getFillColor(), s->getFillColor());
    auto circular = s->cloneCircularSectionOfClosedStroke(PathParameter(2, 0.5), PathParameter(1, 0.5));
    EXPECT_FALSE(circular->getShape());

    Stroke styled;
    styled.applyStyleFrom(s.get());
    EXPECT_FALSE(styled.getShape());
    EXPECT_EQ(styled.getFillColor(), s->getFillColor());

    s->clearShape();
    EXPECT_FALSE(s->getShape());
    EXPECT_EQ(s->getPointCount(), 5U);
}

TEST(ShapeInfo, copiesKeepTheShape) {
    auto s = shapeStroke(tiltedEllipse());
    auto clone = s->cloneStroke();
    ASSERT_TRUE(clone->getShape());
    EXPECT_EQ(*clone->getShape(), *s->getShape());

    Stroke copy = *s;
    ASSERT_TRUE(copy.getShape());
    EXPECT_EQ(*copy.getShape(), *s->getShape());

    auto element = s->clone();
    auto* elementStroke = dynamic_cast<Stroke*>(element.get());
    ASSERT_NE(elementStroke, nullptr);
    EXPECT_TRUE(elementStroke->getShape());
}

TEST(ShapeInfo, degenerateTransformDropsShape) {
    {
        auto rect = shapeStroke(rotatedRectangle());
        rect->scale(100, 50, 0.0, 1.0, 0.0, false);
        EXPECT_FALSE(rect->getShape().has_value());
    }
    {
        auto rect = shapeStroke(rotatedRectangle());
        rect->scale(100, 50, 1.0, 0.0, 0.0, false);
        EXPECT_FALSE(rect->getShape().has_value());
    }
    {
        auto ellipse = shapeStroke(tiltedEllipse());
        ellipse->scale(200, 150, 0.0, 1.0, 0.0, false);
        EXPECT_FALSE(ellipse->getShape().has_value());
    }
    {
        auto line = shapeStroke(ShapeInfo{ShapeType::LINE, {{10, 20}, {50, 20}}});
        line->scale(10, 20, 0.0, 1.0, 0.0, false);
        EXPECT_FALSE(line->getShape().has_value());
    }
}
