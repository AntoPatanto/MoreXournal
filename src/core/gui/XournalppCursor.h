/*
 * Xournal++
 *
 * Handles the XournalppCursor
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>
#include <optional>

#include <cairo.h>    // for cairo_t
#include <gdk/gdk.h>  // for GdkCursor, GdkRectangle
#include <glib.h>     // for guint, gulong
#include <gtk/gtk.h>  // for GtkWidget

#include "control/settings/SettingsEnums.h"     // for StylusCursorType
#include "control/tools/CursorSelectionType.h"  // for CursorSelectionType
#include "gui/inputdevices/InputEvents.h"       // for InputDeviceClass, INP...
#include "util/Point.h"                         // for Point

class Control;

class XournalppCursor {
public:
    XournalppCursor(Control* control);
    virtual ~XournalppCursor();

    void setCursorBusy(bool busy);
    void updateCursor();

    void setMouseSelectionType(CursorSelectionType selectionType);

    void setMouseDown(bool mouseDown);
    void setInvisible(bool invisible);
    void setInsidePage(bool insidePage);
    void setIsLinkHighlighted(bool higlighted);
    void activateDrawDirCursor(bool enable, bool shift = false, bool ctrl = false);
    void setInputDeviceClass(InputDeviceClass inputDevice);
    void setRotationAngle(double angle);
    void setMirror(bool mirror);

    /// Track the pointer position in GtkXournal widget coordinates for canvas overlays (e.g. eraser indicator)
    void setPointerPosition(double widgetX, double widgetY);
    void clearPointerPosition();

    /// Paint canvas-drawn cursor overlays (eraser indicator) in GtkXournal widget coordinates
    void paintOverlay(cairo_t* cr, GtkWidget* widget);

private:
    void setCursor(guint id);

    GdkCursor* getPenCursor();
    GdkCursor* getHighlighterCursor();
    GdkCursor* getResizeCursor(double deltaAngle);

    GdkCursor* createHighlighterOrPenCursor(double alpha);
    GdkCursor* createCustomDrawDirCursor(int size, bool shift, bool ctrl);

    bool isEraserOverlayVisible() const;
    std::optional<GdkRectangle> computeOverlayBounds() const;
    void invalidateOverlay();

private:
    struct PenCursorKey {
        StylusCursorType cursorType = STYLUS_CURSOR_DOT;
        bool bright = false;
        int alphaKey = 0;
        int sizeQuarterPx = 0;
        uint32_t colorRgb = 0;
        int scaleFactor = 1;

        bool operator==(const PenCursorKey&) const = default;
    };

    InputDeviceClass inputDevice = INPUT_DEVICE_MOUSE;

    Control* control = nullptr;
    bool busy = false;
    bool insidePage = false;
    CursorSelectionType selectionType = CURSOR_SELECTION_NONE;

    bool mouseDown = false;
    bool invisible = false;
    bool linkHighlighted = false;

    std::optional<xoj::util::Point<double>> pointerWidgetPos;
    std::optional<GdkRectangle> lastDrawnOverlayRect;

    // One shot drawDir custom cursor -drawn instead of pen/stylus then cleared.
    bool drawDirActive = false;
    bool drawDirShift = false;
    bool drawDirCtrl = false;

    // avoid re-assigning same cursor
    guint currentCursor = 0;        // enum AVAILABLECURSORS
    gulong currentCursorFlavour{};  // for different flavours of a cursor (i.e. drawdir, resize)
    std::optional<PenCursorKey> currentPenCursorKey;

    // for resizing rotated/mirrored selections
    double angle = 0;
    bool mirror = false;
};
