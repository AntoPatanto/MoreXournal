#include "CircleRecognizer.h"

#include <cmath>     // for hypot, cos, fabs, sin, M_PI
#include <iterator>  // for begin, end, next
#include <memory>
#include <utility>   // for move
#include <vector>    // for vector

#include "model/Point.h"      // for Point
#include "model/ShapeInfo.h"  // for ShapeInfo, ShapeType
#include "model/Stroke.h"     // for Stroke
#include "util/i18n.h"        // for _F, FS

#include "Inertia.h"                // for Inertia
#include "ShapeRecognizerConfig.h"  // for RDEBUG
#include "ShapeSensitivityLogic.h"  // for formatFixed

/**
 * Create circle stroke for inertia
 */
auto CircleRecognizer::makeCircleShape(Stroke* originalStroke, Inertia& inertia) -> std::unique_ptr<Stroke> {
    int npts = static_cast<int>(2 * inertia.rad());
    if (npts < 24) {
        npts = 24;  // min. number of points
    }

    auto s = std::make_unique<Stroke>();
    s->applyStyleFrom(originalStroke);

    for (int i = 0; i <= npts; i++) {
        double x = inertia.centerX() + inertia.rad() * cos((2 * M_PI * i) / npts);
        double y = inertia.centerY() + inertia.rad() * sin((2 * M_PI * i) / npts);
        s->addPoint(Point(x, y));
    }

    // After the addPoint() calls, which drop the shape
    const double cx = inertia.centerX();
    const double cy = inertia.centerY();
    const double r = inertia.rad();
    ShapeInfo info{ShapeType::ELLIPSE, {{cx, cy}, {cx + r, cy}, {cx, cy + r}}};
    if (info.isValid()) {
        s->attachShape(std::move(info));
    }

    return s;
}

/**
 *  Test if we have a circle; inertia has been precomputed by caller
 */
auto CircleRecognizer::scoreCircle(Stroke* s, Inertia& inertia) -> double {
    double r0 = inertia.rad();
    double divisor = inertia.getMass() * r0;

    // Todo: test: std::abs(divisor) <= std::numeric_limits<double>::epsilon()
    if (divisor == 0) {
        return 0;
    }

    double sum = 0.0;
    double x0 = inertia.centerX();
    double y0 = inertia.centerY();

    auto const& pv = s->getPointVector();
    for (auto pt_1st = begin(pv), pt_2nd = std::next(pt_1st), p_end_i = end(pv); pt_1st != p_end_i && pt_2nd != p_end_i;
         ++pt_2nd, ++pt_1st) {
        double dm = hypot(pt_2nd->x - pt_1st->x, pt_2nd->y - pt_1st->y);
        double deltar = hypot(pt_1st->x - x0, pt_1st->y - y0) - r0;
        sum += dm * fabs(deltar);
    }

    return sum / (divisor);
}

auto CircleRecognizer::recognize(Stroke* stroke, const ShapeRecognizerParams& params,
                                 ShapeRecognizerDiagnostics* diagnostics) -> std::unique_ptr<Stroke> {
    Inertia s;
    s.calc(stroke->getPointVector());
    const double det = s.det();
    RDEBUG("Mass=%.0f, Center=(%.1f,%.1f), I=(%.0f,%.0f, %.0f), Rad=%.2f, Det=%.4f", s.getMass(), s.centerX(),
           s.centerY(), s.xx(), s.yy(), s.xy(), s.rad(), det);

    if (det > params.circleMinDet) {
        double score = CircleRecognizer::scoreCircle(stroke, s);
        RDEBUG("Circle score: %.2f", score);
        if (score < params.circleMaxScore) {
            if (diagnostics) {
                diagnostics->shape = RecognizedShapeKind::Circle;
            }
            return CircleRecognizer::makeCircleShape(stroke, s);
        }
        if (diagnostics) {
            diagnostics->reasons.push_back(FS(_F("Not a circle: radius error {1} (limit {2})") %
                                              xoj::shapes::formatFixed(score, 3) %
                                              xoj::shapes::formatFixed(params.circleMaxScore, 3)));
        }
    } else if (diagnostics) {
        diagnostics->reasons.push_back(FS(_F("Not a circle: roundness {1} (minimum {2})") %
                                          xoj::shapes::formatFixed(det, 3) %
                                          xoj::shapes::formatFixed(params.circleMinDet, 3)));
    }

    return nullptr;
}
