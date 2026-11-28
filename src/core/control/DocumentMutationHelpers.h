/*
 * MoreXournal
 *
 * Shared helpers for adding elements to a document layer with locking and undo support
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model/Element.h"
#include "model/Font.h"
#include "model/PageRef.h"
#include "model/Text.h"
#include "util/Color.h"

class Control;
class Layer;

namespace xoj::control {

/// Build a Text element positioned at (x, y) in page coordinates.
auto createTextElement(std::string text, const XojFont& font, Color color, double x, double y,
                       double wrap = Text::NO_WRAP) -> std::unique_ptr<Text>;

/// Insert elements into layer under the document lock, optionally firing page repaint notifications,
/// and register one grouped UndoAction (InsertUndoAction for 1 element, InsertsUndoAction for >1).
/// Returns non-owning pointers to the inserted elements in insertion order.
auto insertElementsWithGroupedUndo(Control* control, const PageRef& page, Layer* layer,
                                   std::vector<ElementPtr> elements, bool fireChanged = true)
        -> std::vector<const Element*>;

}  // namespace xoj::control
