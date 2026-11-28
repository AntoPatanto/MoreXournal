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
#ifdef __APPLE__
    // macOS uses the system menu bar; omit the in-window menubar toggle from all menus
    removeItemsWithClass(G_MENU(menu), "in-window-menubar");
#endif

    createSectionedMenu();
}

namespace {
constexpr std::pair<std::string_view, const char*> MAIN_MENU_ACTION_ICONS[] = {
        {"win.new-file", "mx-menu-new-symbolic"},
        {"win.open", "mx-menu-open-symbolic"},
        {"win.annotate-pdf", "mx-menu-annotate-pdf-symbolic"},
        {"win.save", "mx-menu-save-symbolic"},
        {"win.save-as", "mx-menu-save-as-symbolic"},
        {"win.export-as-pdf", "mx-menu-export-pdf-symbolic"},
        {"win.export-as", "mx-menu-export-symbolic"},
        {"win.print", "mx-menu-print-symbolic"},
        {"app.preferences", "mx-menu-preferences-symbolic"},
        {"app.quit", "mx-menu-quit-symbolic"},
};

const char* iconNameForMainMenuAction(std::string_view action) {
    for (const auto& [name, icon]: MAIN_MENU_ACTION_ICONS) {
        if (name == action) {
            return icon;
        }
    }
    return nullptr;
}

void setMenuItemIcon(GMenuItem* item, const char* iconName) {
    if (iconName == nullptr) {
        return;
    }
    xoj::util::GObjectSPtr<GIcon> icon(g_themed_icon_new(iconName), xoj::util::adopt);
    g_menu_item_set_icon(item, icon.get());
}

void appendSubmenuWithIcon(GMenu* target, const char* label, GMenuModel* submenu, const char* iconName) {
    xoj::util::GObjectSPtr<GMenuItem> item(g_menu_item_new_submenu(label, submenu), xoj::util::adopt);
    setMenuItemIcon(item.get(), iconName);
    g_menu_append_item(target, item.get());
}

/// Copy a top-level section's items into a new GMenu and attach icons, leaving the shared
/// menubar section untouched while preserving live submenu links (such as recent documents).
auto copySectionWithIcons(GMenuModel* src, GMenuModel* recentModel) -> xoj::util::GObjectSPtr<GMenu> {
    xoj::util::GObjectSPtr<GMenu> copy(g_menu_new(), xoj::util::adopt);
    const int n = g_menu_model_get_n_items(src);
    for (int i = 0; i < n; ++i) {
        xoj::util::GObjectSPtr<GMenuItem> item(g_menu_item_new_from_model(src, i), xoj::util::adopt);
        const char* iconName = nullptr;
        xoj::util::GVariantSPtr actionVar(
                g_menu_model_get_item_attribute_value(src, i, G_MENU_ATTRIBUTE_ACTION, G_VARIANT_TYPE_STRING),
                xoj::util::adopt);
        if (actionVar) {
            iconName = iconNameForMainMenuAction(g_variant_get_string(actionVar.get(), nullptr));
        } else if (recentModel != nullptr) {
            xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_model_get_item_link(src, i, G_MENU_LINK_SUBMENU),
                                                   xoj::util::adopt);
            if (sub.get() == recentModel) {
                iconName = "mx-menu-recent-symbolic";
            }
        }
        setMenuItemIcon(item.get(), iconName);
        g_menu_append_item(copy.get(), item.get());
    }
    return copy;
}
}  // namespace

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
    GMenuModel* recentModel = getModelById("menuFileRecent");

    // Inline document items: copy top-level entries to attach icons without altering the classic menu bar;
    // g_menu_item_new_from_model preserves the live submenu link to menuFileRecent.
    for (const char* id: {"sectionFileDocument", "sectionFileExport", "sectionFilePrint"}) {
        if (GMenuModel* sec = getModelById(id)) {
            auto secCopy = copySectionWithIcons(sec, recentModel);
            g_menu_append_section(mainMenu.get(), nullptr, G_MENU_MODEL(secCopy.get()));
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
    appendSubmenuWithIcon(categoriesSection.get(), _("Edit"), G_MENU_MODEL(editSubmenu.get()), "mx-menu-edit-symbolic");

    struct CategorySubmenu {
        const char* id;
        std::string label;
        const char* icon;
    };
    const CategorySubmenu categorySubmenus[] = {
            {"menuPage", _("Page"), "mx-menu-page-symbolic"},
            {"menuLayers", _("Layers"), "mx-menu-layers-symbolic"},
            {"menuView", _("View"), "mx-menu-view-symbolic"},
            {"menuTools", _("Tools"), "mx-menu-tools-symbolic"},
    };
    for (const auto& entry: categorySubmenus) {
        if (GMenuModel* sub = getModelById(entry.id)) {
            appendSubmenuWithIcon(categoriesSection.get(), entry.label.c_str(), sub, entry.icon);
        }
    }
    if (pluginsModel != nullptr) {
        appendSubmenuWithIcon(categoriesSection.get(), _("Plugins"), pluginsModel, "mx-menu-plugins-symbolic");
    }
    g_menu_append_section(mainMenu.get(), nullptr, G_MENU_MODEL(categoriesSection.get()));

    // Preferences and Help in one section
    xoj::util::GObjectSPtr<GMenu> appSection(g_menu_new(), xoj::util::adopt);
    if (GMenuModel* editPrefs = getModelById("sectionEditPreferences")) {
        const int n = g_menu_model_get_n_items(editPrefs);
        for (int i = 0; i < n; ++i) {
            xoj::util::GObjectSPtr<GMenuItem> item(g_menu_item_new_from_model(editPrefs, i), xoj::util::adopt);
            xoj::util::GVariantSPtr actionVar(
                    g_menu_model_get_item_attribute_value(editPrefs, i, G_MENU_ATTRIBUTE_ACTION, G_VARIANT_TYPE_STRING),
                    xoj::util::adopt);
            if (actionVar) {
                setMenuItemIcon(item.get(), iconNameForMainMenuAction(g_variant_get_string(actionVar.get(), nullptr)));
            }
            g_menu_append_item(appSection.get(), item.get());
        }
    }
    if (GMenuModel* help = getModelById("menuHelp")) {
        appendSubmenuWithIcon(appSection.get(), _("Help"), help, "mx-menu-help-symbolic");
    }
    g_menu_append_section(mainMenu.get(), nullptr, G_MENU_MODEL(appSection.get()));

    if (GMenuModel* quitSec = getModelById("sectionFileQuit")) {
        auto quitCopy = copySectionWithIcons(quitSec, recentModel);
        g_menu_append_section(mainMenu.get(), nullptr, G_MENU_MODEL(quitCopy.get()));
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
