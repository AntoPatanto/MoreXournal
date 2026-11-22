#include "FillColorUndoAction.h"

#include <mutex>  // for lock_guard

#include "control/Control.h"  // for Control
#include "model/Document.h"   // for Document
#include "model/Stroke.h"     // for Stroke
#include "model/XojPage.h"    // for XojPage
#include "util/Range.h"       // for Range
#include "util/i18n.h"        // for _

FillColorUndoAction::FillColorUndoAction(const PageRef& page): UndoAction("FillColorUndoAction") { this->page = page; }

FillColorUndoAction::~FillColorUndoAction() = default;

void FillColorUndoAction::addStroke(Stroke* s, FillState before, FillState after) {
    this->entries.push_back({s, std::move(before), std::move(after)});
}

bool FillColorUndoAction::empty() const { return this->entries.empty(); }

auto FillColorUndoAction::stateOf(const Stroke* s) -> FillState { return {s->getFill(), s->getFillColor()}; }

void FillColorUndoAction::apply(Document* doc, bool undo) {
    if (this->entries.empty()) {
        return;
    }
    Range range;
    {
        std::lock_guard lock(*doc);
        for (const Entry& e: this->entries) {
            const FillState& state = undo ? e.before : e.after;
            e.s->setFill(state.fill);
            e.s->setFillColor(state.fillColor);
            range = range.unite(Range(e.s->getBoundingBox()));
        }
    }
    this->page->fireRangeChanged(range);
}

auto FillColorUndoAction::undo(Control* control) -> bool {
    apply(control->getDocument(), true);
    this->undone = true;
    return true;
}

auto FillColorUndoAction::redo(Control* control) -> bool {
    apply(control->getDocument(), false);
    this->undone = false;
    return true;
}

auto FillColorUndoAction::getText() -> std::string { return _("Change fill"); }
