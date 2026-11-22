/*
 * MoreXournal
 *
 * Geometry of the handles used to edit a shape (no GTK, no document)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>   // for size_t
#include <optional>  // for optional
#include <vector>    // for vector

#include "model/ShapeInfo.h"  // for ShapeInfo
#include "util/Point.h"       // for Point

namespace ShapeHandles {
using Vec = xoj::util::Point<double>;

enum class HandleKind {
    VERTEX,  ///< A line end, a polygon vertex or a rectangle corner
    SIDE,    ///< The end of an ellipse's semi-diameter
    ROTATE   ///< The rotation knob
};

struct Handle {
    Vec pos;
    HandleKind kind;
    /// Point of the shape the handle is attached to. For the rotation knob, the end of its connector on the shape.
    Vec anchor;
};

/**
 * @brief The handles of a shape. The index of a handle only depends on the type:
 *  - LINE: 0 and 1 are the ends A and B
 *  - POLYGON: i is the vertex i
 *  - RECTANGLE: 0 to 3 are the corners O, O+u, O+u+v, O+v, and 4 is the rotation knob
 *  - ELLIPSE: 0 to 3 are C+a, C+b, C-a, C-b, and 4 is the rotation knob
 * @param rotateDistance Distance between the top of the shape and the rotation knob
 */
std::vector<Handle> handlesFor(const ShapeInfo& info, double rotateDistance);

/// The centre of rotation of the shape
Vec center(const ShapeInfo& info);

/**
 * @brief The shape after dragging the handle `index` (not the rotation knob) to `target`.
 *  - Line end, polygon vertex: the point goes to the target.
 *  - Rectangle corner: the opposite corner stays fixed and the sides keep their directions, so a rectangle stays a
 *    rectangle (it may flip).
 *  - Ellipse side: the opposite side stays fixed; only the length of that semi-diameter changes.
 * @param minSize Smallest length of a line, of a rectangle side or of an ellipse diameter. A drag below it is clamped
 * (rectangle, ellipse) or ignored (line).
 */
ShapeInfo dragHandle(const ShapeInfo& base, size_t index, Vec target, double minSize);

/// The shape rotated by `angle` (radians, same orientation as xoj::util::Matrix::ROTATION) around `c`
ShapeInfo rotate(const ShapeInfo& info, Vec c, double angle);

/// @return The index of the handle nearest to p within `radius`, if any
std::optional<size_t> handleAt(const std::vector<Handle>& handles, Vec p, double radius);

/**
 * @brief Whether p grabs the shape: inside a closed shape, or within `tolerance` of its outline.
 * The interior counts even when the shape is not filled.
 */
bool contains(const ShapeInfo& info, Vec p, double tolerance);
}  // namespace ShapeHandles
