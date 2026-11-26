/*
 * MoreXournal
 *
 * Geometric description of a shape stroke (line, polygon, rectangle, ellipse)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>     // for array
#include <cstddef>   // for size_t
#include <optional>  // for optional
#include <vector>    // for vector

#include "util/Point.h"  // for Point
#include "util/Range.h"  // for Range

class Point;

namespace xoj::util {
struct Matrix;
template <class T>
class Rectangle;
}  // namespace xoj::util

class ShapeType {
public:
    enum Value { LINE, POLYGON, RECTANGLE, ELLIPSE };
    // Values of the mxShape attribute in .xopp files
    static constexpr std::array<const char8_t*, 4> NAMES = {u8"line", u8"polygon", u8"rectangle", u8"ellipse"};
    ShapeType(Value v): value(v) {}

    operator const Value&() const { return value; }
    operator Value&() { return value; }

private:
    Value value;
};

/**
 * @brief The parameters of a shape, in page coordinates. All control points are points (not vectors), so mapping each
 * of them with an affine transformation maps the whole shape exactly.
 *  - LINE: [A, B]
 *  - POLYGON: [V0, ..., Vn-1], with MIN_POLYGON_VERTICES <= n <= MAX_POLYGON_VERTICES
 *  - RECTANGLE: [O, O+u, O+v]: corners O, O+u, O+u+v, O+v. After a non-uniform scaling of a rotated rectangle this is a
 *    parallelogram, which is kept as a RECTANGLE.
 *  - ELLIPSE: [C, C+a, C+b]: points C + a cos(t) + b sin(t), where a and b are conjugate semi-diameters
 */
struct ShapeInfo {
    ShapeType type = ShapeType::LINE;
    std::vector<xoj::util::Point<double>> pts;

    static constexpr size_t MIN_POLYGON_VERTICES = 3;
    static constexpr size_t MAX_POLYGON_VERTICES = 8;

    bool operator==(const ShapeInfo& other) const;

    /**
     * @brief The points of the stroke drawing this shape, without pressure.
     * Line: 2 points. Polygon and rectangle: the vertices, closed by a copy of the first one. Ellipse: 4n + 1 points
     * (closed), with n depending on the size like in EllipseHandler.
     */
    [[nodiscard]] std::vector<Point> generatePoints() const;

    /// Map every control point with the matrix
    void transform(const xoj::util::Matrix& m);

    /// True if the number of control points matches the type, all coordinates are finite and the shape is not
    /// degenerate (a line of length 0, a flat rectangle or ellipse)
    [[nodiscard]] bool isValid() const;

    /// Smallest range containing the outline (the stroke width is not taken into account)
    [[nodiscard]] Range outlineRange() const;

    /**
     * @brief Check that the outline matches the snapping box of a stroke, i.e. that the stroke's points draw this
     * shape. The tolerance is max(0.5, 1% of the box's diagonal) on each side.
     */
    [[nodiscard]] bool matchesBounds(const xoj::util::Rectangle<double>& snappedBounds) const;

    /// Control points as x0 y0 x1 y1 ...
    [[nodiscard]] std::vector<double> toDoubles() const;

    /// @return The shape, or std::nullopt if the values do not make a valid shape of this type (see isValid())
    static std::optional<ShapeInfo> fromDoubles(ShapeType type, const std::vector<double>& values);
};
