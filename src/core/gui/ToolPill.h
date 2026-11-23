/*
 * MoreXournal
 *
 * The floating pill below the top bar with the options of the active tool
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>  // for uint32_t
#include <utility>  // for pair
#include <vector>   // for vector

#include <glib.h>     // for guint, gulong
#include <gtk/gtk.h>  // for GtkWidget, GtkBox

#include "control/ToolEnums.h"          // for ToolType
#include "control/actions/ActionRef.h"  // for ActionRef
#include "util/Color.h"                 // for Color
#include "util/raii/GObjectSPtr.h"      // for WidgetSPtr

#include "TopBarLogic.h"  // for ToolGroup

class Control;

/**
 * The options of the active tool: colors, thickness, eraser type, selection tools, shapes, font... (see
 * ToolPillLogic.h). Every control uses the existing actions, so menus, shortcuts and toolbars stay in sync.
 * The content is rebuilt when the tool changes to another group.
 */
class ToolPill {
public:
    explicit ToolPill(Control* control);
    ~ToolPill();

    ToolPill(const ToolPill&) = delete;
    ToolPill& operator=(const ToolPill&) = delete;

    /// The pill, owned by this object
    GtkWidget* getWidget() const;

    /// Builds the content again, e.g. after the palette changed
    void rebuild();

    /// Shows the current recent colors, if the content has colors
    void updateColors();

    /// Updates the pill when a selection appears or disappears
    void onSelectionChanged();

private:
    /// Coalesces tool and selection updates onto the next high-priority idle turn
    void scheduleUpdate();
    /// Rebuilds the content if the active tool needs other options than the shown ones
    void onToolChanged();
    void updateThickness();

    void addToolName(GtkBox* box, ToolType tool);
    void addShapeTypes(GtkBox* box);
    void addSelectionModes(GtkBox* box);
    void addSelectionActions(GtkBox* box);
    void addEraserOptions(GtkBox* box);
    void addFont(GtkBox* box);
    void addColors(GtkBox* box);
    void addThickness(GtkBox* box, uint32_t sections, xoj::topbar::ToolGroup group);
    void addTextAlignment(GtkBox* box);
    void addHoldToShape(GtkBox* box);
    void addHint(GtkBox* box, const char* text);

private:
    Control* control;

    xoj::util::WidgetSPtr pill;
    GtkWidget* content = nullptr;  ///< Child of pill, replaced on rebuild

    /// What the content shows, to rebuild only when it changes
    xoj::topbar::ToolGroup shownGroup = xoj::topbar::ToolGroup::NONE;
    ToolType shownTool = TOOL_NONE;
    uint32_t shownSections = 0;
    bool built = false;

    /// Parts of the thickness button, children of content (nullptr if the tool has no thickness)
    GtkWidget* thicknessSample = nullptr;
    GtkLabel* thicknessLabel = nullptr;

    /// Box of the color swatches, child of content (nullptr if the tool has no colors), and the colors it shows
    GtkWidget* swatchBox = nullptr;
    std::vector<Color> shownColors;

    /// notify::state handlers on the tool actions, disconnected in the destructor
    std::vector<std::pair<ActionRef, gulong>> watchedActions;
    /// Idle source of a pending onToolChanged(), removed in the destructor
    guint pendingUpdate = 0;
};
