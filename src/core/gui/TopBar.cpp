#include "TopBar.h"

#include <utility>  // for pair

#include "control/Control.h"                 // for Control
#include "control/ToolHandler.h"             // for ToolHandler
#include "control/actions/ActionDatabase.h"  // for ActionDatabase
#include "control/settings/Settings.h"       // for Settings
#include "enums/Action.enum.h"               // for Action
#include "gui/IconNameHelper.h"              // for IconNameHelper
#include "undo/UndoRedoHandler.h"            // for UndoRedoHandler
#include "util/GVariantTemplate.h"           // for getGVariantValue
#include "util/GtkUtil.h"                    // for fixActionableInitialSensitivity, setWidgetFollowActionEnabled
#include "util/i18n.h"                       // for _
#include "util/raii/GVariantSPtr.h"          // for GVariantSPtr

using xoj::topbar::ToolGroup;

namespace {
constexpr int ICON_PIXEL_SIZE = 20;

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

GtkWidget* newIcon(const std::string& iconName) {
    GtkWidget* img = gtk_image_new_from_icon_name(iconName.c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(img), ICON_PIXEL_SIZE);
    return img;
}

/// A flat button bound to a window action. The bar buttons never take the keyboard focus from the page.
GtkWidget* newActionButton(GtkWidget* btn, Action action, const std::string& iconName, const char* tooltip) {
    gtk_widget_set_can_focus(btn, false);
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
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    startBox.reset(box, xoj::util::refsink);

    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(menuButton, false);
    gtk_style_context_add_class(gtk_widget_get_style_context(menuButton), "flat");
    gtk_style_context_add_class(gtk_widget_get_style_context(menuButton), "mx-menu-button");
    gtk_button_set_image(GTK_BUTTON(menuButton), newIcon("open-menu-symbolic"));
    gtk_widget_set_tooltip_text(menuButton, _("Main menu"));
    // A real menu, not a popover: it scrolls, shows the shortcuts and opens the submenus to the side
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(menuButton), false);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menuButton), mainMenu);
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
    // Keep a few characters of the name when the window is narrow
    gtk_label_set_width_chars(titleLabel, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(titleLabel)), "mx-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(statusLabel)), "mx-status");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(statusLabel)), "dim-label");
    gtk_container_add(GTK_CONTAINER(box), titleBox);

    gtk_widget_show_all(box);
}

void TopBar::createCenter() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    centerBox.reset(box, xoj::util::refsink);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-tools");

    IconNameHelper icons(control->getSettings());
    struct ToolDef {
        ToolGroup group;
        const char* icon;
        std::string tooltip;
    };
    const ToolDef tools[] = {{ToolGroup::PEN, "tool-pencil", _("Pen")},
                             {ToolGroup::HIGHLIGHTER, "tool-highlighter", _("Highlighter")},
                             {ToolGroup::ERASER, "tool-eraser", _("Eraser")},
                             {ToolGroup::SELECTION, "select-lasso", _("Select")},
                             {ToolGroup::SHAPES, "draw-rect", _("Shapes")},
                             {ToolGroup::TEXT, "tool-text", _("Text")},
                             {ToolGroup::IMAGE, "tool-image", _("Image")},
                             {ToolGroup::HAND, "hand", _("Hand")}};

    for (const auto& def: tools) {
        const auto i = static_cast<size_t>(def.group);
        GtkWidget* btn = gtk_toggle_button_new();
        gtk_widget_set_can_focus(btn, false);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "mx-tool");
        gtk_button_set_image(GTK_BUTTON(btn), newIcon(icons.iconName(def.icon)));
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
            gtk_container_add(GTK_CONTAINER(box), newSeparator());
        }
    }
    gtk_widget_show_all(box);
}

void TopBar::createEnd() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    endBox.reset(box, xoj::util::refsink);
    IconNameHelper icons(control->getSettings());

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
    GtkWidget* undo = newActionButton(gtk_button_new(), Action::UNDO, icons.iconName("edit-undo"), _("Undo"));
    g_signal_connect(undo, "query-tooltip", G_CALLBACK(+queryTooltip), control->getUndoRedoHandler());
    GtkWidget* redo = newActionButton(gtk_button_new(), Action::REDO, icons.iconName("edit-redo"), _("Redo"));
    g_signal_connect(redo, "query-tooltip", G_CALLBACK(+queryRedoTooltip), control->getUndoRedoHandler());
    gtk_container_add(GTK_CONTAINER(box), undo);
    gtk_container_add(GTK_CONTAINER(box), redo);
    gtk_container_add(GTK_CONTAINER(box), newSeparator());

    gtk_container_add(GTK_CONTAINER(box), newActionButton(gtk_toggle_button_new(), Action::SHOW_SIDEBAR,
                                                          icons.iconName("sidebar-show"), _("Page sidebar")));

    GtkWidget* pageNav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(pageNav), "mx-pagenav");
    gtk_container_add(GTK_CONTAINER(pageNav), newActionButton(gtk_button_new(), Action::GOTO_PREVIOUS,
                                                              "go-previous-symbolic", _("Previous page")));
    GtkWidget* pageButton = newActionButton(gtk_button_new(), Action::GOTO_PAGE, "", _("Go to page"));
    pageLabel = GTK_LABEL(gtk_label_new(nullptr));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(pageLabel)), "mx-page-label");
    gtk_container_add(GTK_CONTAINER(pageButton), GTK_WIDGET(pageLabel));
    gtk_container_add(GTK_CONTAINER(pageNav), pageButton);
    gtk_container_add(GTK_CONTAINER(pageNav),
                      newActionButton(gtk_button_new(), Action::GOTO_NEXT, "go-next-symbolic", _("Next page")));
    gtk_container_add(GTK_CONTAINER(box), pageNav);

    gtk_container_add(GTK_CONTAINER(box), newActionButton(gtk_button_new(), Action::NEW_PAGE_AFTER,
                                                          icons.iconName("page-add"), _("New page")));
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
