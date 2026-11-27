/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <cstdint>
#include <iterator>  // for size
#include <set>
#include <string>
#include <type_traits>  // for is_same_v
#include <utility>
#include <vector>

#include <config-test.h>
#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "control/actions/ActionProperties.h"
#include "gui/menus/menubar/Menubar.h"
#include "util/EnumIndexedArray.h"
#include "util/StringUtils.h"
#include "util/raii/GObjectSPtr.h"
#include "util/raii/GVariantSPtr.h"
#include "util/safe_casts.h"  // for to_underlying Todo(cpp20) replace with <utility>

#include "filesystem.h"

/**
 * Test if the GActions referred to in MENU_XML_FILE have a counterpart in enums/Action.h, and checks if the (optional)
 * action target value has the same type as set in control/actions/ActionProperties.h
 */

constexpr auto MENU_XML_FILE = "mainmenubar.xml";
constexpr auto MENU_ID = "menubar";

namespace {
template <Action a, class U = void>
struct helper {
    static void setup(EnumIndexedArray<const GVariantType*, Action>& expectedTypes) { expectedTypes[a] = nullptr; };
};
template <Action a>
struct helper<a, std::void_t<typename ActionProperties<a>::parameter_type>> {
    static void setup(EnumIndexedArray<const GVariantType*, Action>& expectedTypes) {
        expectedTypes[a] = gVariantType<typename ActionProperties<a>::parameter_type>();
    }
};

template <size_t... As>
static auto setupImpl(std::index_sequence<As...>) {
    EnumIndexedArray<const GVariantType*, Action> expectedTypes;
    ((helper<static_cast<Action>(As)>::setup(expectedTypes)), ...);
    return expectedTypes;
}

static const auto expectedTypes = setupImpl(std::make_index_sequence<xoj::to_underlying(Action::ENUMERATOR_COUNT)>());

void exploreMenu(GMenuModel* m) {
    int n = g_menu_model_get_n_items(m);
    for (int i = 0; i < n; i++) {
        xoj::util::GVariantSPtr val(g_menu_model_get_item_attribute_value(m, i, "action", nullptr), xoj::util::adopt);

        if (val) {
            std::string value = g_variant_get_string(val.get(), nullptr);
            auto pos = value.find('.');
            EXPECT_TRUE(pos != std::string::npos);
            EXPECT_TRUE(pos != 0);
            std::string prefix = value.substr(0, pos);
            std::string action = value.substr(pos + 1);
            auto opta = Action_fromString(action);
            EXPECT_TRUE(opta) << "Invalid action name in menu: \"" << action << "\"";
            if (!opta) {
                continue;
            }
            auto a = opta.value();

            xoj::util::GVariantSPtr target(g_menu_model_get_item_attribute_value(m, i, "target", nullptr),
                                           xoj::util::adopt);
            if (target) {
                EXPECT_TRUE(g_variant_type_equal(g_variant_get_type(target.get()), expectedTypes[a]));
            } else {
                EXPECT_TRUE(expectedTypes[a] == nullptr);
            }
        }

        {
            xoj::util::GObjectSPtr<GMenuLinkIter> it(g_menu_model_iterate_item_links(m, i), xoj::util::adopt);
            while (g_menu_link_iter_next(it.get())) {
                xoj::util::GObjectSPtr<GMenuModel> subm(g_menu_link_iter_get_value(it.get()), xoj::util::adopt);
                exploreMenu(subm.get());
            }
        }
    }
}
};  // namespace

TEST(ActionDatabaseTest, testActionTargetMatch) {
    xoj::util::GObjectSPtr<GtkBuilder> builder(gtk_builder_new(), xoj::util::adopt);

    GError* error = nullptr;
    auto filepath = fs::path(GET_UI_FOLDER) / MENU_XML_FILE;

    if (!gtk_builder_add_from_file(builder.get(), char_cast(filepath.u8string().c_str()), &error)) {
        std::string msg = "Error loading menubar XML file ";
        msg.append(char_cast(filepath.u8string()));

        if (error != nullptr) {
            msg += "\n";
            msg += error->message;
            g_error_free(error);
        }
        FAIL() << msg;
        return;
    }

    GMenuModel* menu = G_MENU_MODEL(gtk_builder_get_object(builder.get(), MENU_ID));
    ASSERT_TRUE(menu);

    exploreMenu(menu);
}

TEST(ActionDatabaseTest, testHoldToShapeActionName) {
    // Every action needs a name: the name map is edited by hand and must stay in step with the enum
    EXPECT_EQ(std::size(ACTION_NAMES), xoj::to_underlying(Action::ENUMERATOR_COUNT));
    EXPECT_STREQ(Action_toString(Action::HOLD_TO_SHAPE), "hold-to-shape");
    EXPECT_EQ(Action_fromString("hold-to-shape"), Action::HOLD_TO_SHAPE);
    EXPECT_EQ(Action_fromString("position-highlighting"), Action::POSITION_HIGHLIGHTING);
    EXPECT_TRUE((std::is_same_v<ActionProperties<Action::HOLD_TO_SHAPE>::state_type, bool>));
}

namespace {
auto loadMenubarBuilder() -> xoj::util::GObjectSPtr<GtkBuilder> {
    xoj::util::GObjectSPtr<GtkBuilder> builder(gtk_builder_new(), xoj::util::adopt);
    GError* error = nullptr;
    auto filepath = fs::path(GET_UI_FOLDER) / MENU_XML_FILE;
    if (!gtk_builder_add_from_file(builder.get(), char_cast(filepath.u8string().c_str()), &error)) {
        std::string msg = "Error loading menubar XML file ";
        msg.append(char_cast(filepath.u8string()));
        if (error != nullptr) {
            msg += "\n";
            msg += error->message;
            g_error_free(error);
        }
        ADD_FAILURE() << msg;
        return nullptr;
    }
    return builder;
}

std::string getItemStringAttr(GMenuModel* m, int index, const char* attr) {
    xoj::util::GVariantSPtr val(g_menu_model_get_item_attribute_value(m, index, attr, G_VARIANT_TYPE_STRING),
                                xoj::util::adopt);
    return val ? std::string(g_variant_get_string(val.get(), nullptr)) : std::string();
}

void collectMenuActions(GMenuModel* m, std::set<std::string>& actions,
                        std::set<std::pair<std::string, uint64_t>>& uint64Targets) {
    const int n = g_menu_model_get_n_items(m);
    for (int i = 0; i < n; ++i) {
        std::string action = getItemStringAttr(m, i, "action");
        if (!action.empty()) {
            actions.insert(action);
            xoj::util::GVariantSPtr target(g_menu_model_get_item_attribute_value(m, i, "target", G_VARIANT_TYPE_UINT64),
                                           xoj::util::adopt);
            if (target) {
                uint64Targets.emplace(action, g_variant_get_uint64(target.get()));
            }
        }
        xoj::util::GObjectSPtr<GMenuLinkIter> it(g_menu_model_iterate_item_links(m, i), xoj::util::adopt);
        while (g_menu_link_iter_next(it.get())) {
            xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_link_iter_get_value(it.get()), xoj::util::adopt);
            collectMenuActions(sub.get(), actions, uint64Targets);
        }
    }
}

bool findSubmenuWithClass(GMenuModel* m, const std::string& label, const std::string& expectedClass) {
    const int n = g_menu_model_get_n_items(m);
    for (int i = 0; i < n; ++i) {
        if (getItemStringAttr(m, i, G_MENU_ATTRIBUTE_LABEL) == label &&
            getItemStringAttr(m, i, "class") == expectedClass) {
            xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_model_get_item_link(m, i, G_MENU_LINK_SUBMENU),
                                                   xoj::util::adopt);
            if (sub) {
                return true;
            }
        }
        xoj::util::GObjectSPtr<GMenuLinkIter> it(g_menu_model_iterate_item_links(m, i), xoj::util::adopt);
        while (g_menu_link_iter_next(it.get())) {
            xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_link_iter_get_value(it.get()), xoj::util::adopt);
            if (findSubmenuWithClass(sub.get(), label, expectedClass)) {
                return true;
            }
        }
    }
    return false;
}

template <Action a>
bool actionHasAccel(const char* expected) {
    for (const char* accel: ActionProperties<a>::accelerators) {
        if (accel != nullptr && std::string(accel) == expected) {
            return true;
        }
    }
    return false;
}
}  // namespace

TEST(ActionDatabaseTest, testMenubarCategoriesAndRemovedItems) {
    auto builder = loadMenubarBuilder();
    ASSERT_TRUE(builder);

    GMenuModel* menubar = G_MENU_MODEL(gtk_builder_get_object(builder.get(), MENU_ID));
    ASSERT_TRUE(menubar);

    // Classic menu bar top-level categories
    std::vector<std::string> topLabels;
    for (int i = 0; i < g_menu_model_get_n_items(menubar); ++i) {
        topLabels.push_back(getItemStringAttr(menubar, i, G_MENU_ATTRIBUTE_LABEL));
    }
    const std::vector<std::string> expectedTop = {"_File", "_Edit", "_Page", "_Layers", "_View", "_Tools", "_Help"};
    EXPECT_EQ(topLabels, expectedTop);

    // Dynamic submenu IDs expected by *Submenu classes
    EXPECT_TRUE(G_IS_MENU(gtk_builder_get_object(builder.get(), "menuFileRecent")));
    EXPECT_EQ(gtk_builder_get_object(builder.get(), "menuViewToolbar"), nullptr);
    EXPECT_TRUE(G_IS_MENU(gtk_builder_get_object(builder.get(), "menuJournalPaperBackground")));
    EXPECT_EQ(gtk_builder_get_object(builder.get(), "sectionUndoRedo"), nullptr);

    std::set<std::string> actions;
    std::set<std::pair<std::string, uint64_t>> uint64Targets;
    collectMenuActions(menubar, actions, uint64Targets);

    // Actions removed from the menus because they live on the workspace or are debug-only
    for (const char* removed: {"win.undo",
                               "win.redo",
                               "win.delete",
                               "win.show-sidebar",
                               "win.zoom-in",
                               "win.zoom-out",
                               "win.zoom-100",
                               "win.zoom-fit",
                               "win.goto-previous",
                               "win.goto-next",
                               "win.goto-page",
                               "win.new-page-after",
                               "win.layer-new-above-current",
                               "win.layer-new-below-current",
                               "win.tool-draw-rectangle",
                               "win.tool-draw-ellipse",
                               "win.tool-draw-arrow",
                               "win.tool-draw-double-arrow",
                               "win.tool-draw-coordinate-system",
                               "win.tool-draw-line",
                               "win.tool-draw-spline",
                               "win.tool-pen-size",
                               "win.tool-pen-line-style",
                               "win.tool-pen-fill",
                               "win.tool-pen-fill-opacity",
                               "win.tool-eraser-type",
                               "win.tool-highlighter-size",
                               "win.tool-highlighter-fill",
                               "win.tool-highlighter-fill-opacity",
                               "win.select-font",
                               "win.manage-toolbar",
                               "win.customize-toolbar",
                               "win.demo"}) {
        EXPECT_EQ(actions.count(removed), 0U) << "Removed action still present in menu: " << removed;
    }

    // Removed select-tool targets
    for (ToolType removedTool: {TOOL_PEN, TOOL_ERASER, TOOL_HIGHLIGHTER, TOOL_TEXT, TOOL_IMAGE, TOOL_SELECT_RECT,
                                TOOL_SELECT_REGION, TOOL_SELECT_MULTILAYER_RECT, TOOL_SELECT_MULTILAYER_REGION,
                                TOOL_SELECT_OBJECT, TOOL_PLAY_OBJECT, TOOL_HAND}) {
        EXPECT_EQ(uint64Targets.count({"win.select-tool", static_cast<uint64_t>(removedTool)}), 0U)
                << "Removed tool target still present in menu: " << removedTool;
    }

    // Kept select-tool targets
    for (ToolType keptTool:
         {TOOL_SELECT_PDF_TEXT_LINEAR, TOOL_SELECT_PDF_TEXT_RECT, TOOL_LINK, TOOL_LATEX, TOOL_VERTICAL_SPACE}) {
        EXPECT_EQ(uint64Targets.count({"win.select-tool", static_cast<uint64_t>(keptTool)}), 1U)
                << "Expected tool target missing from menu: " << keptTool;
    }

    // Eraser sizes: keep only very fine and very thick
    EXPECT_EQ(uint64Targets.count({"win.tool-eraser-size", static_cast<uint64_t>(TOOL_SIZE_VERY_FINE)}), 1U);
    EXPECT_EQ(uint64Targets.count({"win.tool-eraser-size", static_cast<uint64_t>(TOOL_SIZE_VERY_THICK)}), 1U);
    for (ToolSize removedSize: {TOOL_SIZE_FINE, TOOL_SIZE_MEDIUM, TOOL_SIZE_THICK}) {
        EXPECT_EQ(uint64Targets.count({"win.tool-eraser-size", static_cast<uint64_t>(removedSize)}), 0U)
                << "Removed eraser size still present in menu: " << removedSize;
    }

    // Fullscreen stays in View (so macOS View menu exposes Ctrl+Cmd+F)
    EXPECT_EQ(actions.count("win.fullscreen"), 1U);

    // Shape Recognizer and Automatic shapes stay in Tools
    EXPECT_EQ(actions.count("win.tool-draw-shape-recognizer"), 1U);
    EXPECT_EQ(actions.count("win.hold-to-shape"), 1U);

    // Audio submenu carries class="audio" so disabling audio removes the whole submenu cleanly
    EXPECT_TRUE(findSubmenuWithClass(menubar, "Audio", "audio"));
}

TEST(ActionDatabaseTest, testMainMenuStructureAndLiveLinks) {
    auto builder = loadMenubarBuilder();
    ASSERT_TRUE(builder);

    // Without plugins
    auto mainMenuNoPlugins = Menubar::buildMainMenu(builder.get(), nullptr);
    ASSERT_TRUE(mainMenuNoPlugins);
    GMenuModel* model = G_MENU_MODEL(mainMenuNoPlugins.get());
    ASSERT_EQ(g_menu_model_get_n_items(model), 6);

    auto sectionAt = [](GMenuModel* m, int idx) {
        return xoj::util::GObjectSPtr<GMenuModel>(g_menu_model_get_item_link(m, idx, G_MENU_LINK_SECTION),
                                                  xoj::util::adopt);
    };

    auto docSec = sectionAt(model, 0);
    auto exportSec = sectionAt(model, 1);
    auto printSec = sectionAt(model, 2);
    auto catSec = sectionAt(model, 3);
    auto appSec = sectionAt(model, 4);
    auto quitSec = sectionAt(model, 5);
    ASSERT_TRUE(docSec && exportSec && printSec && catSec && appSec && quitSec);

    EXPECT_EQ(g_menu_model_get_n_items(docSec.get()), 6);
    EXPECT_EQ(g_menu_model_get_n_items(exportSec.get()), 2);
    EXPECT_EQ(g_menu_model_get_n_items(printSec.get()), 1);
    EXPECT_EQ(g_menu_model_get_n_items(catSec.get()), 5);
    EXPECT_EQ(g_menu_model_get_n_items(appSec.get()), 2);
    EXPECT_EQ(g_menu_model_get_n_items(quitSec.get()), 1);

    // Edit submenu in main menu must not duplicate Preferences or include Toolbars
    xoj::util::GObjectSPtr<GMenuModel> editSub(g_menu_model_get_item_link(catSec.get(), 0, G_MENU_LINK_SUBMENU),
                                               xoj::util::adopt);
    ASSERT_TRUE(editSub);
    std::set<std::string> editActions;
    std::set<std::pair<std::string, uint64_t>> editTargets;
    collectMenuActions(editSub.get(), editActions, editTargets);
    EXPECT_EQ(editActions.count("win.cut"), 1U);
    EXPECT_EQ(editActions.count("win.grid-snapping"), 1U);
    EXPECT_EQ(editActions.count("app.preferences"), 0U);
    EXPECT_EQ(editActions.count("win.manage-toolbar"), 0U);
    EXPECT_EQ(editActions.count("win.customize-toolbar"), 0U);

    // Preferences and Help live in appSec (Toolbars submenu is removed)
    std::set<std::string> appActions;
    std::set<std::pair<std::string, uint64_t>> appTargets;
    collectMenuActions(appSec.get(), appActions, appTargets);
    EXPECT_EQ(appActions.count("app.preferences"), 1U);
    EXPECT_EQ(appActions.count("win.manage-toolbar"), 0U);
    EXPECT_EQ(appActions.count("win.customize-toolbar"), 0U);
    EXPECT_EQ(appActions.count("win.help"), 1U);
    EXPECT_EQ(appActions.count("app.about"), 1U);

    // With plugins + live link verification
    xoj::util::GObjectSPtr<GMenu> dummyPlugins(g_menu_new(), xoj::util::adopt);
    auto mainMenuWithPlugins = Menubar::buildMainMenu(builder.get(), G_MENU_MODEL(dummyPlugins.get()));
    ASSERT_TRUE(mainMenuWithPlugins);
    auto catSecWithPlugins = sectionAt(G_MENU_MODEL(mainMenuWithPlugins.get()), 3);
    ASSERT_TRUE(catSecWithPlugins);
    EXPECT_EQ(g_menu_model_get_n_items(catSecWithPlugins.get()), 6);

    // Mutate dynamic submenus AFTER building the main menu and verify changes show through live links
    g_menu_append(G_MENU(gtk_builder_get_object(builder.get(), "menuFileRecent")), "test.xopp", "win.open-file-at");
    g_menu_append(G_MENU(gtk_builder_get_object(builder.get(), "menuJournalPaperBackground")), "Plain",
                  "win.menu.pick-page-type");
    g_menu_append(dummyPlugins.get(), "Plugin Manager", "win.plugin-manager");

    std::set<std::string> liveActions;
    std::set<std::pair<std::string, uint64_t>> liveTargets;
    collectMenuActions(G_MENU_MODEL(mainMenuWithPlugins.get()), liveActions, liveTargets);
    EXPECT_EQ(liveActions.count("win.open-file-at"), 1U);
    EXPECT_EQ(liveActions.count("win.menu.pick-page-type"), 1U);
    EXPECT_EQ(liveActions.count("win.plugin-manager"), 1U);
}

TEST(ActionDatabaseTest, testMovedAcceleratorsDefined) {
    EXPECT_TRUE(actionHasAccel<Action::DELETE>("Delete"));
    EXPECT_TRUE(actionHasAccel<Action::FULLSCREEN>("F11"));
#ifdef __APPLE__
    EXPECT_TRUE(actionHasAccel<Action::FULLSCREEN>("<Primary><Control>f"));
#endif
    EXPECT_TRUE(actionHasAccel<Action::SHOW_SIDEBAR>("F12"));
    EXPECT_TRUE(actionHasAccel<Action::GOTO_PREVIOUS>("<Ctrl>Page_Up"));
    EXPECT_TRUE(actionHasAccel<Action::GOTO_PAGE>("<Ctrl>g"));
    EXPECT_TRUE(actionHasAccel<Action::GOTO_NEXT>("<Ctrl>Page_Down"));
    EXPECT_TRUE(actionHasAccel<Action::NEW_PAGE_AFTER>("<Ctrl>d"));
    EXPECT_TRUE(actionHasAccel<Action::LAYER_NEW_ABOVE_CURRENT>("<Ctrl>l"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_RECTANGLE>("<Ctrl>2"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_ELLIPSE>("<Ctrl>3"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_ARROW>("<Ctrl>4"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_DOUBLE_ARROW>("<Ctrl>5"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_COORDINATE_SYSTEM>("<Ctrl>6"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_LINE>("<Ctrl>7"));
    EXPECT_TRUE(actionHasAccel<Action::TOOL_DRAW_SPLINE>("<Ctrl>8"));
    EXPECT_TRUE(actionHasAccel<Action::SELECT_FONT>("<Ctrl><Shift>f"));

    const std::pair<ToolType, std::string> expectedToolAccels[] = {
            {TOOL_PEN, "<Ctrl><Shift>p"},           {TOOL_ERASER, "<Ctrl><Shift>e"},
            {TOOL_HIGHLIGHTER, "<Ctrl><Shift>h"},   {TOOL_TEXT, "<Ctrl><Shift>t"},
            {TOOL_IMAGE, "<Ctrl><Shift>i"},         {TOOL_SELECT_RECT, "<Ctrl><Shift>r"},
            {TOOL_SELECT_REGION, "<Ctrl><Shift>g"}, {TOOL_SELECT_OBJECT, "<Ctrl><Shift>o"},
            {TOOL_HAND, "<Ctrl><Shift>a"},
    };
    const auto& actual = ActionProperties<Action::SELECT_TOOL>::targetedAccelerators;
    EXPECT_EQ(std::size(actual), std::size(expectedToolAccels));
    for (const auto& [tool, accel]: expectedToolAccels) {
        auto it = std::find_if(std::begin(actual), std::end(actual),
                               [&](const auto& entry) { return entry.first == tool && entry.second == accel; });
        EXPECT_NE(it, std::end(actual)) << "Missing targeted accelerator for tool " << tool << ": " << accel;
    }
}
