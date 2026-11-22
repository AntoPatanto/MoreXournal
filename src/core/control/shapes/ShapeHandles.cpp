#include "ShapeHandles.h"

#include <algorithm>  // for max
#include <cmath>      // for abs, copysign, hypot

#include "model/Point.h"  // for Point
#include "util/Matrix.h"  // for Matrix
#include "util/Range.h"   // for Range

using xoj::util::Matrix;

namespace ShapeHandles {
namespace {
constexpr double MIN_SIZE_FLOOR = 1e-3;

double cross(const Vec& u, const Vec& v) { return u.x * v.y - u.y * v.x; }
double dot(const Vec& u, const Vec& v) { return u.x * v.x + u.y * v.y; }
double norm(const Vec& u) { return std::hypot(u.x, u.y); }

Vec toVec(const Point& p) { return {p.x, p.y}; }

double distanceToSegment(const Vec& p, const Vec& a, const Vec& b) {
    const Vec ab = b - a;
    const double len2 = dot(ab, ab);
    if (len2 == 0.0) {
        return norm(p - a);
    }
    double t = dot(p - a, ab) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return norm(p - (a + ab * t));
}

/// The rotation knob sits above the topmost point of the outline, on the vertical through the centre
Handle rotationKnob(const ShapeInfo& info, double rotateDistance) {
    const Range rg = info.outlineRange();
    const Vec anchor{center(info).x, rg.minY};
    return {anchor - Vec{0, rotateDistance}, HandleKind::ROTATE, anchor};
}

/// Corner k of the frame [O, O+u, O+v] as (s, t) in O + s u + t v
constexpr int CORNER_S[4] = {0, 1, 1, 0};
constexpr int CORNER_T[4] = {0, 0, 1, 1};

/// Clamp a signed length so that its absolute value is at least minLength, keeping its sign (or `fallbackSign` for 0)
double clampLength(double length, double minLength, double fallbackSign) {
    if (std::abs(length) >= minLength) {
        return length;
    }
    return std::copysign(minLength, length != 0.0 ? length : fallbackSign);
}
}  // namespace

std::vector<Handle> handlesFor(const ShapeInfo& info, double rotateDistance) {
    std::vector<Handle> res;
    switch (info.type) {
        case ShapeType::LINE:
        case ShapeType::POLYGON:
            for (const auto& p: info.pts) {
                res.push_back({p, HandleKind::VERTEX, p});
            }
            break;
        case ShapeType::RECTANGLE: {
            if (info.pts.size() != 3) {
                break;
            }
            const Vec& o = info.pts[0];
            const Vec u = info.pts[1] - o;
            const Vec v = info.pts[2] - o;
            for (int k = 0; k < 4; k++) {
                const Vec c = o + u * CORNER_S[k] + v * CORNER_T[k];
                res.push_back({c, HandleKind::VERTEX, c});
            }
            res.push_back(rotationKnob(info, rotateDistance));
            break;
        }
        case ShapeType::ELLIPSE: {
            if (info.pts.size() != 3) {
                break;
            }
            const Vec& c = info.pts[0];
            const Vec a = info.pts[1] - c;
            const Vec b = info.pts[2] - c;
            for (const Vec& p: {c + a, c + b, c - a, c - b}) {
                res.push_back({p, HandleKind::SIDE, p});
            }
            res.push_back(rotationKnob(info, rotateDistance));
            break;
        }
    }
    return res;
}

Vec center(const ShapeInfo& info) {
    if (info.pts.empty()) {
        return {0, 0};
    }
    switch (info.type) {
        case ShapeType::RECTANGLE:
            // O + (u + v) / 2
            return (info.pts[1] + info.pts[2]) / 2.0;
        case ShapeType::ELLIPSE:
            return info.pts[0];
        case ShapeType::LINE:
        case ShapeType::POLYGON:
        default: {
            Vec sum{0, 0};
            for (const auto& p: info.pts) {
                sum += p;
            }
            return sum / static_cast<double>(info.pts.size());
        }
    }
}

ShapeInfo dragHandle(const ShapeInfo& base, size_t index, Vec target, double minSize) {
    // Never produce a degenerate shape, even if the caller passes 0
    minSize = std::max(minSize, MIN_SIZE_FLOOR);
    ShapeInfo res = base;
    switch (base.type) {
        case ShapeType::LINE:
            if (index < 2) {
                const double length = norm(target - base.pts[1 - index]);
                if (length > 0.0 && length >= minSize) {
                    res.pts[index] = target;
                }
            }
            break;
        case ShapeType::POLYGON:
            if (index < base.pts.size()) {
                res.pts[index] = target;
            }
            break;
        case ShapeType::RECTANGLE: {
            if (index >= 4 || base.pts.size() != 3) {
                break;
            }
            const Vec& o = base.pts[0];
            const Vec u = base.pts[1] - o;
            const Vec v = base.pts[2] - o;
            const size_t opp = (index + 2) % 4;
            const Vec fixed = o + u * CORNER_S[opp] + v * CORNER_T[opp];
            // Solve target - fixed = alpha u + beta v
            const Vec d = target - fixed;
            const double det = cross(u, v);
            double alpha = cross(d, v) / det;
            double beta = cross(u, d) / det;
            // The corner moves by +-u and +-v from the fixed one
            const double ds = CORNER_S[index] - CORNER_S[opp];
            const double dt = CORNER_T[index] - CORNER_T[opp];
            alpha = clampLength(alpha * norm(u), minSize, ds) / norm(u);
            beta = clampLength(beta * norm(v), minSize, dt) / norm(v);
            const Vec newU = u * (alpha / ds);
            const Vec newV = v * (beta / dt);
            const Vec newO = fixed - newU * CORNER_S[opp] - newV * CORNER_T[opp];
            res.pts = {newO, newO + newU, newO + newV};
            break;
        }
        case ShapeType::ELLIPSE: {
            if (index >= 4 || base.pts.size() != 3) {
                break;
            }
            const Vec& c = base.pts[0];
            const bool alongA = index % 2 == 0;
            const double sign = index < 2 ? 1.0 : -1.0;
            const Vec axis = (alongA ? base.pts[1] : base.pts[2]) - c;
            // Unit vector from the fixed opposite side towards the dragged one
            const Vec dir = axis * (sign / norm(axis));
            const Vec fixed = c - axis * sign;
            const double diameter = clampLength(dot(target - fixed, dir), minSize, 1.0);
            const Vec newC = fixed + dir * (diameter / 2);
            const Vec newAxis = dir * (sign * diameter / 2);
            const Vec other = (alongA ? base.pts[2] : base.pts[1]) - c;
            const Vec newA = alongA ? newAxis : other;
            const Vec newB = alongA ? other : newAxis;
            res.pts = {newC, newC + newA, newC + newB};
            break;
        }
    }
    return res;
}

ShapeInfo rotate(const ShapeInfo& info, Vec c, double angle) {
    ShapeInfo res = info;
    res.transform(Matrix::TRANSLATION(c.x, c.y).rotate(angle).translate(-c.x, -c.y));
    return res;
}

std::optional<size_t> handleAt(const std::vector<Handle>& handles, Vec p, double radius) {
    std::optional<size_t> best;
    double bestDistance = radius;
    for (size_t i = 0; i < handles.size(); i++) {
        const double dist = norm(handles[i].pos - p);
        if (dist <= bestDistance) {
            best = i;
            bestDistance = dist;
        }
    }
    return best;
}

bool contains(const ShapeInfo& info, Vec p, double tolerance) {
    const std::vector<Point> outline = info.generatePoints();
    if (outline.empty()) {
        return false;
    }
    for (size_t i = 0; i + 1 < outline.size(); i++) {
        if (distanceToSegment(p, toVec(outline[i]), toVec(outline[i + 1])) <= tolerance) {
            return true;
        }
    }
    if (info.type == ShapeType::LINE) {
        return false;
    }
    // Even-odd rule on the closed outline
    bool inside = false;
    for (size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++) {
        const Point& a = outline[i];
        const Point& b = outline[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}
}  // namespace ShapeHandles
