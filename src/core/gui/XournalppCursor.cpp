#include "XournalppCursor.h"

#include <algorithm>         // for clamp, max, min
#include <cmath>             // for cos, sin, M_PI, NAN, fmod
#include <cstdint>           // for uint32_t
#include <initializer_list>  // for initializer_list

#include <cairo.h>                  // for cairo_move_to, cairo_lin...
#include <gdk-pixbuf/gdk-pixbuf.h>  // for GdkPixbuf
#include <glib-object.h>            // for g_object_unref
#include <gtk/gtk.h>                // for gtk_widget_get_window

#include "control/Control.h"                 // for Control
#include "control/ToolEnums.h"               // for TOOL_HAND, TOOL_PEN, TOO...
#include "control/ToolHandler.h"             // for ToolHandler
#include "control/settings/Settings.h"       // for Settings
#include "control/settings/SettingsEnums.h"  // for STYLUS_CURSOR_BIG, STYLU...
#include "control/zoom/ZoomControl.h"        // for ZoomControl
#include "gui/EraserIndicatorLogic.h"        // for computeEraserIndicatorGe...
#include "gui/Layout.h"                      // for Layout
#include "gui/MainWindow.h"                  // for MainWindow
#include "gui/PageView.h"                    // for XojPageView
#include "gui/widgets/XournalWidget.h"       // for GTK_XOURNAL
#include "model/XojPage.h"                   // for XojPage
#include "util/Color.h"                      // for argb_to_GdkRGBA, rgb_to_...
#include "util/safe_casts.h"                 // for ceil_cast, floor_cast

#include "XournalView.h"  // for XournalView


// NOTE:  Every cursor change must result in the setting of this->currentCursor to the new cursor type even for custom
// cursors.
// Custom cursors can also compare then set this->currentCursorFlavour to notice changes in colour or size etc. The
// flavour calculation is specific to each cursor type and is calculated differently within each type.


//  All the cursors we want to use. WARNING Make sure to set their css names in cssCursors[] below WARNING
enum AVAILABLECURSORS {
    CRSR_nullptr = 0,  // <--- Do Not Modify
    CRSR_BUSY,
    CRSR_MOVE,
    CRSR_MOVING,
    CRSR_GRAB,
    CRSR_GRABBING,
    CRSR_TOP_LEFT_CORNER,
    CRSR_TOP_RIGHT_CORNER,
    CRSR_BOTTOM_LEFT_CORNER,
    CRSR_BOTTOM_RIGHT_CORNER,
    CRSR_SB_H_DOUBLE_ARROW,
    CRSR_EXCHANGE,
    CRSR_SB_V_DOUBLE_ARROW,
    CRSR_ARROW,
    CRSR_BLANK_CURSOR,
    CRSR_XTERM,
    CRSR_DEFAULT,
    CRSR_HAND2,
    CRSR_TCROSS,
    CRSR_PENORHIGHLIGHTER,
    CRSR_ERASER,
    CRSR_DRAWDIRNONE,       // drawdir* keep these consecutive and in order: none,shift,ctrl,shiftctrl
    CRSR_DRAWDIRSHIFT,      // "
    CRSR_DRAWDIRCTRL,       // "
    CRSR_DRAWDIRSHIFTCTRL,  // "
    CRSR_RESIZE,
    CRSR_ALIAS,

    CRSR_END_OF_CURSORS
};


struct cursorStruct {
    const gchar* cssName;
    const gchar* cssBackupName;
};


//  our enum mapped to css name and a place to store the cursor pointer.
// Note: including enum as error check (for now?)
cursorStruct cssCursors[CRSR_END_OF_CURSORS];


XournalppCursor::XournalppCursor(Control* control): control(control) {
    // clang-format off
	// NOTE: Go ahead and use a fancy css cursor... but specify a common backup cursor.
	cssCursors[CRSR_nullptr                ] = 	{"",""};
	cssCursors[CRSR_BUSY                ] = 	{"wait", 		""					};
	cssCursors[CRSR_MOVE                ] = 	{"all-scroll", 	""					};
	cssCursors[CRSR_MOVING              ] = 	{"grabbing", 	""					};
	cssCursors[CRSR_GRAB                ] = 	{"grab", 		""					};
	cssCursors[CRSR_GRABBING            ] = 	{"grabbing", 	""					};
	cssCursors[CRSR_TOP_LEFT_CORNER     ] = 	{"nw-resize", 	""					};
	cssCursors[CRSR_TOP_RIGHT_CORNER    ] = 	{"ne-resize", 	""					};
	cssCursors[CRSR_BOTTOM_LEFT_CORNER  ] = 	{"sw-resize", 	""					};
	cssCursors[CRSR_BOTTOM_RIGHT_CORNER ] = 	{"se-resize", 	""					};
	cssCursors[CRSR_SB_H_DOUBLE_ARROW   ] = 	{"ew-resize", 	""					};
	cssCursors[CRSR_EXCHANGE            ] = 	{"exchange", 	"arrow"				};
	cssCursors[CRSR_SB_V_DOUBLE_ARROW   ] = 	{"ns-resize", 	""					};
	cssCursors[CRSR_ARROW               ] = 	{"default", 	""					};
	cssCursors[CRSR_BLANK_CURSOR        ] = 	{"none", 		""					};
	cssCursors[CRSR_XTERM               ] = 	{"text", 		""					};
	cssCursors[CRSR_DEFAULT             ] = 	{"default", 	""					};
	cssCursors[CRSR_HAND2               ] = 	{"hand2", 		""					};
	cssCursors[CRSR_TCROSS              ] = 	{"crosshair", 	""					};
	cssCursors[CRSR_PENORHIGHLIGHTER    ] = 	{"",""};			// custom cursors - enum used only for check
	cssCursors[CRSR_ERASER              ] = 	{"",""};			// "
	cssCursors[CRSR_DRAWDIRNONE         ] = 	{"",""};			// "
	cssCursors[CRSR_DRAWDIRSHIFT        ] = 	{"",""};			// "
	cssCursors[CRSR_DRAWDIRCTRL         ] = 	{"",""};			// "
	cssCursors[CRSR_DRAWDIRSHIFTCTRL    ] = 	{"",""};			// "
    cssCursors[CRSR_RESIZE              ] =     {"",""};            // "
    cssCursors[CRSR_ALIAS               ] =     {"alias", ""};
};
// clang-format on

constexpr auto RESIZE_CURSOR_SIZE = 16;
constexpr auto DELTA_ANGLE_ARROW_HEAD = M_PI / 6.0;
constexpr auto LENGTH_ARROW_HEAD = 0.7;
constexpr auto RESIZE_CURSOR_HASH_PRECISION = 1000;

namespace {

void addRoundedRect(cairo_t* cr, double x, double y, double w, double h, double radius) {
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    const double r = std::clamp(radius, 0.0, std::min(w, h) * 0.5);
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, M_PI * 0.5, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, M_PI * 1.5);
    cairo_close_path(cr);
}

void drawCornerBrackets(cairo_t* cr, double cx, double cy, double boxSize, double leg, double strokeWidth,
                        const xoj::cursor::EraserIndicatorPalette& palette) {
    const double half = boxSize * 0.5;
    cairo_new_path(cr);
    // Top-left
    cairo_move_to(cr, cx - half, cy - half + leg);
    cairo_line_to(cr, cx - half, cy - half);
    cairo_line_to(cr, cx - half + leg, cy - half);
    // Top-right
    cairo_move_to(cr, cx + half - leg, cy - half);
    cairo_line_to(cr, cx + half, cy - half);
    cairo_line_to(cr, cx + half, cy - half + leg);
    // Bottom-right
    cairo_move_to(cr, cx + half, cy + half - leg);
    cairo_line_to(cr, cx + half, cy + half);
    cairo_line_to(cr, cx + half - leg, cy + half);
    // Bottom-left
    cairo_move_to(cr, cx - half + leg, cy + half);
    cairo_line_to(cr, cx - half, cy + half);
    cairo_line_to(cr, cx - half, cy + half - leg);

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    // Outer halo behind brackets
    cairo_set_source_rgba(cr, palette.halo.r, palette.halo.g, palette.halo.b, palette.halo.a);
    cairo_set_line_width(cr, strokeWidth + 2.0 * xoj::cursor::HALO_OUTER_PX);
    cairo_stroke_preserve(cr);

    // Bracket stroke
    cairo_set_source_rgba(cr, palette.stroke.r, palette.stroke.g, palette.stroke.b, palette.stroke.a);
    cairo_set_line_width(cr, strokeWidth);
    cairo_stroke(cr);
}

void queueWidgetRect(GtkWidget* widget, const GdkRectangle& rect) {
    const int allocW = gtk_widget_get_allocated_width(widget);
    const int allocH = gtk_widget_get_allocated_height(widget);
    const int x1 = std::clamp(rect.x, 0, allocW);
    const int y1 = std::clamp(rect.y, 0, allocH);
    const int x2 = std::clamp(rect.x + rect.width, 0, allocW);
    const int y2 = std::clamp(rect.y + rect.height, 0, allocH);
    if (x2 > x1 && y2 > y1) {
        gtk_widget_queue_draw_area(widget, x1, y1, x2 - x1, y2 - y1);
    }
}

}  // namespace


XournalppCursor::~XournalppCursor() = default;


void XournalppCursor::setInputDeviceClass(InputDeviceClass device) {
    if (this->inputDevice == device) {
        return;
    }
    this->inputDevice = device;
    if (this->lastDrawnOverlayRect || isEraserOverlayVisible()) {
        invalidateOverlay();
    }
}


void XournalppCursor::setPointerPosition(double widgetX, double widgetY) {
    const bool moved = !this->pointerWidgetPos || this->pointerWidgetPos->x != widgetX ||
                       this->pointerWidgetPos->y != widgetY;
    this->pointerWidgetPos = xoj::util::Point<double>{widgetX, widgetY};
    if (moved && (this->lastDrawnOverlayRect || isEraserOverlayVisible())) {
        invalidateOverlay();
    }
}


void XournalppCursor::clearPointerPosition() {
    if (!this->pointerWidgetPos) {
        return;
    }
    this->pointerWidgetPos.reset();
    if (this->lastDrawnOverlayRect) {
        invalidateOverlay();
    }
}


auto XournalppCursor::isEraserOverlayVisible() const -> bool {
    ToolHandler* handler = control->getToolHandler();
    if (!handler) {
        return false;
    }
    xoj::cursor::EraserOverlayInput input{};
    input.busy = this->busy;
    input.insidePage = this->insidePage;
    input.pointerInWidget = this->pointerWidgetPos.has_value();
    input.selectionType = this->selectionType;
    input.toolType = handler->getToolType();
    input.inputDevice = this->inputDevice;
    input.pressed = this->mouseDown;
    input.eraserVisibility = control->getSettings()->getEraserVisibility();
    return xoj::cursor::shouldShowEraserOverlay(input);
}


auto XournalppCursor::computeOverlayBounds() const -> std::optional<GdkRectangle> {
    if (!this->pointerWidgetPos || !isEraserOverlayVisible()) {
        return std::nullopt;
    }
    ToolHandler* handler = control->getToolHandler();
    const auto geom = xoj::cursor::computeEraserIndicatorGeometry(
            handler->getEraserType(), handler->getThickness(), control->getZoomControl()->getZoom(), this->mouseDown);
    const double extent = geom.paintExtentHalfPx;
    const int minX = floor_cast<int>(this->pointerWidgetPos->x - extent);
    const int minY = floor_cast<int>(this->pointerWidgetPos->y - extent);
    const int maxX = ceil_cast<int>(this->pointerWidgetPos->x + extent);
    const int maxY = ceil_cast<int>(this->pointerWidgetPos->y + extent);
    return GdkRectangle{minX, minY, maxX - minX, maxY - minY};
}


void XournalppCursor::invalidateOverlay() {
    MainWindow* win = control->getWindow();
    if (!win) {
        return;
    }
    XournalView* xournal = win->getXournal();
    if (!xournal || !xournal->getWidget()) {
        return;
    }
    GtkWidget* widget = xournal->getWidget();
    if (!gtk_widget_get_realized(widget)) {
        return;
    }

    const auto nextRect = computeOverlayBounds();
    if (!this->lastDrawnOverlayRect && !nextRect) {
        return;
    }
    if (this->lastDrawnOverlayRect) {
        queueWidgetRect(widget, *this->lastDrawnOverlayRect);
    }
    if (nextRect) {
        queueWidgetRect(widget, *nextRect);
    }
}


void XournalppCursor::paintOverlay(cairo_t* cr, GtkWidget* widget) {
    if (!isEraserOverlayVisible() || !this->pointerWidgetPos) {
        this->lastDrawnOverlayRect.reset();
        return;
    }

    GtkXournal* xournal = GTK_XOURNAL(widget);
    const double cx = this->pointerWidgetPos->x;
    const double cy = this->pointerWidgetPos->y;
    const double layoutX = cx + gtk_adjustment_get_value(xournal->hadjustment);
    const double layoutY = cy + gtk_adjustment_get_value(xournal->vadjustment);

    XojPageView* pv = xournal->layout ? xournal->layout->getPageViewAt(round_cast<int>(layoutX), round_cast<int>(layoutY)) :
                                        nullptr;
    if (!pv) {
        this->lastDrawnOverlayRect.reset();
        return;
    }

    ToolHandler* handler = control->getToolHandler();
    const EraserType eraserType = handler->getEraserType();
    const double thickness = handler->getThickness();
    const double zoom = control->getZoomControl()->getZoom();

    const auto geom = xoj::cursor::computeEraserIndicatorGeometry(eraserType, thickness, zoom, this->mouseDown);

    const Color pageBg = pv->getPage()->getBackgroundColor();
    const auto& recolorParams = control->getSettings()->getRecolorParameters();
    const Color displayedPaper =
            recolorParams.recolorizeMainView ? Color(recolorParams.recolor.convertColor(pageBg)) : pageBg;
    const Color whiteoutFillColor =
            recolorParams.recolorizeMainView ? Color(recolorParams.recolor.convertColor(Colors::white)) : Colors::white;
    const auto palette =
            xoj::cursor::computeEraserIndicatorPalette(xoj::cursor::isDarkColor(displayedPaper), whiteoutFillColor);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (geom.shape == xoj::cursor::EraserIndicatorShape::Circle) {
        const double rad = geom.trueSizePx * 0.5;
        const double sw = geom.strokeWidthPx;

        if (rad > 0.0) {
            // 90% paper-matched whiteout fill
            cairo_arc(cr, cx, cy, rad, 0.0, 2.0 * M_PI);
            cairo_set_source_rgba(cr, palette.whiteoutFill.r, palette.whiteoutFill.g, palette.whiteoutFill.b,
                                  palette.whiteoutFill.a);
            cairo_fill(cr);

            // 1 px outer halo outside the dashed ring so it reads over dark ink
            cairo_arc(cr, cx, cy, rad + (sw + xoj::cursor::HALO_OUTER_PX) * 0.5, 0.0, 2.0 * M_PI);
            cairo_set_source_rgba(cr, palette.halo.r, palette.halo.g, palette.halo.b, palette.halo.a);
            cairo_set_line_width(cr, xoj::cursor::HALO_OUTER_PX);
            cairo_stroke(cr);

            // Dashed 3/2 ring at the exact whiteout diameter
            const double dashes[2] = {xoj::cursor::WHITEOUT_DASH_ON_PX, xoj::cursor::WHITEOUT_DASH_OFF_PX};
            cairo_set_dash(cr, dashes, 2, 0.0);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
            cairo_arc(cr, cx, cy, rad, 0.0, 2.0 * M_PI);
            cairo_set_source_rgba(cr, palette.whiteoutRing.r, palette.whiteoutRing.g, palette.whiteoutRing.b,
                                  palette.whiteoutRing.a);
            cairo_set_line_width(cr, sw);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0.0);
        }

        if (geom.showCornerBrackets) {
            drawCornerBrackets(cr, cx, cy, geom.bracketBoxSizePx, geom.bracketLegPx, sw, palette);
        }
    } else {
        const double s = geom.trueSizePx;
        const double r = geom.cornerRadiusPx;
        const double sw = geom.strokeWidthPx;

        if (s > 0.0) {
            if (this->mouseDown) {
                addRoundedRect(cr, cx - s * 0.5, cy - s * 0.5, s, s, r);
                cairo_set_source_rgba(cr, palette.pressedFill.r, palette.pressedFill.g, palette.pressedFill.b,
                                      palette.pressedFill.a);
                cairo_fill(cr);
            }

            const double outlineSw = (s < 4.0) ? std::min(sw, std::max(1.0, s * 0.5)) : sw;
            const double haloOffset = (outlineSw + xoj::cursor::HALO_OUTER_PX) * 0.5;
            const double haloHalf = s * 0.5 + haloOffset;

            // 1 px outer halo strictly outside the stroke so interior ink stays unobstructed
            addRoundedRect(cr, cx - haloHalf, cy - haloHalf, 2.0 * haloHalf, 2.0 * haloHalf, r + haloOffset);
            cairo_set_source_rgba(cr, palette.halo.r, palette.halo.g, palette.halo.b, palette.halo.a);
            cairo_set_line_width(cr, xoj::cursor::HALO_OUTER_PX);
            cairo_stroke(cr);

            // Hollow rounded square on the true erase boundary
            addRoundedRect(cr, cx - s * 0.5, cy - s * 0.5, s, s, r);
            cairo_set_source_rgba(cr, palette.stroke.r, palette.stroke.g, palette.stroke.b, palette.stroke.a);
            cairo_set_line_width(cr, outlineSw);
            cairo_stroke(cr);
        }

        if (geom.showCornerBrackets) {
            drawCornerBrackets(cr, cx, cy, geom.bracketBoxSizePx, geom.bracketLegPx, sw, palette);
        }
    }

    cairo_restore(cr);

    const double extent = geom.paintExtentHalfPx;
    const int minX = floor_cast<int>(cx - extent);
    const int minY = floor_cast<int>(cy - extent);
    const int maxX = ceil_cast<int>(cx + extent);
    const int maxY = ceil_cast<int>(cy + extent);
    this->lastDrawnOverlayRect = GdkRectangle{minX, minY, maxX - minX, maxY - minY};
}


// pen or hi-light cursor will be a DrawDir cursor instead
void XournalppCursor::activateDrawDirCursor(bool enable, bool shift, bool ctrl) {
    this->drawDirActive = enable;
    this->drawDirShift = shift;
    this->drawDirCtrl = ctrl;
}


void XournalppCursor::setMouseDown(bool mouseDown) {
    if (this->mouseDown == mouseDown) {
        return;
    }

    this->mouseDown = mouseDown;
    ToolHandler* handler = control->getToolHandler();
    ToolType type = handler->getToolType();

    // Not always an update is needed
    if (type == TOOL_HAND || type == TOOL_VERTICAL_SPACE || type == TOOL_ERASER) {
        updateCursor();
    }
}


void XournalppCursor::setMouseSelectionType(CursorSelectionType selectionType) {
    if (this->selectionType == selectionType) {
        return;
    }
    this->selectionType = selectionType;
    updateCursor();
}

void XournalppCursor::setRotationAngle(double angle) { this->angle = angle; }
void XournalppCursor::setMirror(bool mirror) { this->mirror = mirror; }

/*This handles setting the busy cursor for the main window and calls
 * updateCursor to set the busy cursor for the XournalWidget region.
 */
void XournalppCursor::setCursorBusy(bool busy) {
    MainWindow* win = control->getWindow();
    if (!win) {
        return;
    }

    if (this->busy == busy) {
        return;
    }

    this->busy = busy;

    if (busy) {
        GdkWindow* window = gtk_widget_get_window(win->getWindow());
        xoj::util::GObjectSPtr<GdkCursor> cursor(
                gdk_cursor_new_from_name(gdk_window_get_display(window), cssCursors[CRSR_BUSY].cssName),
                xoj::util::adopt);
        gdk_window_set_cursor(window, cursor.get());
    } else {
        if (gtk_widget_get_window(win->getWindow())) {
            gdk_window_set_cursor(gtk_widget_get_window(win->getWindow()), nullptr);
        }
    }

    updateCursor();
}


void XournalppCursor::setInsidePage(bool insidePage) {
    if (this->insidePage == insidePage) {
        return;
    }

    this->insidePage = insidePage;

    updateCursor();
}

void XournalppCursor::setIsLinkHighlighted(bool highlighted) {
    if (this->linkHighlighted == highlighted) {
        return;
    }
    this->linkHighlighted = highlighted;

    updateCursor();
}

void XournalppCursor::setInvisible(bool invisible) {
    if (this->invisible == invisible) {
        return;
    }

    this->invisible = invisible;

    updateCursor();
}


void XournalppCursor::updateCursor() {
    MainWindow* win = control->getWindow();
    if (!win) {
        return;
    }

    XournalView* xournal = win->getXournal();
    if (!xournal) {
        return;
    }

    GdkCursor* cursor = nullptr;


    if (this->busy) {
        setCursor(CRSR_BUSY);
    } else {
        ToolHandler* handler = control->getToolHandler();
        ToolType type = handler->getToolType();


        if (type == TOOL_HAND) {
            if (this->mouseDown) {
                setCursor(CRSR_GRABBING);
            } else {
                setCursor(CRSR_GRAB);
            }
        } else if (this->selectionType) {
            switch (this->selectionType) {
                case CURSOR_SELECTION_MOVE:
                    if (this->mouseDown) {
                        setCursor(CRSR_MOVING);
                    } else {
                        setCursor(CRSR_MOVE);
                    }
                    break;
                case CURSOR_SELECTION_TOP_LEFT:
                    [[fallthrough]];
                case CURSOR_SELECTION_BOTTOM_RIGHT:
                    cursor = getResizeCursor(45);
                    break;
                case CURSOR_SELECTION_TOP_RIGHT:
                    [[fallthrough]];
                case CURSOR_SELECTION_BOTTOM_LEFT:
                    cursor = getResizeCursor(135);
                    break;
                case CURSOR_SELECTION_LEFT:
                    [[fallthrough]];
                case CURSOR_SELECTION_RIGHT:
                    cursor = getResizeCursor(180);
                    break;
                case CURSOR_SELECTION_TOP:
                    [[fallthrough]];
                case CURSOR_SELECTION_BOTTOM:
                    cursor = getResizeCursor(90);
                    break;
                case CURSOR_SELECTION_ROTATE:
                    setCursor(CRSR_EXCHANGE);
                    break;
                case CURSOR_SELECTION_SHAPE_HANDLE:
                    setCursor(CRSR_TCROSS);
                    break;
                default:
                    break;
            }
        } else if (!this->insidePage) {
            setCursor(CRSR_DEFAULT);
        } else if (type == TOOL_PEN || type == TOOL_HIGHLIGHTER || type == TOOL_LASER_POINTER_PEN ||
                   type == TOOL_LASER_POINTER_HIGHLIGHTER) {
            if (this->inputDevice == INPUT_DEVICE_MOUSE && !this->mouseDown) {
                // mouse and not pressed
                setCursor(CRSR_ARROW);
            } else {
                if (type == TOOL_PEN || type == TOOL_LASER_POINTER_PEN) {
                    cursor = getPenCursor();
                } else  // must be:  if (type == TOOL_HIGHLIGHTER || type == TOOL_LASER_POINTER_HIGHLIGHTER)
                {
                    cursor = getHighlighterCursor();
                }
            }
        } else if (type == TOOL_ERASER) {
            // The eraser indicator is drawn as a HiDPI-sharp canvas overlay in paintOverlay();
            // hide the native pointer cursor over the page.
            setCursor(CRSR_BLANK_CURSOR);
        } else if (type == TOOL_TEXT) {
            if (this->invisible) {
                setCursor(CRSR_BLANK_CURSOR);
            } else {
                setCursor(CRSR_XTERM);
            }
        } else if (type == TOOL_LINK) {
            if (this->linkHighlighted) {
                setCursor(CRSR_ALIAS);
            } else {
                setCursor(CRSR_HAND2);
            }
        } else if (type == TOOL_LATEX) {
            setCursor(CRSR_XTERM);
        } else if (type == TOOL_IMAGE) {
            setCursor(CRSR_TCROSS);
        } else if (type == TOOL_FLOATING_TOOLBOX) {
            setCursor(CRSR_DEFAULT);
        } else if (type == TOOL_VERTICAL_SPACE) {
            setCursor(CRSR_SB_V_DOUBLE_ARROW);
        } else if (type == TOOL_SELECT_OBJECT) {
            setCursor(CRSR_DEFAULT);
        } else if (type == TOOL_PLAY_OBJECT) {
            setCursor(CRSR_HAND2);
        } else if (type == TOOL_SELECT_PDF_TEXT_LINEAR) {
            setCursor(CRSR_XTERM);
        } else  // other selections are handled before anyway, because you can move a selection with every tool
        {
            setCursor(CRSR_TCROSS);
        }
    }

    GdkWindow* window = gtk_widget_get_window(xournal->getWidget());
    if (window) {
        if (cursor != nullptr) {
            gdk_window_set_cursor(window, cursor);
        }
        gtk_widget_set_sensitive(xournal->getWidget(), !this->busy);
    }
    // Performance removal!
    // If this line is ever required, make sure this function is never called, when the cursor is not set.
    // gdk_display_sync(gdk_display_get_default());

    if (cursor != nullptr) {
        g_object_unref(cursor);
    }

    if (this->lastDrawnOverlayRect || isEraserOverlayVisible()) {
        invalidateOverlay();
    }
}

auto XournalppCursor::getResizeCursor(double deltaAngle) -> GdkCursor* {
    if (this->mirror) {
        deltaAngle = -deltaAngle;
    }
    gulong flavour = static_cast<gulong>(RESIZE_CURSOR_HASH_PRECISION * fmod(angle + deltaAngle, 180.0));
    if (CRSR_RESIZE == this->currentCursor && flavour == this->currentCursorFlavour) {
        return nullptr;
    }
    this->currentCursor = CRSR_RESIZE;
    this->currentCursorFlavour = flavour;

    double a = (this->angle + deltaAngle) * M_PI / 180;
    cairo_surface_t* crCursor = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, RESIZE_CURSOR_SIZE, RESIZE_CURSOR_SIZE);
    cairo_t* cr = cairo_create(crCursor);
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1);
    cairo_translate(cr, RESIZE_CURSOR_SIZE / 2, RESIZE_CURSOR_SIZE / 2);
    cairo_scale(cr, RESIZE_CURSOR_SIZE / 2, RESIZE_CURSOR_SIZE / 2);
    cairo_set_line_width(cr, 0.2);
    // draw double headed arrow rotated accordingly
    cairo_move_to(cr, cos(a), sin(a));
    cairo_line_to(cr, -cos(a), -sin(a));
    cairo_stroke(cr);
    // head and tail
    for (auto s: {-1, 1}) {
        cairo_move_to(cr, s * cos(a), s * sin(a));
        cairo_rel_line_to(cr, s * cos(a + M_PI + DELTA_ANGLE_ARROW_HEAD) * LENGTH_ARROW_HEAD,
                          s * sin(a + M_PI + DELTA_ANGLE_ARROW_HEAD) * LENGTH_ARROW_HEAD);
        cairo_move_to(cr, s * cos(a), s * sin(a));
        cairo_rel_line_to(cr, s * cos(a + M_PI - DELTA_ANGLE_ARROW_HEAD) * LENGTH_ARROW_HEAD,
                          s * sin(a + M_PI - DELTA_ANGLE_ARROW_HEAD) * LENGTH_ARROW_HEAD);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(crCursor, 0, 0, RESIZE_CURSOR_SIZE, RESIZE_CURSOR_SIZE);
    cairo_surface_destroy(crCursor);
    GdkCursor* cursor =
            gdk_cursor_new_from_pixbuf(gtk_widget_get_display(control->getWindow()->getXournal()->getWidget()), pixbuf,
                                       RESIZE_CURSOR_SIZE / 2, RESIZE_CURSOR_SIZE / 2);
    g_object_unref(pixbuf);
    return cursor;
}

auto XournalppCursor::getHighlighterCursor() -> GdkCursor* {
    if (this->drawDirActive) {
        return createCustomDrawDirCursor(48, this->drawDirShift, this->drawDirCtrl);
    }

    return createHighlighterOrPenCursor(120 / 255.0);
}


auto XournalppCursor::getPenCursor() -> GdkCursor* {
    if ((control->getSettings()->getStylusCursorType() == STYLUS_CURSOR_NONE) &&
        !control->getSettings()->isHighlightPosition()) {
        setCursor(CRSR_BLANK_CURSOR);
        return nullptr;
    }
    if (control->getSettings()->getStylusCursorType() == STYLUS_CURSOR_ARROW) {
        setCursor(CRSR_ARROW);
        return nullptr;
    }
    if ((control->getSettings()->getStylusCursorType() != STYLUS_CURSOR_NONE) && this->drawDirActive) {
        return createCustomDrawDirCursor(48, this->drawDirShift, this->drawDirCtrl);
    }

    return createHighlighterOrPenCursor(1.0);
}

auto XournalppCursor::createHighlighterOrPenCursor(double alpha) -> GdkCursor* {
    auto irgb = control->getToolHandler()->getColor();
    if (const auto& params = control->getSettings()->getRecolorParameters(); params.recolorizeMainView) {
        irgb = params.recolor.convertColor(irgb);
    }
    auto drgb = Util::rgb_to_GdkRGBA(irgb);
    auto cursorType = control->getSettings()->getStylusCursorType();
    bool bright = control->getSettings()->isHighlightPosition();
    // With the highlight circle a custom cursor is set even without a stylus cursor, so its id must differ from
    // CRSR_BLANK_CURSOR: otherwise setCursor(CRSR_BLANK_CURSOR), e.g. for the eraser overlay, would keep the circle
    auto cursor = (cursorType == STYLUS_CURSOR_NONE && !bright) ? CRSR_BLANK_CURSOR : CRSR_PENORHIGHLIGHTER;
    const double cursorSize = xoj::cursor::computePenDotDiameterPx(control->getToolHandler()->getThickness(),
                                                                   control->getZoomControl()->getZoom());

    GtkWidget* widget = control->getWindow()->getXournal()->getWidget();
    const int scale = std::max(1, gtk_widget_get_scale_factor(widget));

    const PenCursorKey key{
            cursorType,
            bright,
            round_cast<int>(alpha * 255.0),
            round_cast<int>(cursorSize * 4.0),
            static_cast<uint32_t>(irgb),
            scale,
    };

    if (cursor == this->currentCursor && this->currentPenCursorKey == key) {
        return nullptr;
    }
    this->currentCursor = cursor;
    this->currentPenCursorKey = key;

    // Leave room for the 1 px outer white halo plus antialiasing margin
    int width = ceil_cast<int>(cursorSize + 2.0 * xoj::cursor::PEN_DOT_HALO_PX + 2.0);
    int height = width;
    if ((cursorType == STYLUS_CURSOR_BIG) || bright) {
        height = width = std::max(90, width);
    }

    const double centerX = static_cast<double>(width) / 2.0;
    const double centerY = static_cast<double>(height) / 2.0;
    cairo_surface_t* crCursor = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width * scale, height * scale);
    cairo_surface_set_device_scale(crCursor, scale, scale);
    cairo_t* cr = cairo_create(crCursor);

    if (cursorType == STYLUS_CURSOR_BIG) {
        // When using highlighter, paint the icon with the current color
        if (alpha != 1.0) {
            gdk_cairo_set_source_rgba(cr, &drgb);
        } else {
            cairo_set_source_rgb(cr, 1, 1, 1);
        }
        cairo_set_line_width(cr, 1.2);

        // Starting point
        cairo_move_to(cr, centerX + 2, centerY);
        // Pencil cursor
        cairo_line_to(cr, centerX + 2, centerY - 4);
        cairo_line_to(cr, centerX + 15, centerY - 17.5);
        cairo_line_to(cr, centerX + 19, centerY - 14);
        cairo_line_to(cr, centerX + 6, centerY);

        cairo_close_path(cr);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);

        cairo_fill_preserve(cr);
    }

    if (bright) {
        // Highlight cursor with a circle
        auto&& color = Util::argb_to_GdkRGBA(control->getSettings()->getCursorHighlightColor());
        gdk_cairo_set_source_rgba(cr, &color);
        cairo_arc(cr, centerX, centerY, control->getSettings()->getCursorHighlightRadius(), 0, 2 * M_PI);
        cairo_fill_preserve(cr);
        auto&& borderColor = Util::argb_to_GdkRGBA(control->getSettings()->getCursorHighlightBorderColor());
        gdk_cairo_set_source_rgba(cr, &borderColor);
        cairo_set_line_width(cr, control->getSettings()->getCursorHighlightBorderWidth());
        cairo_stroke(cr);
    }

    if (cursorType != STYLUS_CURSOR_NONE) {
        // 1 px white halo at 90% strictly outside the dot so it stays visible over same-colour ink
        // without clouding translucent highlighter dots
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.90);
        cairo_set_line_width(cr, xoj::cursor::PEN_DOT_HALO_PX);
        cairo_arc(cr, centerX, centerY, cursorSize / 2.0 + xoj::cursor::PEN_DOT_HALO_PX / 2.0, 0, 2.0 * M_PI);
        cairo_stroke(cr);

        auto drgbCopy = drgb;
        drgbCopy.alpha = alpha;
        gdk_cairo_set_source_rgba(cr, &drgbCopy);
        cairo_arc(cr, centerX, centerY, cursorSize / 2.0, 0, 2.0 * M_PI);
        cairo_fill(cr);
    }

    cairo_destroy(cr);
    GdkCursor* gdkCursor = gdk_cursor_new_from_surface(gtk_widget_get_display(widget), crCursor, centerX, centerY);
    cairo_surface_destroy(crCursor);
    return gdkCursor;
}


void XournalppCursor::setCursor(guint cursorID) {
    if (cursorID == this->currentCursor) {
        return;
    }

    MainWindow* win = control->getWindow();
    if (!win) {
        return;
    }

    XournalView* xournal = win->getXournal();
    if (!xournal) {
        return;
    }

    GdkWindow* window = gtk_widget_get_window(xournal->getWidget());
    if (!window) {
        return;
    }

    GdkCursor* cursor = gdk_cursor_new_from_name(gdk_window_get_display(window), cssCursors[cursorID].cssName);
    if (cursor == nullptr)  // failed to get a cursor, try backup cursor.
    {
        if (cursorID != CRSR_nullptr) {
            cursor = gdk_cursor_new_from_name(gdk_window_get_display(window), cssCursors[cursorID].cssBackupName);

            // Null cursor is ok but not wanted ... warn user
            if (cursor == nullptr) {
                if (CRSR_nullptr == this->currentCursor) {
                    return;  // We've already been here
                }
                g_warning("CSS Cursor and backup not valid '%s', '%s'", cssCursors[cursorID].cssName,
                          cssCursors[cursorID].cssBackupName);
            }
        }
        cursorID = cursor == nullptr ? CRSR_nullptr : cursorID;
    }

    this->currentCursor = cursorID;
    this->currentPenCursorKey.reset();
    gdk_window_set_cursor(gtk_widget_get_window(xournal->getWidget()), cursor);
    gdk_window_set_cursor(window, cursor);
    if (cursor) {
        g_object_unref(cursor);
    }
}


auto XournalppCursor::createCustomDrawDirCursor(int size, bool shift, bool ctrl) -> GdkCursor* {
    bool big = control->getSettings()->getStylusCursorType() == STYLUS_CURSOR_BIG;
    bool bright = control->getSettings()->isHighlightPosition();

    guint newCursorID = CRSR_DRAWDIRNONE + (shift ? 1 : 0) + (ctrl ? 2 : 0);
    gulong flavour =
            (big ? 1 : 0) | (bright ? 2 : 0) | static_cast<gulong>(size) << 2;  // hash of variables for comparison only

    if (newCursorID == this->currentCursor && flavour == this->currentCursorFlavour) {
        return nullptr;
    }
    this->currentCursor = newCursorID;
    this->currentCursorFlavour = flavour;

    int height = size;
    int width = size;
    int fontSize = 8;
    if (big || bright) {
        height = width = 60;
        fontSize = 12;
    }
    int centerX = width - width / 4;
    int centerY = height - height / 4;


    cairo_surface_t* crCursor = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(crCursor);
    cairo_set_line_width(cr, 1.2);

    // Starting point
    cairo_move_to(cr, centerX, height / 2);
    cairo_line_to(cr, centerX, height);
    cairo_stroke(cr);

    cairo_move_to(cr, width / 2, centerY);
    cairo_line_to(cr, width, centerY);
    cairo_stroke(cr);

    if (ctrl) {
        cairo_text_extents_t extents;
        const char* utf8 = "CONTROL";
        double x = NAN, y = NAN;
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, fontSize);
        cairo_text_extents(cr, utf8, &extents);
        x = 0;
        y = extents.height;
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, utf8);
    }

    if (shift) {
        cairo_text_extents_t extents;
        const char* utf8 = "SHIFT";
        double x = NAN, y = NAN;
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, fontSize);
        cairo_text_extents(cr, utf8, &extents);
        x = 0;
        y = extents.height * 2.5;
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, utf8);
    }

    cairo_destroy(cr);
    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(crCursor, 0, 0, width, height);
    cairo_surface_destroy(crCursor);
    GdkCursor* cursor = gdk_cursor_new_from_pixbuf(
            gtk_widget_get_display(control->getWindow()->getXournal()->getWidget()), pixbuf, centerX, centerY);
    g_object_unref(pixbuf);

    return cursor;
}
