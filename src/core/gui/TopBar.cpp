#include "TopBar.h"

#include <utility>  // for pair

#include "control/Control.h"                 // for Control
#include "control/ToolHandler.h"             // for ToolHandler
#include "control/actions/ActionDatabase.h"  // for ActionDatabase
#include "control/settings/Settings.h"       // for Settings
#include "enums/Action.enum.h"               // for Action
#include "gui/ChromeUtil.h"                  // for newChromeIcon, updateScalableIcons, updateMenuTouchClass
#include "undo/UndoRedoHandler.h"            // for UndoRedoHandler
#include "util/GVariantTemplate.h"           // for getGVariantValue
#include "util/GtkUtil.h"                    // for fixActionableInitialSensitivity, setWidgetFollowActionEnabled
#include "util/i18n.h"                       // for _
#include "util/raii/GVariantSPtr.h"          // for GVariantSPtr

using xoj::topbar::ToolGroup;

namespace {
constexpr int COMPACT_ICON_PIXEL_SIZE = 18;

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

/// A flat button bound to a window action.
GtkWidget* newActionButton(GtkWidget* btn, Action action, const std::string& iconName, const char* tooltip) {
    gtk_widget_set_can_focus(btn, false);
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
    for (size_t i = 0; i < TOOL_BUTTON_COUNT; i++) {
        g_signal_handler_disconnect(toolButtons[i], toolClickedIds[i]);
    }
    for (auto& [action, id]: watchedActions) {
        g_signal_handler_disconnect(action.get(), id);
    }
}

void TopBar::createStart(GMenuModel* mainMenu) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    startBox.reset(box, xoj::util::refsink);

    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(menuButton, false);
    gtk_widget_set_valign(menuButton, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(menuButton), "flat");
    gtk_style_context_add_class(gtk_widget_get_style_context(menuButton), "mx-menu-button");
    gtk_button_set_image(GTK_BUTTON(menuButton), newIcon("mx-menu-symbolic"));
    gtk_widget_set_tooltip_text(menuButton, _("Main menu"));
    // A real menu, not a popover: it scrolls, shows the shortcuts and opens the submenus to the side
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(menuButton), false);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menuButton), mainMenu);
    if (GtkMenu* popup = gtk_menu_button_get_popup(GTK_MENU_BUTTON(menuButton))) {
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(popup)), "mx-menu");
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
    gtk_container_add(GTK_CONTAINER(box), menuButton);

    GtkWidget* titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(titleBox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(titleBox, 4);

    titleLabel = GTK_LABEL(gtk_label_new(nullptr));
    statusLabel = GTK_LABEL(gtk_label_new(nullptr));
    for (GtkLabel* l: {titleLabel, statusLabel}) {
        gtk_label_set_xalign(l, 0.0);
        gtk_label_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(l, 36);
        gtk_label_set_single_line_mode(l, true);
        gtk_container_add(GTK_CONTAINER(titleBox), GTK_WIDGET(l));
    }
    // Let the title ellipsize cleanly on narrow windows so the window can shrink below 900 px into compact mode
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
        gtk_widget_set_can_focus(btn, false);
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

    gtk_container_add(GTK_CONTAINER(box), newActionButton(gtk_toggle_button_new(), Action::SHOW_SIDEBAR,
                                                          "mx-sidebar-symbolic", _("Page sidebar")));

    GtkWidget* pageNav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
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

    gtk_container_add(GTK_CONTAINER(box),
                      newActionButton(gtk_button_new(), Action::NEW_PAGE_AFTER, "mx-page-add-symbolic", _("New page")));
    gtk_container_add(GTK_CONTAINER(box), newActionButton(gtk_toggle_button_new(), Action::FULLSCREEN,
                                                          "mx-fullscreen-symbolic", _("Fullscreen")));
    gtk_widget_show_all(box);
}

void TopBar::attachTo(GtkHeaderBar* bar) {
    if (bar == attachedBar) {
        return;
    }
    if (attachedBar) {
        gtk_container_remove(GTK_CONTAINER(attachedBar), startBox.get());
        gtk_container_remove(GTK_CONTAINER(attachedBar), endBox.get());
        gtk_header_bar_set_custom_title(attachedBar, nullptr);
    }
    gtk_header_bar_pack_start(bar, startBox.get());
    // The custom title is centred on the whole bar, so the tools stay in the middle of the window
    gtk_header_bar_set_custom_title(bar, centerBox.get());
    gtk_header_bar_pack_end(bar, endBox.get());
    attachedBar = bar;
}

void TopBar::setDocumentTitle(const std::string& name, const std::string& status) {
    gtk_label_set_text(titleLabel, name.c_str());
    gtk_label_set_text(statusLabel, status.c_str());
    gtk_widget_set_tooltip_text(GTK_WIDGET(titleLabel), name.c_str());
}

void TopBar::setPageInfo(size_t page, size_t pageCount) {
    std::string text =
            pageCount == 0 ? std::string("0 / 0") : std::to_string(page + 1) + " / " + std::to_string(pageCount);
    gtk_label_set_text(pageLabel, text.c_str());
}

void TopBar::setToolsVisible(bool visible) {
    gtk_widget_set_visible(centerBox.get(), visible);
    gtk_widget_set_visible(endBox.get(), visible);
}

void TopBar::setCompact(bool c) {
    if (compact == c) {
        return;
    }
    compact = c;
    gtk_widget_set_visible(GTK_WIDGET(statusLabel), !compact);
    for (GtkWidget* sep: separators) {
        gtk_widget_set_visible(sep, !compact);
    }
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
}

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
