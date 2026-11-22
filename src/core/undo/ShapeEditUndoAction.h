/*
 * MoreXournal
 *
 * Undo action for an edit of a shape stroke (handle drag, conversion to a plain stroke)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include "model/PageRef.h"  // for PageRef
#include "model/Stroke.h"   // for Stroke

#include "UndoAction.h"  // for UndoAction

class Control;
class Document;

class ShapeEditUndoAction: public UndoAction {
public:
    /**
     * @param target The edited stroke, already in its new state
     * @param before A copy of the stroke before the edit
     * @param text The description shown in the Edit menu
     */
    ShapeEditUndoAction(const PageRef& page, Stroke* target, Stroke before, std::string text);
    ~ShapeEditUndoAction() override;

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;

    /// Exchange the target stroke with the stored copy, under the document's lock. Undo and redo both do this.
    void swapStates(Document* doc);

private:
    Stroke* target;
    Stroke other;
    std::string text;
};
