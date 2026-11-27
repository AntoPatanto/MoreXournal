#include "ShapeInfo.h"

#include <algorithm>  // for all_of, max, min
#include <cmath>      // for abs, atan2, ceil, cos, hypot, isfinite, sin, M_PI

#include "model/Point.h"  // for Point
#include "util/Matrix.h"  // for Matrix

using xoj::util::Matrix;
using Vec = xoj::util::Point<double>;

static double cross(const Vec& u, const Vec& v) { return u.x * v.y - u.y * v.x; }

bool ShapeInfo::operator==(const ShapeInfo& other) const { return type == other.type && pts == other.pts; }

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

bool ShapeInfo::matchesPoints(const std::vector<Point>& strokePoints) const {
    if (!isValid() || strokePoints.size() < 2) {
        return false;
    }

    const Range outline = outlineRange();
    if (!outline.isValid()) {
        return false;
    }
    const double tolerance = std::max(0.5, 0.01 * std::hypot(outline.getWidth(), outline.getHeight()));

    Range strokeRange;
    for (const auto& p: strokePoints) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return false;
        }
        strokeRange.addPoint(p.x, p.y);
    }
    if (std::abs(outline.minX - strokeRange.minX) > tolerance ||
        std::abs(outline.minY - strokeRange.minY) > tolerance ||
        std::abs(outline.maxX - strokeRange.maxX) > tolerance ||
        std::abs(outline.maxY - strokeRange.maxY) > tolerance) {
        return false;
    }

    if (type == ShapeType::ELLIPSE) {
        // Ellipses drawn by EllipseHandler use non-uniform angular spacing and keep their original point count
        // across affine transforms, so check closure, curve distance, step size and full-loop winding in the
        // normalized (a, b) frame.
        constexpr size_t MIN_ELLIPSE_POINTS = 9;
        if (strokePoints.size() < MIN_ELLIPSE_POINTS) {
            return false;
        }
        if (std::hypot(strokePoints.front().x - strokePoints.back().x, strokePoints.front().y - strokePoints.back().y) >
            tolerance) {
            return false;
        }

        const Vec& c = pts[0];
        const Vec a = pts[1] - c;
        const Vec b = pts[2] - c;
        const double det = cross(a, b);
        const double minSemiAxis = std::min(std::hypot(a.x, a.y), std::hypot(b.x, b.y));

        auto wrapDelta = [](double d) {
            if (d > M_PI) {
                d -= 2.0 * M_PI;
            } else if (d < -M_PI) {
                d += 2.0 * M_PI;
            }
            return d;
        };

        double firstAngle = 0.0;
        double prevAngle = 0.0;
        double totalAngle = 0.0;
        for (size_t i = 0; i < strokePoints.size(); i++) {
            const double dx = strokePoints[i].x - c.x;
            const double dy = strokePoints[i].y - c.y;
            const double u = (dx * b.y - dy * b.x) / det;
            const double v = (a.x * dy - a.y * dx) / det;
            const double r = std::hypot(u, v);
            const double radialDist = (r > 1e-9) ? (std::abs(r - 1.0) / r) * std::hypot(dx, dy) : minSemiAxis;
            if (!std::isfinite(radialDist) || radialDist > tolerance) {
                return false;
            }
            const double angle = std::atan2(v, u);
            if (i == 0) {
                firstAngle = angle;
            } else {
                const double delta = wrapDelta(angle - prevAngle);
                if (std::abs(delta) >= 0.5 * M_PI) {
                    return false;
                }
                totalAngle += delta;
            }
            prevAngle = angle;
        }
        const double closeDelta = wrapDelta(firstAngle - prevAngle);
        if (std::abs(closeDelta) >= 0.5 * M_PI) {
            return false;
        }
        totalAngle += closeDelta;
        return std::abs(std::abs(totalAngle) - 2.0 * M_PI) <= 0.1;
    }

    const std::vector<Point> expected = generatePoints();
    if (strokePoints.size() != expected.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.size(); i++) {
        if (std::hypot(strokePoints[i].x - expected[i].x, strokePoints[i].y - expected[i].y) > tolerance) {
            return false;
        }
    }
    return true;
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
