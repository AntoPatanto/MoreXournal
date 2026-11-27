/*
 * Xournal++
 *
 * The Main window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>    // for array
#include <atomic>   // for atomic_bool
#include <cstddef>  // for size_t
#include <memory>   // for unique_ptr
#include <string>   // for string

#include <gdk/gdk.h>      // for GdkDragContext, GdkEvent
#include <glib-object.h>  // for GClosure
#include <glib.h>         // for gpointer, gboolean, gint
#include <gtk/gtk.h>      // for GtkWidget, GtkCheckMenu...

#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

#include "GladeGui.h"            // for GladeGui
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIONS_LEN

class Control;
class Layout;
class ScrollHandling;
class VerticalScrollbarRevealController;
class ToolMenuHandler;
class ToolbarData;
class XournalView;
class PdfFloatingToolbox;
class ShapeToolbox;
class FloatingToolbox;
class FloatingTools;
class GladeSearchpath;

class Menubar;
class TopBar;

typedef std::array<xoj::util::WidgetSPtr, TOOLBAR_DEFINITIONS_LEN> ToolbarWidgetArray;

class MainWindow: public GladeGui {
public:
    MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent);
    ~MainWindow() override;

    void populate(GladeSearchpath* gladeSearchPath);

public:
    GMenuModel* getMenuModel() const;

    void show(GtkWindow* parent) override;

    void toolbarSelected(const std::string& id);
    void toolbarSelected(ToolbarData* d);
    ToolbarData* getSelectedToolbar() const;

private:
    const ToolbarData* clearToolbar();
    void loadToolbar(ToolbarData* d);

public:
    /**
     * reloadToolbars reloads the currently selected toolbar
     *
     * This is especially useful when a change in the SettingsDialog should be reflected right away
     */
    void reloadToolbars();

    /// Shows the new recent colors in the tool pill
    void updateRecentColors();

    /// Updates the tool pill when a selection appears or disappears
    void onSelectionChanged();

    void updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage);

    /// Shows the document name and its save state in the top bar
    void setDocumentTitle(const std::string& name, const std::string& status, bool unsaved = false);

    void setMaximized(bool maximized);
    bool isMaximized() const;

    void setFullscreen(bool enabled) const;

    bool isDarkTheme() const;

    XournalView* getXournal() const;

    /// Always show the menu bar (true), or only after a lone Alt press (false)
    void setMenubarVisible(bool visible);
    /// A pen, mouse or touch press on the page: Alt is now a drawing modifier, and the menu bar shown with Alt hides
    void onPagePressed();
    void setSidebarVisible(bool visible);
    void setToolbarVisible(bool visible);

    Control* getControl() const;

    PdfFloatingToolbox* getPdfToolbox() const;
    FloatingToolbox* getFloatingToolbox() const;
    /// nullptr while the window is built or destroyed
    ShapeToolbox* getShapeToolbox() const;

    void updateScrollbarSidebarPosition();

    ToolMenuHandler* getToolMenuHandler() const;

    void setDynamicallyGeneratedSubmenuDisabled(bool disabled);

    void updateColorscheme();

    /// Re-evaluates the touch chrome layout from Settings::getTouchMode() (e.g. after Preferences closes)
    void updateTouchMode();
    /// Updates touch chrome layout from a press/touch event (when TouchMode::AUTO is active)
    void onPointerInput(const GdkEvent* event);
    bool isTouchLayout() const;

    Layout* getLayout() const;

    [[maybe_unused]] Menubar* getMenubar() const;

    /**
     * Disable kinetic scrolling if there is a touchscreen device that was manually mapped to another enabled input
     * device class. This is required so the GtkScrolledWindow does not swallow all the events.
     */
    void setGtkTouchscreenScrollingForDeviceMapping();
    void setGtkTouchscreenScrollingEnabled(bool enabled);

    /// Infer the window's DPI from available monitor info and use it to set the default zoom value.
    void setDPI() const;

private:
    void initXournalWidget();

    void createToolbar();
    /// Once after an update from settings older than UI_LAYOUT_VERSION 1, see
    /// Settings::consumePendingToolbarMigration()
    void migrateSelectedToolbar();

    /**
     * Update the position of the separator in the paned container, adjusting it to the saved sidebar width.
     * @param contentWidth should be the width of the paned container. The caller should retrieve the width
     * of the container before any modifications to it, as that will reset its allocation.
     */
    void updatePanedPosition(int contentWidth);

    /**
     * Window close Button is pressed
     */
    static bool deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control);

    /**
     * Window is maximized/minimized
     */
    static void windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win);

    /**
     * Callback for drag & drop files
     */
    static void dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                guint info, guint time, MainWindow* win);

    /**
     * Load Overall CSS file with custom icons, other styling and potentially, user changes
     */
    static void loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename);

    /// Wraps the window content in a box that also holds the inline top bar and the menu bar
    void createWindowBox();
    void createTopBar();
    /// The top bar goes inline while fullscreen or without client-side decorations, otherwise in the title bar
    void placeTopBar(bool fullscreen);

    static gboolean onKeyEvent(GtkWidget* window, GdkEventKey* event, MainWindow* self);
#ifdef __APPLE__
    static gboolean onInlineBarButtonPress(GtkWidget* widget, GdkEventButton* event, MainWindow* self);
#endif
    /// @return true if the event was used to hide the menu bar and must not go further
    bool handleMenubarKey(GdkEventKey* event);
    /// Cycles keyboard focus across top bar -> tool pill -> floating layer/zoom -> page (F6 / Shift+F6)
    bool handleChromeFocusKey(GdkEventKey* event);
    /// Shows the menu bar until the next menu action, Escape, click, focus loss or Alt press
    void setMenubarTemporarilyVisible(bool visible);
    void updateMenubarReveal();
    void setTouchLayout(bool touch);

private:
    Control* control;

    std::unique_ptr<XournalView> xournal;
    GtkWidget* winXournal = nullptr;
    std::unique_ptr<ScrollHandling> scrollHandling;
    std::unique_ptr<VerticalScrollbarRevealController> vscrollbarReveal;

    std::atomic_bool gtkTouchscreenScrollingEnabled{true};

    std::unique_ptr<PdfFloatingToolbox> pdfFloatingToolBox;
    std::unique_ptr<FloatingToolbox> floatingToolbox;
    std::unique_ptr<ShapeToolbox> shapeToolbox;
    std::unique_ptr<FloatingTools> floatingTools;

    // Toolbars
    std::unique_ptr<ToolMenuHandler> toolbar;
    ToolbarData* selectedToolbar = nullptr;

    std::unique_ptr<Menubar> menubar;
    std::unique_ptr<TopBar> topBar;

    xoj::util::GObjectSPtr<GtkCssProvider> themeCssProvider;
    xoj::util::GObjectSPtr<GtkGesture> pointerGesture;
    bool touchLayout = false;
    int lastAllocatedWidth = 0;
    int pendingAllocatedWidth = 0;
    guint resizeIdleId = 0;

    GtkWidget* titlebar = nullptr;   ///< Header bar used as window title bar, owned by the window
    GtkWidget* inlineBar = nullptr;  ///< Header bar at the top of the content, owned by the window
#ifdef __APPLE__
    GtkWidget* inlineBarEventBox = nullptr;  ///< Input-only event box behind inlineBar for window drag and zoom
#endif
    GtkWidget* menubarRevealer = nullptr;  ///< Owned by the window
    GtkWidget* menubarWidget = nullptr;    ///< Owned by menubarRevealer

    bool menubarPinned = false;     ///< The menu bar is always shown
    bool menubarTemporary = false;  ///< Shown by a lone Alt press
    bool altArmed = false;          ///< Alt is down and nothing else happened since it was pressed

    size_t pageNumber = 0;
    size_t pageCount = 0;

    bool maximized = false;
    bool darkMode = false;
    bool modifiedGtkSettingsTheme = false;

    ToolbarWidgetArray toolbarWidgets;

    bool sidebarVisible = true;

    /// The last monitor the window has been moved to -- used for setting dpi
    GdkMonitor* lastMonitor = nullptr;

    xoj::util::WidgetSPtr boxContainerWidget;
    xoj::util::WidgetSPtr panedContainerWidget;
    xoj::util::WidgetSPtr mainContentWidget;
    xoj::util::WidgetSPtr sidebarWidget;
};
