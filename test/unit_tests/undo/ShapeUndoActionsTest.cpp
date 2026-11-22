/*
 * MoreXournal
 *
 * Tests of the undo actions of the shape editor
 *
 * @license GNU GPLv2 or later
 */

#include <memory>    // for make_shared, make_unique
#include <optional>  // for optional

#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/ShapeInfo.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/FillColorUndoAction.h"
#include "undo/ShapeEditUndoAction.h"
#include "util/Color.h"

namespace {
struct Fixture {
    DocumentHandler handler;
    Document doc{&handler};
    PageRef page = std::make_shared<XojPage>(200, 200);
    Stroke* stroke = nullptr;

    Fixture() {
        auto s = std::make_unique<Stroke>();
        s->setWidth(2);
        s->setShape(ShapeInfo{ShapeType::RECTANGLE, {{10, 10}, {10, 30}, {50, 10}}});
        stroke = s.get();
        page->getSelectedLayer()->addElement(std::move(s));
    }
};

void expectSameStroke(const Stroke& actual, const Stroke& expected) {
    EXPECT_EQ(actual.getShape(), expected.getShape());
    ASSERT_EQ(actual.getPointCount(), expected.getPointCount());
    for (size_t i = 0; i < actual.getPointCount(); i++) {
        EXPECT_EQ(actual.getPoint(i).x, expected.getPoint(i).x);
        EXPECT_EQ(actual.getPoint(i).y, expected.getPoint(i).y);
    }
    EXPECT_EQ(actual.getBoundingBox(), expected.getBoundingBox());
}
}  // namespace

TEST(ShapeUndoActions, shapeEditUndoAndRedoRestoreExactly) {
    Fixture f;
    const Stroke original = *f.stroke;

    Stroke before = *f.stroke;
    f.stroke->setShape(ShapeInfo{ShapeType::RECTANGLE, {{10, 10}, {10, 60}, {90, 10}}});
    const Stroke edited = *f.stroke;
    ShapeEditUndoAction action(f.page, f.stroke, std::move(before), "Edit shape");

    action.swapStates(&f.doc);  // undo
    expectSameStroke(*f.stroke, original);
    action.swapStates(&f.doc);  // redo
    expectSameStroke(*f.stroke, edited);
    action.swapStates(&f.doc);  // undo again
    expectSameStroke(*f.stroke, original);
    // The stroke is still the one in the layer
    EXPECT_EQ(f.page->getSelectedLayer()->indexOf(f.stroke), 0);
}

TEST(ShapeUndoActions, convertToStrokeUndoBringsTheShapeBack) {
    Fixture f;
    const auto shape = f.stroke->getShape();
    Stroke before = *f.stroke;
    f.stroke->clearShape();
    ShapeEditUndoAction action(f.page, f.stroke, std::move(before), "Convert shape to stroke");

    action.swapStates(&f.doc);
    EXPECT_EQ(f.stroke->getShape(), shape);
    action.swapStates(&f.doc);
    EXPECT_FALSE(f.stroke->getShape().has_value());
}

TEST(ShapeUndoActions, fillColorUndoRestoresOpacityAndColorTogether) {
    Fixture f;
    ASSERT_EQ(f.stroke->getFill(), -1);
    ASSERT_FALSE(f.stroke->getFillColor().has_value());

    const FillColorUndoAction::FillState before = FillColorUndoAction::stateOf(f.stroke);
    const FillColorUndoAction::FillState after{100, Color(0xff00ff00U)};
    FillColorUndoAction action(f.page);
    action.addStroke(f.stroke, before, after);

    action.apply(&f.doc, false);  // redo
    EXPECT_EQ(f.stroke->getFill(), 100);
    EXPECT_EQ(f.stroke->getFillColor(), Color(0xff00ff00U));
    action.apply(&f.doc, true);  // undo
    EXPECT_EQ(f.stroke->getFill(), -1);
    EXPECT_FALSE(f.stroke->getFillColor().has_value());
}
