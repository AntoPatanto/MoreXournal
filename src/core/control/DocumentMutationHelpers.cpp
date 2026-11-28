#include "DocumentMutationHelpers.h"

#include <mutex>
#include <utility>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

namespace xoj::control {

auto createTextElement(std::string text, const XojFont& font, Color color, double x, double y, double wrap)
        -> std::unique_ptr<Text> {
    auto elem = std::make_unique<Text>();
    elem->setText(std::move(text));
    elem->setFont(font);
    elem->setColor(color);
    elem->move(x, y);
    elem->setWrap(wrap);
    return elem;
}

auto insertElementsWithGroupedUndo(Control* control, const PageRef& page, Layer* layer,
                                   std::vector<ElementPtr> elements, bool fireChanged) -> std::vector<const Element*> {
    std::vector<const Element*> inserted;
    if (!control || !page || !layer || elements.empty()) {
        return inserted;
    }

    inserted.reserve(elements.size());
    {
        std::lock_guard lock(*control->getDocument());
        for (auto& elem: elements) {
            inserted.push_back(elem.get());
            layer->addElement(std::move(elem));
        }
    }

    if (fireChanged) {
        for (const Element* elem: inserted) {
            page->fireElementChanged(elem);
        }
    }

    UndoRedoHandler* undo = control->getUndoRedoHandler();
    if (undo) {
        if (inserted.size() == 1) {
            undo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, inserted.front()));
        } else {
            undo->addUndoAction(std::make_unique<InsertsUndoAction>(page, layer, inserted));
        }
    }

    return inserted;
}

}  // namespace xoj::control
