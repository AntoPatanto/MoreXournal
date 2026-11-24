#include "ShapeToolbox.h"

#include <algorithm>  // for clamp, max
#include <cmath>      // for abs, lround
#include <cstdio>     // for snprintf
#include <string>     // for string, to_string

#include "control/Control.h"                        // for Control
#include "control/ToolEnums.h"                      // for TOOL_PEN, TOOL_SIZE_...
#include "control/ToolHandler.h"                    // for ToolHandler
#include "control/settings/Settings.h"              // for Settings
#include "control/shapes/ShapeEditController.h"     // for duplicate, convertToStroke
#include "control/tools/EditSelection.h"            // for EditSelection
#include "gui/ChromeUtil.h"                         // for newChromeIcon, updateScalableIcons
#include "gui/XournalView.h"                        // for XournalView
#include "gui/scroll/ScrollHandling.h"              // for ScrollHandling
#include "gui/toolbarMenubar/model/ColorPalette.h"  // for Palette
#include "model/LineStyle.h"                        // for LineStyle
#include "model/Stroke.h"                           // for Stroke
#include "model/StrokeStyle.h"                      // for parseStyle, formatStyle
#include "undo/UndoRedoHandler.h"                   // for UndoRedoHandler
#include "util/Color.h"                             // for Color, GdkRGBA_to_rgb
#include "util/i18n.h"                              // for _, N_

#include "MainWindow.h"  // for MainWindow

namespace {
/// Extra gap added to the 6 px CSS shadow margin of #shapeToolbox
constexpr int GAP = 4;

/// Fill opacity offered when a stroke has none yet
constexpr int DEFAULT_FILL_ALPHA = 128;

constexpr int ICON_SIZE = 18;
constexpr int SWATCH_SIZE = 16;
constexpr int PALETTE_DOT_SIZE = 20;
constexpr int PALETTE_COLUMNS = 6;

constexpr const char* LINE_STYLES[3] = {"plain", "dash", "dot"};

/// GObject data keys
constexpr const char* INDEX_KEY = "shape-toolbox-index";
constexpr const char* COLOR_KEY = "shape-toolbox-color";
constexpr const char* WIDTH_KEY = "shape-toolbox-width";

struct WidthChoice {
    ToolSize size;
    const char* name;
};
const WidthChoice WIDTHS[5] = {{TOOL_SIZE_VERY_FINE, N_("Very Fine")},
                               {TOOL_SIZE_FINE, N_("Fine")},
                               {TOOL_SIZE_MEDIUM, N_("Medium")},
                               {TOOL_SIZE_THICK, N_("Thick")},
                               {TOOL_SIZE_VERY_THICK, N_("Very Thick")}};

int getIndex(gpointer widget) { return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), INDEX_KEY)); }

void addClass(GtkWidget* w, const char* cssClass) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cssClass);
}

GtkWidget* newIcon(const char* iconName, int size = ICON_SIZE) {
    return xoj::chrome::newChromeIcon(iconName, size, size == ICON_SIZE);
}

GtkWidget* newSeparator() {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    addClass(sep, "mx-separator");
    return sep;
}

GtkWidget* prepareFlatButton(GtkWidget* btn, const char* tooltip) {
    gtk_widget_set_can_focus(btn, true);
    gtk_widget_set_focus_on_click(btn, false);
    addClass(btn, "flat");
    if (tooltip) {
        gtk_widget_set_tooltip_text(btn, tooltip);
    }
    return btn;
}

GtkBox* newPopover(GtkWidget* menuButton, GtkWidget** outPopover) {
    GtkWidget* popover = gtk_popover_new(menuButton);
    addClass(popover, "mx-popover");
    GtkWidget* popBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(popover), popBox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuButton), popover);
    *outPopover = popover;
    return GTK_BOX(popBox);
}

void addPopoverTitle(GtkBox* box, const char* title) {
    GtkWidget* lbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    addClass(lbl, "mx-popover-title");
    gtk_box_pack_start(box, lbl, false, false, 0);
}

GtkWidget* newPopoverRowChild(const char* label, const char* iconName) {
    GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    if (iconName) {
        gtk_box_pack_start(GTK_BOX(rowBox), newIcon(iconName), false, false, 0);
    }
    GtkWidget* lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(rowBox), lbl, true, true, 0);
    return rowBox;
}

double sampleWidth(double thickness) { return std::clamp(thickness * 1.5, 1.0, 12.0); }

void setSampleWidth(GtkWidget* sample, double px) {
    g_object_set_data(G_OBJECT(sample), WIDTH_KEY, GINT_TO_POINTER(static_cast<int>(px * 100)));
    gtk_widget_queue_draw(sample);
}

/// A horizontal thickness line in the text color of the button
GtkWidget* newSample(double px, int width, int height) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, width, height);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    setSampleWidth(area, px);
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                         const double lineWidth = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), WIDTH_KEY)) / 100.0;
                         GtkStyleContext* ctx = gtk_widget_get_style_context(w);
                         GdkRGBA color;
                         gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &color);
                         gdk_cairo_set_source_rgba(cr, &color);
                         const double h = gtk_widget_get_allocated_height(w);
                         const double inset = 2.0 + lineWidth / 2.0;
                         cairo_set_line_width(cr, lineWidth);
                         cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                         cairo_move_to(cr, inset, h / 2.0);
                         cairo_line_to(cr, gtk_widget_get_allocated_width(w) - inset, h / 2.0);
                         cairo_stroke(cr);
                         return true;
                     }),
                     nullptr);
    return area;
}

/// A circular color swatch (or a checkered disk when empty), with a contrast ring when < 3:1
GtkWidget* newColorSwatch(int size) { return xoj::chrome::newContrastSwatch(Colors::black, size); }

void setColorSwatch(GtkWidget* swatch, Color color, bool empty = false) {
    xoj::chrome::setContrastSwatch(swatch, color, std::nullopt, empty);
}

gboolean drawLineStyleSample(GtkWidget* w, cairo_t* cr, gpointer) {
    const int k = getIndex(w);
    GtkStyleContext* ctx = gtk_widget_get_style_context(w);
    GdkRGBA color;
    gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &color);
    gdk_cairo_set_source_rgba(cr, &color);
    const double h = gtk_widget_get_allocated_height(w);
    const double width = gtk_widget_get_allocated_width(w);
    cairo_set_line_width(cr, 2.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    if (k == 1) {
        const double dashes[] = {5.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0);
    } else if (k == 2) {
        const double dashes[] = {0.1, 4.5};
        cairo_set_dash(cr, dashes, 2, 0);
    }
    cairo_move_to(cr, 3.0, h / 2.0);
    cairo_line_to(cr, width - 3.0, h / 2.0);
    cairo_stroke(cr);
    return true;
}

/// Solid (0), dashed (1) or dotted (2) horizontal line in the button's text color
GtkWidget* newLineStyleSample(int kind) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 24, 12);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(area), INDEX_KEY, GINT_TO_POINTER(kind));
    g_signal_connect(area, "draw", G_CALLBACK(drawLineStyleSample), nullptr);
    return area;
}

std::string formatThickness(double thickness) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", thickness);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s + " pt";
}

bool sameRgb(Color a, Color b) { return a.red == b.red && a.green == b.green && a.blue == b.blue; }

int alphaToPercent(int alpha) { return static_cast<int>(std::lround(std::clamp(alpha, 0, 255) * 100.0 / 255.0)); }
}  // namespace

ShapeToolbox::ShapeToolbox(MainWindow* win, GtkOverlay* overlay): win(win), overlay(overlay, xoj::util::ref) {
    build();

    gtk_overlay_add_overlay(overlay, this->box.get());
    // The menu takes the clicks in its area, so that missing a button does not draw on the page below
    gtk_overlay_set_overlay_pass_through(overlay, this->box.get(), false);
    this->positionHandler =
            g_signal_connect(overlay, "get-child-position", G_CALLBACK(ShapeToolbox::getOverlayPosition), this);

    // Follow the shape when the view scrolls or zooms
    ScrollHandling* scroll = win->getXournal()->getScrollHandling();
    this->adjustments[0].reset(scroll->getHorizontal(), xoj::util::ref);
    this->adjustments[1].reset(scroll->getVertical(), xoj::util::ref);
    size_t n = 0;
    for (auto& adj: this->adjustments) {
        for (const char* signal: {"value-changed", "changed"}) {
            this->scrollHandlers[n++] = g_signal_connect(adj.get(), signal, G_CALLBACK(ShapeToolbox::scrolledCb), this);
        }
    }

    gtk_widget_hide(this->box.get());
}

ShapeToolbox::~ShapeToolbox() {
    this->updating = true;
    this->opacityDragStart.reset();
    size_t n = 0;
    for (auto& adj: this->adjustments) {
        for (int i = 0; i < 2; i++) {
            g_signal_handler_disconnect(adj.get(), this->scrollHandlers[n++]);
        }
    }
    g_signal_handler_disconnect(this->overlay.get(), this->positionHandler);
    // Destroy the overlay box and its popovers while `this` is alive, so no widget signal can fire after destruction
    gtk_widget_destroy(this->box.get());
}

GtkWidget* ShapeToolbox::makeIconButton(const char* iconName, const char* tooltip, GCallback callback) {
    GtkWidget* button = prepareFlatButton(gtk_button_new(), tooltip);
    gtk_button_set_image(GTK_BUTTON(button), newIcon(iconName));
    g_signal_connect(button, "clicked", callback, this);
    return button;
}

void ShapeToolbox::populatePaletteGrid(GtkWidget* grid, std::vector<std::pair<Color, GtkWidget*>>& dots,
                                       GCallback callback) {
    dots.clear();
    gtk_container_foreach(GTK_CONTAINER(grid), +[](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);

    const Palette& palette = this->win->getControl()->getPalette();
    const auto& recolorParams = this->win->getControl()->getSettings()->getRecolorParameters();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), PALETTE_COLUMNS);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid),
                                           static_cast<guint>(std::min<size_t>(PALETTE_COLUMNS, palette.size())));

    for (size_t i = 0; i < palette.size(); i++) {
        const NamedColor& nc = palette.getColorAt(i);
        std::optional<Color> secondary = recolorParams.recolorizeMainView ?
                                                 std::make_optional(recolorParams.recolor.convertColor(nc.getColor())) :
                                                 std::nullopt;
        GtkWidget* btn = prepareFlatButton(gtk_toggle_button_new(), nc.getName().c_str());
        addClass(btn, "mx-swatch");
        gtk_container_add(GTK_CONTAINER(btn),
                          xoj::chrome::newContrastSwatch(nc.getColor(), PALETTE_DOT_SIZE, secondary));
        g_object_set_data(G_OBJECT(btn), COLOR_KEY, GUINT_TO_POINTER(static_cast<uint32_t>(nc.getColor())));
        g_signal_connect(btn, "toggled", callback, this);
        gtk_container_add(GTK_CONTAINER(grid), btn);
        gtk_widget_set_can_focus(gtk_widget_get_parent(btn), false);
        dots.emplace_back(nc.getColor(), btn);
    }
    gtk_widget_show_all(grid);
}

void ShapeToolbox::rebuildPalettes() {
    if (this->strokePaletteGrid) {
        populatePaletteGrid(this->strokePaletteGrid, this->strokeDots, G_CALLBACK(ShapeToolbox::strokeDotClickedCb));
    }
    if (this->fillPaletteGrid) {
        populatePaletteGrid(this->fillPaletteGrid, this->fillDots, G_CALLBACK(ShapeToolbox::fillDotClickedCb));
    }
    if (EditSelection* sel = getShapeSelection()) {
        refresh(sel->getShapeStroke());
    }
}

void ShapeToolbox::setTouchLayout(bool touch) {
    xoj::chrome::updateScalableIcons(this->box.get(), xoj::chrome::chromeIconSize(touch));
}

void ShapeToolbox::build() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    this->box.reset(root, xoj::util::refsink);
    gtk_widget_set_name(root, "shapeToolbox");
    addClass(root, "mx-pill");
    addClass(root, "mx-float");
    gtk_widget_set_halign(root, GTK_ALIGN_START);
    gtk_widget_set_valign(root, GTK_ALIGN_START);

    // Stroke color
    this->strokeColorButton = prepareFlatButton(gtk_menu_button_new(), _("Stroke color"));
    addClass(this->strokeColorButton, "mx-wide");
    GtkWidget* strokeBtnInner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    this->strokeSwatch = newColorSwatch(SWATCH_SIZE);
    gtk_box_pack_start(GTK_BOX(strokeBtnInner), this->strokeSwatch, false, false, 0);
    gtk_box_pack_start(GTK_BOX(strokeBtnInner), gtk_label_new(_("Stroke color")), false, false, 0);
    gtk_box_pack_start(GTK_BOX(strokeBtnInner), newIcon("mx-down-symbolic", 16), false, false, 0);
    gtk_container_add(GTK_CONTAINER(this->strokeColorButton), strokeBtnInner);

    GtkBox* strokePop = newPopover(this->strokeColorButton, &this->strokeColorPopover);
    addPopoverTitle(strokePop, _("Stroke color"));
    this->strokePaletteGrid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(this->strokePaletteGrid), GTK_SELECTION_NONE);
    gtk_box_pack_start(strokePop, this->strokePaletteGrid, false, false, 0);
    GtkWidget* customStroke = prepareFlatButton(gtk_button_new(), nullptr);
    addClass(customStroke, "mx-prow");
    gtk_container_add(GTK_CONTAINER(customStroke), newPopoverRowChild(_("Custom…"), "mx-plus-symbolic"));
    g_signal_connect(customStroke, "clicked", G_CALLBACK(ShapeToolbox::strokeCustomColorCb), this);
    gtk_box_pack_start(strokePop, customStroke, false, false, 0);
    gtk_widget_show_all(GTK_WIDGET(strokePop));
    gtk_box_pack_start(GTK_BOX(root), this->strokeColorButton, false, false, 0);

    // Thickness
    this->widthButton = prepareFlatButton(gtk_menu_button_new(), _("Stroke width"));
    addClass(this->widthButton, "mx-wide");
    GtkWidget* widthBtnInner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    this->widthSample = newSample(2.0, 24, 14);
    gtk_box_pack_start(GTK_BOX(widthBtnInner), this->widthSample, false, false, 0);
    gtk_box_pack_start(GTK_BOX(widthBtnInner), gtk_label_new(_("Thickness")), false, false, 0);
    gtk_box_pack_start(GTK_BOX(widthBtnInner), newIcon("mx-down-symbolic", 16), false, false, 0);
    gtk_container_add(GTK_CONTAINER(this->widthButton), widthBtnInner);

    GtkBox* widthPop = newPopover(this->widthButton, &this->widthPopover);
    addPopoverTitle(widthPop, _("Thickness"));
    const double* penWidths = this->win->getControl()->getToolHandler()->getToolThickness(TOOL_PEN);
    for (size_t i = 0; i < std::size(WIDTHS); i++) {
        const double thickness = penWidths[WIDTHS[i].size];
        GtkWidget* row = prepareFlatButton(gtk_toggle_button_new(), nullptr);
        addClass(row, "mx-prow");
        g_object_set_data(G_OBJECT(row), INDEX_KEY, GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(row, "clicked", G_CALLBACK(ShapeToolbox::widthCb), this);

        GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget* name = gtk_label_new(_(WIDTHS[i].name));
        gtk_label_set_xalign(GTK_LABEL(name), 0.0);
        gtk_label_set_width_chars(GTK_LABEL(name), 9);
        GtkWidget* sample = newSample(sampleWidth(thickness), 120, 16);
        gtk_widget_set_hexpand(sample, true);
        GtkWidget* val = gtk_label_new(formatThickness(thickness).c_str());
        gtk_label_set_xalign(GTK_LABEL(val), 1.0);
        gtk_label_set_width_chars(GTK_LABEL(val), 7);
        addClass(val, "mx-numeric");
        gtk_box_pack_start(GTK_BOX(rowBox), name, false, false, 0);
        gtk_box_pack_start(GTK_BOX(rowBox), sample, true, true, 0);
        gtk_box_pack_start(GTK_BOX(rowBox), val, false, false, 0);
        gtk_container_add(GTK_CONTAINER(row), rowBox);
        gtk_box_pack_start(widthPop, row, false, false, 0);
        this->widthRows[i] = {row, sample, GTK_LABEL(val)};
    }
    gtk_widget_show_all(GTK_WIDGET(widthPop));
    gtk_box_pack_start(GTK_BOX(root), this->widthButton, false, false, 0);

    gtk_box_pack_start(GTK_BOX(root), newSeparator(), false, false, 0);

    // Fill switch + fill color/opacity popover
    GtkWidget* fillSwitchRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    addClass(fillSwitchRow, "mx-switch-row");
    this->fillSwitch = gtk_switch_new();
    gtk_widget_set_can_focus(this->fillSwitch, true);
    gtk_widget_set_focus_on_click(this->fillSwitch, false);
    gtk_widget_set_valign(this->fillSwitch, GTK_ALIGN_CENTER);
    g_signal_connect(this->fillSwitch, "notify::active", G_CALLBACK(ShapeToolbox::fillSwitchChangedCb), this);
    gtk_box_pack_start(GTK_BOX(fillSwitchRow), this->fillSwitch, false, false, 0);

    GtkWidget* fillLabelBox = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(fillLabelBox), gtk_label_new(_("Fill")));
    g_signal_connect_object(fillLabelBox, "button-release-event",
                            G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer sw) -> gboolean {
                                if (e->button == 1) {
                                    gtk_switch_set_active(GTK_SWITCH(sw), !gtk_switch_get_active(GTK_SWITCH(sw)));
                                }
                                return true;
                            }),
                            this->fillSwitch, GConnectFlags(0));
    gtk_box_pack_start(GTK_BOX(fillSwitchRow), fillLabelBox, false, false, 0);
    gtk_widget_set_tooltip_text(fillSwitchRow, _("Fill"));
    gtk_box_pack_start(GTK_BOX(root), fillSwitchRow, false, false, 0);

    this->fillButton = prepareFlatButton(gtk_menu_button_new(), _("Fill color and opacity"));
    addClass(this->fillButton, "mx-wide");
    GtkWidget* fillBtnInner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    this->fillSwatch = newColorSwatch(SWATCH_SIZE);
    this->fillPercentLabel = GTK_LABEL(gtk_label_new("50%"));
    addClass(GTK_WIDGET(this->fillPercentLabel), "mx-numeric");
    gtk_label_set_width_chars(this->fillPercentLabel, 4);
    gtk_box_pack_start(GTK_BOX(fillBtnInner), this->fillSwatch, false, false, 0);
    gtk_box_pack_start(GTK_BOX(fillBtnInner), GTK_WIDGET(this->fillPercentLabel), false, false, 0);
    gtk_box_pack_start(GTK_BOX(fillBtnInner), newIcon("mx-down-symbolic", 16), false, false, 0);
    gtk_container_add(GTK_CONTAINER(this->fillButton), fillBtnInner);

    GtkBox* fillPop = newPopover(this->fillButton, &this->fillPopover);
    g_signal_connect(this->fillPopover, "hide", G_CALLBACK(+[](GtkWidget*, gpointer self) {
                         static_cast<ShapeToolbox*>(self)->commitOpacityDrag();
                     }),
                     this);
    addPopoverTitle(fillPop, _("Fill color"));
    this->fillPaletteGrid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(this->fillPaletteGrid), GTK_SELECTION_NONE);
    gtk_box_pack_start(fillPop, this->fillPaletteGrid, false, false, 0);
    GtkWidget* customFill = prepareFlatButton(gtk_button_new(), nullptr);
    addClass(customFill, "mx-prow");
    gtk_container_add(GTK_CONTAINER(customFill), newPopoverRowChild(_("Custom…"), "mx-plus-symbolic"));
    g_signal_connect(customFill, "clicked", G_CALLBACK(ShapeToolbox::fillCustomColorCb), this);
    gtk_box_pack_start(fillPop, customFill, false, false, 0);

    addPopoverTitle(fillPop, _("Opacity"));
    GtkWidget* opacityRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(opacityRow, 6);
    gtk_widget_set_margin_end(opacityRow, 6);
    gtk_widget_set_margin_bottom(opacityRow, 4);
    this->opacityScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5.0, 100.0, 5.0);
    gtk_scale_set_draw_value(GTK_SCALE(this->opacityScale), false);
    gtk_widget_set_can_focus(this->opacityScale, true);
    gtk_widget_set_focus_on_click(this->opacityScale, false);
    gtk_widget_set_size_request(this->opacityScale, 140, -1);
    g_signal_connect(this->opacityScale, "button-press-event", G_CALLBACK(ShapeToolbox::opacityPressCb), this);
    g_signal_connect(this->opacityScale, "button-release-event", G_CALLBACK(ShapeToolbox::opacityReleaseCb), this);
    g_signal_connect(this->opacityScale, "value-changed", G_CALLBACK(ShapeToolbox::opacityValueChangedCb), this);
    this->opacityValueLabel = GTK_LABEL(gtk_label_new("50%"));
    addClass(GTK_WIDGET(this->opacityValueLabel), "mx-numeric");
    gtk_label_set_width_chars(this->opacityValueLabel, 4);
    gtk_label_set_xalign(this->opacityValueLabel, 1.0);
    gtk_box_pack_start(GTK_BOX(opacityRow), this->opacityScale, true, true, 0);
    gtk_box_pack_start(GTK_BOX(opacityRow), GTK_WIDGET(this->opacityValueLabel), false, false, 0);
    gtk_box_pack_start(fillPop, opacityRow, false, false, 0);
    gtk_widget_show_all(GTK_WIDGET(fillPop));
    gtk_box_pack_start(GTK_BOX(root), this->fillButton, false, false, 0);

    // Line style segment
    this->lineStyleSeparator = newSeparator();
    gtk_box_pack_start(GTK_BOX(root), this->lineStyleSeparator, false, false, 0);

    this->lineStyleBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(this->lineStyleBox, "mx-seg");
    gtk_widget_set_valign(this->lineStyleBox, GTK_ALIGN_CENTER);
    const char* lineStyleTooltips[3] = {_("Solid line"), _("Dashed line"), _("Dotted line")};
    for (size_t i = 0; i < this->lineStyleButtons.size(); i++) {
        GtkWidget* b = prepareFlatButton(gtk_toggle_button_new(), lineStyleTooltips[i]);
        gtk_container_add(GTK_CONTAINER(b), newLineStyleSample(static_cast<int>(i)));
        g_object_set_data(G_OBJECT(b), INDEX_KEY, GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(b, "toggled", G_CALLBACK(ShapeToolbox::lineStyleToggledCb), this);
        gtk_box_pack_start(GTK_BOX(this->lineStyleBox), b, false, false, 0);
        this->lineStyleButtons[i] = b;
    }
    gtk_box_pack_start(GTK_BOX(root), this->lineStyleBox, false, false, 0);

    gtk_box_pack_start(GTK_BOX(root), newSeparator(), false, false, 0);

    // Actions: Duplicate, Delete (destructive), Convert to stroke
    gtk_box_pack_start(GTK_BOX(root),
                       makeIconButton("mx-copy-symbolic", _("Duplicate"), G_CALLBACK(ShapeToolbox::duplicateCb)), false,
                       false, 0);

    GtkWidget* delBtn = makeIconButton("mx-trash-symbolic", _("Delete"), G_CALLBACK(ShapeToolbox::deleteCb));
    addClass(delBtn, "destructive");
    gtk_box_pack_start(GTK_BOX(root), delBtn, false, false, 0);

    GtkWidget* convBtn = prepareFlatButton(gtk_button_new(), _("Convert to stroke"));
    addClass(convBtn, "mx-wide");
    GtkWidget* convInner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(convInner), newIcon("mx-spline-symbolic"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(convInner), gtk_label_new(_("Convert to stroke")), false, false, 0);
    gtk_container_add(GTK_CONTAINER(convBtn), convInner);
    g_signal_connect(convBtn, "clicked", G_CALLBACK(ShapeToolbox::convertCb), this);
    gtk_box_pack_start(GTK_BOX(root), convBtn, false, false, 0);

    rebuildPalettes();
    gtk_widget_show_all(root);
}

auto ShapeToolbox::getShapeSelection() const -> EditSelection* {
    XournalView* xournal = this->win->getXournal();
    EditSelection* sel = xournal ? xournal->getSelection() : nullptr;
    return sel && sel->getShapeStroke() ? sel : nullptr;
}

void ShapeToolbox::update() {
    EditSelection* sel = getShapeSelection();
    if (!sel) {
        commitOpacityDrag();
        this->lastStroke = nullptr;
        if (gtk_widget_get_visible(this->box.get())) {
            gtk_widget_hide(this->box.get());
            gtk_widget_queue_draw(this->win->getXournal()->getWidget());
        }
        return;
    }
    if (sel->isMoving()) {
        if (gtk_widget_get_visible(this->box.get())) {
            gtk_widget_hide(this->box.get());
            gtk_widget_queue_draw(this->win->getXournal()->getWidget());
        }
        return;
    }
    refresh(sel->getShapeStroke());
    gtk_widget_show(this->box.get());
    // Recompute the position
    gtk_widget_queue_resize(this->box.get());
}

void ShapeToolbox::refresh(const Stroke* s) {
    this->updating = true;

    // Stroke color
    const Color strokeCol = s->getColor();
    setColorSwatch(this->strokeSwatch, strokeCol);
    for (const auto& [col, btn]: this->strokeDots) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), sameRgb(col, strokeCol));
    }

    // Width
    setSampleWidth(this->widthSample, sampleWidth(s->getWidth()));
    const ToolType thicknessTool = s->getToolType() == StrokeTool::HIGHLIGHTER ? TOOL_HIGHLIGHTER : TOOL_PEN;
    const double* widths = this->win->getControl()->getToolHandler()->getToolThickness(thicknessTool);
    for (size_t i = 0; i < this->widthRows.size(); i++) {
        const double w = widths[WIDTHS[i].size];
        setSampleWidth(this->widthRows[i].sample, sampleWidth(w));
        gtk_label_set_text(this->widthRows[i].valueLabel, formatThickness(w).c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->widthRows[i].button), std::abs(s->getWidth() - w) < 0.05);
    }

    // Filling
    const bool filled = s->getFill() != -1;
    if (filled) {
        this->fillAlpha = s->getFill();
    } else if (s != this->lastStroke) {
        this->fillAlpha = DEFAULT_FILL_ALPHA;
    }
    this->lastStroke = s;

    gtk_switch_set_active(GTK_SWITCH(this->fillSwitch), filled);
    const Color fillCol = s->getFillColor().value_or(strokeCol);
    setColorSwatch(this->fillSwatch, fillCol, !filled);
    const int pct = alphaToPercent(this->fillAlpha);
    const std::string pctStr = std::to_string(pct) + "%";
    gtk_label_set_text(this->fillPercentLabel, pctStr.c_str());
    gtk_label_set_text(this->opacityValueLabel, pctStr.c_str());
    gtk_range_set_value(GTK_RANGE(this->opacityScale), pct);
    for (const auto& [col, btn]: this->fillDots) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), sameRgb(col, fillCol));
    }
    // Filled highlighters are drawn through a colorless mask: only the opacity matters
    const bool showFillBtn = s->getToolType() != StrokeTool::HIGHLIGHTER;
    gtk_widget_set_visible(this->fillButton, showFillBtn);
    gtk_widget_set_sensitive(this->fillButton, filled);

    // Line style
    const bool hasLineStyle = s->getToolType().hasLineStyle();
    gtk_widget_set_visible(this->lineStyleSeparator, hasLineStyle);
    gtk_widget_set_visible(this->lineStyleBox, hasLineStyle);
    const std::string style = StrokeStyle::formatStyle(s->getLineStyle());
    for (size_t i = 0; i < this->lineStyleButtons.size(); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->lineStyleButtons[i]), style == LINE_STYLES[i]);
    }

    this->updating = false;
}

void ShapeToolbox::addUndoAction(UndoActionPtr action) {
    if (action) {
        this->win->getControl()->getUndoRedoHandler()->addUndoAction(std::move(action));
    }
}

void ShapeToolbox::applyStrokeColor(Color color) {
    EditSelection* sel = getShapeSelection();
    if (!sel) {
        return;
    }
    // Keep the alpha the stroke had: it is not the opacity of the stroke, but part of the tool color
    const Color current = sel->getShapeStroke()->getColor();
    color.alpha = current.alpha;
    if (color != current) {
        addUndoAction(sel->setColor(color));
    }
    update();
}

void ShapeToolbox::applyFill(std::optional<Color> color, int alpha) {
    EditSelection* sel = getShapeSelection();
    if (!sel) {
        return;
    }
    const Stroke* s = sel->getShapeStroke();
    this->fillAlpha = std::clamp(alpha, 0, 255);
    // Keep fill disabled until the user enables it; when enabled, update RGB in fillColor and alpha in fill
    const int fill = s->getFill() != -1 ? this->fillAlpha : -1;
    const Color currentColor = s->getFillColor().value_or(s->getColor());
    const Color targetColor = color.value_or(s->getColor());
    if (fill != s->getFill() || !sameRgb(targetColor, currentColor)) {
        addUndoAction(sel->setFillStyle(fill, color));
    }
    update();
}

void ShapeToolbox::commitOpacityDrag() {
    if (!this->opacityDragStart.has_value()) {
        return;
    }
    const int startFill = *this->opacityDragStart;
    this->opacityDragStart.reset();

    EditSelection* sel = getShapeSelection();
    if (!sel) {
        return;
    }
    const Stroke* s = sel->getShapeStroke();
    const int endFill = this->fillAlpha;
    if (s->getFill() != -1 && startFill != endFill) {
        // Restore the pre-drag fill and re-apply the final opacity so the whole drag is one undo step.
        // Keep endFill in a local: setFillStyle() repaints the selection, which refreshes this->fillAlpha.
        const auto fillColor = s->getFillColor();
        sel->setFillStyle(startFill, fillColor);
        addUndoAction(sel->setFillStyle(endFill, fillColor));
        update();
    }
}

auto ShapeToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                      ShapeToolbox* self) -> gboolean {
    if (widget != self->box.get()) {
        return false;
    }
    EditSelection* sel = self->getShapeSelection();
    if (!sel) {
        return false;
    }

    GtkRequisition natural;
    gtk_widget_get_preferred_size(widget, nullptr, &natural);
    allocation->width = natural.width;
    allocation->height = natural.height;

    XournalView* xournal = self->win->getXournal();
    GtkWidget* view = xournal->getWidget();
    const int viewWidth = gtk_widget_get_allocated_width(view);
    const int viewHeight = gtk_widget_get_allocated_height(view);

    // From the coordinates of the whole layout to the ones of the visible part
    const auto bbox = sel->getBoundingBoxInView();
    const auto scroll = xournal->getScrollHandling()->getPosition();
    const double left = bbox.x - scroll.x;
    const double top = bbox.y - scroll.y;
    const double bottom = top + bbox.height;

    // Centred below the shape; above it if there is no room below; at the bottom of the view if neither fits
    int x = static_cast<int>(std::lround(left + bbox.width / 2 - natural.width / 2.0));
    x = std::clamp(x, GAP, std::max(GAP, viewWidth - natural.width - GAP));
    int y = static_cast<int>(std::lround(bottom)) + GAP;
    if (y + natural.height + GAP > viewHeight) {
        y = static_cast<int>(std::lround(top)) - GAP - natural.height;
        if (y < GAP) {
            y = std::max(GAP, viewHeight - natural.height - GAP);
        }
    }

    gtk_widget_translate_coordinates(view, GTK_WIDGET(overlay), x, y, &allocation->x, &allocation->y);
    return true;
}

void ShapeToolbox::scrolledCb(GtkAdjustment*, ShapeToolbox* self) {
    if (gtk_widget_get_visible(self->box.get())) {
        gtk_widget_queue_resize(self->box.get());
    }
}

void ShapeToolbox::strokeDotClickedCb(GtkToggleButton* button, ShapeToolbox* self) {
    if (self->updating) {
        return;
    }
    gtk_popover_popdown(GTK_POPOVER(self->strokeColorPopover));
    const Color c(static_cast<uint32_t>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), COLOR_KEY))));
    self->applyStrokeColor(c);
}

void ShapeToolbox::strokeCustomColorCb(GtkButton*, ShapeToolbox* self) {
    gtk_popover_popdown(GTK_POPOVER(self->strokeColorPopover));
    EditSelection* sel = self->getShapeSelection();
    if (!sel) {
        return;
    }
    GtkWidget* dlg = gtk_color_chooser_dialog_new(_("Stroke color"), GTK_WINDOW(self->win->getWindow()));
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dlg), false);
    GdkRGBA initial = Util::rgb_to_GdkRGBA(sel->getShapeStroke()->getColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dlg), &initial);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GdkRGBA chosen;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg), &chosen);
        self->applyStrokeColor(Util::GdkRGBA_to_rgb(chosen));
    }
    gtk_widget_destroy(dlg);
}

void ShapeToolbox::widthCb(GtkButton* button, ShapeToolbox* self) {
    if (self->updating) {
        return;
    }
    gtk_popover_popdown(GTK_POPOVER(self->widthPopover));
    EditSelection* sel = self->getShapeSelection();
    if (!sel) {
        return;
    }
    const ToolSize size = WIDTHS[getIndex(button)].size;
    ToolHandler* toolHandler = self->win->getControl()->getToolHandler();
    const double pen = toolHandler->getToolThickness(TOOL_PEN)[size];
    const double highlighter = toolHandler->getToolThickness(TOOL_HIGHLIGHTER)[size];
    const double eraser = toolHandler->getToolThickness(TOOL_ERASER)[size];
    const Stroke* s = sel->getShapeStroke();
    const double targetWidth = s->getToolType() == StrokeTool::HIGHLIGHTER ? highlighter : pen;
    if (std::abs(s->getWidth() - targetWidth) >= 1e-6) {
        self->addUndoAction(sel->setSize(pen, highlighter, eraser));
    }
    self->update();
}

void ShapeToolbox::fillSwitchChangedCb(GObject* sw, GParamSpec*, ShapeToolbox* self) {
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    const Stroke* s = sel->getShapeStroke();
    const bool active = gtk_switch_get_active(GTK_SWITCH(sw));
    int fill = -1;
    if (active) {
        fill = s->getToolType() == StrokeTool::HIGHLIGHTER ?
                       self->win->getControl()->getToolHandler()->getHighlighterFill() :
                       self->fillAlpha;
    } else {
        gtk_popover_popdown(GTK_POPOVER(self->fillPopover));
    }
    self->addUndoAction(sel->setFillStyle(fill, s->getFillColor()));
    self->update();
}

void ShapeToolbox::fillDotClickedCb(GtkToggleButton* button, ShapeToolbox* self) {
    if (self->updating) {
        return;
    }
    const Color c(static_cast<uint32_t>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), COLOR_KEY))));
    self->applyFill(c, self->fillAlpha);
}

void ShapeToolbox::fillCustomColorCb(GtkButton*, ShapeToolbox* self) {
    gtk_popover_popdown(GTK_POPOVER(self->fillPopover));
    EditSelection* sel = self->getShapeSelection();
    if (!sel) {
        return;
    }
    const Stroke* s = sel->getShapeStroke();
    GtkWidget* dlg = gtk_color_chooser_dialog_new(_("Fill color"), GTK_WINDOW(self->win->getWindow()));
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dlg), false);
    GdkRGBA initial = Util::rgb_to_GdkRGBA(s->getFillColor().value_or(s->getColor()));
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dlg), &initial);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GdkRGBA chosen;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg), &chosen);
        self->applyFill(Util::GdkRGBA_to_rgb(chosen), self->fillAlpha);
    }
    gtk_widget_destroy(dlg);
}

auto ShapeToolbox::opacityPressCb(GtkWidget*, GdkEventButton*, ShapeToolbox* self) -> gboolean {
    if (EditSelection* sel = self->getShapeSelection()) {
        const Stroke* s = sel->getShapeStroke();
        if (s->getFill() != -1) {
            self->opacityDragStart = s->getFill();
        }
    }
    return false;
}

auto ShapeToolbox::opacityReleaseCb(GtkWidget*, GdkEventButton*, ShapeToolbox* self) -> gboolean {
    self->commitOpacityDrag();
    return false;
}

void ShapeToolbox::opacityValueChangedCb(GtkRange* range, ShapeToolbox* self) {
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    const int pct = static_cast<int>(std::lround(gtk_range_get_value(range)));
    const int alpha = std::clamp(static_cast<int>(std::lround(pct * 255.0 / 100.0)), 0, 255);
    const std::string pctStr = std::to_string(pct) + "%";
    gtk_label_set_text(self->fillPercentLabel, pctStr.c_str());
    gtk_label_set_text(self->opacityValueLabel, pctStr.c_str());

    const Stroke* s = sel->getShapeStroke();
    if (self->opacityDragStart.has_value()) {
        // Preview during the drag; commitOpacityDrag() pushes a single undo step on release
        self->fillAlpha = alpha;
        if (s->getFill() != -1) {
            sel->setFillStyle(alpha, s->getFillColor());
        }
    } else {
        self->applyFill(s->getFillColor(), alpha);
    }
}

void ShapeToolbox::lineStyleToggledCb(GtkToggleButton* button, ShapeToolbox* self) {
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    if (gtk_toggle_button_get_active(button)) {
        self->addUndoAction(sel->setLineStyle(StrokeStyle::parseStyle(LINE_STYLES[getIndex(button)])));
    }
    // Show the style of the stroke again (a click on the active style would otherwise untoggle it)
    self->update();
}

void ShapeToolbox::duplicateCb(GtkButton*, ShapeToolbox* self) {
    if (EditSelection* sel = self->getShapeSelection()) {
        ShapeEditController::duplicate(self->win->getControl(), sel);
    }
}

void ShapeToolbox::deleteCb(GtkButton*, ShapeToolbox* self) {
    if (self->getShapeSelection()) {
        self->win->getXournal()->deleteSelection();
    }
}

void ShapeToolbox::convertCb(GtkButton*, ShapeToolbox* self) {
    if (EditSelection* sel = self->getShapeSelection()) {
        ShapeEditController::convertToStroke(self->win->getControl(), sel);
    }
}
