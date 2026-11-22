#include "ShapeInfo.h"

#include <algorithm>  // for all_of, max, min
#include <cmath>      // for ceil, cos, hypot, isfinite, sin, sqrt, M_PI

#include "model/Point.h"     // for Point
#include "util/Matrix.h"     // for Matrix
#include "util/Rectangle.h"  // for Rectangle

using xoj::util::Matrix;
using Vec = xoj::util::Point<double>;

static double cross(const Vec& u, const Vec& v) { return u.x * v.y - u.y * v.x; }

bool ShapeInfo::operator==(const ShapeInfo& other) const { return type == other.type && pts == other.pts; }

size_t ShapeInfo::controlPointCount(ShapeType type) {
    switch (type) {
        case ShapeType::LINE:
            return 2;
        case ShapeType::RECTANGLE:
        case ShapeType::ELLIPSE:
            return 3;
        case ShapeType::POLYGON:
        default:
            return 0;
    }
}

auto ShapeInfo::generatePoints() const -> std::vector<Point> {
    std::vector<Point> res;
    switch (type) {
        case ShapeType::LINE:
        case ShapeType::POLYGON:
            res.reserve(pts.size() + 1);
            for (const auto& p: pts) {
                res.emplace_back(p.x, p.y);
            }
            if (type == ShapeType::POLYGON && !res.empty()) {
                res.emplace_back(res.front());
            }
            break;
        case ShapeType::RECTANGLE: {
            if (pts.size() != 3) {
                break;
            }
            const Vec& o = pts[0];
            const Vec u = pts[1] - o;
            const Vec v = pts[2] - o;
            const Vec opposite = o + u + v;
            res = {Point(o.x, o.y), Point(pts[1].x, pts[1].y), Point(opposite.x, opposite.y), Point(pts[2].x, pts[2].y),
                   Point(o.x, o.y)};
            break;
        }
        case ShapeType::ELLIPSE: {
            if (pts.size() != 3) {
                break;
            }
            const Vec& c = pts[0];
            const Vec a = pts[1] - c;
            const Vec b = pts[2] - c;
            // Same resolution heuristic as EllipseHandler.
            // shortcut: the count is capped so that a huge ellipse from a file cannot exhaust memory; raise the cap if
            // very large ellipses ever look jagged.
            constexpr double MAX_POINTS_PER_QUADRANT = 2048;
            const double perQuadrant = std::min(std::ceil(5 + 0.3 * (std::hypot(a.x, a.y) + std::hypot(b.x, b.y))),
                                                MAX_POINTS_PER_QUADRANT);
            const auto n = 4 * static_cast<size_t>(perQuadrant);
            res.reserve(n + 1);
            for (size_t i = 0; i < n; i++) {
                const double t = 2 * M_PI * static_cast<double>(i) / static_cast<double>(n);
                const double cs = std::cos(t);
                const double sn = std::sin(t);
                res.emplace_back(c.x + a.x * cs + b.x * sn, c.y + a.y * cs + b.y * sn);
            }
            res.emplace_back(res.front());
            break;
        }
    }
    return res;
}

void ShapeInfo::transform(const Matrix& m) {
    for (auto& p: pts) {
        p = m * p;
    }
}

bool ShapeInfo::isValid() const {
    if (!std::all_of(pts.begin(), pts.end(), [](const Vec& p) { return std::isfinite(p.x) && std::isfinite(p.y); })) {
        return false;
    }
    switch (type) {
        case ShapeType::LINE:
            return pts.size() == 2 && pts[0] != pts[1];
        case ShapeType::POLYGON:
            return pts.size() >= MIN_POLYGON_VERTICES && pts.size() <= MAX_POLYGON_VERTICES;
        case ShapeType::RECTANGLE:
        case ShapeType::ELLIPSE:
            // The frame must not be flat
            return pts.size() == 3 && cross(pts[1] - pts[0], pts[2] - pts[0]) != 0.0;
        default:
            return false;
    }
}

auto ShapeInfo::outlineRange() const -> Range {
    Range rg;
    if (type == ShapeType::ELLIPSE && pts.size() == 3) {
        // The extent of C + a cos(t) + b sin(t) along x is sqrt(a.x^2 + b.x^2), and likewise along y
        const Vec& c = pts[0];
        const Vec a = pts[1] - c;
        const Vec b = pts[2] - c;
        const double hx = std::hypot(a.x, b.x);
        const double hy = std::hypot(a.y, b.y);
        return Range(c.x - hx, c.y - hy, c.x + hx, c.y + hy);
    }
    for (const auto& p: generatePoints()) {
        rg.addPoint(p.x, p.y);
    }
    return rg;
}

bool ShapeInfo::matchesBounds(const xoj::util::Rectangle<double>& snappedBounds) const {
    const Range outline = outlineRange();
    if (!outline.isValid()) {
        return false;
    }
    const double tolerance = std::max(0.5, 0.01 * std::hypot(snappedBounds.width, snappedBounds.height));
    const double maxX = snappedBounds.x + snappedBounds.width;
    const double maxY = snappedBounds.y + snappedBounds.height;
    return std::abs(outline.minX - snappedBounds.x) <= tolerance &&
           std::abs(outline.minY - snappedBounds.y) <= tolerance && std::abs(outline.maxX - maxX) <= tolerance &&
           std::abs(outline.maxY - maxY) <= tolerance;
}

auto ShapeInfo::toDoubles() const -> std::vector<double> {
    std::vector<double> res;
    res.reserve(2 * pts.size());
    for (const auto& p: pts) {
        res.emplace_back(p.x);
        res.emplace_back(p.y);
    }
    return res;
}

auto ShapeInfo::fromDoubles(ShapeType type, const std::vector<double>& values) -> std::optional<ShapeInfo> {
    if (values.size() % 2 != 0) {
        return std::nullopt;
    }
    ShapeInfo info{type, {}};
    info.pts.reserve(values.size() / 2);
    for (size_t i = 0; i + 1 < values.size(); i += 2) {
        info.pts.emplace_back(values[i], values[i + 1]);
    }
    if (!info.isValid()) {
        return std::nullopt;
    }
    return info;
}
