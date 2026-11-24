/*
 * MoreXournal
 *
 * The bar at the top of the window: main menu and document name, the tools, history and pages
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>    // for array
#include <cstddef>  // for size_t
#include <string>   // for string
#include <utility>  // for pair
#include <vector>   // for vector

#include <gio/gio.h>  // for GMenuModel
#include <gtk/gtk.h>  // for GtkWidget, GtkHeaderBar

#include "control/ToolEnums.h"          // for ToolType
#include "control/actions/ActionRef.h"  // for ActionRef
#include "util/raii/GObjectSPtr.h"

#include "TopBarLogic.h"  // for ToolGroup

class Control;

class TopBar {
public:
    /// @param mainMenu The model shown by the main menu button
    TopBar(Control* control, GMenuModel* mainMenu);
    ~TopBar();

    TopBar(const TopBar&) = delete;
    TopBar& operator=(const TopBar&) = delete;

    /**
     * Moves the three groups of buttons into `bar`: the menu and the title at the start, the tools centred on the
     * window, history and pages at the end. Call it again to move them to another bar.
     */
    void attachTo(GtkHeaderBar* bar);

    void setDocumentTitle(const std::string& name, const std::string& status, bool unsaved = false);
    /// @param page 0-based index of the current page
    void setPageInfo(size_t page, size_t pageCount);

    /// View > Toolbars hides the tools, the history and the page buttons. The menu and the title stay.
    void setToolsVisible(bool visible);

    /// Updates the bar box's target natural width from the window width; layout breakpoints follow usable bar width
    void setWidth(int width);

    /// Switches icon sizes between compact (18 px) and touch (20 px) and recomputes density-aware breakpoints
    void setTouchLayout(bool touch);

    /// Header bar currently holding the top bar controls (for F6 focus cycling)
    GtkWidget* getActiveBar() const;

private:
    void createStart(GMenuModel* mainMenu);
    void createCenter();
    void createEnd();
    void onUsableWidthAllocated(int usableWidth);
    void updateLayoutForUsableWidth(int usableWidth);
    void applyLayout();
    void updateMenuButtonTooltip();
    void rebuildOverflowMenu();

    void onToolClicked(xoj::topbar::ToolGroup group);
    void onActionStateChanged(GAction* action);
    /// Shows the button of the active tool as pressed
    void syncTools();

private:
    Control* control;

    xoj::util::WidgetSPtr barContainer;  ///< Custom 3-slot container so the bar shrinks cleanly to 600 px without overlap
    xoj::util::WidgetSPtr emptyTitle;    ///< Unshown 0-width custom_title so GtkHeaderBar does not show default window title
    xoj::util::WidgetSPtr startBox;
    xoj::util::WidgetSPtr centerBox;
    xoj::util::WidgetSPtr endBox;
    GtkHeaderBar* attachedBar = nullptr;

    GtkWidget* menuButton = nullptr;
    GtkWidget* titleBox = nullptr;
    GtkLabel* titleLabel = nullptr;
    GtkLabel* statusLabel = nullptr;
    GtkWidget* menuHeaderItem = nullptr;
    GtkWidget* menuHeaderSep = nullptr;
    GtkLabel* menuHeaderTitle = nullptr;
    GtkLabel* menuHeaderStatus = nullptr;

    GtkWidget* sidebarButton = nullptr;
    GtkWidget* pageNav = nullptr;
    GtkLabel* pageLabel = nullptr;
    GtkWidget* pageCompactButton = nullptr;
    GtkLabel* compactPageLabel = nullptr;
    GtkWidget* newPageButton = nullptr;
    GtkWidget* fullscreenButton = nullptr;
    GtkWidget* overflowButton = nullptr;
    xoj::util::GObjectSPtr<GMenu> overflowMenu;

    std::vector<GtkWidget*> separators;
    xoj::topbar::BarLayout layout{};
    int lastUsableWidth = 0;
    int pendingUsableWidth = 0;
    guint layoutIdleId = 0;
    bool touchLayout = false;
    bool docUnsaved = false;
    std::string docName;
    std::string docStatus;

    struct ToolButtonData {
        TopBar* self;
        xoj::topbar::ToolGroup group;
    };
    static constexpr size_t TOOL_BUTTON_COUNT = static_cast<size_t>(xoj::topbar::ToolGroup::NONE);
    std::array<GtkToggleButton*, TOOL_BUTTON_COUNT> toolButtons{};
    std::array<ToolButtonData, TOOL_BUTTON_COUNT> toolButtonData{};
    std::array<gulong, TOOL_BUTTON_COUNT> toolClickedIds{};

    /// notify::state handlers on the tool actions, disconnected in the destructor
    std::vector<std::pair<ActionRef, gulong>> watchedActions;

    /// The selection tool the Selection button selects: the last one used
    ToolType lastSelectionTool = TOOL_SELECT_REGION;
};
