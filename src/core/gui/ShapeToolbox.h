/*
 * MoreXournal
 *
 * Floating menu shown next to a selected shape
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>     // for array
#include <optional>  // for optional
#include <utility>   // for pair
#include <vector>    // for vector

#include <gtk/gtk.h>  // for GtkWidget, GtkOverlay, GtkLabel

#include "undo/UndoAction.h"        // for UndoActionPtr
#include "util/Color.h"             // for Color
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr, WidgetSPtr

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

    /// Rebuild the color grids in the stroke and fill popovers (e.g. after the palette changed)
    void rebuildPalettes();

private:
    void build();
    GtkWidget* makeIconButton(const char* iconName, const char* tooltip, GCallback callback);
    void populatePaletteGrid(GtkWidget* grid, std::vector<std::pair<Color, GtkWidget*>>& dots, GCallback callback);

    /// Set the widgets from the stroke, without triggering their callbacks
    void refresh(const Stroke* s);

    /// The selection if it is in shape mode
    EditSelection* getShapeSelection() const;

    void addUndoAction(UndoActionPtr action);
    void applyStrokeColor(Color color);
    void applyFill(Color color, int alpha);
    void commitOpacityDrag();

    static gboolean getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                       ShapeToolbox* self);
    static void scrolledCb(GtkAdjustment* adjustment, ShapeToolbox* self);

    static void strokeDotClickedCb(GtkToggleButton* button, ShapeToolbox* self);
    static void strokeCustomColorCb(GtkButton* button, ShapeToolbox* self);
    static void widthCb(GtkButton* button, ShapeToolbox* self);
    static void fillSwitchChangedCb(GObject* sw, GParamSpec*, ShapeToolbox* self);
    static void fillDotClickedCb(GtkToggleButton* button, ShapeToolbox* self);
    static void fillCustomColorCb(GtkButton* button, ShapeToolbox* self);
    static gboolean opacityPressCb(GtkWidget* scale, GdkEventButton* event, ShapeToolbox* self);
    static gboolean opacityReleaseCb(GtkWidget* scale, GdkEventButton* event, ShapeToolbox* self);
    static void opacityValueChangedCb(GtkRange* range, ShapeToolbox* self);
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

    xoj::util::WidgetSPtr box;

    // Stroke color
    GtkWidget* strokeColorButton = nullptr;
    GtkWidget* strokeSwatch = nullptr;
    GtkWidget* strokeColorPopover = nullptr;
    GtkWidget* strokePaletteGrid = nullptr;
    std::vector<std::pair<Color, GtkWidget*>> strokeDots;

    // Thickness
    GtkWidget* widthButton = nullptr;
    GtkWidget* widthSample = nullptr;
    GtkWidget* widthPopover = nullptr;
    struct WidthRow {
        GtkWidget* button = nullptr;
        GtkWidget* sample = nullptr;
        GtkLabel* valueLabel = nullptr;
    };
    std::array<WidthRow, 5> widthRows{};

    // Fill
    GtkWidget* fillSwitch = nullptr;
    GtkWidget* fillButton = nullptr;
    GtkWidget* fillSwatch = nullptr;
    GtkLabel* fillPercentLabel = nullptr;
    GtkWidget* fillPopover = nullptr;
    GtkWidget* fillPaletteGrid = nullptr;
    std::vector<std::pair<Color, GtkWidget*>> fillDots;
    GtkWidget* opacityScale = nullptr;
    GtkLabel* opacityValueLabel = nullptr;

    // Line style (plain, dash, dot)
    GtkWidget* lineStyleSeparator = nullptr;
    GtkWidget* lineStyleBox = nullptr;
    std::array<GtkWidget*, 3> lineStyleButtons{};

    /// Set while the widgets are updated from the stroke, to ignore their signals
    bool updating = false;
    /// Opacity remembered for the current stroke while fill is off (0..255)
    int fillAlpha = 128;
    /// Fill alpha recorded when an opacity slider drag begins, committed as one undo step on release
    std::optional<int> opacityDragStart;
    const Stroke* lastStroke = nullptr;
};
