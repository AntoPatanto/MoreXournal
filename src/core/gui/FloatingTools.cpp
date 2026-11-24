#include "FloatingTools.h"

#include <algorithm>  // for clamp, max, min
#include <cmath>      // for lround
#include <string>     // for string, to_string

#include "control/Control.h"                    // for Control
#include "control/actions/ActionDatabase.h"     // for ActionDatabase
#include "control/settings/Settings.h"          // for Settings
#include "enums/Action.enum.h"                  // for Action
#include "gui/ChromeUtil.h"                     // for newChromeIcon, updateScalableIcons
#include "gui/IconNameHelper.h"                 // for IconNameHelper
#include "gui/toolbarMenubar/ToolPageLayer.h"   // for ToolPageLayer
#include "gui/toolbarMenubar/ToolZoomSlider.h"  // for ToolZoomSlider
#include "util/GVariantTemplate.h"              // for getGVariantValue
#include "util/GtkUtil.h"                       // for fixActionableInitialSensitivity
#include "util/i18n.h"                          // for _
#include "util/raii/GVariantSPtr.h"             // for GVariantSPtr

#include "ToolPill.h"  // for ToolPill

namespace {
/// Extra gaps added to the 10 px CSS shadow margin of .mx-float (visual: 8..10 px top/side, 16 px corners)
constexpr int TOP_GAP = 0;
constexpr int CORNER_GAP = 6;
constexpr int SIDE_GAP = 0;
constexpr int ICON_SIZE = 18;

GtkWidget* prepareButton(GtkWidget* btn, Action action, const char* iconName, const char* tooltip) {
    gtk_widget_set_can_focus(btn, false);
    gtk_widget_set_focus_on_click(btn, false);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat");
    if (iconName) {
        gtk_button_set_image(GTK_BUTTON(btn), xoj::chrome::newChromeIcon(iconName, ICON_SIZE));
    }
    gtk_widget_set_tooltip_text(btn, tooltip);
    std::string name = std::string("win.") + Action_toString(action);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), name.c_str());
    xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(btn));
    return btn;
}

/// "120%" from the relative zoom of the ZOOM action
void setZoomLabel(GtkLabel* label, GAction* zoomAction) {
    xoj::util::GVariantSPtr state(g_action_get_state(zoomAction), xoj::util::adopt);
    const auto percent = std::lround(getGVariantValue<double>(state.get()) * 100);
    gtk_label_set_text(label, (std::to_string(percent) + "%").c_str());
}
}  // namespace

FloatingTools::FloatingTools(Control* control, GtkOverlay* overlay, GtkWidget* pageArea):
        control(control),
        overlay(overlay, xoj::util::ref),
        pageArea(pageArea, xoj::util::ref),
        pill(std::make_unique<ToolPill>(control)) {
    zoom.reset(createZoom(), xoj::util::refsink);
    layer.reset(createLayer(), xoj::util::refsink);

    for (GtkWidget* w: {pill->getWidget(), zoom.get(), layer.get()}) {
        gtk_overlay_add_overlay(overlay, w);
        // The controls take the clicks in their area, so that missing a button does not draw on the page below
        gtk_overlay_set_overlay_pass_through(overlay, w, false);
    }
    positionHandler =
            g_signal_connect(overlay, "get-child-position", G_CALLBACK(FloatingTools::getChildPosition), this);
    // Reposition when the page area moves or resizes (sidebar toggle, paned drag, Ctrl+F search bar)
    pageAreaSizeHandler =
            g_signal_connect(pageArea, "size-allocate", G_CALLBACK(FloatingTools::onPageAreaSizeAllocate), this);

    presentationAction = control->getActionDatabase()->getAction(Action::PRESENTATION_MODE);
    presentationHandler = g_signal_connect(presentationAction.get(), "notify::state",
                                           G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
                                               static_cast<FloatingTools*>(self)->updateVisibility();
                                           }),
                                           this);
    updateVisibility();
}

FloatingTools::~FloatingTools() {
    g_signal_handler_disconnect(presentationAction.get(), presentationHandler);
    g_signal_handler_disconnect(pageArea.get(), pageAreaSizeHandler);
    g_signal_handler_disconnect(overlay.get(), positionHandler);
    // The layer control listens to the layer controller: remove it while the controller exists
    gtk_widget_destroy(zoom.get());
    gtk_widget_destroy(layer.get());
}

GtkWidget* FloatingTools::createZoom() {
    ActionDatabase* db = control->getActionDatabase();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(box, "zoomFloat");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-float");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-zoom");
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(box),
                       prepareButton(gtk_button_new(), Action::ZOOM_OUT, "mx-minus-symbolic", _("Zoom out")), false,
                       false, 0);

    // The zoom value; its popover has the zoom slider and 100%
    GtkWidget* value = gtk_menu_button_new();
    gtk_widget_set_can_focus(value, false);
    gtk_widget_set_focus_on_click(value, false);
    gtk_style_context_add_class(gtk_widget_get_style_context(value), "flat");
    gtk_style_context_add_class(gtk_widget_get_style_context(value), "mx-zoom-val");
    gtk_widget_set_tooltip_text(value, _("Zoom"));
    GtkWidget* label = gtk_label_new(nullptr);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "mx-numeric");
    gtk_label_set_width_chars(GTK_LABEL(label), 5);
    gtk_container_add(GTK_CONTAINER(value), label);
    GAction* zoomAction = G_ACTION(db->getAction(Action::ZOOM).get());
    setZoomLabel(GTK_LABEL(label), zoomAction);
    g_signal_connect_object(zoomAction, "notify::state", G_CALLBACK(+[](GObject* a, GParamSpec*, gpointer label) {
                                setZoomLabel(GTK_LABEL(label), G_ACTION(a));
                            }),
                            label, GConnectFlags(0));
    xoj::util::gtk::setWidgetFollowActionEnabled(value, zoomAction);

    GtkWidget* popover = gtk_popover_new(value);
    gtk_style_context_add_class(gtk_widget_get_style_context(popover), "mx-popover");
    GtkWidget* popBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    zoomSlider = std::make_unique<ToolZoomSlider>("ZOOM_SLIDER", control->getZoomControl(),
                                                  IconNameHelper(control->getSettings()), *db);
    auto slider = zoomSlider->createItem(true);
    gtk_widget_set_size_request(slider.get(), 220, -1);
    gtk_box_pack_start(GTK_BOX(popBox), slider.get(), false, false, 0);
    GtkWidget* zoom100 =
            prepareButton(gtk_button_new_with_label(_("Zoom to 100%")), Action::ZOOM_100, nullptr, nullptr);
    gtk_style_context_add_class(gtk_widget_get_style_context(zoom100), "mx-prow");
    g_signal_connect_object(zoom100, "clicked",
                            G_CALLBACK(+[](GtkButton*, gpointer p) { gtk_popover_popdown(GTK_POPOVER(p)); }), popover,
                            GConnectFlags(0));
    gtk_box_pack_start(GTK_BOX(popBox), zoom100, false, false, 0);
    gtk_widget_show_all(popBox);
    gtk_container_add(GTK_CONTAINER(popover), popBox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(value), popover);
    gtk_menu_button_set_direction(GTK_MENU_BUTTON(value), GTK_ARROW_UP);
    gtk_box_pack_start(GTK_BOX(box), value, false, false, 0);

    gtk_box_pack_start(GTK_BOX(box), prepareButton(gtk_button_new(), Action::ZOOM_IN, "mx-plus-symbolic", _("Zoom in")),
                       false, false, 0);
    gtk_box_pack_start(
            GTK_BOX(box),
            prepareButton(gtk_toggle_button_new(), Action::ZOOM_FIT, "mx-fit-symbolic", _("Zoom fit to screen")), false,
            false, 0);
    gtk_widget_show_all(box);
    return box;
}

GtkWidget* FloatingTools::createLayer() {
    layerItem = std::make_unique<ToolPageLayer>("LAYER", control->getLayerController(),
                                                IconNameHelper(control->getSettings()), true);
    auto item = layerItem->createItem(true);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(box, "layerFloat");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-float");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-chip");
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), item.get(), false, false, 0);
    gtk_widget_show_all(box);
    return box;
}

void FloatingTools::setToolbarVisible(bool visible) {
    toolbarVisible = visible;
    updateVisibility();
}

void FloatingTools::rebuildToolOptions() { pill->rebuild(); }

void FloatingTools::updateRecentColors() { pill->updateColors(); }

void FloatingTools::onSelectionChanged() { pill->onSelectionChanged(); }

void FloatingTools::setTouchLayout(bool touch) {
    pill->setTouchLayout(touch);
    const int iconSize = xoj::chrome::chromeIconSize(touch);
    xoj::chrome::updateScalableIcons(zoom.get(), iconSize);
    xoj::chrome::updateScalableIcons(layer.get(), iconSize);
}

void FloatingTools::updateVisibility() {
    xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(presentationAction.get())), xoj::util::adopt);
    const bool visible = toolbarVisible && !g_variant_get_boolean(state.get());
    pill->setVisible(visible);
    for (GtkWidget* w: {zoom.get(), layer.get()}) {
        gtk_widget_set_visible(w, visible);
    }
}

auto FloatingTools::getChildPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                     FloatingTools* self) -> gboolean {
    const bool isPill = widget == self->pill->getWidget();
    const bool isZoom = widget == self->zoom.get();
    const bool isLayer = widget == self->layer.get();
    if (!isPill && !isZoom && !isLayer) {
        return false;
    }

    // The page area in the coordinates of the overlay
    int areaX = 0;
    int areaY = 0;
    if (!gtk_widget_translate_coordinates(self->pageArea.get(), GTK_WIDGET(overlay), 0, 0, &areaX, &areaY)) {
        return false;
    }
    const int areaW = gtk_widget_get_allocated_width(self->pageArea.get());
    const int areaH = gtk_widget_get_allocated_height(self->pageArea.get());
    if (areaW <= 0 || areaH <= 0) {
        return false;
    }

    GtkRequisition minimum;
    GtkRequisition natural;
    gtk_widget_get_preferred_size(widget, &minimum, &natural);
    // Shrink to the page area if the natural size does not fit, never below the minimum size
    const int w = std::max(minimum.width, std::min(natural.width, areaW - 2 * SIDE_GAP));
    const int h = natural.height;
    allocation->width = w;
    allocation->height = h;

    if (isPill) {
        // Centred on the window, so it stays below the tools of the top bar, but inside the page area
        const int centred = gtk_widget_get_allocated_width(GTK_WIDGET(overlay)) / 2 - w / 2;
        allocation->x = std::clamp(centred, areaX + SIDE_GAP, std::max(areaX + SIDE_GAP, areaX + areaW - w - SIDE_GAP));
        allocation->y = areaY + TOP_GAP;
    } else {
        allocation->x = isZoom ? areaX + areaW - w - CORNER_GAP : areaX + CORNER_GAP;
        allocation->y = areaY + areaH - h - CORNER_GAP;
    }
    return true;
}

void FloatingTools::onPageAreaSizeAllocate(GtkWidget* pageArea, GtkAllocation* allocation, FloatingTools* self) {
    int areaX = 0;
    int areaY = 0;
    if (!gtk_widget_translate_coordinates(pageArea, GTK_WIDGET(self->overlay.get()), 0, 0, &areaX, &areaY)) {
        return;
    }
    const GdkRectangle rect{areaX, areaY, allocation->width, allocation->height};
    if (rect.x != self->lastPageRect.x || rect.y != self->lastPageRect.y || rect.width != self->lastPageRect.width ||
        rect.height != self->lastPageRect.height) {
        self->lastPageRect = rect;
        gtk_widget_queue_allocate(GTK_WIDGET(self->overlay.get()));
    }
}
