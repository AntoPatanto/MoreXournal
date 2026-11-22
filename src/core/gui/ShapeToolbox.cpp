#include "ShapeToolbox.h"

#include <algorithm>  // for clamp, max
#include <cmath>      // for lround
#include <string>     // for string

#include "control/Control.h"                     // for Control
#include "control/ToolEnums.h"                   // for TOOL_PEN, TOOL_SIZE_...
#include "control/ToolHandler.h"                 // for ToolHandler
#include "control/settings/Settings.h"           // for Settings
#include "control/shapes/ShapeEditController.h"  // for duplicate, convertToStroke
#include "control/tools/EditSelection.h"         // for EditSelection
#include "gui/IconNameHelper.h"                  // for IconNameHelper
#include "gui/XournalView.h"                     // for XournalView
#include "gui/scroll/ScrollHandling.h"           // for ScrollHandling
#include "model/LineStyle.h"                     // for LineStyle
#include "model/Stroke.h"                        // for Stroke
#include "model/StrokeStyle.h"                   // for parseStyle, formatStyle
#include "undo/UndoRedoHandler.h"                // for UndoRedoHandler
#include "util/Color.h"                          // for Color, GdkRGBA_to_rgb
#include "util/i18n.h"                           // for _

#include "MainWindow.h"  // for MainWindow

namespace {
/// Gap between the shape and the menu, and between the menu and the edges of the view, in pixels
constexpr int GAP = 8;

/// Fill opacity offered when a stroke has none yet
constexpr int DEFAULT_FILL_ALPHA = 128;

constexpr const char* LINE_STYLES[3] = {"plain", "dash", "dot"};

/// Data key of the index of a width or line style button
constexpr const char* INDEX_KEY = "shape-toolbox-index";

struct WidthChoice {
    ToolSize size;
    const char* icon;
    const char* tooltip;
};
const WidthChoice WIDTHS[] = {{TOOL_SIZE_VERY_FINE, "thickness-finer", N_("Very fine")},
                              {TOOL_SIZE_FINE, "thickness-fine", N_("Fine")},
                              {TOOL_SIZE_MEDIUM, "thickness-medium", N_("Medium")},
                              {TOOL_SIZE_THICK, "thickness-thick", N_("Thick")},
                              {TOOL_SIZE_VERY_THICK, "thickness-thicker", N_("Very thick")}};

int getIndex(gpointer widget) { return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), INDEX_KEY)); }
}  // namespace

ShapeToolbox::ShapeToolbox(MainWindow* win, GtkOverlay* overlay): win(win), overlay(overlay, xoj::util::ref) {
    build();

    gtk_overlay_add_overlay(overlay, this->box);
    // The menu takes the clicks in its area, so that missing a button does not draw on the page below
    gtk_overlay_set_overlay_pass_through(overlay, this->box, false);
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

    gtk_widget_hide(this->box);
}

ShapeToolbox::~ShapeToolbox() {
    size_t n = 0;
    for (auto& adj: this->adjustments) {
        for (int i = 0; i < 2; i++) {
            g_signal_handler_disconnect(adj.get(), this->scrollHandlers[n++]);
        }
    }
    g_signal_handler_disconnect(this->overlay.get(), this->positionHandler);
}

GtkWidget* ShapeToolbox::makeButton(const char* iconName, const char* tooltip, GCallback callback) {
    GtkWidget* button = gtk_button_new_from_icon_name(iconName, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(button, false);
    g_signal_connect(button, "clicked", callback, this);
    return button;
}

void ShapeToolbox::build() {
    IconNameHelper icons(this->win->getControl()->getSettings());

    this->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(this->box, "shapeToolbox");
    gtk_style_context_add_class(gtk_widget_get_style_context(this->box), "osd");
    gtk_container_set_border_width(GTK_CONTAINER(this->box), 4);
    gtk_widget_set_halign(this->box, GTK_ALIGN_START);
    gtk_widget_set_valign(this->box, GTK_ALIGN_START);

    // Stroke color
    this->colorButton = gtk_color_button_new();
    gtk_widget_set_tooltip_text(this->colorButton, _("Stroke color"));
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(this->colorButton), false);
    g_signal_connect(this->colorButton, "color-set", G_CALLBACK(ShapeToolbox::colorSetCb), this);
    gtk_box_pack_start(GTK_BOX(this->box), this->colorButton, false, false, 0);

    // Width
    this->widthButton = gtk_menu_button_new();
    gtk_widget_set_tooltip_text(this->widthButton, _("Stroke width"));
    gtk_button_set_relief(GTK_BUTTON(this->widthButton), GTK_RELIEF_NONE);
    gtk_button_set_image(
            GTK_BUTTON(this->widthButton),
            gtk_image_new_from_icon_name(icons.iconName("thickness-medium").c_str(), GTK_ICON_SIZE_SMALL_TOOLBAR));
    this->widthPopover = gtk_popover_new(this->widthButton);
    GtkWidget* widthBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(widthBox), 4);
    for (size_t i = 0; i < std::size(WIDTHS); i++) {
        GtkWidget* b = makeButton(icons.iconName(WIDTHS[i].icon).c_str(), _(WIDTHS[i].tooltip),
                                  G_CALLBACK(ShapeToolbox::widthCb));
        g_object_set_data(G_OBJECT(b), INDEX_KEY, GINT_TO_POINTER(static_cast<int>(i)));
        gtk_box_pack_start(GTK_BOX(widthBox), b, false, false, 0);
    }
    gtk_widget_show_all(widthBox);
    gtk_container_add(GTK_CONTAINER(this->widthPopover), widthBox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(this->widthButton), this->widthPopover);
    gtk_box_pack_start(GTK_BOX(this->box), this->widthButton, false, false, 0);

    // Filling
    this->fillToggle = gtk_toggle_button_new();
    gtk_button_set_image(GTK_BUTTON(this->fillToggle),
                         gtk_image_new_from_icon_name(icons.iconName("fill").c_str(), GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_widget_set_tooltip_text(this->fillToggle, _("Fill"));
    gtk_button_set_relief(GTK_BUTTON(this->fillToggle), GTK_RELIEF_NONE);
    g_signal_connect(this->fillToggle, "toggled", G_CALLBACK(ShapeToolbox::fillToggledCb), this);
    gtk_box_pack_start(GTK_BOX(this->box), this->fillToggle, false, false, 0);

    this->fillColorButton = gtk_color_button_new();
    gtk_widget_set_tooltip_text(this->fillColorButton, _("Fill color and opacity"));
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(this->fillColorButton), true);
    g_signal_connect(this->fillColorButton, "color-set", G_CALLBACK(ShapeToolbox::fillColorSetCb), this);
    gtk_box_pack_start(GTK_BOX(this->box), this->fillColorButton, false, false, 0);

    // Line style
    this->lineStyleBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    const char* lineStyleIcons[3] = {"line-style-plain", "line-style-dash", "line-style-dot"};
    const char* lineStyleTooltips[3] = {_("Solid line"), _("Dashed line"), _("Dotted line")};
    for (size_t i = 0; i < this->lineStyleButtons.size(); i++) {
        GtkWidget* b = gtk_toggle_button_new();
        gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_icon_name(icons.iconName(lineStyleIcons[i]).c_str(),
                                                                         GTK_ICON_SIZE_SMALL_TOOLBAR));
        gtk_widget_set_tooltip_text(b, lineStyleTooltips[i]);
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        g_object_set_data(G_OBJECT(b), INDEX_KEY, GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(b, "toggled", G_CALLBACK(ShapeToolbox::lineStyleToggledCb), this);
        gtk_box_pack_start(GTK_BOX(this->lineStyleBox), b, false, false, 0);
        this->lineStyleButtons[i] = b;
    }
    gtk_box_pack_start(GTK_BOX(this->box), this->lineStyleBox, false, false, 0);

    gtk_box_pack_start(GTK_BOX(this->box), gtk_separator_new(GTK_ORIENTATION_VERTICAL), false, false, 2);

    // Actions
    gtk_box_pack_start(GTK_BOX(this->box),
                       makeButton("edit-copy-symbolic", _("Duplicate"), G_CALLBACK(ShapeToolbox::duplicateCb)), false,
                       false, 0);
    gtk_box_pack_start(GTK_BOX(this->box),
                       makeButton(icons.iconName("tool-pencil").c_str(), _("Convert to stroke"),
                                  G_CALLBACK(ShapeToolbox::convertCb)),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(this->box),
                       makeButton("edit-delete-symbolic", _("Delete"), G_CALLBACK(ShapeToolbox::deleteCb)), false,
                       false, 0);

    // Keep the keyboard focus on the page, for the shortcuts and the arrow keys
    gtk_container_foreach(
            GTK_CONTAINER(this->box), +[](GtkWidget* w, gpointer) { gtk_widget_set_focus_on_click(w, false); },
            nullptr);
    gtk_container_foreach(
            GTK_CONTAINER(this->lineStyleBox), +[](GtkWidget* w, gpointer) { gtk_widget_set_focus_on_click(w, false); },
            nullptr);

    gtk_widget_show_all(this->box);
}

auto ShapeToolbox::getShapeSelection() const -> EditSelection* {
    XournalView* xournal = this->win->getXournal();
    EditSelection* sel = xournal ? xournal->getSelection() : nullptr;
    return sel && sel->getShapeStroke() ? sel : nullptr;
}

void ShapeToolbox::update() {
    EditSelection* sel = getShapeSelection();
    if (!sel) {
        this->lastStroke = nullptr;
        gtk_widget_hide(this->box);
        return;
    }
    if (sel->isMoving()) {
        gtk_widget_hide(this->box);
        return;
    }
    refresh(sel->getShapeStroke());
    gtk_widget_show(this->box);
    // Recompute the position
    gtk_widget_queue_resize(this->box);
}

void ShapeToolbox::refresh(const Stroke* s) {
    this->updating = true;

    GdkRGBA color = Util::rgb_to_GdkRGBA(s->getColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(this->colorButton), &color);

    const bool filled = s->getFill() != -1;
    if (filled) {
        this->fillAlpha = s->getFill();
    } else if (s != this->lastStroke) {
        this->fillAlpha = DEFAULT_FILL_ALPHA;
    }
    this->lastStroke = s;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->fillToggle), filled);
    GdkRGBA fill = Util::argb_to_GdkRGBA(s->getFillColor().value_or(s->getColor()), this->fillAlpha / 255.0);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(this->fillColorButton), &fill);
    // Filled highlighters are drawn through a colorless mask: only the opacity matters
    gtk_widget_set_visible(this->fillColorButton, s->getToolType() != StrokeTool::HIGHLIGHTER);

    const bool hasLineStyle = s->getToolType().hasLineStyle();
    gtk_widget_set_visible(this->lineStyleBox, hasLineStyle);
    const std::string style = StrokeStyle::formatStyle(s->getLineStyle());
    for (size_t i = 0; i < this->lineStyleButtons.size(); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->lineStyleButtons[i]), style == LINE_STYLES[i]);
    }

    this->updating = false;
}

void ShapeToolbox::addUndoAction(UndoActionPtr action) {
    this->win->getControl()->getUndoRedoHandler()->addUndoAction(std::move(action));
}

auto ShapeToolbox::getFillAlpha() const -> int {
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(this->fillColorButton), &rgba);
    return std::clamp(static_cast<int>(std::lround(rgba.alpha * 255)), 0, 255);
}

auto ShapeToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                      ShapeToolbox* self) -> gboolean {
    if (widget != self->box) {
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
    if (gtk_widget_get_visible(self->box)) {
        gtk_widget_queue_resize(self->box);
    }
}

void ShapeToolbox::colorSetCb(GtkColorButton* button, ShapeToolbox* self) {
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    Color color = Util::GdkRGBA_to_rgb(rgba);
    // Keep the alpha the stroke had: it is not the opacity of the stroke, but part of the tool color
    color.alpha = sel->getShapeStroke()->getColor().alpha;
    self->addUndoAction(sel->setColor(color));
}

void ShapeToolbox::widthCb(GtkButton* button, ShapeToolbox* self) {
    gtk_popover_popdown(GTK_POPOVER(self->widthPopover));
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    const ToolSize size = WIDTHS[getIndex(button)].size;
    ToolHandler* toolHandler = self->win->getControl()->getToolHandler();
    const double pen = toolHandler->getToolThickness(TOOL_PEN)[size];
    const double highlighter = toolHandler->getToolThickness(TOOL_HIGHLIGHTER)[size];
    const double eraser = toolHandler->getToolThickness(TOOL_ERASER)[size];
    self->addUndoAction(sel->setSize(pen, highlighter, eraser));
}

void ShapeToolbox::fillToggledCb(GtkToggleButton* button, ShapeToolbox* self) {
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    const Stroke* s = sel->getShapeStroke();
    int fill = -1;
    if (gtk_toggle_button_get_active(button)) {
        fill = s->getToolType() == StrokeTool::HIGHLIGHTER ?
                       self->win->getControl()->getToolHandler()->getHighlighterFill() :
                       self->getFillAlpha();
    }
    self->addUndoAction(sel->setFillStyle(fill, s->getFillColor()));
    self->update();
}

void ShapeToolbox::fillColorSetCb(GtkColorButton* button, ShapeToolbox* self) {
    EditSelection* sel = self->getShapeSelection();
    if (self->updating || !sel) {
        return;
    }
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    const Stroke* s = sel->getShapeStroke();
    self->fillAlpha = self->getFillAlpha();
    // Keep fill disabled until the user enables it; when enabled, update RGB in fillColor and alpha in fill
    const int fill = s->getFill() != -1 ? self->fillAlpha : -1;
    self->addUndoAction(sel->setFillStyle(fill, Util::GdkRGBA_to_rgb(rgba)));
    self->update();
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
