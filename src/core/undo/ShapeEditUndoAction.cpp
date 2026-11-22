#include "ShapeEditUndoAction.h"

#include <mutex>    // for lock_guard
#include <utility>  // for move, swap

#include "control/Control.h"  // for Control
#include "model/Document.h"   // for Document
#include "model/XojPage.h"    // for XojPage
#include "util/Range.h"       // for Range

ShapeEditUndoAction::ShapeEditUndoAction(const PageRef& page, Stroke* target, Stroke before, std::string text):
        UndoAction("ShapeEditUndoAction"), target(target), other(std::move(before)), text(std::move(text)) {
    this->page = page;
}

ShapeEditUndoAction::~ShapeEditUndoAction() = default;

void ShapeEditUndoAction::swapStates(Document* doc) {
    Range range;
    {
        std::lock_guard lock(*doc);
        range = Range(this->target->getBoundingBox());
        std::swap(*this->target, this->other);
        range = range.unite(Range(this->target->getBoundingBox()));
    }
    this->page->fireRangeChanged(range);
}

auto ShapeEditUndoAction::undo(Control* control) -> bool {
    swapStates(control->getDocument());
    this->undone = true;
    return true;
}

auto ShapeEditUndoAction::redo(Control* control) -> bool {
    swapStates(control->getDocument());
    this->undone = false;
    return true;
}

auto ShapeEditUndoAction::getText() -> std::string { return this->text; }
