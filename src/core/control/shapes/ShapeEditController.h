/*
 * MoreXournal
 *
 * Edits of the shape shown in a selection, each one undoable in one step
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "model/ShapeInfo.h"  // for ShapeInfo

class Control;
class EditSelection;

/**
 * Every function takes a selection in shape mode (EditSelection::getShapeStroke() != nullptr), finalizes it (the
 * selection is deleted), edits the stroke on its layer and pushes one undo action. Unless said otherwise, the stroke is
 * then selected again, with the same transient flag.
 */
namespace ShapeEditController {
/**
 * @brief Give the shape a new geometry
 * @param info The new geometry, in the coordinates of the stroke without the pending move of the selection (the ones of
 * getShapeStroke()->getShape())
 */
void applyGeometry(Control* control, EditSelection* sel, ShapeInfo info);

/// Drop the shape metadata and keep the drawing. A transient selection is closed, another one shows the normal box.
void convertToStroke(Control* control, EditSelection* sel);

/// Add a copy of the shape, shifted down and right, on top of the layer, and select the copy
void duplicate(Control* control, EditSelection* sel);
}  // namespace ShapeEditController
