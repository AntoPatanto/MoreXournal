#include "TopBar.h"

#include <algorithm>  // for clamp, max, min
#include <utility>    // for pair

#include "control/Control.h"                 // for Control
#include "control/ToolHandler.h"             // for ToolHandler
#include "control/actions/ActionDatabase.h"  // for ActionDatabase
#include "control/settings/Settings.h"       // for Settings
#include "enums/Action.enum.h"               // for Action
#include "gui/ChromeUtil.h"                  // for newChromeIcon, updateScalableIcons, updateMenuTouchClass
#include "undo/UndoRedoHandler.h"            // for UndoRedoHandler
#include "util/GVariantTemplate.h"           // for getGVariantValue
#include "util/GtkUtil.h"                    // for fixActionableInitialSensitivity, setWidgetFollowActionEnabled
#include "util/i18n.h"                       // for _, _F, FS
#include "util/raii/GVariantSPtr.h"          // for GVariantSPtr

using xoj::topbar::ToolGroup;

namespace {
constexpr int COMPACT_ICON_PIXEL_SIZE = 18;

// Custom 3-slot container (start, center, end) packed into GtkHeaderBar with hexpand=true.
// Keeps the tools centered on the full window when there is room, shifts them smoothly left when
// narrow so end controls never collide with CSD window buttons, ellipsizes the title cleanly,
// and reports a minimum width under 600 px so the window can shrink or half-tile freely.
struct MxTopBarBox {
    GtkContainer parent;
    GtkWidget* start;
    GtkWidget* center;
    GtkWidget* end;
    gint targetWidth;
};
struct MxTopBarBoxClass {
    GtkContainerClass parent_class;
};

G_DEFINE_TYPE(MxTopBarBox, mx_top_bar_box, GTK_TYPE_CONTAINER)

void mx_top_bar_box_set_target_width(GtkWidget* widget, gint width) {
    auto* self = reinterpret_cast<MxTopBarBox*>(widget);
    if (self->targetWidth != width) {
        self->targetWidth = width;
        gtk_widget_queue_resize(widget);
    }
}

void mx_top_bar_box_get_preferred_width(GtkWidget* widget, gint* minimum, gint* natural) {
    auto* self = reinterpret_cast<MxTopBarBox*>(widget);
    auto natOf = [](GtkWidget* w) -> gint {
        if (!w || !gtk_widget_get_visible(w)) {
            return 0;
        }
        gint m = 0;
        gint n = 0;
        gtk_widget_get_preferred_width(w, &m, &n);
        return n;
    };
    const gint nat = natOf(self->start) + natOf(self->center) + natOf(self->end) + 16;
    if (minimum) {
        // Below MIN_WINDOW_WIDTH minus CSD window buttons (~126 px) so WM allows shrinking to 600 px in both densities
        *minimum = std::min(nat, 470);
    }
    if (natural) {
        // GtkHeaderBar caps pack_start children to their natural width; request targetWidth so this
        // container spans the full header bar up to the CSD window controls at any window width.
        *natural = std::max(nat, self->targetWidth);
    }
}

void mx_top_bar_box_get_preferred_height(GtkWidget* widget, gint* minimum, gint* natural) {
    auto* self = reinterpret_cast<MxTopBarBox*>(widget);
    gint maxMin = 0;
    gint maxNat = 0;
    for (GtkWidget* w: {self->start, self->center, self->end}) {
        if (w && gtk_widget_get_visible(w)) {
            gint m = 0;
            gint n = 0;
            gtk_widget_get_preferred_height(w, &m, &n);
            maxMin = std::max(maxMin, m);
            maxNat = std::max(maxNat, n);
        }
    }
    if (minimum) {
        *minimum = maxMin;
    }
    if (natural) {
        *natural = maxNat;
    }
}

void mx_top_bar_box_size_allocate(GtkWidget* widget, GtkAllocation* allocation) {
    gtk_widget_set_allocation(widget, allocation);
    auto* self = reinterpret_cast<MxTopBarBox*>(widget);

    auto measure = [](GtkWidget* w, gint& minW, gint& natW) {
        minW = 0;
        natW = 0;
        if (w && gtk_widget_get_visible(w)) {
            gtk_widget_get_preferred_width(w, &minW, &natW);
        }
    };
    gint startMin = 0, startNat = 0;
    gint centerMin = 0, centerNat = 0;
    gint endMin = 0, endNat = 0;
    measure(self->start, startMin, startNat);
    measure(self->center, centerMin, centerNat);
    measure(self->end, endMin, endNat);

    const bool hasStart = self->start && gtk_widget_get_visible(self->start);
    const bool hasCenter = self->center && gtk_widget_get_visible(self->center);
    const bool hasEnd = self->end && gtk_widget_get_visible(self->end);
    constexpr gint GAP = 6;

    const gint endW = hasEnd ? std::min(endNat, allocation->width) : 0;
    const gint endX = allocation->x + allocation->width - endW;

    const gint gapStart = hasStart ? GAP : 0;
    const gint gapEnd = hasEnd ? GAP : 0;

    // Aim to centre the tools on the whole header bar (window width), not only this box
    gint headerCenterX = allocation->x + allocation->width / 2;
    if (GtkWidget* parent = gtk_widget_get_parent(widget)) {
        GtkAllocation pa{};
        gtk_widget_get_allocation(parent, &pa);
        if (pa.width > allocation->width) {
            headerCenterX = pa.x + pa.width / 2;
        }
    }

    const gint minCenterX = allocation->x + (hasStart ? startMin + gapStart : 0);
    const gint maxCenterRight = std::max(allocation->x, endX - gapEnd);
    const gint availBetween = std::max(0, maxCenterRight - minCenterX);
    const gint centerW = hasCenter ? std::min(centerNat, availBetween) : 0;
    const gint maxCenterX = std::max(minCenterX, maxCenterRight - centerW);
    const gint desiredCenterX = headerCenterX - centerW / 2;
    const gint centerX = std::clamp(desiredCenterX, minCenterX, maxCenterX);

    const gint availStart = hasCenter ? std::max(0, centerX - allocation->x - gapStart) :
                                        std::max(0, endX - allocation->x - gapEnd);
    const gint startW = hasStart ? std::min(startNat, availStart) : 0;

    if (hasStart) {
        GtkAllocation a{allocation->x, allocation->y, startW, allocation->height};
        gtk_widget_size_allocate(self->start, &a);
    }
    if (hasCenter) {
        GtkAllocation a{centerX, allocation->y, centerW, allocation->height};
        gtk_widget_size_allocate(self->center, &a);
    }
    if (hasEnd) {
        GtkAllocation a{endX, allocation->y, endW, allocation->height};
        gtk_widget_size_allocate(self->end, &a);
    }
}

void mx_top_bar_box_forall(GtkContainer* container, gboolean, GtkCallback callback, gpointer data) {
    auto* self = reinterpret_cast<MxTopBarBox*>(container);
    for (GtkWidget* w: {self->start, self->center, self->end}) {
        if (w) {
            callback(w, data);
        }
    }
}

void mx_top_bar_box_remove(GtkContainer* container, GtkWidget* child) {
    auto* self = reinterpret_cast<MxTopBarBox*>(container);
    if (self->start == child) {
        self->start = nullptr;
        gtk_widget_unparent(child);
    } else if (self->center == child) {
        self->center = nullptr;
        gtk_widget_unparent(child);
    } else if (self->end == child) {
        self->end = nullptr;
        gtk_widget_unparent(child);
    }
}

void mx_top_bar_box_class_init(MxTopBarBoxClass* klass) {
    GtkWidgetClass* wc = GTK_WIDGET_CLASS(klass);
    wc->get_preferred_width = mx_top_bar_box_get_preferred_width;
    wc->get_preferred_height = mx_top_bar_box_get_preferred_height;
    wc->size_allocate = mx_top_bar_box_size_allocate;

    GtkContainerClass* cc = GTK_CONTAINER_CLASS(klass);
    cc->forall = mx_top_bar_box_forall;
    cc->remove = mx_top_bar_box_remove;
}

void mx_top_bar_box_init(MxTopBarBox* self) {
    gtk_widget_set_has_window(GTK_WIDGET(self), false);
    gtk_widget_set_hexpand(GTK_WIDGET(self), true);
    self->start = nullptr;
    self->center = nullptr;
    self->end = nullptr;
    self->targetWidth = 0;
}

GtkWidget* newTopBarBox(GtkWidget* start, GtkWidget* center, GtkWidget* end) {
    auto* box = reinterpret_cast<MxTopBarBox*>(g_object_new(mx_top_bar_box_get_type(), nullptr));
    box->start = start;
    box->center = center;
    box->end = end;
    gtk_widget_set_parent(start, GTK_WIDGET(box));
    gtk_widget_set_parent(center, GTK_WIDGET(box));
    gtk_widget_set_parent(end, GTK_WIDGET(box));
    gtk_widget_show(GTK_WIDGET(box));
    return GTK_WIDGET(box);
}

/// Draws an 8 px @mx_accent unsaved dot with a 2 px @mx_chrome halo on the top-right of the menu button
gboolean drawUnsavedDot(GtkWidget* btn, cairo_t* cr, gpointer showFlagPtr) {
    if (!*static_cast<const bool*>(showFlagPtr)) {
        return false;
    }
    GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
    GdkRGBA accent{0.21, 0.39, 0.91, 1.0};
    GdkRGBA chrome{0.96, 0.96, 0.97, 1.0};
    gtk_style_context_lookup_color(ctx, "mx_accent", &accent);
    gtk_style_context_lookup_color(ctx, "mx_chrome", &chrome);

    const double w = gtk_widget_get_allocated_width(btn);
    const double cx = w - 7.0;
    const double cy = 7.0;

    cairo_save(cr);
    cairo_arc(cr, cx, cy, 6.0, 0.0, 2.0 * G_PI);
    gdk_cairo_set_source_rgba(cr, &chrome);
    cairo_fill(cr);

    cairo_arc(cr, cx, cy, 4.0, 0.0, 2.0 * G_PI);
    gdk_cairo_set_source_rgba(cr, &accent);
    cairo_fill(cr);
    cairo_restore(cr);
    return false;
}

/// The actions holding the drawing type of the active tool, and the drawing type each one stands for
constexpr std::pair<Action, DrawingType> DRAWING_TYPE_ACTIONS[] = {
        {Action::TOOL_DRAW_SHAPE_RECOGNIZER, DRAWING_TYPE_SHAPE_RECOGNIZER},
        {Action::TOOL_DRAW_RECTANGLE, DRAWING_TYPE_RECTANGLE},
        {Action::TOOL_DRAW_ELLIPSE, DRAWING_TYPE_ELLIPSE},
        {Action::TOOL_DRAW_ARROW, DRAWING_TYPE_ARROW},
        {Action::TOOL_DRAW_DOUBLE_ARROW, DRAWING_TYPE_DOUBLE_ARROW},
        {Action::TOOL_DRAW_COORDINATE_SYSTEM, DRAWING_TYPE_COORDINATE_SYSTEM},
        {Action::TOOL_DRAW_LINE, DRAWING_TYPE_LINE},
        {Action::TOOL_DRAW_SPLINE, DRAWING_TYPE_SPLINE}};

Action actionForDrawingType(DrawingType dt) {
    for (const auto& [action, type]: DRAWING_TYPE_ACTIONS) {
        if (type == dt) {
            return action;
        }
    }
    return Action::TOOL_DRAW_RECTANGLE;
}

std::string winAction(Action a) { return std::string("win.") + Action_toString(a); }

GtkWidget* newIcon(const std::string& iconName, int size = COMPACT_ICON_PIXEL_SIZE) {
    return xoj::chrome::newChromeIcon(iconName.c_str(), size);
}

/// A flat button bound to a window action. Focusable via keyboard (F6/Tab), never steals focus on click.
GtkWidget* newActionButton(GtkWidget* btn, Action action, const std::string& iconName, const char* tooltip) {
    gtk_widget_set_can_focus(btn, true);
    gtk_widget_set_focus_on_click(btn, false);
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat");
    if (!iconName.empty()) {
        gtk_button_set_image(GTK_BUTTON(btn), newIcon(iconName));
    }
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), winAction(action).c_str());
    xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(btn));
    return btn;
}

GtkWidget* newSeparator() {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sep), "mx-separator");
    return sep;
}

template <typename T>
T getActionState(ActionDatabase* db, Action a) {
    xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(db->getAction(a).get())), xoj::util::adopt);
    return getGVariantValue<T>(state.get());
}
}  // namespace

TopBar::TopBar(Control* control, GMenuModel* mainMenu): control(control) {
    createStart(mainMenu);
    createCenter();
    createEnd();
    barContainer.reset(newTopBarBox(startBox.get(), centerBox.get(), endBox.get()), xoj::util::refsink);
    emptyTitle.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), xoj::util::refsink);
    g_signal_connect(barContainer.get(), "size-allocate",
                     G_CALLBACK(+[](GtkWidget*, GtkAllocation* alloc, gpointer self) {
                         if (alloc->width > 1) {
                             static_cast<TopBar*>(self)->onUsableWidthAllocated(alloc->width);
                         }
                     }),
                     this);

    // Follow tool changes from shortcuts, menus, toolbars and stylus buttons
    ActionDatabase* db = control->getActionDatabase();
    auto onStateChanged = +[](GObject* action, GParamSpec*, gpointer self) {
        static_cast<TopBar*>(self)->onActionStateChanged(G_ACTION(action));
    };
    auto watch = [&](Action a) {
        ActionRef action = db->getAction(a);
        gulong id = g_signal_connect(action.get(), "notify::state", G_CALLBACK(onStateChanged), this);
        watchedActions.emplace_back(std::move(action), id);
    };
    watch(Action::SELECT_TOOL);
    for (const auto& [action, type]: DRAWING_TYPE_ACTIONS) {
        watch(action);
    }
    syncTools();
}

TopBar::~TopBar() {
    if (layoutIdleId != 0) {
        g_source_remove(layoutIdleId);
        layoutIdleId = 0;
    }
    for (size_t i = 0; i < TOOL_BUTTON_COUNT; i++) {
        g_signal_handler_disconnect(toolButtons[i], toolClickedIds[i]);
    }
    for (auto& [action, id]: watchedActions) {
        g_signal_handler_disconnect(action.get(), id);
    }
    if (menuButton) {
        g_signal_handlers_disconnect_by_data(menuButton, this);
        if (GtkMenu* popup = gtk_menu_button_get_popup(GTK_MENU_BUTTON(menuButton))) {
            g_signal_handlers_disconnect_by_data(popup, this);
        }
    }
    if (overflowButton) {
        if (GtkMenu* popup = gtk_menu_button_get_popup(GTK_MENU_BUTTON(overflowButton))) {
            g_signal_handlers_disconnect_by_data(popup, control);
        }
    }
    if (attachedBar) {
        gtk_header_bar_set_custom_title(attachedBar, nullptr);
        attachedBar = nullptr;
    }
    if (barContainer) {
        g_signal_handlers_disconnect_by_data(barContainer.get(), this);
        gtk_widget_destroy(barContainer.get());
    }
}

void TopBar::createStart(GMenuModel* mainMenu) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    startBox.reset(box, xoj::util::refsink);

    menuButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(menuButton, true);
    gtk_widget_set_focus_on_click(menuButton, false);
    gtk_widget_set_valign(menuButton, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(menuButton), "flat");
    gtk_style_context_add_class(gtk_widget_get_style_context(menuButton), "mx-menu-button");
    gtk_button_set_image(GTK_BUTTON(menuButton), newIcon("mx-menu-symbolic"));
    gtk_widget_set_tooltip_text(menuButton, _("Main menu"));
    // A real menu, not a popover: it scrolls, shows the shortcuts and opens the submenus to the side
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(menuButton), false);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menuButton), mainMenu);

    // Draw the unsaved indicator badge on the menu button when the title is hidden (< 860 px)
    g_signal_connect_after(menuButton, "draw",
                           G_CALLBACK(+[](GtkWidget* btn, cairo_t* cr, gpointer selfPtr) -> gboolean {
                               auto* self = static_cast<TopBar*>(selfPtr);
                               const bool showDot = self->layout.showTitleInMenu && self->docUnsaved;
                               return drawUnsavedDot(btn, cr, const_cast<bool*>(&showDot));
                           }),
                           this);

    if (GtkMenu* popup = gtk_menu_button_get_popup(GTK_MENU_BUTTON(menuButton))) {
        xoj::chrome::applyMenuClassesRecursive(GTK_WIDGET(popup), false);
        xoj::chrome::hideTooltipWhilePopupOpen(menuButton, GTK_WIDGET(popup));

        // Document title + save status header, shown at the top of the main menu when narrow
        menuHeaderItem = gtk_menu_item_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(menuHeaderItem), "mx-menu-header");
        gtk_widget_set_sensitive(menuHeaderItem, false);
        GtkWidget* hdrBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        menuHeaderTitle = GTK_LABEL(gtk_label_new(nullptr));
        menuHeaderStatus = GTK_LABEL(gtk_label_new(nullptr));
        for (GtkLabel* l: {menuHeaderTitle, menuHeaderStatus}) {
            gtk_label_set_xalign(l, 0.0);
            gtk_label_set_ellipsize(l, PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(l, 32);
            gtk_label_set_single_line_mode(l, true);
            gtk_box_pack_start(GTK_BOX(hdrBox), GTK_WIDGET(l), false, false, 0);
        }
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(menuHeaderTitle)), "mx-menu-header-title");
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(menuHeaderStatus)),
                                    "mx-menu-header-status");
        gtk_container_add(GTK_CONTAINER(menuHeaderItem), hdrBox);
        gtk_widget_show_all(hdrBox);

        menuHeaderSep = gtk_separator_menu_item_new();
        gtk_menu_shell_prepend(GTK_MENU_SHELL(popup), menuHeaderSep);
        gtk_menu_shell_prepend(GTK_MENU_SHELL(popup), menuHeaderItem);
        gtk_widget_set_no_show_all(menuHeaderItem, true);
        gtk_widget_set_no_show_all(menuHeaderSep, true);
        gtk_widget_hide(menuHeaderItem);
        gtk_widget_hide(menuHeaderSep);

        g_signal_connect(popup, "show",
                         G_CALLBACK(+[](GtkWidget* menu, gpointer selfPtr) {
                             auto* self = static_cast<TopBar*>(selfPtr);
                             GdkEvent* ev = gtk_get_current_event();
                             const bool fromTouch = xoj::chrome::isTouchEvent(ev);
                             if (ev) {
                                 gdk_event_free(ev);
                             }
                             xoj::chrome::updateMenuTouchClass(
                                     menu, self->control->getSettings()->getTouchMode(), fromTouch);
                             gtk_widget_set_visible(self->menuHeaderItem, self->layout.showTitleInMenu);
                             gtk_widget_set_visible(self->menuHeaderSep, self->layout.showTitleInMenu);
                         }),
                         this);
    }
    gtk_container_add(GTK_CONTAINER(box), menuButton);

    titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(titleBox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(titleBox, 4);

    titleLabel = GTK_LABEL(gtk_label_new(nullptr));
    statusLabel = GTK_LABEL(gtk_label_new(nullptr));
    for (GtkLabel* l: {titleLabel, statusLabel}) {
        gtk_label_set_xalign(l, 0.0);
        gtk_label_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(l, 28);
        gtk_label_set_single_line_mode(l, true);
        gtk_container_add(GTK_CONTAINER(titleBox), GTK_WIDGET(l));
    }
    gtk_label_set_width_chars(titleLabel, 1);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(titleLabel)), "mx-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(statusLabel)), "mx-status");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(statusLabel)), "dim-label");
    gtk_container_add(GTK_CONTAINER(box), titleBox);

    gtk_widget_show_all(box);
}

void TopBar::createCenter() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    centerBox.reset(box, xoj::util::refsink);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-tools");

    struct ToolDef {
        ToolGroup group;
        const char* icon;
        std::string tooltip;
    };
    const ToolDef tools[] = {{ToolGroup::PEN, "mx-pen-symbolic", _("Pen")},
                             {ToolGroup::HIGHLIGHTER, "mx-highlighter-symbolic", _("Highlighter")},
                             {ToolGroup::ERASER, "mx-eraser-symbolic", _("Eraser")},
                             {ToolGroup::SELECTION, "mx-lasso-symbolic", _("Select")},
                             {ToolGroup::SHAPES, "mx-shapes-symbolic", _("Shapes")},
                             {ToolGroup::TEXT, "mx-text-symbolic", _("Text")},
                             {ToolGroup::IMAGE, "mx-image-symbolic", _("Image")},
                             {ToolGroup::HAND, "mx-hand-symbolic", _("Hand")}};

    for (const auto& def: tools) {
        const auto i = static_cast<size_t>(def.group);
        GtkWidget* btn = gtk_toggle_button_new();
        gtk_widget_set_can_focus(btn, true);
        gtk_widget_set_focus_on_click(btn, false);
        gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "mx-tool");
        gtk_button_set_image(GTK_BUTTON(btn), newIcon(def.icon));
        gtk_widget_set_tooltip_text(btn, def.tooltip.c_str());

        toolButtonData[i] = {this, def.group};
        toolClickedIds[i] = g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                                                 auto* data = static_cast<ToolButtonData*>(d);
                                                 data->self->onToolClicked(data->group);
                                             }),
                                             &toolButtonData[i]);
        toolButtons[i] = GTK_TOGGLE_BUTTON(btn);
        // Control::block() disables the tool action: the buttons follow
        xoj::util::gtk::setWidgetFollowActionEnabled(
                btn, G_ACTION(control->getActionDatabase()->getAction(Action::SELECT_TOOL).get()));

        gtk_container_add(GTK_CONTAINER(box), btn);
        if (def.group == ToolGroup::ERASER || def.group == ToolGroup::SHAPES) {
            GtkWidget* sep = newSeparator();
            separators.push_back(sep);
            gtk_container_add(GTK_CONTAINER(box), sep);
        }
    }
    gtk_widget_show_all(box);
}

void TopBar::createEnd() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    endBox.reset(box, xoj::util::refsink);

    // Undo and redo name the action they revert or repeat in the tooltip
    auto queryTooltip = [](GtkWidget*, gint, gint, gboolean, GtkTooltip* tooltip, gpointer d) -> gboolean {
        auto* undoRedo = static_cast<UndoRedoHandler*>(d);
        gtk_tooltip_set_text(tooltip, undoRedo->undoDescription().c_str());
        return true;
    };
    auto queryRedoTooltip = [](GtkWidget*, gint, gint, gboolean, GtkTooltip* tooltip, gpointer d) -> gboolean {
        auto* undoRedo = static_cast<UndoRedoHandler*>(d);
        gtk_tooltip_set_text(tooltip, undoRedo->redoDescription().c_str());
        return true;
    };
    GtkWidget* undo = newActionButton(gtk_button_new(), Action::UNDO, "mx-undo-symbolic", _("Undo"));
    g_signal_connect(undo, "query-tooltip", G_CALLBACK(+queryTooltip), control->getUndoRedoHandler());
    GtkWidget* redo = newActionButton(gtk_button_new(), Action::REDO, "mx-redo-symbolic", _("Redo"));
    g_signal_connect(redo, "query-tooltip", G_CALLBACK(+queryRedoTooltip), control->getUndoRedoHandler());
    gtk_container_add(GTK_CONTAINER(box), undo);
    gtk_container_add(GTK_CONTAINER(box), redo);
    GtkWidget* sep = newSeparator();
    separators.push_back(sep);
    gtk_container_add(GTK_CONTAINER(box), sep);

    sidebarButton =
            newActionButton(gtk_toggle_button_new(), Action::SHOW_SIDEBAR, "mx-sidebar-symbolic", _("Page sidebar"));
    gtk_container_add(GTK_CONTAINER(box), sidebarButton);

    pageNav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(pageNav, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(pageNav), "mx-pagenav");
    gtk_container_add(GTK_CONTAINER(pageNav),
                      newActionButton(gtk_button_new(), Action::GOTO_PREVIOUS, "mx-prev-symbolic", _("Previous page")));
    GtkWidget* pageButton = newActionButton(gtk_button_new(), Action::GOTO_PAGE, "", _("Go to page"));
    gtk_style_context_add_class(gtk_widget_get_style_context(pageButton), "mx-page-goto");
    pageLabel = GTK_LABEL(gtk_label_new(nullptr));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(pageLabel)), "mx-page-label");
    gtk_container_add(GTK_CONTAINER(pageButton), GTK_WIDGET(pageLabel));
    gtk_container_add(GTK_CONTAINER(pageNav), pageButton);
    gtk_container_add(GTK_CONTAINER(pageNav),
                      newActionButton(gtk_button_new(), Action::GOTO_NEXT, "mx-next-symbolic", _("Next page")));
    gtk_container_add(GTK_CONTAINER(box), pageNav);

    // Compact "3 / 12 v" popover button shown at 700..859 px
    pageCompactButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(pageCompactButton, true);
    gtk_widget_set_focus_on_click(pageCompactButton, false);
    gtk_widget_set_valign(pageCompactButton, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(pageCompactButton), "flat");
    gtk_style_context_add_class(gtk_widget_get_style_context(pageCompactButton), "mx-wide");
    gtk_widget_set_tooltip_text(pageCompactButton, _("Go to page"));
    GtkWidget* compactInner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    compactPageLabel = GTK_LABEL(gtk_label_new(nullptr));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(compactPageLabel)), "mx-page-label");
    gtk_box_pack_start(GTK_BOX(compactInner), GTK_WIDGET(compactPageLabel), false, false, 0);
    gtk_box_pack_start(GTK_BOX(compactInner), xoj::chrome::newChromeIcon("mx-down-symbolic", 16, false), false, false,
                       0);
    gtk_container_add(GTK_CONTAINER(pageCompactButton), compactInner);

    GtkWidget* pagePopover = gtk_popover_new(pageCompactButton);
    gtk_style_context_add_class(gtk_widget_get_style_context(pagePopover), "mx-popover");
    GtkWidget* pagePopBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    auto makePopRow = [](Action action, const char* label, const char* iconName) {
        GtkWidget* btn = gtk_button_new();
        gtk_widget_set_can_focus(btn, true);
        gtk_widget_set_focus_on_click(btn, false);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "mx-prow");
        GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        if (iconName) {
            gtk_box_pack_start(GTK_BOX(rowBox), newIcon(iconName), false, false, 0);
        }
        GtkWidget* lbl = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_box_pack_start(GTK_BOX(rowBox), lbl, true, true, 0);
        gtk_container_add(GTK_CONTAINER(btn), rowBox);
        gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), winAction(action).c_str());
        xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(btn));
        return btn;
    };
    gtk_box_pack_start(GTK_BOX(pagePopBox), makePopRow(Action::GOTO_PREVIOUS, _("Previous page"), "mx-prev-symbolic"),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(pagePopBox), makePopRow(Action::GOTO_NEXT, _("Next page"), "mx-next-symbolic"), false,
                       false, 0);
    gtk_box_pack_start(GTK_BOX(pagePopBox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), false, false, 0);
    GtkWidget* gotoRow = makePopRow(Action::GOTO_PAGE, _("Go to page…"), nullptr);
    g_signal_connect_object(gotoRow, "clicked",
                            G_CALLBACK(+[](GtkButton*, gpointer p) { gtk_popover_popdown(GTK_POPOVER(p)); }),
                            pagePopover, GConnectFlags(0));
    gtk_box_pack_start(GTK_BOX(pagePopBox), gotoRow, false, false, 0);
    gtk_widget_show_all(pagePopBox);
    gtk_container_add(GTK_CONTAINER(pagePopover), pagePopBox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(pageCompactButton), pagePopover);
    xoj::chrome::hideTooltipWhilePopupOpen(pageCompactButton, pagePopover);
    gtk_container_add(GTK_CONTAINER(box), pageCompactButton);

    newPageButton =
            newActionButton(gtk_button_new(), Action::NEW_PAGE_AFTER, "mx-page-add-symbolic", _("New page"));
    gtk_container_add(GTK_CONTAINER(box), newPageButton);

    fullscreenButton =
            newActionButton(gtk_toggle_button_new(), Action::FULLSCREEN, "mx-fullscreen-symbolic", _("Fullscreen"));
    gtk_container_add(GTK_CONTAINER(box), fullscreenButton);

    // Overflow "..." menu button shown below 1000 px
    overflowButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(overflowButton, true);
    gtk_widget_set_focus_on_click(overflowButton, false);
    gtk_widget_set_valign(overflowButton, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(overflowButton), "flat");
    gtk_button_set_image(GTK_BUTTON(overflowButton), newIcon("mx-more-symbolic"));
    gtk_widget_set_tooltip_text(overflowButton, _("More"));
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(overflowButton), false);
    overflowMenu.reset(g_menu_new(), xoj::util::adopt);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(overflowButton), G_MENU_MODEL(overflowMenu.get()));
    if (GtkMenu* popup = gtk_menu_button_get_popup(GTK_MENU_BUTTON(overflowButton))) {
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(popup)), "mx-menu");
        xoj::chrome::hideTooltipWhilePopupOpen(overflowButton, GTK_WIDGET(popup));
        g_signal_connect(popup, "show",
                         G_CALLBACK(+[](GtkWidget* menu, gpointer ctrl) {
                             GdkEvent* ev = gtk_get_current_event();
                             const bool fromTouch = xoj::chrome::isTouchEvent(ev);
                             if (ev) {
                                 gdk_event_free(ev);
                             }
                             xoj::chrome::updateMenuTouchClass(
                                     menu, static_cast<Control*>(ctrl)->getSettings()->getTouchMode(), fromTouch);
                         }),
                         control);
    }
    gtk_container_add(GTK_CONTAINER(box), overflowButton);

    gtk_widget_show_all(box);
    applyLayout();
}

void TopBar::attachTo(GtkHeaderBar* bar) {
    if (bar == attachedBar) {
        return;
    }
    if (attachedBar) {
        gtk_header_bar_set_custom_title(attachedBar, nullptr);
        gtk_container_remove(GTK_CONTAINER(attachedBar), barContainer.get());
    }
    // Setting an unshown custom_title suppresses GtkHeaderBar's default window-title label
    // while letting the hexpand barContainer span the full width up to the CSD window buttons.
    gtk_header_bar_set_custom_title(bar, emptyTitle.get());
    gtk_header_bar_pack_start(bar, barContainer.get());
    attachedBar = bar;
}

void TopBar::setDocumentTitle(const std::string& name, const std::string& status, bool unsaved) {
    docName = name;
    docStatus = status;
    docUnsaved = unsaved;
    gtk_label_set_text(titleLabel, name.c_str());
    gtk_label_set_text(statusLabel, status.c_str());
    gtk_widget_set_tooltip_text(GTK_WIDGET(titleLabel), name.c_str());
    if (menuHeaderTitle) {
        gtk_label_set_text(menuHeaderTitle, name.c_str());
    }
    if (menuHeaderStatus) {
        gtk_label_set_text(menuHeaderStatus, status.c_str());
    }
    updateMenuButtonTooltip();
    if (menuButton) {
        gtk_widget_queue_draw(menuButton);
    }
}

void TopBar::setPageInfo(size_t page, size_t pageCount) {
    std::string text =
            pageCount == 0 ? std::string("0 / 0") : std::to_string(page + 1) + " / " + std::to_string(pageCount);
    gtk_label_set_text(pageLabel, text.c_str());
    if (compactPageLabel) {
        gtk_label_set_text(compactPageLabel, text.c_str());
    }
}

void TopBar::setToolsVisible(bool visible) {
    gtk_widget_set_visible(centerBox.get(), visible);
    gtk_widget_set_visible(endBox.get(), visible);
}

void TopBar::setWidth(int width) {
    if (barContainer) {
        mx_top_bar_box_set_target_width(barContainer.get(), width);
    }
}

void TopBar::onUsableWidthAllocated(int usableWidth) {
    pendingUsableWidth = usableWidth;
    if (usableWidth == lastUsableWidth || layoutIdleId != 0) {
        return;
    }
    // Apply outside size-allocate so GTK does not mutate widget visibility mid-layout
    layoutIdleId = g_idle_add_full(
            G_PRIORITY_HIGH_IDLE,
            +[](gpointer selfPtr) -> gboolean {
                auto* self = static_cast<TopBar*>(selfPtr);
                self->layoutIdleId = 0;
                if (self->pendingUsableWidth == self->lastUsableWidth) {
                    return G_SOURCE_REMOVE;
                }
                self->lastUsableWidth = self->pendingUsableWidth;
                self->updateLayoutForUsableWidth(self->lastUsableWidth);
                return G_SOURCE_REMOVE;
            },
            this, nullptr);
}

void TopBar::updateLayoutForUsableWidth(int usableWidth) {
    if (usableWidth <= 0) {
        return;
    }
    const auto next = xoj::topbar::barLayoutForWidth(usableWidth, touchLayout);
    if (next == layout) {
        return;
    }
    layout = next;
    applyLayout();
}

void TopBar::applyLayout() {
    if (titleBox) {
        gtk_widget_set_visible(titleBox, layout.showTitle);
    }
    if (sidebarButton) {
        gtk_widget_set_visible(sidebarButton, layout.showSidebarBtn);
    }
    if (pageNav) {
        gtk_widget_set_visible(pageNav, layout.showPageArrows);
    }
    if (pageCompactButton) {
        gtk_widget_set_visible(pageCompactButton, layout.showPageCompactBtn);
    }
    if (newPageButton) {
        gtk_widget_set_visible(newPageButton, layout.showNewPageBtn);
    }
    if (fullscreenButton) {
        gtk_widget_set_visible(fullscreenButton, layout.showFullscreenBtn);
    }
    if (overflowButton) {
        gtk_widget_set_visible(overflowButton, layout.showOverflowBtn);
    }
    for (GtkWidget* sep: separators) {
        gtk_widget_set_visible(sep, layout.showSeparators);
    }
    if (menuHeaderItem) {
        gtk_widget_set_visible(menuHeaderItem, layout.showTitleInMenu);
    }
    if (menuHeaderSep) {
        gtk_widget_set_visible(menuHeaderSep, layout.showTitleInMenu);
    }
    if (barContainer) {
        GtkStyleContext* ctx = gtk_widget_get_style_context(barContainer.get());
        if (layout.compactBarButtons) {
            gtk_style_context_add_class(ctx, "mx-topbar-tight");
        } else {
            gtk_style_context_remove_class(ctx, "mx-topbar-tight");
        }
    }
    rebuildOverflowMenu();
    updateMenuButtonTooltip();
    if (menuButton) {
        gtk_widget_queue_draw(menuButton);
    }
}

void TopBar::updateMenuButtonTooltip() {
    if (!menuButton) {
        return;
    }
    if (layout.showTitleInMenu && !docName.empty()) {
        std::string tip = FS(_F("Main menu: {1}, {2}") % docName % docStatus);
        gtk_widget_set_tooltip_text(menuButton, tip.c_str());
    } else {
        gtk_widget_set_tooltip_text(menuButton, _("Main menu"));
    }
}

void TopBar::rebuildOverflowMenu() {
    if (!overflowMenu) {
        return;
    }
    g_menu_remove_all(overflowMenu.get());
    if (!layout.showOverflowBtn) {
        return;
    }
    if (!layout.showSidebarBtn) {
        xoj::util::GObjectSPtr<GMenu> navSec(g_menu_new(), xoj::util::adopt);
        g_menu_append(navSec.get(), _("Page sidebar"), winAction(Action::SHOW_SIDEBAR).c_str());
        g_menu_append(navSec.get(), _("Previous page"), winAction(Action::GOTO_PREVIOUS).c_str());
        g_menu_append(navSec.get(), _("Next page"), winAction(Action::GOTO_NEXT).c_str());
        g_menu_append(navSec.get(), _("Go to page…"), winAction(Action::GOTO_PAGE).c_str());
        g_menu_append_section(overflowMenu.get(), nullptr, G_MENU_MODEL(navSec.get()));
    }
    xoj::util::GObjectSPtr<GMenu> pageSec(g_menu_new(), xoj::util::adopt);
    if (!layout.showNewPageBtn) {
        g_menu_append(pageSec.get(), _("New page"), winAction(Action::NEW_PAGE_AFTER).c_str());
    }
    if (!layout.showFullscreenBtn) {
        g_menu_append(pageSec.get(), _("Fullscreen"), winAction(Action::FULLSCREEN).c_str());
    }
    g_menu_append_section(overflowMenu.get(), nullptr, G_MENU_MODEL(pageSec.get()));
}

void TopBar::setTouchLayout(bool touch) {
    if (touchLayout == touch) {
        return;
    }
    touchLayout = touch;
    const int iconSize = xoj::chrome::chromeIconSize(touch);
    xoj::chrome::updateScalableIcons(startBox.get(), iconSize);
    xoj::chrome::updateScalableIcons(centerBox.get(), iconSize);
    xoj::chrome::updateScalableIcons(endBox.get(), iconSize);
    if (lastUsableWidth > 0) {
        updateLayoutForUsableWidth(lastUsableWidth);
    }
}

GtkWidget* TopBar::getActiveBar() const { return GTK_WIDGET(attachedBar); }

void TopBar::onToolClicked(ToolGroup group) {
    ActionDatabase* db = control->getActionDatabase();
    if (!g_action_get_enabled(G_ACTION(db->getAction(Action::SELECT_TOOL).get()))) {
        syncTools();
        return;
    }
    switch (group) {
        case ToolGroup::PEN:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_PEN);
            // Pen and Shapes share the pen tool: drop the shape so the pen draws freehand again
            if (DrawingType dt = control->getToolHandler()->getDrawingType(); xoj::topbar::isShapeDrawingType(dt)) {
                db->fireChangeActionState(actionForDrawingType(dt), false);
            }
            break;
        case ToolGroup::SHAPES:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_PEN);
            db->fireChangeActionState(actionForDrawingType(control->getSettings()->getLastShapeType()), true);
            break;
        case ToolGroup::HIGHLIGHTER:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_HIGHLIGHTER);
            break;
        case ToolGroup::ERASER:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_ERASER);
            break;
        case ToolGroup::SELECTION:
            db->fireActivateAction(Action::SELECT_TOOL, lastSelectionTool);
            break;
        case ToolGroup::TEXT:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_TEXT);
            break;
        case ToolGroup::IMAGE:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_IMAGE);
            break;
        case ToolGroup::HAND:
            db->fireActivateAction(Action::SELECT_TOOL, TOOL_HAND);
            break;
        case ToolGroup::NONE:
            break;
    }
    // GTK already flipped the clicked button: show the real state, also when the tool did not change
    syncTools();
}

void TopBar::onActionStateChanged(GAction*) {
    // ToolHandler::setDrawingType() remembers the last pen shape: here only the pressed button follows the tool
    syncTools();
}

void TopBar::syncTools() {
    ActionDatabase* db = control->getActionDatabase();
    const auto tool = getActionState<ToolType>(db, Action::SELECT_TOOL);
    DrawingType dt = DRAWING_TYPE_DEFAULT;
    for (const auto& [action, type]: DRAWING_TYPE_ACTIONS) {
        if (getActionState<bool>(db, action)) {
            dt = type;
        }
    }

    const ToolGroup active = xoj::topbar::toolGroupFor(tool, dt);
    if (active == ToolGroup::SELECTION) {
        lastSelectionTool = tool;
    }

    for (size_t i = 0; i < TOOL_BUTTON_COUNT; i++) {
        g_signal_handler_block(toolButtons[i], toolClickedIds[i]);
        gtk_toggle_button_set_active(toolButtons[i], i == static_cast<size_t>(active));
        g_signal_handler_unblock(toolButtons[i], toolClickedIds[i]);
    }
}
