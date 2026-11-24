/*
 * Xournal++
 *
 * A selection for editing, every selection (Rect, Lasso...) is
 * converted to this one if the selection is finished
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>
#include <memory>    // for unique_ptr
#include <optional>  // for optional
#include <string>
#include <utility>  // for pair
#include <vector>   // for vector

#include <cairo.h>  // for cairo_t, cairo_matrix_t

#include "model/Element.h"                   // for Element, Element::Index
#include "model/ElementContainer.h"          // for ElementContainer
#include "model/ElementInsertionPosition.h"  // for InsertionOrder
#include "model/PageRef.h"                   // for PageRef
#include "undo/UndoAction.h"                 // for UndoAction (ptr only)
#include "util/Color.h"                      // for Color
#include "util/Point.h"                      // for Point
#include "util/PointerContainerView.h"       // for PointerContainerView
#include "util/Rectangle.h"                  // for Rectangle
#include "util/raii/GSourceURef.h"           // for GSourceURef
#include "util/serializing/Serializable.h"   // for Serializable

#include "CursorSelectionType.h"     // for CursorSelectionType, CURS...
#include "SelectionHandleLogic.h"    // for DeviceKind, HandlePalette
#include "SnapToGridInputHandler.h"  // for SnapToGridInputHandler

class UndoRedoHandler;
class Layer;
class XojPageView;
class Selection;
class EditSelectionContents;
class DeleteUndoAction;
class LineStyle;
class ObjectInputStream;
class ObjectOutputStream;
class XojFont;
class Document;
class EditSelection;
class Stroke;

namespace SelectionFactory {
auto createFromFloatingElement(Control* ctrl, const PageRef& page, Layer* layer, XojPageView* view, ElementPtr e)
        -> std::unique_ptr<EditSelection>;
auto createFromFloatingElements(Control* ctrl, const PageRef& page, Layer* layer, XojPageView* view,
                                InsertionOrder elts)  //
        -> std::pair<std::unique_ptr<EditSelection>, Range>;
auto createFromElementOnActiveLayer(Control* ctrl, const PageRef& page, XojPageView* view, const Element* e,
                                    Element::Index pos = Element::InvalidIndex)  //
        -> std::unique_ptr<EditSelection>;
auto createFromElementsOnActiveLayer(Control* ctrl, const PageRef& page, XojPageView* view, InsertionOrderRef elts)
        -> std::unique_ptr<EditSelection>;
/**
 * @brief Creates a new instance containing base->getElements() and *e. The content of *base is cleared but *base is not
 * destroyed.
 */
auto addElementFromActiveLayer(Control* ctrl, EditSelection* base, const Element* e, Element::Index pos)
        -> std::unique_ptr<EditSelection>;
/**
 * @brief Creates a new instance containing base->getElements() and the content of elts. The content of *base is cleared
 * but *base is not destroyed.
 */
auto addElementsFromActiveLayer(Control* ctrl, EditSelection* base, const InsertionOrderRef& elts)
        -> std::unique_ptr<EditSelection>;
};  // namespace SelectionFactory

class EditSelection: public ElementContainer, public Serializable {
public:
    EditSelection(Control* ctrl, InsertionOrder elts, const PageRef& page, Layer* layer, XojPageView* view,
                  const Range& bounds, const Range& snappingBounds);

    /// Construct an empty selection
    EditSelection(Control* ctrl, const PageRef& page, Layer* layer, XojPageView* view);

    ~EditSelection() override;

public:
    /**
     * get the X coordinate relative to the provided view (getView())
     * in document coordinates
     */
    double getXOnView() const;

    /**
     * Get the Y coordinate relative to the provided view (getView())
     * in document coordinates
     */
    double getYOnView() const;

    /**
     * @return The original X coordinates of the provided view in document
     * coordinates.
     */
    double getOriginalXOnView();

    /**
     * @return The original Y coordinates of the provided view in document
     * coordinates.
     */
    double getOriginalYOnView();

    /**
     * Get the width in document coordinates (multiple with zoom)
     */
    double getWidth() const;

    /**
     * Get the height in document coordinates (multiple with zoom)
     */
    double getHeight() const;

    /**
     * Get the bounding rectangle in document coordinates (multiple with zoom)
     */
    xoj::util::Rectangle<double> getRect() const;

    /**
     * gets the minimal bounding box containing all elements of the selection used for e.g. grid snapping
     */
    xoj::util::Rectangle<double> getSnappedBounds() const;

    /**
     * get the original bounding rectangle in document coordinates
     */
    xoj::util::Rectangle<double> getOriginalBounds() const;

    /**
     * Get the rotation angle of the selection
     */
    double getRotation() const;

    /**
     * Get the source page (where the selection was done)
     */
    PageRef getSourcePage() const;

    /**
     * Get the source layer (form where the Elements come)
     */
    Layer* getSourceLayer() const;

    inline XojPageView* getView() const { return view; }

public:
    /**
     * Sets the tool size for pen or eraser, returns an undo action
     * (or nullptr if nothing is done)
     */
    UndoActionPtr setSize(double thicknessPen, double thicknessHighlighter, double thicknessEraser);

    /**
     * Set the line style of all strokes, return an undo action
     * (Or nullptr if nothing done)
     */
    UndoActionPtr setLineStyle(LineStyle style);

    /**
     * Set the color of all elements, return an undo action
     * (Or nullptr if nothing done, e.g. because there is only an image)
     */
    UndoActionPtr setColor(Color color);

    /**
     * Sets the font of all containing text elements, return an undo action
     * (or nullptr if there are no Text elements)
     */
    UndoActionPtr setFont(const XojFont& font);

    /**
     * Fills the undo item if the selection is deleted
     * the selection is cleared after
     */
    void fillUndoItem(DeleteUndoAction* undo);

    /**
     * Fills the stroke, return an undo action
     * (Or nullptr if nothing done, e.g. because there is only an image)
     */
    UndoActionPtr setFill(int alphaPen, int alphaHighligther);

    /**
     * Sets the fill opacity (-1 for no filling) and the fill color of all strokes but eraser strokes, returns an undo
     * action (or nullptr if nothing changed)
     */
    UndoActionPtr setFillStyle(int fill, std::optional<Color> fillColor);

public:
    /**
     * @brief The selected shape, if the selection is exactly one stroke with shape metadata. The selection is then in
     * shape mode: it shows the handles of the shape instead of the bounding box.
     */
    Stroke* getShapeStroke() const;

    /**
     * A transient selection is opened on a drawn shape or automatically recognized stroke right after release. A press
     * outside of it dismisses it and starts drawing in the same gesture, and toolbar changes apply to the tool instead
     * of the selection.
     */
    void setTransient(bool transient);
    bool isTransient() const;

    /// Let the centre of the selection decide which page the elements land on when the selection is finalized
    void setGrabPointToCenter();

    /// The view whose page receives the elements when the selection is finalized
    XojPageView* getDestinationView();

public:
    /**
     * Add an element to the this selection
     * @param pos: specifies the index of the element from the source layer,
     * in case we want to replace it back where it came from.
     */
    void addElement(ElementPtr e, Element::Index pos);

    /**
     * Returns all containing elements of this selection
     */
    auto getElementsView() const -> xoj::util::PointerContainerView<std::vector<Element*>>;

    void forEachElement(std::function<void(const Element*)> f) const override;

    /**
     * Returns the insert order of this selection
     */
    auto getInsertionOrder() const -> InsertionOrder const&;

    enum class OrderChange {
        BringToFront,
        BringForward,
        SendBackward,
        SendToBack,
    };

    static constexpr std::array<std::string_view, 4> orderChangeNames{"bringToFront", "bringForward", "sendBackward",
                                                                      "sendToBack"};
    static constexpr std::array<OrderChange, 4> allChanges = {OrderChange::BringToFront, OrderChange::BringForward,
                                                              OrderChange::SendBackward, OrderChange::SendToBack};

    static constexpr auto orderChangeToString(const OrderChange change) -> std::string_view {
        return orderChangeNames.at(static_cast<size_t>(change));
    }

    /**
     * Change the insert order of this selection.
     */
    auto rearrangeInsertionOrder(const OrderChange change) -> UndoActionPtr;

    /**
     * Finish the current movement
     * (should be called in the mouse-button-released event handler)
     */
    void mouseUp();

    /**
     * Move the selection
     */
    void moveSelection(double dx, double dy, bool addMoveUndo = false);

    /**
     * Get the cursor type for the current position (if 0 then the default cursor should be used)
     */
    CursorSelectionType getSelectionTypeForPos(double x, double y, double zoom,
                                               xoj::handles::DeviceKind device = xoj::handles::DeviceKind::Fine);

    /**
     * Update hover state for handle highlight and return the cursor selection type at (x, y)
     */
    CursorSelectionType updateHover(double x, double y, double zoom, xoj::handles::DeviceKind device);

    /// Clear any hovered handle highlight and repaint if needed
    void clearHover();

    /**
     * Paints the selection to cr, with the given zoom factor. The coordinates of cr
     * should be relative to the provided view by getView() (use translateEvent())
     */
    void paint(cairo_t* cr, double zoom);

    /// Paints only the selected elements (run before page recolouring so selected ink is recoloured)
    void paintContents(cairo_t* cr, double zoom);

    /// Paints the selection border, stem and handles (run after page recolouring so handles keep paper-aware colours)
    void paintOverlay(cairo_t* cr, double zoom);

    /**
     * Gets the selection's bounding box in view coordinates. This takes document zoom
     * and selection rotation into account.
     */
    auto getBoundingBoxInView() const -> xoj::util::Rectangle<double>;

    /**
     * If the selection is outside the visible area correct the coordinates
     */
    void ensureWithinVisibleArea();

public:
    /**
     * Handles mouse input for moving and resizing, in pixel-coordinates relative to "view"
     */
    void mouseDown(CursorSelectionType type, double x, double y,
                   xoj::handles::DeviceKind device = xoj::handles::DeviceKind::Fine);

    /**
     * Handles mouse input for moving and resizing, in pixel-coordinates relative to "view"
     */
    void mouseMove(double x, double y, bool alt);

    /**
     * If the user is currently moving the selection.
     */
    bool isMoving() const;

    void copySelection();

public:
    XojPageView* getView();

public:
    // Serialize interface
    void serialize(ObjectOutputStream& out) const override;
    void readSerialized(ObjectInputStream& in) override;


    /// Applies the transformation to the selected elements, empties the selection and return the modified elements
    InsertionOrder makeMoveEffective();

private:
    /// Whether the underlying page is dark (e.g. dark-recoloured or dark paper colour)
    bool isPaperDark() const;

    /// Whether (rxView, ryView) in unrotated view pixels lies within stroke hit tolerance of a selected element
    bool isNearSelectedStroke(double rxView, double ryView, double zoom, xoj::handles::DeviceKind device) const;

    /// Compute the screen-space layout of a rectangular selection at `zoom`
    xoj::handles::SelectionLayout computeLayout(double zoom) const;

    /// Draw a single circular handle in screen space on whole pixels
    static void drawHandle(cairo_t* cr, double rawX, double rawY, double baseDiameter, bool hovered, bool filledAccent,
                           const xoj::handles::HandlePalette& palette);

    /**
     * Shape mode: the translation from the shape's coordinates to the coordinates of the current view, i.e. the
     * pending move of the selection
     */
    xoj::util::Point<double> getShapeOffset() const;

    /// Shape mode: the index of the handle under (x, y), in pixels relative to the view
    std::optional<size_t> shapeHandleAt(const Stroke* s, double x, double y, double zoom,
                                        xoj::handles::DeviceKind device) const;

    CursorSelectionType getShapeSelectionTypeForPos(const Stroke* s, double x, double y, double zoom,
                                                    xoj::handles::DeviceKind device) const;
    void shapeMouseMove(double mouseX, double mouseY, bool alt, double zoom);
    void paintShapeContents(cairo_t* cr, const Stroke* s, double zoom);
    void paintShapeOverlay(cairo_t* cr, const Stroke* s, double zoom);


    /**
     * Finishes all pending changes, move the elements, scale the elements and add
     * them to new layer if any or to the old if no new layer
     */
    void finalizeSelection();

    /**
     * Gets the PageView under the cursor
     */
    XojPageView* getPageViewUnderCursor();

    /**
     * Translate all coordinates which are relative to the current view to the new view,
     * and set the attribute view to the new view
     */
    void translateToView(XojPageView* v);

    /**
     * Updates rotation matrix
     */
    void updateMatrix();

    /**
     * scales and shifts to update bounding boxes
     */
    void scaleShift(double fx, double fy, bool changeLeft, bool changeTop);

    /**
     * Set edge panning signal.
     */
    void setEdgePan(bool edgePan);

    /**
     * Whether the edge pan signal is set.
     */
    bool isEdgePanning() const;

    static bool handleEdgePan(EditSelection* self);

private:  // DATA
    /**
     * The position (and rotation) relative to the current view
     */
    double x{};
    double y{};
    double rotation = 0;

    /**
     * Use to translate to rotated selection
     */
    cairo_matrix_t cmatrix{};

    /**
     * The size, including the padding and frame
     */
    double width{};
    double height{};

    /**
     * The size and dimensions for snapping
     */
    xoj::util::Rectangle<double> snappedBounds{};


    /**
     * Mouse coordinates for moving / resizing
     */
    CursorSelectionType mouseDownType = CURSOR_SELECTION_NONE;
    double relMousePosX{};
    double relMousePosY{};
    double relMousePosRotX{};
    double relMousePosRotY{};

    /**
     * The source page (form where the Elements come)
     */
    PageRef sourcePage;

    /**
     * The source layer (form where the Elements come)
     */
    Layer* sourceLayer{};

    /**
     * The contents of the selection
     */
    std::unique_ptr<EditSelectionContents> contents;

private:  // HANDLER
    /**
     * The page view for the anchor
     */
    XojPageView* view{};

    /**
     * Undo redo handler
     */
    UndoRedoHandler* undo{};

    /**
     * The handler for snapping points
     */
    SnapToGridInputHandler snappingHandler;

    /**
     * Edge pan timer
     */
    xoj::util::GSourceURef edgePanHandler;

    /**
     * Inhibit the next move event after edge panning finishes. This prevents
     * the selection from teleporting if the page has changed during panning.
     * Additionally, this reduces the amount of "jitter" resulting from moving
     * the selection in mouseDown while edge panning.
     */
    bool edgePanInhibitNext = false;

    /// See setTransient()
    bool transient = false;

    /// Device class that started the active drag gesture (kept until mouseUp)
    xoj::handles::DeviceKind activeGestureDevice = xoj::handles::DeviceKind::Fine;

    /// Currently hovered handle for visual feedback (only for mouse/pen hover when not dragging)
    CursorSelectionType hoveredHandle = CURSOR_SELECTION_NONE;
    std::optional<size_t> hoveredShapeHandle;

    /// Initial pointer offset from the logical box edge when dragging a scale handle (prevents jumps on off-center/tiny grabs)
    double scaleGrabOffsetX = 0.0;
    double scaleGrabOffsetY = 0.0;

    /**
     * Shape mode, while a handle is dragged: the index of the handle, the press and the current positions (in the
     * shape's coordinates) and a copy of the stroke with the new geometry (nullptr until the first motion)
     */
    size_t shapeHandle = 0;
    xoj::util::Point<double> shapeDownPos{};
    xoj::util::Point<double> shapeMousePos{};
    std::unique_ptr<Stroke> shapePreview;
};
