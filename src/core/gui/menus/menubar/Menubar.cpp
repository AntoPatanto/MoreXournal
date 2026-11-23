#include "Menubar.h"

#include <utility>  // for pair

#include "control/Control.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"
#include "util/Assert.h"
#include "util/XojMsgBox.h"
#include "util/i18n.h"
#include "util/raii/GVariantSPtr.h"

#include "PageTypeSubmenu.h"
#include "PluginsSubmenu.h"
#include "RecentDocumentsSubmenu.h"
#include "ToolbarSelectionSubmenu.h"
#include "config-features.h"  // for ENABLE_PLUGINS

constexpr auto MENU_XML_FILE = "mainmenubar.xml";
constexpr auto MENU_ID = "menubar";
constexpr auto UNDO_REDO_SECTION_ID = "sectionUndoRedo";

Menubar::Menubar() = default;
Menubar::~Menubar() noexcept = default;

/// @brief Recursively removes all items with attribute "class" with value classname
static void removeItemsWithClass(GMenu* menu, const char* classname) {
    int N = g_menu_model_get_n_items(G_MENU_MODEL(menu));
    for (int n = 0; n < N; ++n) {
        xoj::util::GVariantSPtr c(
                g_menu_model_get_item_attribute_value(G_MENU_MODEL(menu), n, "class", G_VARIANT_TYPE_STRING),
                xoj::util::adopt);
        if (c && std::string_view(g_variant_get_string(c.get(), nullptr)) == classname) {
            g_menu_remove(menu, n--);
            N--;
        } else {
            xoj::util::GObjectSPtr<GMenuLinkIter> it(g_menu_model_iterate_item_links(G_MENU_MODEL(menu), n),
                                                     xoj::util::adopt);
            while (g_menu_link_iter_next(it.get())) {
                xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_link_iter_get_value(it.get()), xoj::util::adopt);
                if (sub && G_IS_MENU(sub.get())) {
                    removeItemsWithClass(G_MENU(sub.get()), classname);
                }
            }
        }
    }
}

void Menubar::populate(const GladeSearchpath* gladeSearchPath, MainWindow* win) {
    builder.reset(gtk_builder_new(), xoj::util::adopt);

    auto filepath = gladeSearchPath->findFile("", MENU_XML_FILE);
    GError* error = nullptr;

    if (auto u8fp = filepath.u8string(); !gtk_builder_add_from_file(builder.get(), char_cast(u8fp.c_str()), &error)) {
        std::string msg = FS(_F("Error loading menubar XML file (try to load \"{1}\")") % u8fp);

        if (error != nullptr) {
            msg += "\n";
            msg += error->message;
            g_error_free(error);
        }
        XojMsgBox::showErrorToUser(nullptr, msg);
        return;
    }

    menu = G_MENU_MODEL(gtk_builder_get_object(builder.get(), MENU_ID));

    Control* ctrl = win->getControl();

    recentDocumentsSubmenu = std::make_unique<RecentDocumentsSubmenu>(ctrl, GTK_APPLICATION_WINDOW(win->getWindow()));
    toolbarSelectionSubmenu =
            std::make_unique<ToolbarSelectionSubmenu>(win, ctrl->getSettings(), win->getToolMenuHandler());
    pageTypeSubmenu = std::make_unique<PageTypeSubmenu>(ctrl->getPageTypes(), ctrl->getPageBackgroundChangeController(),
                                                        ctrl->getSettings(), GTK_APPLICATION_WINDOW(win->getWindow()));
#ifdef ENABLE_PLUGINS
    pluginsSubmenu =
            std::make_unique<PluginsSubmenu>(ctrl->getPluginController(), GTK_APPLICATION_WINDOW(win->getWindow()));
#endif

    forEachSubmenu([&](auto& subm) { subm.addToMenubar(*this); });

    if (!ctrl->getAudioController()) {
        removeItemsWithClass(G_MENU(menu), "audio");
    }

    undoRedoSection = G_MENU(gtk_builder_get_object(builder.get(), UNDO_REDO_SECTION_ID));

    createSectionedMenu();
}

void Menubar::createSectionedMenu() {
    // Section titles of the main menu. They differ from the menu bar labels and have no mnemonic.
    const std::pair<const char*, std::string> titles[] = {
            {"menuFile", _("Document")}, {"menuEdit", _("Edit")},
            {"menuView", _("View")},     {"menuNavigation", _("Navigation")},
            {"menuJournal", _("Page")},  {"menuTools", _("Tools")},
            {"menuHelp", _("Help")}};

    sectionedMenu.reset(g_menu_new(), xoj::util::adopt);
    const int n = g_menu_model_get_n_items(menu);
    for (int i = 0; i < n; ++i) {
        // Link the existing submenu instead of copying it, so its dynamic parts stay up to date
        xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_model_get_item_link(menu, i, G_MENU_LINK_SUBMENU),
                                               xoj::util::adopt);
        if (!sub) {
            continue;
        }
        std::string title;
        for (const auto& [id, label]: titles) {
            if (G_OBJECT(sub.get()) == gtk_builder_get_object(builder.get(), id)) {
                title = label;
            }
        }
#ifdef ENABLE_PLUGINS
        if (sub.get() == pluginsSubmenu->getModel()) {
            title = _("Plugins");
        }
#endif
        if (title.empty()) {
            // Unknown menu: use its menu bar label without the mnemonic marker
            gchar* label = nullptr;
            if (g_menu_model_get_item_attribute(menu, i, G_MENU_ATTRIBUTE_LABEL, "s", &label)) {
                for (const char* c = label; *c; ++c) {
                    if (*c != '_') {
                        title += *c;
                    }
                }
                g_free(label);
            }
        }
        g_menu_append_section(sectionedMenu.get(), title.c_str(), sub.get());
    }
}

void Menubar::setUndoDescription(const std::string& description) {
    xoj_assert(undoRedoSection);
    g_menu_remove(undoRedoSection, 0);
    g_menu_prepend(undoRedoSection, description.c_str(), "win.undo");
}

void Menubar::setRedoDescription(const std::string& description) {
    xoj_assert(undoRedoSection);
    g_menu_remove(undoRedoSection, 1);
    g_menu_append(undoRedoSection, description.c_str(), "win.redo");
}

void Menubar::setDisabled(bool disabled) {
    forEachSubmenu([&](auto& subm) { subm.setDisabled(disabled); });
}
