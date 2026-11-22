/*
 * MoreXournal
 *
 * Undo action for a change of the filling of strokes: the fill opacity and the fill color together
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

#include "model/PageRef.h"  // for PageRef
#include "util/Color.h"     // for Color

#include "UndoAction.h"  // for UndoAction

class Control;
class Document;
class Stroke;

class FillColorUndoAction: public UndoAction {
public:
    struct FillState {
        int fill;  ///< As Stroke::getFill(): -1 for no filling, otherwise the opacity 0..255
        std::optional<Color> fillColor;
        bool operator==(const FillState& other) const = default;
    };

    explicit FillColorUndoAction(const PageRef& page);
    ~FillColorUndoAction() override;

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;

    void addStroke(Stroke* s, FillState before, FillState after);
    bool empty() const;

    /// Put every stroke back in its state before (undo = true) or after the change, under the document's lock
    void apply(Document* doc, bool undo);

    static FillState stateOf(const Stroke* s);

private:
    struct Entry {
        Stroke* s;
        FillState before;
        FillState after;
    };
    std::vector<Entry> entries;
};
