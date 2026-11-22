/*
 * MoreXournal
 *
 * Tests of the handle geometry used to edit shapes
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>   // for cos, sin, hypot, M_PI
#include <vector>  // for vector

#include <gtest/gtest.h>

#include "control/shapes/ShapeHandles.h"
#include "model/ShapeInfo.h"

using namespace ShapeHandles;

namespace {
constexpr double EPS = 1e-9;
constexpr double MIN_SIZE = 1.0;

double cross(const Vec& u, const Vec& v) { return u.x * v.y - u.y * v.x; }
double dot(const Vec& u, const Vec& v) { return u.x * v.x + u.y * v.y; }
double norm(const Vec& u) { return std::hypot(u.x, u.y); }

void expectNear(const Vec& actual, const Vec& expected, double tol = EPS) {
    EXPECT_NEAR(actual.x, expected.x, tol);
    EXPECT_NEAR(actual.y, expected.y, tol);
}

/// Corners of a rectangle shape, in the handle order
std::vector<Vec> corners(const ShapeInfo& info) {
    std::vector<Vec> res;
    for (const auto& h: handlesFor(info, 10)) {
        if (h.kind == HandleKind::VERTEX) {
            res.push_back(h.pos);
        }
    }
    return res;
}

/// The frame keeps the directions of base's sides and stays a rectangle
void expectRectangleLike(const ShapeInfo& base, const ShapeInfo& res) {
    ASSERT_TRUE(res.isValid());
    ASSERT_EQ(res.type, ShapeType::RECTANGLE);
    const Vec u0 = base.pts[1] - base.pts[0];
    const Vec v0 = base.pts[2] - base.pts[0];
    const Vec u = res.pts[1] - res.pts[0];
    const Vec v = res.pts[2] - res.pts[0];
    EXPECT_NEAR(dot(u, v), 0.0, 1e-6) << "the sides must stay perpendicular";
    EXPECT_NEAR(cross(u, u0), 0.0, 1e-6) << "u must keep its direction";
    EXPECT_NEAR(cross(v, v0), 0.0, 1e-6) << "v must keep its direction";
}

/// Like RectangleHandler: {p1, (p1.x, p2.y), (p2.x, p1.y)}
ShapeInfo toolRectangle() { return ShapeInfo{ShapeType::RECTANGLE, {{10, 10}, {10, 30}, {50, 10}}}; }

ShapeInfo rotatedRectangle() {
    const Vec o{100, 50};
    const Vec u{40 * std::cos(M_PI / 6), 40 * std::sin(M_PI / 6)};
    const Vec v{-20 * std::sin(M_PI / 6), 20 * std::cos(M_PI / 6)};
    return ShapeInfo{ShapeType::RECTANGLE, {o, o + u, o + v}};
}

ShapeInfo circle() { return ShapeInfo{ShapeType::ELLIPSE, {{100, 100}, {120, 100}, {100, 120}}}; }
}  // namespace

TEST(ShapeHandles, handleCountsAndKinds) {
    const ShapeInfo line{ShapeType::LINE, {{0, 0}, {10, 0}}};
    const ShapeInfo triangle{ShapeType::POLYGON, {{0, 0}, {10, 0}, {0, 10}}};
    EXPECT_EQ(handlesFor(line, 10).size(), 2U);
    EXPECT_EQ(handlesFor(triangle, 10).size(), 3U);

    const auto rect = handlesFor(toolRectangle(), 10);
    ASSERT_EQ(rect.size(), 5U);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(rect[i].kind, HandleKind::VERTEX);
    }
    EXPECT_EQ(rect[4].kind, HandleKind::ROTATE);

    const auto ell = handlesFor(circle(), 10);
    ASSERT_EQ(ell.size(), 5U);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(ell[i].kind, HandleKind::SIDE);
    }
    EXPECT_EQ(ell[4].kind, HandleKind::ROTATE);
    expectNear(ell[0].pos, {120, 100});
    expectNear(ell[2].pos, {80, 100});
}

TEST(ShapeHandles, rotationKnobIsAboveTheShape) {
    for (const ShapeInfo& info: {toolRectangle(), rotatedRectangle(), circle()}) {
        const auto handles = handlesFor(info, 10);
        const Handle& knob = handles.back();
        ASSERT_EQ(knob.kind, HandleKind::ROTATE);
        EXPECT_NEAR(knob.anchor.y, info.outlineRange().minY, EPS);
        EXPECT_NEAR(knob.pos.y, knob.anchor.y - 10, EPS);
        EXPECT_NEAR(knob.pos.x, center(info).x, EPS);
    }
}

TEST(ShapeHandles, rectangleCornerDragKeepsOppositeCornerAndRightAngles) {
    const ShapeInfo base = toolRectangle();
    const auto before = corners(base);
    for (size_t k = 0; k < 4; k++) {
        const Vec target = before[k] + Vec{7, -3};
        const ShapeInfo res = dragHandle(base, k, target, MIN_SIZE);
        expectRectangleLike(base, res);
        const auto after = corners(res);
        expectNear(after[(k + 2) % 4], before[(k + 2) % 4]);
        expectNear(after[k], target);
    }
}

TEST(ShapeHandles, rotatedRectangleStaysRotated) {
    const ShapeInfo base = rotatedRectangle();
    const auto before = corners(base);
    const Vec target{170, 120};
    const ShapeInfo res = dragHandle(base, 2, target, MIN_SIZE);
    expectRectangleLike(base, res);
    const auto after = corners(res);
    expectNear(after[0], before[0]);
    expectNear(after[2], target);
}

TEST(ShapeHandles, rectangleCornerDragPastTheOppositeCornerFlips) {
    const ShapeInfo base = toolRectangle();
    const auto before = corners(base);
    // Corner 2 is (50, 30), the opposite one is (10, 10)
    const Vec target{0, 0};
    const ShapeInfo res = dragHandle(base, 2, target, MIN_SIZE);
    expectRectangleLike(base, res);
    const auto after = corners(res);
    expectNear(after[0], before[0]);
    expectNear(after[2], target);
}

TEST(ShapeHandles, rectangleCornerDragOntoTheOppositeCornerIsClamped) {
    const ShapeInfo base = toolRectangle();
    const auto before = corners(base);
    const ShapeInfo res = dragHandle(base, 2, before[0], MIN_SIZE);
    expectRectangleLike(base, res);
    const Vec u = res.pts[1] - res.pts[0];
    const Vec v = res.pts[2] - res.pts[0];
    EXPECT_NEAR(norm(u), MIN_SIZE, EPS);
    EXPECT_NEAR(norm(v), MIN_SIZE, EPS);
    expectNear(corners(res)[0], before[0]);
}

TEST(ShapeHandles, ellipseSideDragStretchesACircleIntoAnEllipse) {
    const ShapeInfo base = circle();
    // Drag C + a = (120, 100) to the right (the vertical part of the motion is ignored)
    const ShapeInfo res = dragHandle(base, 0, {150, 130}, MIN_SIZE);
    ASSERT_TRUE(res.isValid());
    ASSERT_EQ(res.type, ShapeType::ELLIPSE);
    expectNear(res.pts[0], {115, 100});
    expectNear(res.pts[1] - res.pts[0], {35, 0});
    expectNear(res.pts[2] - res.pts[0], {0, 20});
    // The opposite side did not move
    expectNear(handlesFor(res, 10)[2].pos, {80, 100});
}

TEST(ShapeHandles, ellipseSideDragKeepsTheOppositeSide) {
    const ShapeInfo base{ShapeType::ELLIPSE, {{0, 0}, {30, 10}, {-5, 15}}};
    const auto before = handlesFor(base, 10);
    for (size_t i = 0; i < 4; i++) {
        const ShapeInfo res = dragHandle(base, i, before[i].pos * 1.5 + Vec{3, -2}, MIN_SIZE);
        ASSERT_TRUE(res.isValid());
        const auto after = handlesFor(res, 10);
        expectNear(after[(i + 2) % 4].pos, before[(i + 2) % 4].pos, 1e-9);
        // The other diameter keeps its length and direction
        const size_t other = i % 2 == 0 ? 2 : 1;
        const Vec otherBefore = base.pts[other] - base.pts[0];
        const Vec otherAfter = res.pts[other] - res.pts[0];
        expectNear(otherAfter, otherBefore, 1e-9);
    }
}

TEST(ShapeHandles, ellipseSideDragOntoTheOppositeSideIsClamped) {
    const ShapeInfo res = dragHandle(circle(), 0, {80, 100}, MIN_SIZE);
    ASSERT_TRUE(res.isValid());
    EXPECT_NEAR(norm(res.pts[1] - res.pts[0]), MIN_SIZE / 2, EPS);
}

TEST(ShapeHandles, rotateAboutTheCentre) {
    const ShapeInfo base = toolRectangle();
    const Vec c = center(base);
    expectNear(c, {30, 20});
    const ShapeInfo res = rotate(base, c, M_PI / 2);
    expectNear(center(res), c);
    const auto before = corners(base);
    const auto after = corners(res);
    for (size_t k = 0; k < 4; k++) {
        // A quarter turn maps (dx, dy) to (-dy, dx)
        const Vec d = before[k] - c;
        expectNear(after[k], c + Vec{-d.y, d.x}, 1e-9);
    }
    expectNear(center(circle()), {100, 100});
}

TEST(ShapeHandles, lineEndpointDrag) {
    const ShapeInfo base{ShapeType::LINE, {{0, 0}, {10, 0}}};
    const ShapeInfo res = dragHandle(base, 1, {20, 5}, MIN_SIZE);
    expectNear(res.pts[0], {0, 0});
    expectNear(res.pts[1], {20, 5});

    // Onto the other end: ignored, the line would vanish
    EXPECT_EQ(dragHandle(base, 1, {0.5, 0}, MIN_SIZE), base);
    EXPECT_EQ(dragHandle(base, 0, {10, 0}, 0.0), base);
}

TEST(ShapeHandles, polygonVertexDrag) {
    const ShapeInfo base{ShapeType::POLYGON, {{0, 0}, {10, 0}, {0, 10}}};
    const ShapeInfo res = dragHandle(base, 2, {3, 12}, MIN_SIZE);
    expectNear(res.pts[0], {0, 0});
    expectNear(res.pts[1], {10, 0});
    expectNear(res.pts[2], {3, 12});
}

TEST(ShapeHandles, handleAtPicksTheNearestWithinTheRadius) {
    const std::vector<Handle> handles = {{{0, 0}, HandleKind::VERTEX, {0, 0}}, {{4, 0}, HandleKind::VERTEX, {4, 0}}};
    EXPECT_EQ(handleAt(handles, {1, 0}, 3), 0U);
    EXPECT_EQ(handleAt(handles, {3, 0}, 3), 1U);
    EXPECT_FALSE(handleAt(handles, {10, 0}, 3).has_value());
}

TEST(ShapeHandles, contains) {
    const ShapeInfo rect = toolRectangle();  // (10, 10) to (50, 30)
    EXPECT_TRUE(contains(rect, {30, 20}, 1));
    EXPECT_TRUE(contains(rect, {50.5, 20}, 1));
    EXPECT_FALSE(contains(rect, {60, 20}, 1));
    EXPECT_FALSE(contains(rect, {30, 35}, 1));

    EXPECT_TRUE(contains(circle(), {100, 100}, 1));
    EXPECT_FALSE(contains(circle(), {117, 117}, 1));

    const ShapeInfo line{ShapeType::LINE, {{0, 0}, {10, 0}}};
    EXPECT_TRUE(contains(line, {5, 0.5}, 1));
    EXPECT_FALSE(contains(line, {5, 3}, 1));
    EXPECT_FALSE(contains(line, {12, 0}, 1));
}
