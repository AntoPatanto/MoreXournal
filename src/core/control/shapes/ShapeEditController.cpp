#include "ShapeEditController.h"

#include <memory>   // for make_unique, unique_ptr
#include <mutex>    // for lock_guard
#include <utility>  // for move

#include "control/Control.h"              // for Control
#include "control/tools/EditSelection.h"  // for EditSelection, SelectionFactory
#include "gui/MainWindow.h"               // for MainWindow
#include "gui/PageView.h"                 // for XojPageView
#include "gui/XournalView.h"              // for XournalView
#include "model/Document.h"               // for Document
#include "model/Layer.h"                  // for Layer
#include "model/Stroke.h"                 // for Stroke
#include "model/XojPage.h"                // for XojPage
#include "undo/ShapeEditUndoAction.h"     // for ShapeEditUndoAction
#include "undo/UndoRedoHandler.h"         // for UndoRedoHandler
#include "util/Assert.h"                  // for xoj_assert
#include "util/Matrix.h"                  // for Matrix
#include "util/Range.h"                   // for Range
#include "util/i18n.h"                    // for _

namespace {
struct Committed {
    Stroke* stroke;
    XojPageView* view;  ///< The view of the page the stroke is on now
    bool transient;
    /// How much the stroke moved when the selection was finalized (its pending move)
    xoj::util::Point<double> shift;
};

/// Finalize the selection: the stroke goes back to a layer, with the pending move of the selection applied
Committed commit(Control* control, EditSelection* sel) {
    Stroke* s = sel->getShapeStroke();
    xoj_assert(s);
    const auto before = s->getShape()->pts.front();
    sel->setGrabPointToCenter();
    XojPageView* view = sel->getDestinationView();
    const bool transient = sel->isTransient();

    control->getWindow()->getXournal()->clearSelection();  // Deletes sel

    const auto after = s->getShape() ? s->getShape()->pts.front() : before;
    return {s, view, transient, after - before};
}

/// Select the stroke, which is on the selected layer of the view's page
void select(Control* control, XojPageView* view, Stroke* s, bool transient) {
    auto sel = SelectionFactory::createFromElementOnActiveLayer(control, view->getPage(), view, s);
    sel->setTransient(transient);
    control->getWindow()->getXournal()->setSelection(sel.release());
}

/// Replace *s by its edited version (under the lock) and push the undo action
template <class Edit>
void editStroke(Control* control, const Committed& c, const char* text, Edit edit) {
    Stroke* s = c.stroke;
    const PageRef page = c.view->getPage();
    Stroke before = *s;
    Range range;
    {
        std::lock_guard lock(*control->getDocument());
        range = Range(s->getBoundingBox());
        edit(*s);
        range = range.unite(Range(s->getBoundingBox()));
    }
    control->getUndoRedoHandler()->addUndoAction(
            std::make_unique<ShapeEditUndoAction>(page, s, std::move(before), text));
    page->fireRangeChanged(range);
}
}  // namespace

void ShapeEditController::applyGeometry(Control* control, EditSelection* sel, ShapeInfo info) {
    const Committed c = commit(control, sel);
    if (c.stroke->getShape()) {
        info.transform(xoj::util::Matrix::TRANSLATION(c.shift.x, c.shift.y));
        if (info.isValid() && info != *c.stroke->getShape()) {
            editStroke(control, c, _("Edit shape"), [&info](Stroke& s) { s.setShape(std::move(info)); });
        }
    }
    select(control, c.view, c.stroke, c.transient);
}

void ShapeEditController::convertToStroke(Control* control, EditSelection* sel) {
    const Committed c = commit(control, sel);
    editStroke(control, c, _("Convert shape to stroke"), [](Stroke& s) { s.clearShape(); });
    if (!c.transient) {
        select(control, c.view, c.stroke, false);
    }
}

void ShapeEditController::duplicate(Control* control, EditSelection* sel) {
    xoj_assert(sel && sel == control->getWindow()->getXournal()->getSelection());
    control->duplicateSelection();
}
