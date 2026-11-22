/*
 * MoreXournal
 *
 * Floating menu shown next to a selected shape
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>  // for array

#include <gtk/gtk.h>  // for GtkWidget, GtkOverlay

#include "undo/UndoAction.h"        // for UndoActionPtr
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

class MainWindow;
class EditSelection;
class Stroke;

/**
 * The menu of a shape: stroke color, width, filling, line style, duplicate, delete and conversion to a plain stroke.
 * It is shown below the shape while the selection is in shape mode (see EditSelection::getShapeStroke()), and every
 * action is one undo step.
 *
 * All the widgets are built here, in code, so that the look can be changed in one place.
 */
class ShapeToolbox {
public:
    ShapeToolbox(MainWindow* win, GtkOverlay* overlay);
    ShapeToolbox(const ShapeToolbox&) = delete;
    ShapeToolbox& operator=(const ShapeToolbox&) = delete;
    ~ShapeToolbox();

    /// Show the menu for the current selection, or hide it if there is no shape selected or the shape is being edited
    void update();

private:
    void build();
    GtkWidget* makeButton(const char* iconName, const char* tooltip, GCallback callback);

    /// Set the widgets from the stroke, without triggering their callbacks
    void refresh(const Stroke* s);

    /// The selection if it is in shape mode
    EditSelection* getShapeSelection() const;

    void addUndoAction(UndoActionPtr action);

    /// Opacity of the filling chosen in the fill color button (0..255)
    int getFillAlpha() const;

    static gboolean getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                       ShapeToolbox* self);
    static void scrolledCb(GtkAdjustment* adjustment, ShapeToolbox* self);

    static void colorSetCb(GtkColorButton* button, ShapeToolbox* self);
    static void widthCb(GtkButton* button, ShapeToolbox* self);
    static void fillToggledCb(GtkToggleButton* button, ShapeToolbox* self);
    static void fillColorSetCb(GtkColorButton* button, ShapeToolbox* self);
    static void lineStyleToggledCb(GtkToggleButton* button, ShapeToolbox* self);
    static void duplicateCb(GtkButton* button, ShapeToolbox* self);
    static void deleteCb(GtkButton* button, ShapeToolbox* self);
    static void convertCb(GtkButton* button, ShapeToolbox* self);

private:
    MainWindow* win;
    xoj::util::GObjectSPtr<GtkOverlay> overlay;
    gulong positionHandler = 0;

    std::array<xoj::util::GObjectSPtr<GtkAdjustment>, 2> adjustments;
    std::array<gulong, 4> scrollHandlers{};

    GtkWidget* box = nullptr;
    GtkWidget* colorButton = nullptr;
    GtkWidget* widthButton = nullptr;
    GtkWidget* widthPopover = nullptr;
    GtkWidget* fillToggle = nullptr;
    GtkWidget* fillColorButton = nullptr;
    /// plain, dash, dot
    std::array<GtkWidget*, 3> lineStyleButtons{};
    GtkWidget* lineStyleBox = nullptr;

    /// Set while the widgets are updated from the stroke, to ignore their signals
    bool updating = false;
    /// Opacity remembered for the current stroke while fill is off (0..255)
    int fillAlpha = 128;
    const Stroke* lastStroke = nullptr;
};
