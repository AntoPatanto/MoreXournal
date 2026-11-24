#include "ToolPill.h"

#include <algorithm>  // for clamp, min
#include <cstdio>     // for snprintf
#include <optional>   // for optional
#include <string>     // for string

#include "control/Control.h"                        // for Control
#include "control/Tool.h"                           // for Tool
#include "control/ToolHandler.h"                    // for ToolHandler
#include "control/actions/ActionDatabase.h"         // for ActionDatabase
#include "control/settings/Settings.h"              // for Settings
#include "control/tools/EditSelection.h"            // for EditSelection
#include "control/tools/ImageHandler.h"             // for ImageHandler
#include "enums/Action.enum.h"                      // for Action
#include "gui/ChromeUtil.h"                         // for newChromeIcon, updateScalableIcons
#include "gui/IconNameHelper.h"                     // for IconNameHelper
#include "gui/MainWindow.h"                         // for MainWindow
#include "gui/XournalView.h"                        // for XournalView
#include "gui/toolbarMenubar/ColorToolItem.h"       // for ColorToolItem
#include "gui/toolbarMenubar/FontButton.h"          // for FontButton
#include "gui/toolbarMenubar/model/ColorPalette.h"  // for Palette
#include "model/Document.h"                         // for Document
#include "model/Image.h"                            // for Image
#include "model/TextAlignment.h"                    // for TextAlignment
#include "model/XojPage.h"                          // for XojPage
#include "util/GtkUtil.h"                           // for setToggleButtonUnreleasable
#include "util/NamedColor.h"                        // for NamedColor
#include "util/Point.h"                             // for Point
#include "util/Util.h"                              // for npos
#include "util/i18n.h"                              // for _
#include "util/raii/GVariantSPtr.h"                 // for makeGVariantSPtr

#include "RecentColors.h"   // for pillColors
#include "ToolPillLogic.h"  // for sectionsFor

using xoj::topbar::ToolGroup;
namespace pill = xoj::toolpill;

namespace {
/// How many colors the pill shows (black and the recent colors); the palette is in the "more colors" popover
constexpr size_t PILL_COLOR_COUNT = 1 + xoj::toolpill::RECENT_COLOR_COUNT;
constexpr int ICON_SIZE = 18;
/// Data key of the line width (in 1/100 px) drawn by a thickness sample
constexpr const char* SAMPLE_WIDTH_KEY = "mx-sample-width";

std::string winAction(Action a) { return std::string("win.") + Action_toString(a); }

void addClass(GtkWidget* w, const char* cssClass) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cssClass);
}

GtkWidget* newIcon(const std::string& iconName, int size = ICON_SIZE) {
    return xoj::chrome::newChromeIcon(iconName.c_str(), size);
}

GtkWidget* newSeparator() {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    addClass(sep, "mx-separator");
    return sep;
}

/// A group of buttons drawn as one control
GtkWidget* newSegment() {
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(seg, "mx-seg");
    gtk_widget_set_valign(seg, GTK_ALIGN_CENTER);
    return seg;
}

/// A flat button focusable via keyboard (F6/Tab), never stealing focus on click
GtkWidget* prepareButton(GtkWidget* btn, const char* tooltip) {
    gtk_widget_set_can_focus(btn, true);
    gtk_widget_set_focus_on_click(btn, false);
    addClass(btn, "flat");
    if (tooltip) {
        gtk_widget_set_tooltip_text(btn, tooltip);
    }
    return btn;
}

/// A left-aligned button for a popover row (avoids deprecated gtk_button_set_alignment)
GtkWidget* makePopoverRowChild(const char* label, const std::string& icon) {
    GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    if (!icon.empty()) {
        gtk_box_pack_start(GTK_BOX(rowBox), newIcon(icon), false, false, 0);
    }
    if (label) {
        GtkWidget* lbl = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_box_pack_start(GTK_BOX(rowBox), lbl, true, true, 0);
    }
    return rowBox;
}

/// A toggle button showing whether the state of `action` equals `target`, and setting it when clicked
GtkWidget* newTargetToggle(Action action, GVariant* target, const char* label, const std::string& icon,
                           const char* tooltip) {
    GtkWidget* btn = prepareButton(gtk_toggle_button_new(), tooltip);
    if (!icon.empty()) {
        gtk_button_set_image(GTK_BUTTON(btn), newIcon(icon));
    }
    if (label) {
        gtk_button_set_label(GTK_BUTTON(btn), label);
    }
    if (!icon.empty() && label) {
        gtk_button_set_always_show_image(GTK_BUTTON(btn), true);
    }
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), winAction(action).c_str());
    gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), target);
    xoj::util::gtk::setToggleButtonUnreleasable(GTK_TOGGLE_BUTTON(btn));
    xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(btn));
    return btn;
}

template <typename T>
GtkWidget* newTargetToggle(Action action, T target, const char* label, const std::string& icon = {},
                           const char* tooltip = nullptr) {
    auto value = xoj::util::makeGVariantSPtr(target);
    return newTargetToggle(action, value.get(), label, icon, tooltip);
}

/// A button activating `action`
GtkWidget* newActionButton(GtkWidget* btn, Action action, const char* label, const std::string& icon,
                           const char* tooltip) {
    prepareButton(btn, tooltip);
    if (!icon.empty()) {
        gtk_button_set_image(GTK_BUTTON(btn), newIcon(icon));
        gtk_button_set_always_show_image(GTK_BUTTON(btn), true);
    }
    if (label) {
        gtk_button_set_label(GTK_BUTTON(btn), label);
    }
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), winAction(action).c_str());
    xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(btn));
    return btn;
}

void latchClicked(GtkButton* b, gpointer a) {
    xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(a)), xoj::util::adopt);
    if (!g_variant_get_boolean(state.get())) {
        g_action_change_state(G_ACTION(a), g_variant_new_boolean(true));
    } else {
        // GTK already untoggled the button: restore it without re-entering this handler
        g_signal_handlers_block_by_func(b, reinterpret_cast<gpointer>(latchClicked), a);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), true);
        g_signal_handlers_unblock_by_func(b, reinterpret_cast<gpointer>(latchClicked), a);
    }
}

void latchSync(GObject* a, GParamSpec*, gpointer b) {
    xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(a)), xoj::util::adopt);
    g_signal_handlers_block_by_func(b, reinterpret_cast<gpointer>(latchClicked), a);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), g_variant_get_boolean(state.get()));
    g_signal_handlers_unblock_by_func(b, reinterpret_cast<gpointer>(latchClicked), a);
}

/**
 * A toggle button for a boolean action that a click turns on but never off. Used for the shape types: a click on the
 * active shape keeps it, instead of going back to freehand drawing (and to the Pen button).
 */
GtkWidget* newLatchButton(GAction* action, const std::string& icon, const char* tooltip) {
    GtkWidget* btn = prepareButton(gtk_toggle_button_new(), tooltip);
    gtk_button_set_image(GTK_BUTTON(btn), newIcon(icon));

    // Both handlers go away with the button
    g_signal_connect_object(btn, "clicked", G_CALLBACK(latchClicked), action, GConnectFlags(0));
    g_signal_connect_object(action, "notify::state", G_CALLBACK(latchSync), btn, GConnectFlags(0));
    latchSync(G_OBJECT(action), nullptr, btn);
    xoj::util::gtk::setWidgetFollowActionEnabled(btn, action);
    return btn;
}

/// Closes `popover` when `btn` is clicked
void popdownOnClick(GtkWidget* btn, GtkWidget* popover) {
    g_signal_connect_object(btn, "clicked",
                            G_CALLBACK(+[](GtkButton*, gpointer p) { gtk_popover_popdown(GTK_POPOVER(p)); }), popover,
                            GConnectFlags(0));
}

/// A popover attached to a menu button, with a vertical box as content
GtkBox* newPopover(GtkWidget* menuButton) {
    GtkWidget* popover = gtk_popover_new(menuButton);
    addClass(popover, "mx-popover");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(popover), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuButton), popover);
    xoj::chrome::hideTooltipWhilePopupOpen(menuButton, popover);
    return GTK_BOX(box);
}

void addPopoverTitle(GtkBox* box, const char* title) {
    GtkWidget* lbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    addClass(lbl, "mx-popover-title");
    gtk_box_pack_start(box, lbl, false, false, 0);
}

/// Line width shown for a thickness in pt, in pixels
double sampleWidth(double thickness) { return std::clamp(thickness * 1.5, 1.0, 12.0); }

void setSampleWidth(GtkWidget* sample, double px) {
    g_object_set_data(G_OBJECT(sample), SAMPLE_WIDTH_KEY, GINT_TO_POINTER(static_cast<int>(px * 100)));
    gtk_widget_queue_draw(sample);
}

/// A horizontal line of the given width, in the color of the text
GtkWidget* newSample(double px, int width, int height) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, width, height);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    setSampleWidth(area, px);
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                         const double lineWidth =
                                 GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), SAMPLE_WIDTH_KEY)) / 100.0;
                         GtkStyleContext* ctx = gtk_widget_get_style_context(w);
                         GdkRGBA color;
                         gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &color);
                         gdk_cairo_set_source_rgba(cr, &color);
                         const double h = gtk_widget_get_allocated_height(w);
                         const double inset = 2 + lineWidth / 2;
                         cairo_set_line_width(cr, lineWidth);
                         cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                         cairo_move_to(cr, inset, h / 2);
                         cairo_line_to(cr, gtk_widget_get_allocated_width(w) - inset, h / 2);
                         cairo_stroke(cr);
                         return true;
                     }),
                     nullptr);
    return area;
}

/// "1.41 pt", without trailing zeros
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

struct ShapeChoice {
    Action action;
    const char* icon;
    const char* tooltip;
};
const ShapeChoice SHAPES[] = {{Action::TOOL_DRAW_RECTANGLE, "mx-rect-symbolic", N_("Draw Rectangle")},
                              {Action::TOOL_DRAW_ELLIPSE, "mx-ellipse-symbolic", N_("Draw Ellipse")},
                              {Action::TOOL_DRAW_ARROW, "mx-arrow-symbolic", N_("Draw Arrow")},
                              {Action::TOOL_DRAW_DOUBLE_ARROW, "mx-double-arrow-symbolic", N_("Draw Double Arrow")},
                              {Action::TOOL_DRAW_LINE, "mx-line-symbolic", N_("Draw Line")},
                              {Action::TOOL_DRAW_SPLINE, "mx-spline-symbolic", N_("Draw Spline")},
                              {Action::TOOL_DRAW_COORDINATE_SYSTEM, "mx-coord-symbolic", N_("Draw Coordinate System")}};

/// The actions whose state changes when the tool or the drawing type changes
constexpr Action TOOL_ACTIONS[] = {Action::SELECT_TOOL,
                                   Action::TOOL_DRAW_SHAPE_RECOGNIZER,
                                   Action::TOOL_DRAW_RECTANGLE,
                                   Action::TOOL_DRAW_ELLIPSE,
                                   Action::TOOL_DRAW_ARROW,
                                   Action::TOOL_DRAW_DOUBLE_ARROW,
                                   Action::TOOL_DRAW_COORDINATE_SYSTEM,
                                   Action::TOOL_DRAW_LINE,
                                   Action::TOOL_DRAW_SPLINE,
                                   Action::TOOL_SIZE,
                                   Action::TOOL_PEN_LINE_STYLE};

/// Actions whose enabled flag changes when a selection enables or disables style editing
constexpr Action SELECTION_CAP_ACTIONS[] = {Action::TOOL_COLOR, Action::TOOL_SIZE, Action::TOOL_FILL,
                                            Action::TOOL_PEN_LINE_STYLE};
}  // namespace

ToolPill::ToolPill(Control* control): control(control) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    pill.reset(box, xoj::util::refsink);
    gtk_widget_set_name(box, "toolPill");
    addClass(box, "mx-pill");
    addClass(box, "mx-float");
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_no_show_all(box, true);

    // The action callbacks change the tool after the state: coalesce multiple action updates at high idle priority
    // so the pill rebuilds before the next frame is drawn (avoiding starvation by button transition animations)
    ActionDatabase* db = control->getActionDatabase();
    auto onStateChanged = +[](GObject*, GParamSpec*, gpointer self) { static_cast<ToolPill*>(self)->scheduleUpdate(); };
    for (Action a: TOOL_ACTIONS) {
        ActionRef action = db->getAction(a);
        gulong id = g_signal_connect(action.get(), "notify::state", G_CALLBACK(onStateChanged), this);
        watchedActions.emplace_back(std::move(action), id);
    }
    for (Action a: SELECTION_CAP_ACTIONS) {
        ActionRef action = db->getAction(a);
        gulong id = g_signal_connect(action.get(), "notify::enabled", G_CALLBACK(onStateChanged), this);
        watchedActions.emplace_back(std::move(action), id);
    }
    onToolChanged();
}

ToolPill::~ToolPill() {
    if (pendingUpdate != 0) {
        g_source_remove(pendingUpdate);
    }
    for (auto& [action, id]: watchedActions) {
        g_signal_handler_disconnect(action.get(), id);
    }
    gtk_widget_destroy(pill.get());
}

GtkWidget* ToolPill::getWidget() const { return pill.get(); }

void ToolPill::onSelectionChanged() { scheduleUpdate(); }

void ToolPill::setVisible(bool visible) {
    allowedVisible = visible;
    updateVisibility();
}

void ToolPill::setTouchLayout(bool touch) {
    if (touchLayout == touch) {
        return;
    }
    touchLayout = touch;
    if (built) {
        rebuild();
    }
}

void ToolPill::updateVisibility() {
    const bool show = allowedVisible && shownSections != 0;
    const bool wasVisible = gtk_widget_get_visible(pill.get());
    gtk_widget_set_visible(pill.get(), show);
    if (wasVisible && !show) {
        queueRedraw();
    }
}

void ToolPill::queueRedraw() {
    // Redraw the overlay and canvas below when the pill changes width or hides, so no drop shadow stays behind
    if (GtkWidget* parent = gtk_widget_get_parent(pill.get())) {
        gtk_widget_queue_draw(parent);
    }
    if (MainWindow* win = control->getWindow(); win && win->getXournal()) {
        gtk_widget_queue_draw(win->getXournal()->getWidget());
    }
}

void ToolPill::scheduleUpdate() {
    if (pendingUpdate != 0) {
        return;
    }
    pendingUpdate = g_idle_add_full(
            G_PRIORITY_HIGH_IDLE,
            +[](gpointer self) -> gboolean {
                auto* p = static_cast<ToolPill*>(self);
                p->pendingUpdate = 0;
                p->onToolChanged();
                return G_SOURCE_REMOVE;
            },
            this, nullptr);
}

void ToolPill::onToolChanged() {
    ToolHandler* th = control->getToolHandler();
    const ToolType tool = th->getToolType();
    const ToolGroup group = xoj::topbar::toolGroupFor(tool, th->getDrawingType());
    MainWindow* win = control->getWindow();
    EditSelection* sel = (win && win->getXournal()) ? win->getXournal()->getSelection() : nullptr;
    // A single selected shape already shows the dedicated ShapeToolbox right below it: keep the pill free of
    // duplicate style controls and selection actions
    const bool isShapeSel = sel && sel->getShapeStroke() != nullptr;
    const bool handledByShapeToolbox = group == ToolGroup::SELECTION && isShapeSel;
    const pill::Capabilities caps =
            handledByShapeToolbox ?
                    pill::Capabilities{} :
                    pill::Capabilities{th->hasCapability(TOOL_CAP_COLOR), th->hasCapability(TOOL_CAP_SIZE),
                                       th->hasCapability(TOOL_CAP_FILL), th->hasCapability(TOOL_CAP_LINE_STYLE)};
    uint32_t sections = pill::sectionsFor(group, caps, tool);
    if (pill::showSelectionActionsInPill(sel != nullptr, isShapeSel)) {
        sections |= pill::SELECTION_ACTIONS;
    }

    const bool samePdfText = (tool == TOOL_SELECT_PDF_TEXT_LINEAR || tool == TOOL_SELECT_PDF_TEXT_RECT) &&
                             (shownTool == TOOL_SELECT_PDF_TEXT_LINEAR || shownTool == TOOL_SELECT_PDF_TEXT_RECT);
    // The thickness values or chip labels differ between tools without a button, so rebuild when switching between them
    if (built && group == shownGroup && sections == shownSections &&
        (group != ToolGroup::NONE || tool == shownTool || samePdfText)) {
        shownTool = tool;
        updateThickness();
        return;
    }
    shownGroup = group;
    shownTool = tool;
    shownSections = sections;
    rebuild();
}

void ToolPill::rebuild() {
    built = true;
    if (content) {
        gtk_widget_destroy(content);
        content = nullptr;
    }
    thicknessSample = nullptr;
    thicknessLabel = nullptr;
    swatchBox = nullptr;
    shownColors.clear();

    const uint32_t s = shownSections;
    if (s != 0) {
        content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkBox* box = GTK_BOX(content);

        // Each section gets a separator before it, except the first one
        bool first = true;
        auto section = [&](uint32_t bit, auto&& add) {
            if (!(s & bit)) {
                return;
            }
            if (!first) {
                gtk_box_pack_start(box, newSeparator(), false, false, 0);
            }
            first = false;
            add();
        };
        section(pill::SPECIAL_TOOL_CHIP, [&] { addSpecialToolChip(box); });
        section(pill::PDF_TEXT_MODES, [&] { addPdfTextModes(box); });
        section(pill::SHAPE_TYPES, [&] { addShapeTypes(box); });
        section(pill::SELECTION_MODES, [&] { addSelectionModes(box); });
        section(pill::ERASER_OPTIONS, [&] { addEraserOptions(box); });
        section(pill::FONT, [&] { addFont(box); });
        section(pill::COLORS, [&] { addColors(box); });
        section(pill::THICKNESS, [&] { addThickness(box, s, shownGroup); });
        section(pill::TEXT_ALIGNMENT, [&] { addTextAlignment(box); });
        section(pill::IMAGE_PASTE, [&] { addImageOptions(box); });
        section(pill::SELECTION_ACTIONS, [&] { addSelectionActions(box); });

        gtk_container_add(GTK_CONTAINER(pill.get()), content);
        xoj::chrome::updateScalableIcons(content, xoj::chrome::chromeIconSize(touchLayout));
        gtk_widget_show_all(content);
        updateThickness();
    }
    gtk_widget_set_visible(pill.get(), allowedVisible && s != 0);
    queueRedraw();
}

void ToolPill::addShapeTypes(GtkBox* box) {
    ActionDatabase* db = control->getActionDatabase();
    GtkWidget* seg = newSegment();
    for (const auto& shape: SHAPES) {
        gtk_box_pack_start(GTK_BOX(seg),
                           newLatchButton(G_ACTION(db->getAction(shape.action).get()), shape.icon, _(shape.tooltip)),
                           false, false, 0);
    }
    gtk_box_pack_start(box, seg, false, false, 0);
}

void ToolPill::addSelectionModes(GtkBox* box) {
    GtkWidget* seg = newSegment();
    gtk_box_pack_start(GTK_BOX(seg), newTargetToggle(Action::SELECT_TOOL, TOOL_SELECT_REGION, _("Lasso")), false, false,
                       0);
    gtk_box_pack_start(GTK_BOX(seg), newTargetToggle(Action::SELECT_TOOL, TOOL_SELECT_RECT, _("Rectangle")), false,
                       false, 0);
    gtk_box_pack_start(GTK_BOX(seg), newTargetToggle(Action::SELECT_TOOL, TOOL_SELECT_OBJECT, _("Object")), false,
                       false, 0);
    gtk_box_pack_start(box, seg, false, false, 0);

    // The other selection tools
    GtkWidget* more = prepareButton(gtk_menu_button_new(), _("More selection tools"));
    gtk_button_set_image(GTK_BUTTON(more), newIcon("mx-more-symbolic"));
    GtkBox* pop = newPopover(more);
    GtkWidget* popover = GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(more)));
    IconNameHelper icons(control->getSettings());
    struct Choice {
        ToolType tool;
        const char* icon;
        const char* label;
    };
    std::vector<Choice> choices = {
            {TOOL_SELECT_MULTILAYER_REGION, "select-multilayer-lasso", _("Select Multi-Layer Region")},
            {TOOL_SELECT_MULTILAYER_RECT, "select-multilayer-rect", _("Select Multi-Layer Rectangle")}};
    if (control->getAudioController()) {
        choices.push_back({TOOL_PLAY_OBJECT, "object-play", _("Play Object")});
    }
    for (const auto& c: choices) {
        GtkWidget* row = newTargetToggle(Action::SELECT_TOOL, c.tool, nullptr);
        gtk_container_add(GTK_CONTAINER(row), makePopoverRowChild(c.label, icons.iconName(c.icon)));
        addClass(row, "mx-prow");
        popdownOnClick(row, popover);
        gtk_box_pack_start(pop, row, false, false, 0);
    }
    gtk_widget_show_all(GTK_WIDGET(pop));
    gtk_box_pack_start(box, more, false, false, 0);
}

void ToolPill::addSelectionActions(GtkBox* box) {
    GtkWidget* dup = prepareButton(gtk_button_new(), _("Duplicate"));
    gtk_button_set_image(GTK_BUTTON(dup), newIcon("mx-copy-symbolic"));
    g_signal_connect(dup, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer ctrl) { static_cast<Control*>(ctrl)->duplicateSelection(); }),
                     control);
    // Disabled with Delete while Control::block() runs (saving, exporting), so the document is not changed meanwhile
    xoj::util::gtk::setWidgetFollowActionEnabled(
            dup, G_ACTION(control->getActionDatabase()->getAction(Action::DELETE).get()));
    gtk_box_pack_start(box, dup, false, false, 0);

    GtkWidget* del = newActionButton(gtk_button_new(), Action::DELETE, nullptr, "mx-trash-symbolic", _("Delete"));
    addClass(del, "destructive");
    gtk_box_pack_start(box, del, false, false, 0);
}

void ToolPill::addEraserOptions(GtkBox* box) {
    GtkWidget* type = newSegment();
    gtk_box_pack_start(GTK_BOX(type), newTargetToggle(Action::TOOL_ERASER_TYPE, ERASER_TYPE_DEFAULT, _("Standard")),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(type), newTargetToggle(Action::TOOL_ERASER_TYPE, ERASER_TYPE_WHITEOUT, _("Whiteout")),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(type),
                       newTargetToggle(Action::TOOL_ERASER_TYPE, ERASER_TYPE_DELETE_STROKE, _("Whole stroke")), false,
                       false, 0);
    gtk_box_pack_start(box, type, false, false, 0);

    gtk_box_pack_start(box, newSeparator(), false, false, 0);
    GtkWidget* size = newSegment();
    gtk_box_pack_start(GTK_BOX(size), newTargetToggle(Action::TOOL_ERASER_SIZE, TOOL_SIZE_FINE, _("Fine")), false,
                       false, 0);
    gtk_box_pack_start(GTK_BOX(size), newTargetToggle(Action::TOOL_ERASER_SIZE, TOOL_SIZE_MEDIUM, _("Medium")), false,
                       false, 0);
    gtk_box_pack_start(GTK_BOX(size), newTargetToggle(Action::TOOL_ERASER_SIZE, TOOL_SIZE_THICK, _("Thick")), false,
                       false, 0);
    gtk_box_pack_start(box, size, false, false, 0);
}

void ToolPill::addFont(GtkBox* box) {
    GtkWidget* btn = FontButton::makeButton(G_ACTION(control->getActionDatabase()->getAction(Action::FONT).get()));
    prepareButton(btn, nullptr);
    addClass(btn, "mx-wide");
    gtk_box_pack_start(box, btn, false, false, 0);
}

namespace {
/// A color button of the pill, drawn with a contrast ring when its contrast against @mx_surface is below 3:1
GtkWidget* newSwatch(const NamedColor& c, const RecolorParameters& recolorParams, int dotSize) {
    std::optional<Color> secondary;
    if (recolorParams.recolorizeMainView) {
        secondary = recolorParams.recolor.convertColor(c.getColor());
    }
    GtkWidget* btn = ColorToolItem::makeButton(c, secondary, dotSize);
    prepareButton(btn, nullptr);
    addClass(btn, "mx-swatch");
    if (GtkWidget* oldChild = gtk_bin_get_child(GTK_BIN(btn))) {
        gtk_container_remove(GTK_CONTAINER(btn), oldChild);
    }
    gtk_container_add(GTK_CONTAINER(btn), xoj::chrome::newContrastSwatch(c.getColor(), dotSize, secondary));
    return btn;
}
}  // namespace

void ToolPill::updateColors() {
    if (!swatchBox) {
        return;
    }
    const Palette& palette = control->getPalette();
    std::vector<Color> paletteColors;
    paletteColors.reserve(palette.size());
    for (size_t i = 0; i < palette.size(); i++) {
        paletteColors.push_back(palette.getColorAt(i).getColor());
    }
    const bool pinBlack = pill::pinBlackColorFor(shownGroup);
    const size_t colorCount = pinBlack ? PILL_COLOR_COUNT : xoj::toolpill::RECENT_COLOR_COUNT;
    std::vector<Color> colors =
            pill::pillColors(control->getSettings()->getRecentColors(), paletteColors, colorCount, pinBlack);
    if (colors == shownColors) {
        return;
    }

    gtk_container_foreach(GTK_CONTAINER(swatchBox), +[](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);
    const auto& recolorParams = control->getSettings()->getRecolorParameters();
    const int dotSize = xoj::chrome::chromeSwatchDotSize(touchLayout);
    for (Color color: colors) {
        // The palette entry gives the tooltip its name; other colors are custom
        NamedColor named(color);
        for (size_t i = 0; i < palette.size(); i++) {
            Color c = palette.getColorAt(i).getColor();
            c.alpha = 0xff;
            if (c == color) {
                named = palette.getColorAt(i);
                break;
            }
        }
        gtk_box_pack_start(GTK_BOX(swatchBox), newSwatch(named, recolorParams, dotSize), false, false, 0);
    }
    gtk_widget_show_all(swatchBox);
    shownColors = std::move(colors);
}

void ToolPill::addColors(GtkBox* box) {
    const Palette& palette = control->getPalette();
    const auto& recolorParams = control->getSettings()->getRecolorParameters();
    const int dotSize = xoj::chrome::chromeSwatchDotSize(touchLayout);

    GtkWidget* dots = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    swatchBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(dots), swatchBox, false, false, 0);
    updateColors();

    // The whole palette and the color chooser
    GtkWidget* more = prepareButton(gtk_menu_button_new(), _("More colors"));
    gtk_button_set_image(GTK_BUTTON(more), newIcon("mx-down-symbolic"));
    GtkBox* pop = newPopover(more);
    GtkWidget* popover = GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(more)));
    addPopoverTitle(pop, _("Colors"));
    GtkWidget* grid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), static_cast<guint>(PILL_COLOR_COUNT));
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid),
                                           static_cast<guint>(std::min(PILL_COLOR_COUNT, palette.size())));
    for (size_t i = 0; i < palette.size(); i++) {
        GtkWidget* swatch = newSwatch(palette.getColorAt(i), recolorParams, dotSize);
        popdownOnClick(swatch, popover);
        gtk_container_add(GTK_CONTAINER(grid), swatch);
        // Flow box children take the focus and draw a hover box: keep only the button's look
        GtkWidget* child = gtk_widget_get_parent(swatch);
        gtk_widget_set_can_focus(child, false);
    }
    gtk_box_pack_start(pop, grid, false, false, 0);
    GtkWidget* custom = newActionButton(gtk_button_new(), Action::SELECT_COLOR, nullptr, {}, nullptr);
    gtk_container_add(GTK_CONTAINER(custom), makePopoverRowChild(_("Custom color…"), "mx-plus-symbolic"));
    addClass(custom, "mx-prow");
    popdownOnClick(custom, popover);
    gtk_box_pack_start(pop, custom, false, false, 0);
    gtk_widget_show_all(GTK_WIDGET(pop));
    gtk_box_pack_start(GTK_BOX(dots), more, false, false, 0);

    gtk_box_pack_start(box, dots, false, false, 0);
}

void ToolPill::addThickness(GtkBox* box, uint32_t sections, ToolGroup group) {
    ToolHandler* th = control->getToolHandler();
    const Tool* tool = th->getActiveTool();
    const double* widths =
            th->getToolThickness(pill::thicknessTableTool(tool ? tool->getToolType() : TOOL_NONE, group));

    GtkWidget* btn = prepareButton(gtk_menu_button_new(), group == ToolGroup::SHAPES ?
                                                                  _("Thickness and line style, shared with the pen") :
                                                                  _("Thickness"));
    addClass(btn, "mx-wide");
    GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    thicknessSample = newSample(1, 28, 14);
    thicknessLabel = GTK_LABEL(gtk_label_new(nullptr));
    addClass(GTK_WIDGET(thicknessLabel), "mx-numeric");
    gtk_box_pack_start(GTK_BOX(inner), thicknessSample, false, false, 0);
    gtk_box_pack_start(GTK_BOX(inner), GTK_WIDGET(thicknessLabel), false, false, 0);
    gtk_box_pack_start(GTK_BOX(inner), newIcon("mx-down-symbolic"), false, false, 0);
    gtk_container_add(GTK_CONTAINER(btn), inner);

    GtkBox* pop = newPopover(btn);
    GtkWidget* popover = GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(btn)));
    addPopoverTitle(pop, _("Thickness"));
    struct SizeChoice {
        ToolSize size;
        const char* name;
    };
    const SizeChoice sizes[] = {{TOOL_SIZE_VERY_FINE, _("Very Fine")},
                                {TOOL_SIZE_FINE, _("Fine")},
                                {TOOL_SIZE_MEDIUM, _("Medium")},
                                {TOOL_SIZE_THICK, _("Thick")},
                                {TOOL_SIZE_VERY_THICK, _("Very Thick")}};
    for (const auto& c: sizes) {
        const double thickness = widths[c.size - TOOL_SIZE_VERY_FINE];
        GtkWidget* row = newTargetToggle(Action::TOOL_SIZE, c.size, nullptr);
        addClass(row, "mx-prow");
        GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget* name = gtk_label_new(c.name);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0);
        gtk_label_set_width_chars(GTK_LABEL(name), 9);
        GtkWidget* sample = newSample(sampleWidth(thickness), 120, 16);
        gtk_widget_set_hexpand(sample, true);
        GtkWidget* value = gtk_label_new(formatThickness(thickness).c_str());
        gtk_label_set_xalign(GTK_LABEL(value), 1.0);
        gtk_label_set_width_chars(GTK_LABEL(value), 7);
        addClass(value, "mx-numeric");
        gtk_box_pack_start(GTK_BOX(rowBox), name, false, false, 0);
        gtk_box_pack_start(GTK_BOX(rowBox), sample, true, true, 0);
        gtk_box_pack_start(GTK_BOX(rowBox), value, false, false, 0);
        gtk_container_add(GTK_CONTAINER(row), rowBox);
        popdownOnClick(row, popover);
        gtk_box_pack_start(pop, row, false, false, 0);
    }

    if (sections & pill::LINE_STYLE) {
        addPopoverTitle(pop, _("Line style"));
        GtkWidget* seg = newSegment();
        gtk_widget_set_halign(seg, GTK_ALIGN_START);
        struct StyleChoice {
            const char* style;
            int kind;
            const char* tooltip;
        };
        const StyleChoice styles[] = {{"plain", 0, C_("line style", "Solid")},
                                      {"dash", 1, C_("line style", "Dashed")},
                                      {"dashdot", 2, C_("line style", "Dash-dot")},
                                      {"dot", 3, C_("line style", "Dotted")}};
        for (const auto& st: styles) {
            GtkWidget* b = newTargetToggle(Action::TOOL_PEN_LINE_STYLE, st.style, nullptr, {}, st.tooltip);
            gtk_container_add(GTK_CONTAINER(b), xoj::chrome::newLineStyleSample(st.kind));
            gtk_box_pack_start(GTK_BOX(seg), b, false, false, 0);
        }
        gtk_box_pack_start(pop, seg, false, false, 0);
    }

    if (sections & pill::FILL) {
        gtk_box_pack_start(pop, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), false, false, 0);

        GtkWidget* eventBox = gtk_event_box_new();
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        addClass(row, "mx-switch-row");

        GtkWidget* fillLbl = gtk_label_new(_("Fill"));
        gtk_label_set_xalign(GTK_LABEL(fillLbl), 0.0);
        gtk_box_pack_start(GTK_BOX(row), fillLbl, true, true, 0);

        GtkWidget* sw = gtk_switch_new();
        gtk_widget_set_can_focus(sw, true);
        gtk_widget_set_focus_on_click(sw, false);
        gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
        gtk_actionable_set_action_name(GTK_ACTIONABLE(sw), winAction(Action::TOOL_FILL).c_str());
        xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(sw));
        gtk_box_pack_start(GTK_BOX(row), sw, false, false, 0);

        gtk_container_add(GTK_CONTAINER(eventBox), row);
        g_signal_connect_object(
                eventBox, "button-release-event",
                G_CALLBACK(+[](GtkWidget* eb, GdkEventButton* e, gpointer swPtr) -> gboolean {
                    if (e->button == 1 && e->window == gtk_widget_get_window(eb) &&
                        gtk_widget_get_sensitive(GTK_WIDGET(swPtr))) {
                        gtk_switch_set_active(GTK_SWITCH(swPtr), !gtk_switch_get_active(GTK_SWITCH(swPtr)));
                        return true;
                    }
                    return false;
                }),
                sw, GConnectFlags(0));
        gtk_box_pack_start(pop, eventBox, false, false, 0);

        if (group != ToolGroup::SELECTION) {
            // Trigger TOOL_FILL_OPACITY directly instead of GtkActionable so GtkActionHelper does not override
            // sensitivity when the fill switch is off
            GtkWidget* opacity = prepareButton(gtk_button_new(), nullptr);
            addClass(opacity, "mx-prow");
            GtkWidget* opBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget* opLbl = gtk_label_new(_("Fill opacity"));
            gtk_label_set_xalign(GTK_LABEL(opLbl), 0.0);
            gtk_box_pack_start(GTK_BOX(opBox), opLbl, true, true, 0);

            auto formatAlpha = [](int alpha) {
                const int pct = static_cast<int>(std::clamp(alpha, 0, 255) * 100.0 / 255.0 + 0.5);
                return std::to_string(pct) + "%";
            };
            const int initAlpha = group == ToolGroup::HIGHLIGHTER ? th->getHighlighterFill() : th->getPenFill();
            GtkWidget* pctLbl = gtk_label_new(formatAlpha(initAlpha).c_str());
            addClass(pctLbl, "mx-numeric");
            gtk_box_pack_start(GTK_BOX(opBox), pctLbl, false, false, 0);
            gtk_box_pack_start(GTK_BOX(opBox), newIcon("mx-down-symbolic", 16), false, false, 0);
            gtk_container_add(GTK_CONTAINER(opacity), opBox);

            g_signal_connect(
                    opacity, "clicked",
                    G_CALLBACK(+[](GtkButton*, gpointer ctrlPtr) {
                        static_cast<Control*>(ctrlPtr)->getActionDatabase()->fireActivateAction(
                                Action::TOOL_FILL_OPACITY);
                    }),
                    control);

            g_object_bind_property(sw, "active", opacity, "sensitive", G_BINDING_SYNC_CREATE);
            // Refresh the percentage and sensitivity whenever the popover opens
            struct OpacityRefreshData {
                ToolHandler* th;
                bool isHighlighter;
                GtkLabel* pctLbl;
                GtkWidget* sw;
                GtkWidget* opacity;
            };
            g_signal_connect_data(
                    popover, "show",
                    G_CALLBACK(+[](GtkWidget*, gpointer d) {
                        auto* data = static_cast<OpacityRefreshData*>(d);
                        const int alpha =
                                data->isHighlighter ? data->th->getHighlighterFill() : data->th->getPenFill();
                        const int pct = static_cast<int>(std::clamp(alpha, 0, 255) * 100.0 / 255.0 + 0.5);
                        gtk_label_set_text(data->pctLbl, (std::to_string(pct) + "%").c_str());
                        gtk_widget_set_sensitive(data->opacity, gtk_switch_get_active(GTK_SWITCH(data->sw)));
                    }),
                    new OpacityRefreshData{th, group == ToolGroup::HIGHLIGHTER, GTK_LABEL(pctLbl), sw, opacity},
                    +[](gpointer d, GClosure*) { delete static_cast<OpacityRefreshData*>(d); }, GConnectFlags(0));

            popdownOnClick(opacity, popover);
            gtk_box_pack_start(pop, opacity, false, false, 0);
        }
    }
    gtk_widget_show_all(GTK_WIDGET(pop));

    gtk_box_pack_start(box, btn, false, false, 0);
}

void ToolPill::addTextAlignment(GtkBox* box) {
    GtkWidget* seg = newSegment();
    gtk_box_pack_start(GTK_BOX(seg),
                       newTargetToggle(Action::TEXT_ALIGNMENT, TextAlignment::LEFT, nullptr,
                                       "format-justify-left-symbolic", _("Align text to the left")),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(seg),
                       newTargetToggle(Action::TEXT_ALIGNMENT, TextAlignment::CENTER, nullptr,
                                       "format-justify-center-symbolic", _("Center text")),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(seg),
                       newTargetToggle(Action::TEXT_ALIGNMENT, TextAlignment::RIGHT, nullptr,
                                       "format-justify-right-symbolic", _("Align text to the right")),
                       false, false, 0);
    gtk_box_pack_start(box, seg, false, false, 0);

    GtkWidget* justify = prepareButton(gtk_toggle_button_new(), _("Justify text"));
    gtk_button_set_image(GTK_BUTTON(justify), newIcon("format-justify-fill-symbolic"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(justify), winAction(Action::TEXT_JUSTIFY).c_str());
    xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(justify));
    gtk_box_pack_start(box, justify, false, false, 0);
}

void ToolPill::addImageOptions(GtkBox* box) {
    GtkWidget* insertBtn = prepareButton(gtk_button_new(), _("Insert image from file"));
    addClass(insertBtn, "mx-wide");
    gtk_button_set_image(GTK_BUTTON(insertBtn), newIcon("mx-image-add-symbolic"));
    gtk_button_set_label(GTK_BUTTON(insertBtn), _("Insert image…"));
    gtk_button_set_always_show_image(GTK_BUTTON(insertBtn), true);
    g_signal_connect(
            insertBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer ctrlPtr) {
                auto* ctrl = static_cast<Control*>(ctrlPtr);
                const size_t pageNr = ctrl->getCurrentPageNo();
                if (pageNr == npos) {
                    return;
                }
                Document* doc = ctrl->getDocument();
                doc->lock_shared();
                PageRef page = doc->getPage(pageNr);
                doc->unlock_shared();
                if (!page) {
                    return;
                }
                ImageHandler imgHandler(ctrl);
                imgHandler.chooseAndCreateImage([page, ctrl](std::unique_ptr<Image> img) {
                    if (!img) {
                        return;
                    }
                    auto [natW, natH] = img->getNaturalSize();
                    const double maxW = page->getWidth() * 0.75;
                    const double maxH = page->getHeight() * 0.75;
                    double scale = 1.0;
                    if (natW > 0.0 && natH > 0.0) {
                        if (natW > maxW) {
                            scale = std::min(scale, maxW / natW);
                        }
                        if (natH > maxH) {
                            scale = std::min(scale, maxH / natH);
                        }
                    }
                    double cx = page->getWidth() / 2.0;
                    double cy = page->getHeight() / 2.0;
                    if (MainWindow* win = ctrl->getWindow(); win && win->getXournal()) {
                        win->getXournal()->getPasteTarget(cx, cy);
                    }
                    const double w = natW * scale;
                    const double h = natH * scale;
                    const double x = std::clamp(cx - w / 2.0, 0.0, std::max(0.0, page->getWidth() - w));
                    const double y = std::clamp(cy - h / 2.0, 0.0, std::max(0.0, page->getHeight() - h));
                    img->setTransformation({scale, 0.0, 0.0, scale, xoj::util::Point<double>{x, y}});
                    ImageHandler::addImageToDocument(std::move(img), page, ctrl, true);
                });
            }),
            control);
    xoj::util::gtk::setWidgetFollowActionEnabled(
            insertBtn, G_ACTION(control->getActionDatabase()->getAction(Action::SELECT_TOOL).get()));
    gtk_box_pack_start(box, insertBtn, false, false, 0);

    GtkWidget* pasteBtn = newActionButton(gtk_button_new(), Action::PASTE, _("Paste"), "mx-paste-symbolic", nullptr);
    addClass(pasteBtn, "mx-wide");
    gtk_box_pack_start(box, pasteBtn, false, false, 0);

    GtkWidget* hint = gtk_label_new(_("or tap the page"));
    addClass(hint, "mx-pill-hint");
    gtk_box_pack_start(box, hint, false, false, 0);
}

void ToolPill::addSpecialToolChip(GtkBox* box) {
    const char* icon = "mx-sigma-symbolic";
    const char* name = "";
    const char* hintText = nullptr;
    switch (shownTool) {
        case TOOL_LATEX:
            icon = "mx-sigma-symbolic";
            name = _("LaTeX");
            hintText = _("Tap where to insert the formula");
            break;
        case TOOL_LINK:
            icon = "mx-link-symbolic";
            name = _("Link");
            hintText = _("Tap where to insert the link");
            break;
        case TOOL_VERTICAL_SPACE:
            icon = "mx-vspace-symbolic";
            name = _("Vertical space");
            hintText = _("Drag down to make room");
            break;
        case TOOL_SELECT_PDF_TEXT_LINEAR:
        case TOOL_SELECT_PDF_TEXT_RECT:
            icon = "mx-pdftext-symbolic";
            name = _("PDF text selection");
            break;
        default:
            break;
    }

    GtkWidget* chip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    addClass(chip, "mx-tool-chip");
    gtk_widget_set_valign(chip, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(chip), xoj::chrome::newChromeIcon(icon, 16, false), false, false, 0);
    gtk_box_pack_start(GTK_BOX(chip), gtk_label_new(name), false, false, 0);

    GtkWidget* closeBtn = prepareButton(gtk_button_new(), _("Return to default tool"));
    addClass(closeBtn, "mx-chip-close");
    gtk_button_set_image(GTK_BUTTON(closeBtn), xoj::chrome::newChromeIcon("mx-close-symbolic", 14, false));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(closeBtn), winAction(Action::SELECT_DEFAULT_TOOL).c_str());
    xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(closeBtn));
    gtk_box_pack_start(GTK_BOX(chip), closeBtn, false, false, 0);

    gtk_box_pack_start(box, chip, false, false, 0);

    if (hintText) {
        GtkWidget* hint = gtk_label_new(hintText);
        addClass(hint, "mx-pill-hint");
        gtk_box_pack_start(box, hint, false, false, 0);
    }
}

void ToolPill::addPdfTextModes(GtkBox* box) {
    GtkWidget* seg = newSegment();
    gtk_box_pack_start(GTK_BOX(seg),
                       newTargetToggle(Action::SELECT_TOOL, TOOL_SELECT_PDF_TEXT_LINEAR, _("Linear")), false, false,
                       0);
    gtk_box_pack_start(GTK_BOX(seg),
                       newTargetToggle(Action::SELECT_TOOL, TOOL_SELECT_PDF_TEXT_RECT, _("Rectangle")), false, false,
                       0);
    gtk_box_pack_start(box, seg, false, false, 0);
}

void ToolPill::updateThickness() {
    if (!thicknessLabel) {
        return;
    }
    ToolHandler* th = control->getToolHandler();
    const Tool* tool = th->getActiveTool();
    // Legacy shape tools (TOOL_DRAW_RECT..TOOL_DRAW_SPLINE) map to SHAPES without TOOL_CAP_SIZE or a thickness table
    const Tool* sizeTool = (shownGroup == ToolGroup::SHAPES && (!tool || !tool->hasCapability(TOOL_CAP_SIZE))) ?
                                   &th->getTool(TOOL_PEN) :
                                   tool;
    const ToolSize size = (sizeTool && sizeTool->hasCapability(TOOL_CAP_SIZE)) ? sizeTool->getSize() : TOOL_SIZE_NONE;
    if (size < TOOL_SIZE_VERY_FINE || size >= TOOL_SIZE_NONE) {
        gtk_label_set_text(thicknessLabel, "");
        gtk_widget_set_visible(GTK_WIDGET(thicknessLabel), false);
        setSampleWidth(thicknessSample, 1.5);
        return;
    }
    const ToolType tableTool = pill::thicknessTableTool(sizeTool->getToolType(), shownGroup);
    const double thickness = th->getToolThickness(tableTool)[size - TOOL_SIZE_VERY_FINE];
    gtk_label_set_text(thicknessLabel, formatThickness(thickness).c_str());
    gtk_widget_set_visible(GTK_WIDGET(thicknessLabel), true);
    setSampleWidth(thicknessSample, sampleWidth(thickness));
}
