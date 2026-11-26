#include "MainWindow.h"

#include <algorithm>  // for find_if
#include <regex>

#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_new_fr...
#include <gdk/gdk.h>                // for gdk_screen_get_de...
#include <gio/gio.h>                // for g_cancellable_is_...
#include <gtk/gtkcssprovider.h>     // for gtk_css_provider_...

#include "control/AudioController.h"                       // for AudioController
#include "control/Control.h"                               // for Control
#include "control/DeviceListHelper.h"                      // for getSourceMapping
#include "control/ScrollHandler.h"                         // for ScrollHandler
#include "control/actions/ActionDatabase.h"                // for ActionDatabase
#include "control/jobs/XournalScheduler.h"                 // for XournalScheduler
#include "control/layer/LayerController.h"                 // for LayerController
#include "control/settings/Settings.h"                     // for Settings
#include "control/settings/SettingsEnums.h"                // for SCROLLBAR_HIDE_HO...
#include "control/zoom/ZoomControl.h"                      // for ZoomControl
#include "gui/ChromeUtil.h"                                // for classifyEvent
#include "gui/FloatingToolbox.h"                           // for FloatingToolbox
#include "gui/FloatingTools.h"                             // for FloatingTools
#include "gui/GladeGui.h"                                  // for GladeGui
#include "gui/PdfFloatingToolbox.h"                        // for PdfFloatingToolbox
#include "gui/SearchBar.h"                                 // for SearchBar
#include "gui/ShapeToolbox.h"                              // for ShapeToolbox
#include "gui/TopBar.h"                                    // for TopBar
#include "gui/TouchModeLogic.h"                            // for resolveTouchLayout
#include "gui/inputdevices/InputContext.h"                 // for InputContext
#include "gui/inputdevices/InputEvents.h"                  // for INPUT_DEVICE_TOUC...
#include "gui/menus/menubar/Menubar.h"                     // for Menubar
#include "gui/menus/menubar/ToolbarSelectionSubmenu.h"     // for ToolbarSelectionSubmenu
#include "gui/scroll/ScrollHandling.h"                     // for ScrollHandling
#include "gui/scroll/VerticalScrollbarRevealController.h"  // for VerticalScrollbarRevealController
#include "gui/sidebar/Sidebar.h"                           // for Sidebar
#include "gui/toolbarMenubar/ToolMenuHandler.h"            // for ToolMenuHandler
#include "gui/toolbarMenubar/model/ToolbarData.h"          // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"         // for ToolbarModel
#include "gui/widgets/SpinPageAdapter.h"                   // for SpinPageAdapter
#include "gui/widgets/XournalWidget.h"                     // for gtk_xournal_get_l...
#include "util/GListView.h"                                // for GListView, GListV...
#include "util/GtkUtil.h"                                  // for getWidgetDPI
#include "util/PathUtil.h"                                 // for getConfigFile
#include "util/StringUtils.h"                              // for char_cast
#include "util/Util.h"                                     // for execInUiThread, npos
#include "util/XojMsgBox.h"                                // for XojMsgBox
#include "util/glib_casts.h"                               // for wrap_for_once_v
#include "util/gtk4_helper.h"                              // for gtk_widget_get_width
#include "util/i18n.h"                                     // for FS, _F
#include "util/raii/CStringWrapper.h"                      // for OwnedCString

#include "GladeSearchpath.h"     // for GladeSearchpath
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIO...
#include "XournalView.h"         // for XournalView
#include "XournalppCursor.h"     // for XournalppCursor
#include "config-dev.h"          // for TOOLBAR_CONFIG
#include "filesystem.h"          // for path, exists

#ifdef __APPLE__
// the following header file contains a definition of struct Point that conflicts with model/Point.h
#define Point Point_CF
#include <CoreFoundation/CoreFoundation.h>
#undef Point
#include "gui/MacWindowChrome.h"
#endif

using std::string;


static void themeCallback(GObject*, GParamSpec*, gpointer data) { static_cast<MainWindow*>(data)->updateColorscheme(); }

MainWindow::MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent):
        GladeGui(gladeSearchPath, "main.glade", "mainWindow"),
        control(control),
        toolbar(std::make_unique<ToolMenuHandler>(control, this)),
        menubar(std::make_unique<Menubar>()) {
    gtk_window_set_application(GTK_WINDOW(getWindow()), parent);

    panedContainerWidget.reset(get("panelMainContents"), xoj::util::ref);
    boxContainerWidget.reset(get("mainContentContainer"), xoj::util::ref);
    mainContentWidget.reset(get("boxContents"), xoj::util::ref);
    sidebarWidget.reset(get("sidebar"), xoj::util::ref);

    loadMainCSS(gladeSearchPath, "xournalpp.css");

    createWindowBox();

    GtkOverlay* overlay = GTK_OVERLAY(get("mainOverlay"));
    this->pdfFloatingToolBox = std::make_unique<PdfFloatingToolbox>(this, overlay);
    this->floatingToolbox = std::make_unique<FloatingToolbox>(this, overlay);

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbarWidgets[i].reset(get(TOOLBAR_DEFINITIONS[i].guiName), xoj::util::ref);
    }

    initXournalWidget();
    this->shapeToolbox = std::make_unique<ShapeToolbox>(this, overlay);

    // showSidebar is saved from the view mode active on quit (e.g. fullscreen): start from the view mode itself
    setSidebarVisible(
            control->getSettings()->getViewModes().at(control->getSettings()->getActiveViewMode()).showSidebar);

    // Window handler
    g_signal_connect(this->window, "delete-event", xoj::util::wrap_for_g_callback_v<deleteEventCallback>,
                     this->control);
#if GTK_MAJOR_VERSION == 3
    g_signal_connect(this->window, "notify::is-maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#else
    g_signal_connect(this->window, "notify::maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#endif

    g_signal_connect(
            this->window, "configure-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer self) -> gboolean {
                auto win = static_cast<MainWindow*>(self);
                GdkWindow* gdkWindow = gtk_widget_get_window(widget);
                GdkDisplay* display = gdkWindow ? gdk_window_get_display(gdkWindow) : nullptr;
                GdkMonitor* monitor = display ? gdk_display_get_monitor_at_window(display, gdkWindow) : nullptr;
                if (monitor && monitor != win->lastMonitor) {
                    win->lastMonitor = monitor;
                    const char* monitorName = gdk_monitor_get_model(monitor);
                    g_debug("Window moved to monitor \"%s\"", monitorName);
                    win->setDPI();
                }
                return false;
            }),
            this);

    // "watch over" all key events
    g_signal_connect(this->window, "key-press-event", G_CALLBACK(onKeyEvent), this);
    g_signal_connect(this->window, "key-release-event", G_CALLBACK(onKeyEvent), this);

    // Observe mouse, pen and touchscreen presses anywhere in the window (capture phase, before buttons consume them)
    pointerGesture.reset(gtk_gesture_multi_press_new(this->window), xoj::util::adopt);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(pointerGesture.get()), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(pointerGesture.get()), GTK_PHASE_CAPTURE);
    g_signal_connect(pointerGesture.get(), "pressed",
                     G_CALLBACK(+[](GtkGestureMultiPress* gesture, gint, gdouble, gdouble, gpointer self) {
                         GdkEventSequence* seq = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));
                         if (const GdkEvent* ev = gtk_gesture_get_last_event(GTK_GESTURE(gesture), seq)) {
                             static_cast<MainWindow*>(self)->onPointerInput(ev);
                         }
                     }),
                     this);
    g_signal_connect(this->window, "notify::is-active", G_CALLBACK(+[](GObject* w, GParamSpec*, gpointer self) {
                         auto* win = static_cast<MainWindow*>(self);
                         // An open menu of the menu bar grabs the keyboard, which also makes the window inactive
                         const bool menuOpen =
                                 win->menubarWidget &&
                                 gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(win->menubarWidget)) != nullptr;
                         if (!gtk_window_is_active(GTK_WINDOW(w))) {
                             // Switching windows may send no leave event (e.g. on another monitor): hide the eraser
                             // indicator anyway. A menu over the page hides it too.
                             if (win->xournal) {
                                 if (XournalppCursor* cursor = win->xournal->getCursor()) {
                                     cursor->clearPointerPosition();
                                 }
                             }
                             if (!menuOpen) {
                                 win->altArmed = false;
                                 win->setMenubarTemporarilyVisible(false);
                             }
                         }
                     }),
                     this);
    g_signal_connect(
            this->window, "window-state-event",
            G_CALLBACK(+[](GtkWidget*, GdkEventWindowState* e, gpointer self) -> gboolean {
                if (e->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
                    static_cast<MainWindow*>(self)->placeTopBar(e->new_window_state & GDK_WINDOW_STATE_FULLSCREEN);
                }
                return false;
            }),
            this);
#ifdef __APPLE__
    if (xoj::gui::mac::useUnifiedTitlebar()) {
        g_signal_connect_after(this->window, "realize", G_CALLBACK(+[](GtkWidget* widget, gpointer self) {
                                   auto* win = static_cast<MainWindow*>(self);
                                   xoj::gui::mac::configureUnifiedTitlebar(widget, win->isTouchLayout() ? 56 : 48);
                               }),
                               this);
    }
#endif
    g_signal_connect(this->window, "size-allocate", G_CALLBACK(+[](GtkWidget*, GtkAllocation* alloc, gpointer self) {
                         if (alloc->width <= 1) {
                             return;
                         }
                         auto* win = static_cast<MainWindow*>(self);
                         win->pendingAllocatedWidth = alloc->width;
                         if (alloc->width == win->lastAllocatedWidth || win->resizeIdleId != 0) {
                             return;
                         }
                         // Apply outside size-allocate so GTK does not mutate widget visibility mid-layout
                         win->resizeIdleId = g_idle_add_full(
                                 G_PRIORITY_HIGH_IDLE,
                                 +[](gpointer self) -> gboolean {
                                     auto* win = static_cast<MainWindow*>(self);
                                     win->resizeIdleId = 0;
                                     if (win->pendingAllocatedWidth == win->lastAllocatedWidth) {
                                         return G_SOURCE_REMOVE;
                                     }
                                     win->lastAllocatedWidth = win->pendingAllocatedWidth;
                                     if (win->topBar) {
                                         win->topBar->setWidth(win->lastAllocatedWidth);
                                     }
                                     return G_SOURCE_REMOVE;
                                 },
                                 win, nullptr);
                     }),
                     this);

    updateScrollbarSidebarPosition();

    // Allow shrinking down to 600 px so the window can half-tile on 1280/1366 px displays
    GdkGeometry geom{};
    geom.min_width = xoj::topbar::MIN_WINDOW_WIDTH;
    geom.min_height = 400;
    gtk_window_set_geometry_hints(GTK_WINDOW(this->window), nullptr, &geom, GDK_HINT_MIN_SIZE);

    gtk_window_set_default_size(GTK_WINDOW(this->window), control->getSettings()->getMainWndWidth(),
                                control->getSettings()->getMainWndHeight());

    // Always start maximized; the default size above still applies when un-maximizing
    gtk_window_maximize(GTK_WINDOW(this->window));

#ifndef __APPLE__
    Util::execInUiThread([=]() {
        // Execute after the window is visible, else the check won't work
        // menubarVisible is saved from the view mode active on quit (e.g. fullscreen): start from the view mode itself
        Settings* settings = control->getSettings();
        control->setShowMenubar(settings->getViewModes().at(settings->getActiveViewMode()).showMenubar);
    });
#endif

    // Drag and Drop
    g_signal_connect(this->window, "drag-data-received", G_CALLBACK(dragDataRecived), this);

    gtk_drag_dest_set(this->window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(this->window);
    gtk_drag_dest_add_image_targets(this->window);
    gtk_drag_dest_add_text_targets(this->window);

    g_signal_connect(gtk_widget_get_settings(this->window), "notify::gtk-theme-name", G_CALLBACK(themeCallback), this);
    g_signal_connect(gtk_widget_get_settings(this->window), "notify::gtk-application-prefer-dark-theme",
                     G_CALLBACK(themeCallback), this);

    updateColorscheme();
}

void MainWindow::populate(GladeSearchpath* gladeSearchPath) {

    toolbar->populate(gladeSearchPath);
    // Before the menu bar, whose toolbar list shows the selected layout
    migrateSelectedToolbar();
    menubar->populate(gladeSearchPath, this);
    createTopBar();
    if (lastAllocatedWidth > 0 && topBar) {
        topBar->setWidth(lastAllocatedWidth);
    }
    floatingTools = std::make_unique<FloatingTools>(control, GTK_OVERLAY(get("mainOverlay")), mainContentWidget.get());

    // need to create tool buttons registered in plugins, so they can be added to toolbars
    control->registerPluginToolButtons(this->toolbar.get());

    createToolbar();

    setToolbarVisible(control->getSettings()->isToolbarVisible());
    updateTouchMode();
}

GMenuModel* MainWindow::getMenuModel() const { return menubar->getModel(); }

MainWindow::~MainWindow() {
    if (this->resizeIdleId != 0) {
        g_source_remove(this->resizeIdleId);
        this->resizeIdleId = 0;
    }

    // First, so that the selection deleted with the view does not update it, and floating listeners detach cleanly
    this->vscrollbarReveal.reset();
    this->pointerGesture.reset();
    this->floatingTools.reset();
    this->shapeToolbox.reset();

    if (this->themeCssProvider) {
        gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),
                                                     GTK_STYLE_PROVIDER(this->themeCssProvider.get()));
        this->themeCssProvider.reset();
    }

    // The window is destroyed after this object (see GladeGui::~GladeGui()): drop the handlers that use `this`
    g_signal_handlers_disconnect_by_data(this->window, this);
    g_signal_handlers_disconnect_by_data(gtk_widget_get_settings(this->window), this);
    if (menubarWidget) {
        g_signal_handlers_disconnect_by_data(menubarWidget, this);
    }
#ifdef __APPLE__
    if (inlineBarEventBox) {
        g_signal_handlers_disconnect_by_data(inlineBarEventBox, this);
    }
#endif
}

void MainWindow::createWindowBox() {
    GtkWidget* overlay = get("mainOverlay");
    g_object_ref(overlay);
    gtk_container_remove(GTK_CONTAINER(this->window), overlay);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(box, "windowBox");

    // Shown only while fullscreen or without client-side decorations, see placeTopBar()
    inlineBar = gtk_header_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(inlineBar), "mx-topbar");
    gtk_widget_set_no_show_all(inlineBar, true);
#ifdef __APPLE__
    if (xoj::gui::mac::useUnifiedTitlebar()) {
        // GtkHeaderBar has no GdkWindow of its own; an input-only event box behind its children catches
        // primary clicks on empty bar space for native window dragging and double-click zoom.
        inlineBarEventBox = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(inlineBarEventBox), false);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(inlineBarEventBox), false);
        gtk_widget_add_events(inlineBarEventBox, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(inlineBarEventBox, "button-press-event", G_CALLBACK(onInlineBarButtonPress), this);
        gtk_container_add(GTK_CONTAINER(inlineBarEventBox), inlineBar);
        gtk_box_append(GTK_BOX(box), inlineBarEventBox);
        gtk_widget_show(inlineBarEventBox);
    } else {
        gtk_box_append(GTK_BOX(box), inlineBar);
    }
#else
    gtk_box_append(GTK_BOX(box), inlineBar);
#endif

    // The menu bar is always in the window but stays hidden unless pinned or shown with Alt.
    // The GtkApplicationWindow menu bar stays off: gtk_application_set_menubar() is only kept for the accelerators.
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(this->window), false);
    menubarRevealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(menubarRevealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(menubarRevealer), 120);
    gtk_box_append(GTK_BOX(box), menubarRevealer);
#ifdef __APPLE__
    // macOS uses the system menu bar; keep the in-window menu bar permanently hidden
    gtk_widget_set_no_show_all(menubarRevealer, true);
    gtk_widget_hide(menubarRevealer);
#else
    gtk_widget_show(menubarRevealer);
#endif

    gtk_widget_set_vexpand(overlay, true);
    gtk_box_append(GTK_BOX(box), overlay);
    g_object_unref(overlay);

    gtk_container_add(GTK_CONTAINER(this->window), box);
    gtk_widget_show(box);
}

void MainWindow::createTopBar() {
#ifndef __APPLE__
    menubarWidget = gtk_menu_bar_new_from_model(menubar->getModel());
    gtk_container_add(GTK_CONTAINER(menubarRevealer), menubarWidget);
    gtk_widget_show(menubarWidget);
    // Emitted when a menu closes, after an item was chosen or the menu was cancelled
    g_signal_connect(menubarWidget, "deactivate", G_CALLBACK(+[](GtkMenuShell*, gpointer self) {
                         static_cast<MainWindow*>(self)->setMenubarTemporarilyVisible(false);
                     }),
                     this);
#endif

    topBar = std::make_unique<TopBar>(control, menubar->getSectionedModel());

#ifndef __APPLE__
    if (control->getSettings()->isUseClientSideDecorations()) {
        // Must happen before the window is realized
        titlebar = gtk_header_bar_new();
        gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(titlebar), true);
        gtk_style_context_add_class(gtk_widget_get_style_context(titlebar), "mx-topbar");
        gtk_widget_show(titlebar);
        gtk_window_set_titlebar(GTK_WINDOW(this->window), titlebar);
    }
#endif
    placeTopBar(false);

    topBar->setPageInfo(pageNumber, pageCount);
    control->updateWindowTitle();
}

void MainWindow::placeTopBar(bool fullscreen) {
    if (!topBar) {
        return;
    }
    // GTK hides the title bar while fullscreen
    const bool useInline = fullscreen || titlebar == nullptr;
    gtk_widget_set_visible(inlineBar, useInline);
#ifdef __APPLE__
    if (xoj::gui::mac::useUnifiedTitlebar()) {
        GtkStyleContext* ctx = gtk_widget_get_style_context(inlineBar);
        if (fullscreen) {
            gtk_style_context_remove_class(ctx, "mx-macos-traffic-lights");
        } else {
            gtk_style_context_add_class(ctx, "mx-macos-traffic-lights");
        }
    }
#endif
    topBar->attachTo(GTK_HEADER_BAR(useInline ? inlineBar : titlebar));
}

#ifdef __APPLE__
auto MainWindow::onInlineBarButtonPress(GtkWidget* widget, GdkEventButton* event, MainWindow* self) -> gboolean {
    if (event->button != GDK_BUTTON_PRIMARY) {
        return false;
    }
    if (gtk_get_event_widget(reinterpret_cast<GdkEvent*>(event)) != widget) {
        return false;
    }
    // Leave clicks over the traffic-light inset untouched when not fullscreen
    if (gtk_style_context_has_class(gtk_widget_get_style_context(self->inlineBar), "mx-macos-traffic-lights") &&
        event->x < 80.0) {
        return false;
    }
    if (event->type == GDK_2BUTTON_PRESS) {
        xoj::gui::mac::zoomWindow(self->window);
        return true;
    }
    if (event->type == GDK_BUTTON_PRESS) {
        gtk_window_begin_move_drag(GTK_WINDOW(self->window), static_cast<gint>(event->button),
                                   static_cast<gint>(event->x_root), static_cast<gint>(event->y_root), event->time);
        return true;
    }
    return false;
}
#endif

auto MainWindow::onKeyEvent(GtkWidget* window, GdkEventKey* event, MainWindow* self) -> gboolean {
    if (self->handleMenubarKey(event)) {
        return true;
    }
    if (self->handleChromeFocusKey(event)) {
        return true;
    }
    return gtk_window_propagate_key_event(GTK_WINDOW(window), event);
}

namespace {
GtkWidget* findFirstFocusable(GtkWidget* root) {
    if (!root || !gtk_widget_get_visible(root) || !gtk_widget_get_sensitive(root)) {
        return nullptr;
    }
    if (gtk_widget_get_can_focus(root) && gtk_widget_is_sensitive(root) && gtk_widget_get_mapped(root)) {
        return root;
    }
    if (GTK_IS_CONTAINER(root)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(root));
        GtkWidget* found = nullptr;
        for (GList* l = children; l != nullptr && !found; l = l->next) {
            found = findFirstFocusable(GTK_WIDGET(l->data));
        }
        g_list_free(children);
        return found;
    }
    return nullptr;
}
}  // namespace

bool MainWindow::handleChromeFocusKey(GdkEventKey* event) {
    if (event->type != GDK_KEY_PRESS) {
        return false;
    }

    GtkWidget* bar = topBar ? topBar->getActiveBar() : nullptr;
    GtkWidget* pill = floatingTools ? floatingTools->getPillWidget() : nullptr;
    GtkWidget* layer = floatingTools ? floatingTools->getLayerWidget() : nullptr;
    GtkWidget* zoom = floatingTools ? floatingTools->getZoomWidget() : nullptr;
    GtkWidget* page = xournal ? xournal->getWidget() : nullptr;

    GtkWidget* current = gtk_window_get_focus(GTK_WINDOW(this->window));
    // An open popover is a child of its button: let it handle Escape itself, and don't move the focus out of it
    if (current && gtk_widget_get_ancestor(current, GTK_TYPE_POPOVER)) {
        return false;
    }
    auto inWidget = [&](GtkWidget* container) {
        return current && container && (current == container || gtk_widget_is_ancestor(current, container));
    };

    int region = 3;  // default: page
    if (inWidget(bar)) {
        region = 0;
    } else if (inWidget(pill)) {
        region = 1;
    } else if (inWidget(layer) || inWidget(zoom)) {
        region = 2;
    }

    if (event->keyval == GDK_KEY_Escape) {
        if (region != 3 && page) {
            xournal->requestFocus();
            return true;
        }
        return false;
    }

    if (event->keyval != GDK_KEY_F6) {
        return false;
    }

    const int dir = (event->state & GDK_SHIFT_MASK) ? -1 : 1;
    for (int step = 1; step <= 4; step++) {
        const int next = (region + dir * step + 4) % 4;
        GtkWidget* target = nullptr;
        if (next == 0) {
            target = findFirstFocusable(bar);
        } else if (next == 1) {
            target = findFirstFocusable(pill);
        } else if (next == 2) {
            target = findFirstFocusable(layer);
            if (!target) {
                target = findFirstFocusable(zoom);
            }
        } else {
            target = page;
        }
        if (target) {
            gtk_widget_grab_focus(target);
            return true;
        }
    }
    return false;
}

bool MainWindow::handleMenubarKey(GdkEventKey* event) {
#ifdef __APPLE__
    // macOS uses the system menu bar: swallow F10 and ignore lone Alt presses
    if (event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_F10) {
        return true;
    }
    return false;
#else
    constexpr auto BUTTONS =
            GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | GDK_BUTTON4_MASK | GDK_BUTTON5_MASK;
    constexpr auto OTHER_MODIFIERS =
            GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK | GDK_META_MASK;
    const bool isAlt = event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R;

    if (event->type == GDK_KEY_PRESS) {
        if (!isAlt) {
            // Alt+key is a shortcut, not a tap on Alt
            altArmed = false;
            if (event->keyval == GDK_KEY_Escape && menubarTemporary) {
                setMenubarTemporarilyVisible(false);
                return true;
            }
        } else if (!(event->state & GDK_MOD1_MASK)) {
            // First press, not a key repeat. Alt held while drawing is a drawing modifier.
            altArmed = !(event->state & (BUTTONS | OTHER_MODIFIERS));
        }
    } else if (event->type == GDK_KEY_RELEASE && isAlt) {
        if (altArmed && !(event->state & BUTTONS)) {
            setMenubarTemporarilyVisible(!menubarTemporary);
        }
        altArmed = false;
    }
    return false;
#endif
}

void MainWindow::onPagePressed() {
    altArmed = false;
    setMenubarTemporarilyVisible(false);
}

void MainWindow::setMenubarTemporarilyVisible(bool visible) {
#ifdef __APPLE__
    (void)visible;
#else
    visible = visible && !menubarPinned;
    if (menubarTemporary != visible) {
        menubarTemporary = visible;
        updateMenubarReveal();
    }
#endif
}

void MainWindow::updateMenubarReveal() {
#ifdef __APPLE__
    gtk_revealer_set_reveal_child(GTK_REVEALER(menubarRevealer), false);
#else
    gtk_revealer_set_reveal_child(GTK_REVEALER(menubarRevealer), menubarPinned || menubarTemporary);
#endif
}

void MainWindow::updateTouchMode() {
    const TouchMode mode = control->getSettings()->getTouchMode();
    setTouchLayout(xoj::chrome::resolveTouchLayout(mode, xoj::chrome::PointerKind::Other, this->touchLayout));
}

void MainWindow::onPointerInput(const GdkEvent* event) {
    if (!event) {
        return;
    }
    const auto kind = xoj::chrome::classifyEvent(event);
    if (kind == xoj::chrome::PointerKind::Touch) {
        // Ignore touches blocked by HandRecognition (palm rejection) or mapped to Disabled/Ignore
        if (this->xournal) {
            if (GtkWidget* w = this->xournal->getWidget(); w && GTK_IS_XOURNAL(w)) {
                if (InputContext* ic = GTK_XOURNAL(w)->input; ic && ic->isBlocked(InputContext::TOUCHSCREEN)) {
                    return;
                }
            }
        }
        if (GdkDevice* src = gdk_event_get_source_device(event)) {
            if (InputEvents::translateDeviceType(src, control->getSettings()) == INPUT_DEVICE_IGNORE) {
                return;
            }
        }
    }
    const TouchMode mode = control->getSettings()->getTouchMode();
    setTouchLayout(xoj::chrome::resolveTouchLayout(mode, kind, this->touchLayout));
}

bool MainWindow::isTouchLayout() const { return this->touchLayout; }

void MainWindow::setTouchLayout(bool touch) {
    if (this->touchLayout == touch) {
        return;
    }
    this->touchLayout = touch;
    GtkStyleContext* ctx = gtk_widget_get_style_context(this->window);
    if (touch) {
        gtk_style_context_add_class(ctx, "mx-touch");
    } else {
        gtk_style_context_remove_class(ctx, "mx-touch");
    }
    if (this->topBar) {
        this->topBar->setTouchLayout(touch);
    }
    if (this->floatingTools) {
        this->floatingTools->setTouchLayout(touch);
    }
    if (this->shapeToolbox) {
        this->shapeToolbox->setTouchLayout(touch);
    }
#ifdef __APPLE__
    if (xoj::gui::mac::useUnifiedTitlebar()) {
        xoj::gui::mac::updateTopBarHeight(this->window, touch ? 56 : 48);
    }
#endif
}

struct ThemeProperties {
    bool dark;
    bool darkSuffix;
    std::string rootname;  ///< Name without any putative -dark suffix
};
static ThemeProperties getThemeProperties(GtkWidget* w) {
    xoj::util::OwnedCString name;
    [[maybe_unused]] bool useEnv = false;
    // Gtk prioritizes GTK_THEME over GtkSettings content
    // cf https://gitlab.gnome.org/GNOME/gtk/blob/90d84a2af8b367bd5a5312b3fa3b67563462c0ef/gtk/gtksettings.c#L1567-L1622
    if (auto* p = g_getenv("GTK_THEME")) {
        *(name.contentReplacer()) = g_strdup(p);
        useEnv = true;
    } else {
        g_object_get(gtk_widget_get_settings(w), "gtk-theme-name", name.contentReplacer(), nullptr);
    }

    // Try to figure out if the theme is dark or light
    // Some themes handle their dark variant via "gtk-application-prefer-dark-theme" while other just append "-dark"
    const std::regex nameparser("([a-zA-Z0-9_\\.-]+?)([:-][dD]ark)?");
    std::cmatch sm;
    std::regex_match(name.get(), sm, nameparser);

    ThemeProperties props;
    if (sm.size() < 3) {
        g_warning("Fails to extract theme root name from: \"%s\"", name.get());
        props.rootname = name.get();
        props.darkSuffix = false;
    } else {
        props.rootname = sm[1];
        props.darkSuffix = sm[2].length() > 0;
    }
    gboolean dark = false;

#ifdef __APPLE__
    if (!useEnv) {
        CFStringRef interfaceStyle =
                (CFStringRef)CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesCurrentApplication);
        if (interfaceStyle) {
            char buffer[128];
            if (CFStringGetCString(interfaceStyle, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                std::string style = buffer;
                if (auto pos = style.find("Dark"); pos != std::string::npos) {
                    dark = true;
                }
            }
        }
    }
#else
    g_object_get(gtk_widget_get_settings(w), "gtk-application-prefer-dark-theme", &dark, nullptr);
#endif

    g_debug("Extracted theme info: Name = %s, rootname = %s, dark = %s", name.get(), props.rootname.c_str(),
            dark ? "true" : "false");

    props.dark = props.darkSuffix || dark;  // Some themes handle their dark variant via this setting

    return props;
}

void MainWindow::updateColorscheme() {
    g_signal_handlers_block_by_func(gtk_widget_get_settings(this->window),
                                    reinterpret_cast<gpointer>(G_CALLBACK(themeCallback)), this);
    auto variant = control->getSettings()->getThemeVariant();
    if (variant == THEME_VARIANT_USE_SYSTEM) {
        gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme");
        if (modifiedGtkSettingsTheme) {
            // Some bug in Gtk makes an infinite loop despite us blocking the signals
            gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-theme-name");
            modifiedGtkSettingsTheme = false;
        }
    }
    auto props = getThemeProperties(this->window);

    this->darkMode = (props.dark && variant != THEME_VARIANT_FORCE_LIGHT) || variant == THEME_VARIANT_FORCE_DARK;

    // Set up icons
    {
        const auto uiPath = this->getGladeSearchPath()->getFirstSearchPath();
        const auto lightColorIcons = (uiPath / "iconsColor-light");
        const auto darkColorIcons = (uiPath / "iconsColor-dark");
        const auto lightLucideIcons = (uiPath / "iconsLucide-light");
        const auto darkLucideIcons = (uiPath / "iconsLucide-dark");

        // icon load order from lowest priority to highest priority
        std::vector<fs::path> iconLoadOrder = {};
        const auto chosenTheme = control->getSettings()->getIconTheme();
        switch (chosenTheme) {
            case ICON_THEME_COLOR:
                iconLoadOrder = {darkLucideIcons, lightLucideIcons, darkColorIcons, lightColorIcons};
                break;
            case ICON_THEME_LUCIDE:
                iconLoadOrder = {darkColorIcons, lightColorIcons, darkLucideIcons, lightLucideIcons};
                break;
            default:
                g_message("Unknown icon theme!");
        }

        if (this->darkMode) {
            for (size_t i = 0; 2 * i + 1 < iconLoadOrder.size(); ++i) {
                std::swap(iconLoadOrder[2 * i], iconLoadOrder[2 * i + 1]);
            }
        }

        for (auto& p: iconLoadOrder) {
            gtk_icon_theme_prepend_search_path(gtk_icon_theme_get_default(), char_cast(p.u8string().c_str()));
        }
        const auto moreIcons = (uiPath / "iconsMore");
        gtk_icon_theme_prepend_search_path(gtk_icon_theme_get_default(), char_cast(moreIcons.u8string().c_str()));
    }

    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(this->window));

    if (this->darkMode) {
        gtk_style_context_add_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", true, nullptr);
    } else {
        gtk_style_context_remove_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", false, nullptr);
        if (props.darkSuffix) {  // The active theme is all dark. Remove the trailing "-dark"
            g_object_set(gtk_widget_get_settings(this->window), "gtk-theme-name", props.rootname.c_str(), nullptr);
            modifiedGtkSettingsTheme = true;
        }
    }

    // Swap the named MoreXournal theme colours between light and dark
    {
        const char* themeCss = this->darkMode ? "morexournal-dark.css" : "morexournal-light.css";
        auto filepath = this->getGladeSearchPath()->findFile("", themeCss);
        if (!filepath.empty()) {
            if (!this->themeCssProvider) {
                this->themeCssProvider.reset(gtk_css_provider_new(), xoj::util::adopt);
                gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                          GTK_STYLE_PROVIDER(this->themeCssProvider.get()),
                                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            }
            GError* err = nullptr;
            if (!gtk_css_provider_load_from_path(this->themeCssProvider.get(), char_cast(filepath.u8string().c_str()),
                                                 &err)) {
                g_warning("Could not load %s: %s", themeCss, err ? err->message : "unknown error");
                g_clear_error(&err);
            }
        }
    }

    {
        gchar* name = nullptr;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-theme-name", &name, nullptr);
        g_debug("Theme name: %s", name);
        g_debug("Modified in GtkSettings: %s", modifiedGtkSettingsTheme ? "true" : "false");
        g_free(name);
        gboolean gtkdark = true;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", &gtkdark, nullptr);
        g_debug("Theme variant: %s", gtkdark ? "dark" : "light");
        g_debug("Icon theme: %s", iconThemeToString(control->getSettings()->getIconTheme()));
    }
    g_signal_handlers_unblock_by_func(gtk_widget_get_settings(this->window), reinterpret_cast<gpointer>(themeCallback),
                                      this);
}

void MainWindow::initXournalWidget() {
    winXournal = gtk_scrolled_window_new();

    setGtkTouchscreenScrollingForDeviceMapping();

    gtk_box_append(GTK_BOX(get("boxContents")), winXournal);

    scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLED_WINDOW(winXournal));
    vscrollbarReveal = std::make_unique<VerticalScrollbarRevealController>(
            GTK_SCROLLED_WINDOW(winXournal), [ctrl = this->control]() {
                return ctrl->getZoomControl() != nullptr && ctrl->getZoomControl()->isZoomSequenceActive();
            });

    this->xournal = std::make_unique<XournalView>(winXournal, control, scrollHandling.get());

    control->getZoomControl()->initZoomHandler(this->window, winXournal, xournal.get(), control);
    gtk_widget_show_all(winXournal);
}

void MainWindow::setGtkTouchscreenScrollingForDeviceMapping() {
    InputDeviceClass touchscreenClass =
            DeviceListHelper::getSourceMapping(GDK_SOURCE_TOUCHSCREEN, this->getControl()->getSettings());

    setGtkTouchscreenScrollingEnabled(touchscreenClass == INPUT_DEVICE_TOUCHSCREEN &&
                                      !control->getSettings()->getTouchDrawingEnabled());
}

void MainWindow::setGtkTouchscreenScrollingEnabled(bool enabled) {
    if (!control->getSettings()->getGtkTouchInertialScrollingEnabled()) {
        enabled = false;
    }
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(winXournal), enabled);
}

auto MainWindow::getLayout() const -> Layout* { return this->xournal->getLayout(); }

auto cancellable_cancel(GCancellable* cancel) -> bool {
    g_cancellable_cancel(cancel);

    g_warning("Timeout... Cancel loading URL");

    return false;
}

void MainWindow::dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                 guint info, guint time, MainWindow* win) {
    GtkWidget* source = gtk_drag_get_source_widget(dragContext);
    if (source && widget == gtk_widget_get_toplevel(source)) {
        gtk_drag_finish(dragContext, false, false, time);
        return;
    }

    guchar* text = gtk_selection_data_get_text(data);
    if (text) {
        win->control->clipboardPasteText(reinterpret_cast<const char*>(text));

        g_free(text);
        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    xoj::util::GObjectSPtr<GdkPixbuf> image(gtk_selection_data_get_pixbuf(data), xoj::util::adopt);
    if (image) {
        win->control->clipboardPasteImage(image.get());

        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    gchar** uris = gtk_selection_data_get_uris(data);
    if (uris) {
        for (int i = 0; uris[i] != nullptr && i < 3; i++) {
            const char* uri = uris[i];

            GCancellable* cancel = g_cancellable_new();
            auto cancelTimeout = g_timeout_add(3000, xoj::util::wrap_for_once_v<cancellable_cancel>, cancel);

            xoj::util::GObjectSPtr<GFile> file(g_file_new_for_uri(uri), xoj::util::adopt);
            GError* err = nullptr;
            GFileInputStream* in = g_file_read(file.get(), cancel, &err);
            if (g_cancellable_is_cancelled(cancel)) {
                continue;
            }

            if (err == nullptr) {
                xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
                        gdk_pixbuf_new_from_stream(G_INPUT_STREAM(in), cancel, nullptr), xoj::util::adopt);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }
                g_input_stream_close(G_INPUT_STREAM(in), cancel, nullptr);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }

                if (pixbuf) {
                    win->control->clipboardPasteImage(pixbuf.get());
                }
            } else {
                g_error_free(err);
            }

            if (!g_cancellable_is_cancelled(cancel)) {
                g_source_remove(cancelTimeout);
            }
            g_object_unref(cancel);
        }

        gtk_drag_finish(dragContext, true, false, time);

        g_strfreev(uris);
    }

    gtk_drag_finish(dragContext, false, false, time);
}

auto MainWindow::getControl() const -> Control* { return control; }

void MainWindow::updateScrollbarSidebarPosition() {
    // Part 1: update scrollbar position
    if (winXournal != nullptr) {
        GtkScrolledWindow* scrolledWindow = GTK_SCROLLED_WINDOW(winXournal);

        ScrollbarHideType type = this->getControl()->getSettings()->getScrollbarHideType();

        bool scrollbarOnLeft = control->getSettings()->isScrollbarOnLeft();
        if (scrollbarOnLeft) {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_RIGHT);
        } else {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_LEFT);
        }

        gtk_widget_set_visible(gtk_scrolled_window_get_hscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_HORIZONTAL));

        if (vscrollbarReveal) {
            vscrollbarReveal->configure((type & SCROLLBAR_HIDE_VERTICAL) != 0,
                                        control->getSettings()->isScrollbarFadeoutDisabled());
        } else {
            gtk_widget_set_visible(gtk_scrolled_window_get_vscrollbar(scrolledWindow),
                                   !(type & SCROLLBAR_HIDE_VERTICAL));
            gtk_scrolled_window_set_overlay_scrolling(scrolledWindow,
                                                      !control->getSettings()->isScrollbarFadeoutDisabled());
        }
    }

    // Part 2: update sidebar position
    GtkPaned* paned = GTK_PANED(this->panedContainerWidget.get());

    // Allocation is reset when we switch up the contained elements. Fetch the
    // width here in case we need it afterwards.
    int contentWidth = gtk_widget_get_width(this->boxContainerWidget.get());

    bool sidebarRight = control->getSettings()->isSidebarOnRight();
    if (sidebarRight != (gtk_paned_get_child2(paned) == this->sidebarWidget.get())) {
        // switch sidebar and main content
        GtkWidget* sidebar = this->sidebarWidget.get();
        GtkWidget* mainContent = this->sidebarVisible ? this->mainContentWidget.get() : nullptr;
#if GTK_MAJOR_VERSION == 3
        if (this->sidebarVisible) {
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);
            gtk_container_remove(GTK_CONTAINER(paned), mainContent);

            if (sidebarRight) {
                gtk_paned_pack1(paned, mainContent, true, false);
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
                gtk_paned_pack2(paned, mainContent, true, false);
            }
        } else {
            // The sidebar is hidden. That means the paned widget only contains the
            // sidebar while the main contents are shown alone in the box container.
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);

            if (sidebarRight) {
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
            }
        }
#else
        gtk_paned_set_start_child(paned, nullptr);
        gtk_paned_set_end_child(paned, nullptr);
        if (sidebarRight) {
            gtk_paned_set_start_child(paned, mainContent);
            gtk_paned_set_resize_start_child(paned, true);
            gtk_paned_set_shrink_start_child(paned, false);
            gtk_paned_set_end_child(paned, sidebar);
            gtk_paned_set_resize_end_child(paned, false);
            gtk_paned_set_shrink_end_child(paned, false);
        } else {
            gtk_paned_set_end_child(paned, mainContent);
            gtk_paned_set_resize_end_child(paned, true);
            gtk_paned_set_shrink_end_child(paned, false);
            gtk_paned_set_start_child(paned, sidebar);
            gtk_paned_set_resize_start_child(paned, false);
            gtk_paned_set_shrink_start_child(paned, false);
        }
#endif
    }

    if (this->sidebarVisible) {
        updatePanedPosition(contentWidth);
    }
}

auto MainWindow::deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control) -> bool {
    control->quit();

    return true;
}

void MainWindow::setSidebarVisible(bool visible) {
    if (!visible && (this->control->getSidebar() != nullptr)) {
        this->control->getSidebar()->saveSize();
    }

    if (visible != this->sidebarVisible) {
        // Due to a GTK bug, we can't just hide the sidebar widget in the GtkPaned.
        // If we do this, we create a dead region where the pane separator was previously.
        // In this region, we can't use the touchscreen to start horizontal strokes.
        // As such:
        if (!visible) {
            // hide sidebar
#if GTK_MAJOR_VERSION == 3
            gtk_container_remove(GTK_CONTAINER(panedContainerWidget.get()), mainContentWidget.get());
#else
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            }
#endif
            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            gtk_box_append(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());
            this->sidebarVisible = false;
        } else {
            // show sidebar

            // Allocation is reset when we switch up the contained elements. Fetch the
            // width here in case we need it afterwards.
            int contentWidth = gtk_widget_get_width(boxContainerWidget.get());

            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());

#if GTK_MAJOR_VERSION == 3
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_pack1(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            } else {
                gtk_paned_pack2(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            }
#else
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            }
#endif

            gtk_box_append(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            this->sidebarVisible = true;

            updatePanedPosition(contentWidth);
        }
    }

    gtk_widget_set_visible(sidebarWidget.get(), visible);
}

/**
 * Invert the position of the paned widget and disconnect from the signal.
 * @param handlerId should be the ID of the signal handler that should be disconnected.
 */
static void invertPanedPosition(GtkWidget* widget, GtkAllocation* allocation, gulong* handlerId) {
    int newDividerPos = allocation->width - gtk_paned_get_position(GTK_PANED(widget));
    gtk_paned_set_position(GTK_PANED(widget), newDividerPos);

    // We only need to switch the position once, so disconnect the signal right away.
    g_signal_handler_disconnect(widget, *handlerId);
}

void MainWindow::updatePanedPosition(int contentWidth) {
    if (!this->control->getSettings()->isSidebarOnRight()) {
        // Sidebar is on the left side.
        gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()),
                               this->control->getSettings()->getSidebarWidth());
    } else {
        // Sidebar is on the right side.
        if (contentWidth > 0) {
            int dividerPos = contentWidth - this->control->getSettings()->getSidebarWidth();
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()), dividerPos);
        } else {
            // Allocation is unkown (window hasn't been shown yet). We have to wait for the signal.
            // Set position as if the sidebar was on the left side, and let the signal handler
            // simply invert the position when the allocation is known.
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()),
                                   this->control->getSettings()->getSidebarWidth());
            gulong* signal_id = new gulong{};
            *signal_id = g_signal_connect_data(
                    this->panedContainerWidget.get(), "size-allocate",
                    xoj::util::wrap_for_g_callback_v<invertPanedPosition>, signal_id,
                    [](gpointer d, GClosure*) { delete reinterpret_cast<gulong*>(d); }, GConnectFlags(0));
        }
    }
}

void MainWindow::setToolbarVisible(bool visible) {
    Settings* settings = control->getSettings();

    settings->setToolbarVisible(visible);
    if (topBar) {
        topBar->setToolsVisible(visible);
    }
    if (floatingTools) {
        floatingTools->setToolbarVisible(visible);
    }
    for (auto& w: this->toolbarWidgets) {
        if (!visible || (gtk_toolbar_get_n_items(GTK_TOOLBAR(w.get())) != 0)) {
            gtk_widget_set_visible(w.get(), visible);
        }
    }
}

void MainWindow::setMenubarVisible(bool visible) {
#ifdef __APPLE__
    (void)visible;
    menubarPinned = false;
#else
    menubarPinned = visible;
#endif
    menubarTemporary = false;
    updateMenubarReveal();
}

void MainWindow::setMaximized(bool maximized) { this->maximized = maximized; }

auto MainWindow::isMaximized() const -> bool { return this->maximized; }

auto MainWindow::setFullscreen(bool enabled) const -> void {
    if (enabled) {
        gtk_window_fullscreen(GTK_WINDOW(this->getWindow()));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(this->getWindow()));
    }
}

auto MainWindow::isDarkTheme() const -> bool { return this->darkMode; }

auto MainWindow::getXournal() const -> XournalView* { return xournal.get(); }

auto MainWindow::windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win) -> void {
    win->setMaximized(gtk_window_is_maximized(GTK_WINDOW(window)));
}

void MainWindow::toolbarSelected(const std::string& id) {
    const auto& toolbars = toolbar->getModel()->getToolbars();
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&](const auto& d) { return d->getId() == id; });
    toolbarSelected(it == toolbars.end() ? nullptr : it->get());
}

void MainWindow::toolbarSelected(ToolbarData* d) {
    if (!d || this->selectedToolbar == d) {
        return;
    }

    Settings* settings = control->getSettings();
    settings->setSelectedToolbar(d->getId());

    this->clearToolbar();
    this->loadToolbar(d);
}

auto MainWindow::clearToolbar() -> const ToolbarData* {
    if (this->selectedToolbar != nullptr) {
        for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
            ToolMenuHandler::unloadToolbar(this->toolbarWidgets[i].get());
        }

        this->toolbar->freeDynamicToolbarItems();
    }
    return std::exchange(this->selectedToolbar, nullptr);
}

void MainWindow::loadToolbar(ToolbarData* d) {
    this->selectedToolbar = d;

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbar->load(d, this->toolbarWidgets[i].get(), TOOLBAR_DEFINITIONS[i].propName,
                            TOOLBAR_DEFINITIONS[i].horizontal);
    }

    this->floatingToolbox->flagRecalculateSizeRequired();
}

void MainWindow::reloadToolbars() {
    ToolbarData* d = getSelectedToolbar();
    this->clearToolbar();
    this->toolbarSelected(d);
    if (floatingTools) {
        floatingTools->rebuildToolOptions();
    }
    if (shapeToolbox) {
        shapeToolbox->rebuildPalettes();
    }
}

void MainWindow::updateRecentColors() {
    if (floatingTools) {
        floatingTools->updateRecentColors();
    }
}

void MainWindow::onSelectionChanged() {
    if (floatingTools) {
        floatingTools->onSelectionChanged();
    }
}

auto MainWindow::getSelectedToolbar() const -> ToolbarData* { return this->selectedToolbar; }

auto MainWindow::getToolbarWidgets() const -> const ToolbarWidgetArray& { return toolbarWidgets; }

auto MainWindow::getToolbarName(GtkToolbar* toolbar) const -> const char* {
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (static_cast<void*>(this->toolbarWidgets[i].get()) == static_cast<void*>(toolbar)) {
            return TOOLBAR_DEFINITIONS[i].propName;
        }
    }

    return "";
}

void MainWindow::setDynamicallyGeneratedSubmenuDisabled(bool disabled) { menubar->setDisabled(disabled); }

void MainWindow::updateToolbarMenu() {
    menubar->getToolbarSelectionSubmenu().update(toolbar.get(), this->selectedToolbar);
}

void MainWindow::migrateSelectedToolbar() {
    Settings* settings = control->getSettings();
    if (!settings->consumePendingToolbarMigration()) {
        return;
    }
    // Settings from before the top bar: always switch to the default layout (custom toolbar.ini stays on disk)
    settings->setSelectedToolbar(DEFAULT_TOOLBAR);
}

void MainWindow::createToolbar() {
    toolbarSelected(control->getSettings()->getSelectedToolbar());

    this->control->getScheduler()->unblockRerenderZoom();
}

void MainWindow::updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage) {
    toolbar->setPageInfo(page, pagecount, pdfpage);
    this->pageNumber = page;
    this->pageCount = pagecount;
    if (topBar) {
        topBar->setPageInfo(page, pagecount);
    }
}

void MainWindow::setDocumentTitle(const std::string& name, const std::string& status, bool unsaved) {
    if (topBar) {
        topBar->setDocumentTitle(name, status, unsaved);
    }
}

auto MainWindow::getMenubar() const -> Menubar* { return menubar.get(); }

void MainWindow::show(GtkWindow* parent) { gtk_widget_show(this->window); }

auto MainWindow::getToolbarModel() const -> ToolbarModel* { return this->toolbar->getModel(); }

auto MainWindow::getToolMenuHandler() const -> ToolMenuHandler* { return this->toolbar.get(); }

void MainWindow::loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename) {
    auto filepath = gladeSearchPath->findFile("", cssFilename);
    xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
    gtk_css_provider_load_from_path(provider.get(), char_cast(filepath.u8string().c_str()), nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider.get()),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

PdfFloatingToolbox* MainWindow::getPdfToolbox() const { return this->pdfFloatingToolBox.get(); }

FloatingToolbox* MainWindow::getFloatingToolbox() const { return this->floatingToolbox.get(); }

ShapeToolbox* MainWindow::getShapeToolbox() const { return this->shapeToolbox.get(); }

void MainWindow::setDPI() const {
    if (auto dpi = this->getControl()->getSettings()->getDisplayDpi(); dpi == -1) {
        auto res = xoj::util::gtk::getWidgetDPI(this->window);
        this->getControl()->getZoomControl()->setZoom100Value(res.value_or(Util::DPI_NORMALIZATION_FACTOR) /
                                                              Util::DPI_NORMALIZATION_FACTOR);
    } else {
        this->getControl()->getZoomControl()->setZoom100Value(dpi / Util::DPI_NORMALIZATION_FACTOR);
    }
}
