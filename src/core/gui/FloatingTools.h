/*
 * MoreXournal
 *
 * The controls floating over the page: tool options, zoom and layer
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr

#include <gtk/gtk.h>  // for GtkOverlay, GtkWidget

#include "control/actions/ActionRef.h"  // for ActionRef
#include "util/raii/GObjectSPtr.h"      // for GObjectSPtr, WidgetSPtr

class Control;
class ToolPill;
class ToolPageLayer;
class ToolZoomSlider;

/**
 * Three floating controls on the overlay of the window, placed inside the page area (so they move with the sidebar
 * and the search bar): the options of the active tool centred below the top bar, the zoom at the bottom right and
 * the layer at the bottom left. They are hidden with the toolbars and in presentation mode.
 */
class FloatingTools {
public:
    /// @param pageArea The widget holding the page view; the controls are placed inside its allocation
    FloatingTools(Control* control, GtkOverlay* overlay, GtkWidget* pageArea);
    ~FloatingTools();

    FloatingTools(const FloatingTools&) = delete;
    FloatingTools& operator=(const FloatingTools&) = delete;

    /// Follows View > Toolbars
    void setToolbarVisible(bool visible);

    /// Builds the tool options again, e.g. after the palette changed
    void rebuildToolOptions();

    /// Shows the new recent colors in the tool options
    void updateRecentColors();

    /// Updates the tool pill when a selection appears or disappears
    void onSelectionChanged();

private:
    GtkWidget* createZoom();
    GtkWidget* createLayer();
    void updateVisibility();

    static gboolean getChildPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                     FloatingTools* self);
    static void onPageAreaSizeAllocate(GtkWidget* pageArea, GtkAllocation* allocation, FloatingTools* self);

private:
    Control* control;
    xoj::util::GObjectSPtr<GtkOverlay> overlay;
    xoj::util::WidgetSPtr pageArea;

    std::unique_ptr<ToolPill> pill;
    std::unique_ptr<ToolZoomSlider> zoomSlider;
    std::unique_ptr<ToolPageLayer> layerItem;
    xoj::util::WidgetSPtr zoom;
    xoj::util::WidgetSPtr layer;

    gulong positionHandler = 0;
    gulong pageAreaSizeHandler = 0;
    ActionRef presentationAction;
    gulong presentationHandler = 0;

    GdkRectangle lastPageRect{0, 0, 0, 0};
    bool toolbarVisible = true;
};
