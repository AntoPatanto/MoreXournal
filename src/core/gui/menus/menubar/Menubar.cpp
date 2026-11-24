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

    createSectionedMenu();
}

auto Menubar::buildMainMenu(GtkBuilder* builder, GMenuModel* pluginsModel) -> xoj::util::GObjectSPtr<GMenu> {
    auto getModelById = [builder](const char* id) -> GMenuModel* {
        GObject* obj = gtk_builder_get_object(builder, id);
        if (obj == nullptr) {
            g_warning("Menubar::buildMainMenu: Could not find menu object: \"%s\"", id);
            return nullptr;
        }
        return G_MENU_MODEL(obj);
    };

    xoj::util::GObjectSPtr<GMenu> mainMenu(g_menu_new(), xoj::util::adopt);

    // Inline document items: link the File sections so dynamic entries (recent documents) stay live
    for (const char* id: {"sectionFileDocument", "sectionFileExport", "sectionFilePrint"}) {
        if (GMenuModel* sec = getModelById(id)) {
            g_menu_append_section(mainMenu.get(), nullptr, sec);
        }
    }

    // Category submenus
    xoj::util::GObjectSPtr<GMenu> categoriesSection(g_menu_new(), xoj::util::adopt);

    // Edit omits Preferences, which sits in the top-level section below
    xoj::util::GObjectSPtr<GMenu> editSubmenu(g_menu_new(), xoj::util::adopt);
    for (const char* id:
         {"sectionEditClipboard", "sectionEditSelectFind", "sectionEditSelection", "sectionEditSnapping"}) {
        if (GMenuModel* sec = getModelById(id)) {
            g_menu_append_section(editSubmenu.get(), nullptr, sec);
        }
    }
    g_menu_append_submenu(categoriesSection.get(), _("Edit"), G_MENU_MODEL(editSubmenu.get()));

    const std::pair<const char*, std::string> categorySubmenus[] = {
            {"menuPage", _("Page")},
            {"menuLayers", _("Layers")},
            {"menuView", _("View")},
            {"menuTools", _("Tools")},
    };
    for (const auto& [id, label]: categorySubmenus) {
        if (GMenuModel* sub = getModelById(id)) {
            g_menu_append_submenu(categoriesSection.get(), label.c_str(), sub);
        }
    }
    if (pluginsModel != nullptr) {
        g_menu_append_submenu(categoriesSection.get(), _("Plugins"), pluginsModel);
    }
    g_menu_append_section(mainMenu.get(), nullptr, G_MENU_MODEL(categoriesSection.get()));

    // Preferences and Help in one section
    xoj::util::GObjectSPtr<GMenu> appSection(g_menu_new(), xoj::util::adopt);
    if (GMenuModel* editPrefs = getModelById("sectionEditPreferences")) {
        const int n = g_menu_model_get_n_items(editPrefs);
        for (int i = 0; i < n; ++i) {
            xoj::util::GObjectSPtr<GMenuItem> item(g_menu_item_new_from_model(editPrefs, i), xoj::util::adopt);
            g_menu_append_item(appSection.get(), item.get());
        }
    }
    if (GMenuModel* help = getModelById("menuHelp")) {
        g_menu_append_submenu(appSection.get(), _("Help"), help);
    }
    g_menu_append_section(mainMenu.get(), nullptr, G_MENU_MODEL(appSection.get()));

    if (GMenuModel* quitSec = getModelById("sectionFileQuit")) {
        g_menu_append_section(mainMenu.get(), nullptr, quitSec);
    }

    return mainMenu;
}

void Menubar::createSectionedMenu() {
#ifdef ENABLE_PLUGINS
    GMenuModel* pluginsModel = pluginsSubmenu ? pluginsSubmenu->getModel() : nullptr;
#else
    GMenuModel* pluginsModel = nullptr;
#endif
    sectionedMenu = buildMainMenu(builder.get(), pluginsModel);
}

void Menubar::setDisabled(bool disabled) {
    forEachSubmenu([&](auto& subm) { subm.setDisabled(disabled); });
}
