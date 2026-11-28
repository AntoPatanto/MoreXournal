#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib-object.h>

#include "control/AudioController.h"
#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/settings/SettingsEnums.h"
#include "control/tools/StrokeStabilizerEnum.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "gui/widgets/ZoomCallib.h"
#include "util/Color.h"
#include "util/GtkUtil.h"
#include "util/PathUtil.h"
#include "util/Util.h"
#include "util/XojMsgBox.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"
#include "util/raii/CStringWrapper.h"
#include "util/safe_casts.h"
#include "util/utf8_view.h"

#include "ButtonConfigGui.h"
#include "DeviceTestingArea.h"
#include "LanguageConfigGui.h"
#include "LatexSettingsPanel.h"
#include "McpSettingsPanel.h"
#include "ShapeSettingsPanel.h"
#include "XojOpenDlg.h"
#include "filesystem.h"

class GladeSearchpath;

using std::string;
using std::vector;

constexpr auto UI_FILE = "settings.glade";
constexpr auto UI_DIALOG_NAME = "settingsDialog";

namespace {

void addCssClass(GtkWidget* widget, const char* cssClass) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cssClass);
}

void removeCssClass(GtkWidget* widget, const char* cssClass) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget), cssClass);
}

void capSettingsDialogDefaultSize(GtkWindow* dialog, GtkWindow* parent) {
    constexpr int DEFAULT_WIDTH = 1000;
    constexpr int DEFAULT_HEIGHT = 700;
    constexpr int WORKAREA_MARGIN_X = 40;
    constexpr int WORKAREA_MARGIN_Y = 80;
    constexpr int MIN_CAPPED_WIDTH = 640;
    constexpr int MIN_CAPPED_HEIGHT = 480;

    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    if (parent != nullptr) {
        if (GdkWindow* parentWin = gtk_widget_get_window(GTK_WIDGET(parent))) {
            GdkDisplay* display = gdk_window_get_display(parentWin);
            GdkMonitor* monitor = display ? gdk_display_get_monitor_at_window(display, parentWin) : nullptr;
            if (monitor != nullptr) {
                GdkRectangle workarea{};
                gdk_monitor_get_workarea(monitor, &workarea);
                if (workarea.width > 0 && workarea.height > 0) {
                    width = std::min(width, std::max(MIN_CAPPED_WIDTH, workarea.width - WORKAREA_MARGIN_X));
                    height = std::min(height, std::max(MIN_CAPPED_HEIGHT, workarea.height - WORKAREA_MARGIN_Y));
                }
            }
        }
    }
    gtk_window_set_default_size(dialog, width, height);
}

void updateCenteredMargins(GtkWidget* scroller, GtkAllocation* alloc, gpointer pageBoxPtr) {
    auto* pageBox = GTK_WIDGET(pageBoxPtr);
    constexpr int MAX_CONTENT_WIDTH = 640;
    constexpr int MIN_SIDE_MARGIN = 24;
    const int sideMargin = std::max(MIN_SIDE_MARGIN, (alloc->width - MAX_CONTENT_WIDTH) / 2);
    if (gtk_widget_get_margin_start(pageBox) != sideMargin) {
        gtk_widget_set_margin_start(pageBox, sideMargin);
        gtk_widget_set_margin_end(pageBox, sideMargin);
    }
}

void navHeaderFunc(GtkListBoxRow* row, GtkListBoxRow* /*before*/, gpointer /*userData*/) {
    const auto* pageId = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "mx-page-id"));
    if (!pageId) {
        gtk_list_box_row_set_header(row, nullptr);
        return;
    }
    std::string_view id(pageId);
    if (id == "pen" || id == "tools") {
        GtkWidget* lbl = gtk_label_new(id == "pen" ? _("Input") : _("Features"));
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
        addCssClass(lbl, "mx-prefs-nav-section");
        gtk_widget_show(lbl);
        gtk_list_box_row_set_header(row, lbl);
    } else if (id == "advanced") {
        GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        addCssClass(sep, "mx-prefs-nav-sep");
        gtk_widget_show(sep);
        gtk_list_box_row_set_header(row, sep);
    } else {
        gtk_list_box_row_set_header(row, nullptr);
    }
}

void disconnectTree(GtkWidget* w, gpointer data) {
    if (!w) {
        return;
    }
    g_signal_handlers_disconnect_by_data(w, data);
    if (GTK_IS_CONTAINER(w)) {
        gtk_container_forall(GTK_CONTAINER(w), +[](GtkWidget* child, gpointer d) { disconnectTree(child, d); }, data);
    }
}

std::string joinKeywords(const char* a, const char* b) {
    std::string out;
    if (a && *a) {
        out += a;
    }
    if (b && *b) {
        if (!out.empty()) {
            out += ' ';
        }
        out += b;
    }
    return out;
}

GtkWidget* buildPlaceholdersPopover(GtkWidget* relativeTo) {
    GtkWidget* popover = gtk_popover_new(relativeTo);
    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller), 420);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 360);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);

    const char* helpText = _("%{name}\tFilename of the PDF background (if any)\n"
                             "%{file}\t\tSaved filename of the document\n"
                             "%a\t\tAbbreviated weekday name (e.g. Thu)\n"
                             "%A\t\tFull weekday name (e.g. Thursday)\n"
                             "%b\t\tAbbreviated month name (e.g. Aug)\n"
                             "%B\t\tFull month name (e.g. August)\n"
                             "%c\t\tDate and time representation\n"
                             "%d\t\tDay of the month (01-31)\n"
                             "%F\t\tDate representation (e.g. 2026-11-28)\n"
                             "%H\t\tHour in 24h format (00-23)\n"
                             "%I\t\tHour in 12h format (01-12)\n"
                             "%j\t\tDay of the year (001-366)\n"
                             "%m\t\tMonth as a decimal number (01-12)\n"
                             "%M\t\tMinute (00-59)\n"
                             "%p\t\tAM or PM designation\n"
                             "%S\t\tSecond (00-61)\n"
                             "%U\t\tWeek number, Sunday first (00-53)\n"
                             "%w\t\tWeekday as decimal, Sunday = 0 (0-6)\n"
                             "%W\t\tWeek number, Monday first (00-53)\n"
                             "%x\t\tLocale date representation\n"
                             "%X\t\tLocale time representation\n"
                             "%y\t\tYear, last two digits (00-99)\n"
                             "%Y\t\tYear (e.g. 2026)\n"
                             "%Z\t\tTimezone name or abbreviation\n"
                             "%%\t\tLiteral %\n\n"
                             "Illegal characters are replaced with \"_\".");

    GtkWidget* label = gtk_label_new(helpText);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_start(label, 14);
    gtk_widget_set_margin_end(label, 14);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    addCssClass(label, "mx-prefs-row-sub");

    gtk_container_add(GTK_CONTAINER(scroller), label);
    gtk_container_add(GTK_CONTAINER(popover), scroller);
    gtk_widget_show_all(scroller);
    return popover;
}

}  // namespace

SettingsDialog::SettingsDialog(GladeSearchpath* gladeSearchPath, Settings* settings, Control* control,
                               const std::vector<fs::path>& paletteDirectories, std::function<void()> callback):
        settings(settings),
        control(control),
        callib(zoomcallib_new()),
        builder(gladeSearchPath, UI_FILE),
        window(GTK_WINDOW(builder.get(UI_DIALOG_NAME))),
        searchEntry(GTK_SEARCH_ENTRY(builder.get("prefsSearchEntry"))),
        navList(GTK_LIST_BOX(builder.get("prefsNavList"))),
        contentStack(GTK_STACK(builder.get("prefsContentStack"))),
        latexPanel(gladeSearchPath),
        paletteTab(gladeSearchPath, paletteDirectories),
        callback(std::move(callback)) {

    gtk_list_box_set_header_func(navList, navHeaderFunc, nullptr, nullptr);

    shapeSettingsPanel = std::make_unique<ShapeSettingsPanel>([this]() {
        double effectiveDpi = static_cast<double>(this->dpi);
        if (getSwitch("zoomCallibAutomaticCb")) {
            effectiveDpi = xoj::util::gtk::getWidgetDPI(GTK_WIDGET(this->window.get()))
                                   .value_or(Util::DPI_NORMALIZATION_FACTOR);
        }
        return std::clamp(effectiveDpi / Util::DPI_NORMALIZATION_FACTOR, 0.5, 10.0);
    });
    mcpSettingsPanel = std::make_unique<McpSettingsPanel>(this->control);

    buildPages(gladeSearchPath);
    initMouseButtonEvents(gladeSearchPath);

    languageConfig = std::make_unique<LanguageConfigGui>(GTK_BOX(getWidget("hboxLanguageSelect")), settings);
    deviceTestingArea = std::make_unique<DeviceTestingArea>(gladeSearchPath,
                                                            GTK_BOX(getWidget("hboxInputDeviceClasses")), settings);

    capSettingsDialogDefaultSize(this->window.get(), this->control ? this->control->getGtkWindow() : nullptr);

    // Footer Save / Cancel and titlebar Close
    GtkWidget* btOk = builder.get("btOk");
    gtk_window_set_default(this->window.get(), btOk);
    g_signal_connect_swapped(btOk, "clicked", G_CALLBACK(+[](SettingsDialog* self) {
                                 self->save();
                                 self->callback();
                                 gtk_window_close(self->window.get());
                             }),
                             this);
    g_signal_connect_swapped(builder.get("btCancel"), "clicked", G_CALLBACK(gtk_window_close), window.get());
    g_signal_connect_swapped(builder.get("btClose"), "clicked", G_CALLBACK(gtk_window_close), window.get());

    // Navigation and search
    g_signal_connect(navList, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
                         static_cast<SettingsDialog*>(self)->onNavRowSelected(row);
                     }),
                     this);
    // Clicking the page that is already selected emits no row-selected, but must still leave the search results.
    g_signal_connect(navList, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
                         static_cast<SettingsDialog*>(self)->onNavRowSelected(row);
                     }),
                     this);
    g_signal_connect_swapped(searchEntry, "search-changed",
                             G_CALLBACK(+[](SettingsDialog* self) { self->onSearchChanged(); }), this);

    // Keyboard shortcuts: Ctrl+F focuses search; Esc clears search or closes dialog
    g_signal_connect(window.get(), "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         auto* self = static_cast<SettingsDialog*>(data);
                         if ((event->state & GDK_CONTROL_MASK) != 0 &&
                             (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
                             gtk_widget_grab_focus(GTK_WIDGET(self->searchEntry));
                             gtk_editable_select_region(GTK_EDITABLE(self->searchEntry), 0, -1);
                             return TRUE;
                         }
                         if (event->keyval == GDK_KEY_Escape) {
                             const char* txt = gtk_entry_get_text(GTK_ENTRY(self->searchEntry));
                             if (txt && *txt) {
                                 gtk_entry_set_text(GTK_ENTRY(self->searchEntry), "");
                                 return TRUE;
                             }
                             gtk_window_close(self->window.get());
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     this);

    // Control dependency signals
    g_signal_connect(GTK_RANGE(getWidget("zoomCallibSlider")), "value-changed",
                     G_CALLBACK(+[](GtkRange* range, gpointer self) {
                         static_cast<SettingsDialog*>(self)->setDpi(round_cast<int>(gtk_range_get_value(range)));
                     }),
                     this);

    g_signal_connect_swapped(getWidget("zoomCallibAutomaticCb"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) {
                                 const bool automatic = self->getSwitch("zoomCallibAutomaticCb");
                                 self->setRowDepState("dpi-ruler", !automatic);
                                 if (automatic) {
                                     auto detected = xoj::util::gtk::getWidgetDPI(self->getWidget("zoomCallibSlider"));
                                     gtk_range_set_value(GTK_RANGE(self->getWidget("zoomCallibSlider")),
                                                         detected.value_or(Util::DPI_NORMALIZATION_FACTOR));
                                 }
                             }),
                             this);

    g_signal_connect_swapped(getWidget("cbEnablePressureInference"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePressureSensitivityOptions(); }), this);
    g_signal_connect_swapped(getWidget("cbSettingPresureSensitivity"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePressureSensitivityOptions(); }), this);
    g_signal_connect_swapped(getWidget("scaleMinimumPressure"), "value-changed",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePressureValueLabels(); }), this);
    g_signal_connect_swapped(getWidget("scalePressureMultiplier"), "value-changed",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePressureValueLabels(); }), this);

    g_signal_connect_swapped(getWidget("cbAutosave"), "notify::active", G_CALLBACK(+[](SettingsDialog* self) {
                                 self->setRowDepState("spAutosaveTimeout", self->getSwitch("cbAutosave"));
                             }),
                             this);

    g_signal_connect_swapped(
            getWidget("cbIgnoreFirstStylusEvents"), "notify::active", G_CALLBACK(+[](SettingsDialog* self) {
                self->setRowDepState("spNumIgnoredStylusEvents", self->getSwitch("cbIgnoreFirstStylusEvents"));
            }),
            this);

    g_signal_connect_swapped(
            getWidget("btTestEnable"), "clicked", G_CALLBACK(+[](SettingsDialog* self) {
                Util::systemWithMessage(gtk_editable_get_text(GTK_EDITABLE(self->getWidget("txtEnableTouchCommand"))));
            }),
            this);
    g_signal_connect_swapped(
            getWidget("btTestDisable"), "clicked", G_CALLBACK(+[](SettingsDialog* self) {
                Util::systemWithMessage(gtk_editable_get_text(GTK_EDITABLE(self->getWidget("txtDisableTouchCommand"))));
            }),
            this);

    g_signal_connect_swapped(getWidget("cbAddVerticalSpace"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePageSpaceRows(); }), this);
    g_signal_connect_swapped(getWidget("cbAddHorizontalSpace"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePageSpaceRows(); }), this);
    g_signal_connect_swapped(getWidget("cbUnlimitedScrolling"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updatePageSpaceRows(); }), this);

    g_signal_connect_swapped(getWidget("cbDrawDirModsEnabled"), "notify::active", G_CALLBACK(+[](SettingsDialog* self) {
                                 self->setRowDepState("spDrawDirModsRadius", self->getSwitch("cbDrawDirModsEnabled"));
                             }),
                             this);

    g_signal_connect_swapped(
            getWidget("cbHoldToShapeEnabled"), "notify::active", G_CALLBACK(+[](SettingsDialog* self) {
                self->shapeSettingsPanel->setHoldControlsSensitive(self->getSwitch("cbHoldToShapeEnabled"));
            }),
            this);

    g_signal_connect_swapped(getWidget("cbStrokeFilterEnabled"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateStrokeFilterRows(); }), this);

    g_signal_connect_swapped(getWidget("cbRecolorDrawingArea"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateRecolorRows(); }), this);

    g_signal_connect_swapped(getWidget("cbStylusCursorType"), "changed",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateCursorHighlightRows(); }), this);
    g_signal_connect_swapped(getWidget("cbHighlightPosition"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateCursorHighlightRows(); }), this);

    g_signal_connect_swapped(getWidget("cbDisableTouchOnPenNear"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateCustomTouchCommandRows(); }), this);
    g_signal_connect_swapped(getWidget("cbTouchDisableMethod"), "changed",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateCustomTouchCommandRows(); }), this);

    g_signal_connect_swapped(getWidget("cbEnableZoomGestures"), "notify::active", G_CALLBACK(+[](SettingsDialog* self) {
                                 self->setRowDepState("spTouchZoomStartThreshold",
                                                      self->getSwitch("cbEnableZoomGestures"));
                             }),
                             this);

    g_signal_connect(GTK_COMBO_BOX(getWidget("cbStabilizerAveragingMethods")), "changed",
                     G_CALLBACK(+[](GtkComboBox* comboBox, gpointer self) {
                         static_cast<SettingsDialog*>(self)->showStabilizerAvMethodOptions(
                                 static_cast<StrokeStabilizer::AveragingMethod>(gtk_combo_box_get_active(comboBox)));
                     }),
                     this);

    g_signal_connect(GTK_COMBO_BOX(getWidget("cbStabilizerPreprocessors")), "changed",
                     G_CALLBACK(+[](GtkComboBox* comboBox, gpointer self) {
                         static_cast<SettingsDialog*>(self)->showStabilizerPreprocessorOptions(
                                 static_cast<StrokeStabilizer::Preprocessor>(gtk_combo_box_get_active(comboBox)));
                     }),
                     this);

    g_signal_connect_swapped(getWidget("cbUseSpacesAsTab"), "notify::active", G_CALLBACK(+[](SettingsDialog* self) {
                                 self->setRowDepState("sbNumberOfSpacesForTab", self->getSwitch("cbUseSpacesAsTab"));
                             }),
                             this);

#ifdef ENABLE_AUDIO
    g_signal_connect_swapped(getWidget("cbDisableAudio"), "notify::active",
                             G_CALLBACK(+[](SettingsDialog* self) { self->updateAudioRows(); }), this);

    g_signal_connect(getWidget("fcAudioPath"), "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer d) {
                         auto* self = static_cast<SettingsDialog*>(d);
                         xoj::OpenDlg::showSelectFolderDialog(
                                 GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW)),
                                 _("Select folder for audio recordings"), self->audioRecordingsFolder,
                                 [self](fs::path p) { self->setAudioRecordingFolder(std::move(p)); });
                     }),
                     this);
#endif

    gtk_widget_show_all(GTK_WIDGET(builder.get("prefsMainBox")));
    load();

    // Select first page ("General")
    if (!pages.empty() && pages[0].navRow) {
        gtk_list_box_select_row(navList, pages[0].navRow);
    }
}

SettingsDialog::~SettingsDialog() {
    flashTimer.cancel();
    scrollIdle.cancel();
    clearFlash();
    disconnectTree(GTK_WIDGET(window.get()), this);
}

GtkWidget* SettingsDialog::getWidget(const char* name) {
    if (auto it = widgets.find(name); it != widgets.end()) {
        return it->second;
    }
    return builder.get(name);
}

SettingsDialog::PageInfo& SettingsDialog::addPage(const char* id, const char* title, const char* iconName,
                                                  const char* leadText) {
    PageInfo info;
    info.id = id;
    info.title = title;

    // Sidebar row
    GtkWidget* row = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(row), "mx-page-id", g_strdup(id), g_free);
    GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* icon = gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);

    GtkWidget* lbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);

    GtkWidget* countLbl = gtk_label_new("");
    gtk_widget_set_valign(countLbl, GTK_ALIGN_CENTER);
    gtk_widget_set_no_show_all(countLbl, TRUE);
    gtk_widget_hide(countLbl);
    addCssClass(countLbl, "mx-prefs-nav-count");

    gtk_box_pack_start(GTK_BOX(rowBox), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowBox), lbl, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(rowBox), countLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), rowBox);
    gtk_list_box_insert(navList, row, -1);

    info.navRow = GTK_LIST_BOX_ROW(row);
    info.countLabel = GTK_LABEL(countLbl);

    // Scrolled page container with centered max-640px column
    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(contentBox, 4);
    gtk_widget_set_margin_bottom(contentBox, 40);
    gtk_widget_set_margin_start(contentBox, 24);
    gtk_widget_set_margin_end(contentBox, 24);
    gtk_widget_set_valign(contentBox, GTK_ALIGN_START);

    GtkWidget* heading = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
    addCssClass(heading, "mx-prefs-page-title");
    gtk_box_pack_start(GTK_BOX(contentBox), heading, FALSE, FALSE, 0);

    if (leadText && *leadText) {
        GtkWidget* lead = gtk_label_new(leadText);
        gtk_label_set_xalign(GTK_LABEL(lead), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(lead), TRUE);
        addCssClass(lead, "mx-prefs-lead");
        gtk_box_pack_start(GTK_BOX(contentBox), lead, FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(scroller), contentBox);
    g_signal_connect(scroller, "size-allocate", G_CALLBACK(updateCenteredMargins), contentBox);
    gtk_stack_add_named(contentStack, scroller, id);

    info.scroller = GTK_SCROLLED_WINDOW(scroller);
    info.contentBox = GTK_BOX(contentBox);

    pageIndexById[id] = pages.size();
    pages.push_back(std::move(info));
    return pages.back();
}

GtkBox* SettingsDialog::addSection(PageInfo& page, const char* /*sectionId*/, const char* title, const char* note,
                                   const char* tag) {
    GtkWidget* sectionBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget* titleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* titleLbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(titleLbl), 0.0F);
    addCssClass(titleLbl, "mx-prefs-section-title");
    gtk_box_pack_start(GTK_BOX(titleRow), titleLbl, FALSE, FALSE, 0);

    if (tag && *tag) {
        GtkWidget* tagLbl = gtk_label_new(tag);
        gtk_widget_set_valign(tagLbl, GTK_ALIGN_CENTER);
        addCssClass(tagLbl, "mx-prefs-tag");
        gtk_box_pack_start(GTK_BOX(titleRow), tagLbl, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(sectionBox), titleRow, FALSE, FALSE, 0);

    if (note && *note) {
        GtkWidget* noteLbl = gtk_label_new(note);
        gtk_label_set_xalign(GTK_LABEL(noteLbl), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(noteLbl), TRUE);
        addCssClass(noteLbl, "mx-prefs-section-note");
        gtk_box_pack_start(GTK_BOX(sectionBox), noteLbl, FALSE, FALSE, 0);
    }

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(card, 2);
    addCssClass(card, "mx-prefs-card");
    gtk_box_pack_start(GTK_BOX(sectionBox), card, FALSE, FALSE, 0);
    gtk_box_pack_start(page.contentBox, sectionBox, FALSE, FALSE, 0);
    return GTK_BOX(card);
}

void SettingsDialog::registerSearch(const std::string& pageId, const std::string& pageTitle,
                                    const std::string& sectionId, const std::string& sectionTitle,
                                    const std::string& sectionNote, const char* rowId, const char* label,
                                    const char* subtitle, const char* keywords, GtkWidget* targetWidget,
                                    GtkExpander* parentExpander) {
    xoj::prefs::SearchEntry entry;
    entry.rowId = rowId ? rowId : "";
    entry.pageId = pageId;
    entry.pageTitle = pageTitle;
    entry.sectionId = sectionId;
    entry.sectionTitle = sectionTitle;
    entry.sectionNote = sectionNote;
    entry.label = label ? label : "";
    entry.subtitle = subtitle ? subtitle : "";
    entry.keywords = keywords ? keywords : "";

    searchEntries.push_back(entry);
    searchTargets.push_back(SearchTarget{std::move(entry), targetWidget, parentExpander});
}

GtkWidget* SettingsDialog::addRowToCard(GtkBox* card, const char* rowId, const char* label, const char* subtitle,
                                        GtkWidget* control, bool restartNeeded, const char* depHintText, bool wide) {
    GtkWidget* sep = nullptr;
    GList* existing = gtk_container_get_children(GTK_CONTAINER(card));
    if (existing != nullptr) {
        sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        addCssClass(sep, "mx-prefs-row-sep");
        gtk_box_pack_start(card, sep, FALSE, FALSE, 0);
        g_list_free(existing);
    }

    GtkWidget* row = gtk_box_new(wide ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, wide ? 10 : 16);
    addCssClass(row, "mx-prefs-row");

    GtkWidget* textBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(textBox, TRUE);
    gtk_widget_set_valign(textBox, GTK_ALIGN_CENTER);

    if (label && *label) {
        if (restartNeeded) {
            GtkWidget* titleLine = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget* lbl = gtk_label_new(label);
            gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
            gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
            addCssClass(lbl, "mx-prefs-row-title");
            GtkWidget* tag = gtk_label_new(_("Restart needed"));
            gtk_widget_set_valign(tag, GTK_ALIGN_CENTER);
            addCssClass(tag, "mx-prefs-tag");
            gtk_box_pack_start(GTK_BOX(titleLine), lbl, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(titleLine), tag, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(textBox), titleLine, FALSE, FALSE, 0);
        } else {
            GtkWidget* lbl = gtk_label_new(label);
            gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
            gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
            addCssClass(lbl, "mx-prefs-row-title");
            gtk_box_pack_start(GTK_BOX(textBox), lbl, FALSE, FALSE, 0);
        }
    }

    if (subtitle && *subtitle) {
        GtkWidget* subLbl = gtk_label_new(subtitle);
        gtk_label_set_xalign(GTK_LABEL(subLbl), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(subLbl), TRUE);
        addCssClass(subLbl, "mx-prefs-row-sub");
        gtk_box_pack_start(GTK_BOX(textBox), subLbl, FALSE, FALSE, 0);
    }

    GtkWidget* depHint = nullptr;
    if (depHintText && *depHintText) {
        depHint = gtk_label_new(depHintText);
        gtk_label_set_xalign(GTK_LABEL(depHint), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(depHint), TRUE);
        gtk_widget_set_no_show_all(depHint, TRUE);
        gtk_widget_hide(depHint);
        addCssClass(depHint, "mx-prefs-row-sub");
        gtk_box_pack_start(GTK_BOX(textBox), depHint, FALSE, FALSE, 0);
    }

    if ((label && *label) || (subtitle && *subtitle) || depHint) {
        gtk_box_pack_start(GTK_BOX(row), textBox, !wide, TRUE, 0);
    }

    if (control) {
        if (!wide) {
            gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
            gtk_box_pack_end(GTK_BOX(row), control, FALSE, FALSE, 0);
        } else {
            gtk_box_pack_start(GTK_BOX(row), control, FALSE, TRUE, 0);
        }
    }

    gtk_box_pack_start(card, row, FALSE, FALSE, 0);

    if (rowId && *rowId) {
        rows[rowId] = RowWidgets{row, sep, depHint};
    }
    return row;
}

GtkWidget* SettingsDialog::addSwitchRow(GtkBox* card, const char* id, const char* label, const char* subtitle,
                                        bool restartNeeded, const char* depHintText) {
    GtkWidget* sw = gtk_switch_new();
    gtk_widget_set_name(sw, id);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    widgets[id] = sw;
    return addRowToCard(card, id, label, subtitle, sw, restartNeeded, depHintText, false);
}

GtkWidget* SettingsDialog::addSpinRow(GtkBox* card, const char* id, const char* adjustmentId, int digits,
                                      double climbRate, const char* label, const char* subtitle, const char* unit,
                                      const char* depHintText) {
    auto* adj = GTK_ADJUSTMENT(builder.getObject(adjustmentId));
    GtkWidget* spin = gtk_spin_button_new(adj, climbRate, static_cast<guint>(digits));
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_widget_set_name(spin, id);
    widgets[id] = spin;

    GtkWidget* ctl = spin;
    if (unit && *unit) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
        GtkWidget* unitLbl = gtk_label_new(unit);
        addCssClass(unitLbl, "mx-prefs-unit");
        gtk_box_pack_start(GTK_BOX(box), unitLbl, FALSE, FALSE, 0);
        ctl = box;
    }

    return addRowToCard(card, id, label, subtitle, ctl, false, depHintText, false);
}

GtkWidget* SettingsDialog::addComboTextRow(GtkBox* card, const char* id, const std::vector<const char*>& items,
                                           const char* label, const char* subtitle, bool restartNeeded,
                                           const char* depHintText) {
    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_widget_set_name(combo, id);
    for (const char* item: items) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item);
    }
    if (!items.empty()) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    }
    widgets[id] = combo;
    return addRowToCard(card, id, label, subtitle, combo, restartNeeded, depHintText, false);
}

GtkWidget* SettingsDialog::addSegmentedRow(GtkBox* card, const char* id, const std::vector<const char*>& options,
                                           const char* label, const char* subtitle) {
    GtkWidget* segBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addCssClass(segBox, "mx-prefs-seg");
    gtk_widget_set_name(segBox, id);

    SegmentedControl seg;
    seg.box = segBox;
    GtkRadioButton* groupHead = nullptr;
    for (const char* opt: options) {
        GtkWidget* btn = gtk_radio_button_new_with_label_from_widget(groupHead, opt);
        if (!groupHead) {
            groupHead = GTK_RADIO_BUTTON(btn);
        }
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn), FALSE);
        addCssClass(btn, "mx-prefs-seg-btn");
        gtk_box_pack_start(GTK_BOX(segBox), btn, FALSE, FALSE, 0);
        seg.buttons.push_back(GTK_RADIO_BUTTON(btn));
    }

    widgets[id] = segBox;
    segmentedControls[id] = std::move(seg);
    return addRowToCard(card, id, label, subtitle, segBox, false, nullptr, false);
}

GtkWidget* SettingsDialog::addColorRow(GtkBox* card, const char* id, const char* label, const char* subtitle,
                                       bool useAlpha, const char* depHintText) {
    GtkWidget* btn = gtk_color_button_new();
    gtk_widget_set_name(btn, id);
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(btn), useAlpha);
    widgets[id] = btn;
    return addRowToCard(card, id, label, subtitle, btn, false, depHintText, false);
}

GtkWidget* SettingsDialog::addEntryRow(GtkBox* card, const char* id, const char* label, const char* subtitle,
                                       bool monospace, const char* actionButtonId, const char* actionButtonLabel,
                                       const char* depHintText) {
    GtkWidget* entry = gtk_entry_new();
    gtk_widget_set_name(entry, id);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), actionButtonId ? 20 : 24);
    if (monospace) {
        addCssClass(entry, "mx-prefs-mono");
    }
    widgets[id] = entry;

    GtkWidget* ctl = entry;
    if (actionButtonId && actionButtonLabel) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* btn = gtk_button_new_with_label(actionButtonLabel);
        gtk_widget_set_name(btn, actionButtonId);
        widgets[actionButtonId] = btn;
        gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
        ctl = box;
    }

    return addRowToCard(card, id, label, subtitle, ctl, false, depHintText, false);
}

GtkWidget* SettingsDialog::addScaleRow(GtkBox* card, const char* id, const char* adjustmentId, int roundDigits,
                                       const char* label, const char* subtitle, const char* depHintText) {
    auto* adj = GTK_ADJUSTMENT(builder.getObject(adjustmentId));
    GtkWidget* scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_range_set_round_digits(GTK_RANGE(scale), roundDigits);
    gtk_widget_set_size_request(scale, 180, -1);
    gtk_widget_set_name(scale, id);
    widgets[id] = scale;

    GtkWidget* valLbl = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(valLbl), 6);
    gtk_label_set_xalign(GTK_LABEL(valLbl), 1.0F);
    addCssClass(valLbl, "mx-prefs-unit");
    addCssClass(valLbl, "mx-numeric");
    scaleValueLabels[id] = GTK_LABEL(valLbl);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(box), scale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), valLbl, FALSE, FALSE, 0);

    return addRowToCard(card, id, label, subtitle, box, false, depHintText, false);
}

void SettingsDialog::setRowDepState(const char* rowId, bool met) {
    auto it = rows.find(rowId);
    if (it == rows.end()) {
        return;
    }
    gtk_widget_set_sensitive(it->second.rowBox, met);
    if (it->second.depHint) {
        gtk_widget_set_visible(it->second.depHint, !met);
    }
}

void SettingsDialog::setRowVisible(const char* rowId, bool visible) {
    auto it = rows.find(rowId);
    if (it == rows.end()) {
        return;
    }
    gtk_widget_set_no_show_all(it->second.rowBox, !visible);
    gtk_widget_set_visible(it->second.rowBox, visible);
    if (it->second.separator) {
        gtk_widget_set_no_show_all(it->second.separator, !visible);
        gtk_widget_set_visible(it->second.separator, visible);
    }
}

void SettingsDialog::buildPages(GladeSearchpath* /*gladeSearchPath*/) {
    buildGeneralPage();
    buildAppearancePage();
    buildPagesZoomPage();
    buildPenPage();
    buildMousePage();
    buildTouchPage();
    buildToolsPage();
#ifdef ENABLE_AUDIO
    buildAudioPage();
#endif
    buildLatexPage();
    buildAiPage();
    buildAdvancedPage();
    buildSearchResultsPage();
}

void SettingsDialog::buildGeneralPage() {
    PageInfo& page = addPage("general", _("General"), "mx-prefs-general-symbolic");

    // Language
    {
        const char* secTitle = _("Language");
        GtkBox* card = addSection(page, "language", secTitle);
        GtkWidget* langBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        widgets["hboxLanguageSelect"] = langBox;
        GtkWidget* row = addRowToCard(card, "hboxLanguageSelect", _("Interface language"),
                                      _("Takes effect after a restart."), langBox, true);
        registerSearch(page.id, page.title, "language", secTitle, "", "language", _("Interface language"),
                       _("Takes effect after a restart."), "translation locale language", row);
    }

    // Autosave
    {
        const char* secTitle = _("Autosave");
        GtkBox* card = addSection(page, "autosave", secTitle);
        GtkWidget* rOn = addSwitchRow(
                card, "cbAutosave", _("Save a backup copy automatically"),
                _("Saved next to the document as .autosave.xopp. Unsaved documents go to the configuration folder."));
        registerSearch(
                page.id, page.title, "autosave", secTitle, "", "autosave-on", _("Save a backup copy automatically"),
                _("Saved next to the document as .autosave.xopp. Unsaved documents go to the configuration folder."),
                "backup recovery crash load save", rOn);

        GtkWidget* rInt = addSpinRow(card, "spAutosaveTimeout", "adjustmentTimeout", 0, 1.0, _("Interval"), nullptr,
                                     _("min"), _("Turn on “Save a backup copy automatically” to change this."));
        registerSearch(page.id, page.title, "autosave", secTitle, "", "autosave-interval", _("Interval"), "",
                       "autosave minutes every load save", rInt);
    }

    // Default file names
    {
        const char* secTitle = _("Default file names");
        GtkBox* card = addSection(page, "savename", secTitle);

        GtkWidget* rNew =
                addEntryRow(card, "txtDefaultSaveName", _("New documents"), _("Example: 2026-11-28-Note-09-15"), true);
        registerSearch(page.id, page.title, "savename", secTitle, "", "savename-new", _("New documents"),
                       _("Example: 2026-11-28-Note-09-15"), "save name template placeholder date load save", rNew);

        GtkWidget* rPdf = addEntryRow(card, "txtDefaultPdfName", _("Annotated PDF export"),
                                      _("Example: lecture-07_annotated.pdf"), true);
        registerSearch(page.id, page.title, "savename", secTitle, "", "savename-pdf", _("Annotated PDF export"),
                       _("Example: lecture-07_annotated.pdf"), "export pdf name load save", rPdf);

        GtkWidget* helpBtn = gtk_menu_button_new();
        gtk_button_set_label(GTK_BUTTON(helpBtn), _("Show all"));
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(helpBtn), buildPlaceholdersPopover(helpBtn));
        widgets["btSavenamePlaceholders"] = helpBtn;
        GtkWidget* rHelp = addRowToCard(card, "savename-help", _("Placeholders"),
                                        _("%F date, %H hour, %M minute, %{name} PDF name and more"), helpBtn);
        registerSearch(page.id, page.title, "savename", secTitle, "", "savename-help", _("Placeholders"),
                       _("%F date, %H hour, %M minute, %{name} PDF name and more"), "load save", rHelp);
    }
}

void SettingsDialog::buildAppearancePage() {
    PageInfo& page = addPage("appearance", _("Appearance"), "mx-prefs-appearance-symbolic");

    // Theme
    {
        const char* secTitle = _("Theme");
        GtkBox* card = addSection(page, "theme", secTitle);

        GtkWidget* rVar = addSegmentedRow(card, "cbThemeVariant", {_("System"), _("Light"), _("Dark")}, _("Theme"));
        registerSearch(page.id, page.title, "theme", secTitle, "", "theme-variant", _("Theme"), "",
                       "dark mode light mode appearance view", rVar);

        GtkWidget* rIcons = addComboTextRow(card, "cbIconTheme", {_("Color"), _("Lucide")}, _("Icon style"),
                                            _("Takes effect after a restart."), true);
        registerSearch(page.id, page.title, "theme", secTitle, "", "theme-icons", _("Icon style"),
                       _("Takes effect after a restart."), "icon theme view", rIcons);

        GtkWidget* rStock = addSwitchRow(card, "cbStockIcons", _("Use system icons when available"),
                                         _("Takes effect after a restart."), true);
        registerSearch(page.id, page.title, "theme", secTitle, "", "theme-stock", _("Use system icons when available"),
                       _("Takes effect after a restart."), "stock icons view", rStock);

        GtkWidget* rShadow = addSwitchRow(card, "cbShowPageShadow", _("Shadow behind pages"));
        registerSearch(page.id, page.title, "theme", secTitle, "", "theme-shadow", _("Shadow behind pages"), "",
                       "page shadow view", rShadow);
    }

    // Touch mode
    {
        const char* secTitle = _("Touch mode");
        GtkBox* card = addSection(page, "touchmode", secTitle);
        GtkWidget* rTouch = addSegmentedRow(
                card, "cbTouchMode", {_("Automatic"), _("Off"), _("On")}, _("Larger controls for fingers"),
                _("Automatic switches on with finger touch and mouse. The pen keeps the current layout."));
        registerSearch(page.id, page.title, "touchmode", secTitle, "", "touchmode", _("Larger controls for fingers"),
                       _("Automatic switches on with finger touch and mouse. The pen keeps the current layout."),
                       "touch mode tablet bigger buttons view", rTouch);
    }

    // Window
    {
        const char* secTitle = _("Window");
        GtkBox* card = addSection(page, "window", secTitle);

        GtkWidget* rMenu = addSwitchRow(card, "cbHideMenubarStartup", _("Always show the menu bar"),
                                        _("Otherwise press Alt to show it. F10 toggles it."));
        registerSearch(page.id, page.title, "window", secTitle, "", "window-menubar", _("Always show the menu bar"),
                       _("Otherwise press Alt to show it. F10 toggles it."), "menubar menu bar alt f10 view global",
                       rMenu);

        GtkWidget* rPath = addSwitchRow(card, "cbShowFilepathInTitlebar", _("File path in the title"));
        registerSearch(page.id, page.title, "window", secTitle, "", "window-path", _("File path in the title"), "",
                       "titlebar filepath view global", rPath);

        GtkWidget* rPage = addSwitchRow(card, "cbShowPageNumberInTitlebar", _("Page number in the title"));
        registerSearch(page.id, page.title, "window", secTitle, "", "window-pagenum", _("Page number in the title"), "",
                       "titlebar page view global", rPage);

        GtkWidget* rCsd =
                addSwitchRow(card, "cbUseCSD", _("Tools in the title bar"), _("Takes effect after a restart."), true);
        registerSearch(page.id, page.title, "window", secTitle, "", "window-csd", _("Tools in the title bar"),
                       _("Takes effect after a restart."), "csd header bar view global", rCsd);

#ifdef __APPLE__
        setRowVisible("cbHideMenubarStartup", false);
        setRowVisible("cbUseCSD", false);
#endif
    }

    // Left-handed layout
    {
        const char* secTitle = _("Left-handed layout");
        GtkBox* card = addSection(page, "handed", secTitle);

        GtkWidget* rSide = addSwitchRow(card, "cbShowSidebarRight", _("Page sidebar on the right"));
        registerSearch(page.id, page.title, "handed", secTitle, "", "handed-sidebar", _("Page sidebar on the right"),
                       "", "left handed sidebar right view right-handed", rSide);

        GtkWidget* rScroll = addSwitchRow(card, "cbShowScrollbarLeft", _("Vertical scrollbar on the left"));
        registerSearch(page.id, page.title, "handed", secTitle, "", "handed-scroll",
                       _("Vertical scrollbar on the left"), "", "left handed scrollbar view right-handed", rScroll);
    }

    // Page sidebar
    {
        const char* secTitle = _("Page sidebar");
        GtkBox* card = addSection(page, "sidebar", secTitle);

        GtkWidget* rNum = addComboTextRow(
                card, "cbSidebarPageNumberStyle",
                {_("None"), _("Below preview"), _("Circle background"), _("Square background")}, _("Page numbers"));
        registerSearch(page.id, page.title, "sidebar", secTitle, "", "sidebar-numbers", _("Page numbers"), "",
                       "sidebar thumbnails page numbering view", rNum);
    }

    // Scrollbars
    {
        const char* secTitle = _("Scrollbars");
        GtkBox* card = addSection(page, "scrollbars", secTitle);

        GtkWidget* rH = addSwitchRow(card, "cbHideHorizontalScrollbar", _("Hide the horizontal scrollbar"));
        registerSearch(page.id, page.title, "scrollbars", secTitle, "", "scroll-hideh",
                       _("Hide the horizontal scrollbar"), "", "view", rH);

        GtkWidget* rV = addSwitchRow(card, "cbHideVerticalScrollbar", _("Hide the vertical scrollbar"));
        registerSearch(page.id, page.title, "scrollbars", secTitle, "", "scroll-hidev",
                       _("Hide the vertical scrollbar"), "", "view", rV);

        GtkWidget* rFade = addSwitchRow(card, "cbDisableScrollbarFadeout", _("Keep scrollbars visible"),
                                        _("Turns off fading out when you stop scrolling."));
        registerSearch(page.id, page.title, "scrollbars", secTitle, "", "scroll-fade", _("Keep scrollbars visible"),
                       _("Turns off fading out when you stop scrolling."), "fade out scrollbar view", rFade);
    }

    // Interface colors
    {
        const char* secTitle = _("Interface colors");
        GtkBox* card = addSection(page, "colors", secTitle);

        GtkWidget* rBorder = addColorRow(card, "colorBorder", _("Current page and selection border"));
        registerSearch(page.id, page.title, "colors", secTitle, "", "color-border",
                       _("Current page and selection border"), "", "colour border view", rBorder);

        GtkWidget* rBg = addColorRow(card, "colorBackground", _("Background between pages"));
        registerSearch(page.id, page.title, "colors", secTitle, "", "color-bg", _("Background between pages"), "",
                       "colour background canvas view", rBg);

        GtkWidget* rSel = addColorRow(card, "colorSelection", _("Selection"), _("Selected text and strokes."));
        registerSearch(page.id, page.title, "colors", secTitle, "", "color-sel", _("Selection"),
                       _("Selected text and strokes."), "colour selection view", rSel);

        GtkWidget* rAct = addColorRow(card, "colorSelectionActive", _("Active selection"), _("Current search result."));
        registerSearch(page.id, page.title, "colors", secTitle, "", "color-selactive", _("Active selection"),
                       _("Current search result."), "colour search result view", rAct);
    }

    // Recolor pages
    {
        const char* secTitle = _("Recolor pages");
        const char* secNote = _("Recoloring makes pages follow a dark theme. It can slow down moving selections.");
        GtkBox* card = addSection(page, "recolor", secTitle, secNote);
        const char* depHint = _("Turn on “Recolor the pages” to change this.");

        GtkWidget* rArea = addSwitchRow(card, "cbRecolorDrawingArea", _("Recolor the pages"));
        registerSearch(page.id, page.title, "recolor", secTitle, secNote, "recolor-area", _("Recolor the pages"), "",
                       "recolor dark mode pages invert view recoloring", rArea);

        GtkWidget* rSide =
                addSwitchRow(card, "cbRecolorPreviewSidebar", _("Recolor sidebar previews"), nullptr, false, depHint);
        registerSearch(page.id, page.title, "recolor", secTitle, secNote, "recolor-sidebar",
                       _("Recolor sidebar previews"), "", "view recoloring", rSide);

        GtkWidget* rLight = addColorRow(card, "recolorLight", _("Light areas become"), nullptr, false, depHint);
        registerSearch(page.id, page.title, "recolor", secTitle, secNote, "recolor-light", _("Light areas become"), "",
                       "view recoloring", rLight);

        GtkWidget* rDark = addColorRow(card, "recolorDark", _("Dark areas become"), nullptr, false, depHint);
        registerSearch(page.id, page.title, "recolor", secTitle, secNote, "recolor-dark", _("Dark areas become"), "",
                       "view recoloring", rDark);
    }

    // Pen cursor
    {
        const char* secTitle = _("Pen cursor");
        GtkBox* card = addSection(page, "cursor", secTitle);

        GtkWidget* rCur = addComboTextRow(card, "cbStylusCursorType",
                                          {_("No icon"), _("Small dot icon"), _("Big pen icon"), _("System cursor")},
                                          _("Cursor"));
        registerSearch(page.id, page.title, "cursor", secTitle, "", "cursor-icon", _("Cursor"), "",
                       "pointer cursor pen icon view", rCur);

        GtkWidget* rHl = addSwitchRow(card, "cbHighlightPosition", _("Highlight the cursor position"),
                                      _("Useful for screen recordings and presentations."), false,
                                      _("Choose “Small dot icon” or “Big pen icon” for Cursor to change this."));
        registerSearch(page.id, page.title, "cursor", secTitle, "", "cursor-highlight",
                       _("Highlight the cursor position"), _("Useful for screen recordings and presentations."),
                       "highlight cursor circle view", rHl);

        const char* hlDep = _("Turn on “Highlight the cursor position” to change this.");
        GtkWidget* rCol = addColorRow(card, "cursorHighlightColor", _("Circle color"), nullptr, true, hlDep);
        registerSearch(page.id, page.title, "cursor", secTitle, "", "cursor-hl-color", _("Circle color"), "", "view",
                       rCol);

        GtkWidget* rRad = addSpinRow(card, "cursorHighlightRadius", "adjustmentCursorHighlightRadius", 0, 1.0,
                                     _("Radius"), nullptr, _("px"), hlDep);
        registerSearch(page.id, page.title, "cursor", secTitle, "", "cursor-hl-radius", _("Radius"), "", "view", rRad);

        GtkWidget* rBw = addSpinRow(card, "cursorHighlightBorderWidth", "adjustmentCursorHighlightBorderWidth", 0, 1.0,
                                    _("Border width"), nullptr, _("px"), hlDep);
        registerSearch(page.id, page.title, "cursor", secTitle, "", "cursor-hl-border", _("Border width"), "", "view",
                       rBw);

        GtkWidget* rBc = addColorRow(card, "cursorHighlightBorderColor", _("Border color"), nullptr, true, hlDep);
        registerSearch(page.id, page.title, "cursor", secTitle, "", "cursor-hl-bcolor", _("Border color"), "", "view",
                       rBc);
    }

    // Color palette
    {
        const char* secTitle = _("Color palette");
        GtkBox* card = addSection(page, "palette", secTitle);

        GtkWidget* palettePanel = this->paletteTab.getPanel();
        gtk_box_pack_start(card, palettePanel, FALSE, FALSE, 0);
        registerSearch(page.id, page.title, "palette", secTitle, "", "palette-pick", _("Palette"),
                       _("Colors shown in the tool pill and the color menu."), "palette colors colours gpl",
                       palettePanel);

        GtkWidget* openBtn = gtk_button_new_with_label(_("Open folder"));
        g_signal_connect(openBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
                             if (auto uri = Util::toUri(Util::getCustomPaletteDirectoryPath())) {
                                 XojMsgBox::openURL(nullptr, uri->c_str());
                             }
                         }),
                         nullptr);
        GtkWidget* rFolder = addRowToCard(card, "palette-folder", _("Add your own palettes"),
                                          _("Put .gpl files in the palettes folder."), openBtn);
        registerSearch(page.id, page.title, "palette", secTitle, "", "palette-folder", _("Add your own palettes"),
                       _("Put .gpl files in the palettes folder."), "palette", rFolder);
    }
}

void SettingsDialog::buildPagesZoomPage() {
    PageInfo& page = addPage("pages", _("Pages and zoom"), "mx-prefs-pages-symbolic");

    // Zoom
    {
        const char* secTitle = _("Zoom");
        GtkBox* card = addSection(page, "zoomspeed", secTitle);

        GtkWidget* rScroll = addSpinRow(card, "spZoomStepScroll", "adjustmentZoomStepScroll", 1, 0.5,
                                        _("Ctrl + scroll step"), nullptr, "%");
        registerSearch(page.id, page.title, "zoomspeed", secTitle, "", "zoom-scroll", _("Ctrl + scroll step"), "",
                       "zoom speed wheel zoom", rScroll);

        GtkWidget* rStep =
                addSpinRow(card, "spZoomStep", "adjustmentZoomStep", 1, 0.5, _("Zoom in/out step"), nullptr, "%");
        registerSearch(page.id, page.title, "zoomspeed", secTitle, "", "zoom-step", _("Zoom in/out step"), "",
                       "zoom speed button zoom", rStep);

        GtkWidget* rFit = addSwitchRow(card, "cbForceZoomToFitOnLoad", _("Fit the page when opening a document"));
        registerSearch(page.id, page.title, "zoomspeed", secTitle, "", "zoom-fit",
                       _("Fit the page when opening a document"), "", "default zoom fit width load zoom", rFit);
    }

    // Space around pages
    {
        const char* secTitle = _("Space around pages");
        const char* secNote = _("Extra space lets you scroll a page edge to the middle of the screen.");
        GtkBox* card = addSection(page, "pagespace", secTitle, secNote);

        GtkWidget* rUnlim = addSwitchRow(card, "cbUnlimitedScrolling", _("Unlimited scrolling"));
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-unlimited", _("Unlimited scrolling"),
                       "", "scroll outside page space margin infinite drawing area scrolling outside", rUnlim);

        const char* unlimDep = _("Available when “Unlimited scrolling” is off.");
        GtkWidget* rAddH =
                addSwitchRow(card, "cbAddHorizontalSpace", _("Extra space left and right"), nullptr, false, unlimDep);
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-h", _("Extra space left and right"),
                       "", "drawing area scrolling outside", rAddH);

        const char* hDep = _("Turn on “Extra space left and right” while “Unlimited scrolling” is off.");
        GtkWidget* rLeft = addSpinRow(card, "spAddHorizontalSpaceLeft", "adjustmentHorizontalSpaceLeft", 0, 1.0,
                                      _("Left space"), nullptr, _("px"), hDep);
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-h-left", _("Left space"), "",
                       "extra space horizontal margin drawing area scrolling outside", rLeft);

        GtkWidget* rRight = addSpinRow(card, "spAddHorizontalSpaceRight", "adjustmentHorizontalSpaceRight", 0, 1.0,
                                       _("Right space"), nullptr, _("px"), hDep);
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-h-right", _("Right space"), "",
                       "extra space horizontal margin drawing area scrolling outside", rRight);

        GtkWidget* rAddV =
                addSwitchRow(card, "cbAddVerticalSpace", _("Extra space above and below"), nullptr, false, unlimDep);
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-v", _("Extra space above and below"),
                       "", "drawing area scrolling outside", rAddV);

        const char* vDep = _("Turn on “Extra space above and below” while “Unlimited scrolling” is off.");
        GtkWidget* rAbove = addSpinRow(card, "spAddVerticalSpaceAbove", "adjustmentVerticalSpaceAbove", 0, 1.0,
                                       _("Space above"), nullptr, _("px"), vDep);
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-v-above", _("Space above"), "",
                       "extra space vertical margin drawing area scrolling outside", rAbove);

        GtkWidget* rBelow = addSpinRow(card, "spAddVerticalSpaceBelow", "adjustmentVerticalSpaceBelow", 0, 1.0,
                                       _("Space below"), nullptr, _("px"), vDep);
        registerSearch(page.id, page.title, "pagespace", secTitle, secNote, "space-v-below", _("Space below"), "",
                       "extra space vertical margin drawing area scrolling outside", rBelow);
    }

    // New pages
    {
        const char* secTitle = _("New pages");
        GtkBox* card = addSection(page, "lastpage", secTitle);

        GtkWidget* radioBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget* rdScroll = gtk_radio_button_new_with_label(nullptr, _("When scrolling past the last page"));
        GtkWidget* rdDraw = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rdScroll),
                                                                        _("When drawing on the last page"));
        GtkWidget* rdDisabled = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rdScroll), _("Never"));

        widgets["rdLastPageAppendOnScrollToEndOfLastPage"] = rdScroll;
        widgets["rdLastPageAppendOnDrawOfLastPage"] = rdDraw;
        widgets["rdLastPageAppendDisabled"] = rdDisabled;

        gtk_box_pack_start(GTK_BOX(radioBox), rdScroll, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(radioBox), rdDraw, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(radioBox), rdDisabled, FALSE, FALSE, 0);

        GtkWidget* rLast = addRowToCard(card, "lastpage", _("Add a blank page at the end"), _("Not for PDF documents."),
                                        radioBox, false, nullptr, true);
        registerSearch(page.id, page.title, "lastpage", secTitle, "", "lastpage", _("Add a blank page at the end"),
                       _("Not for PDF documents."), "append page automatically empty last drawing area", rLast);
    }

    // Paired pages
    {
        const char* secTitle = _("Paired pages");
        GtkBox* card = addSection(page, "paired", secTitle);

        GtkWidget* rPairs = addSpinRow(card, "spPairsOffset", "adjustmentPairsOffset", 0, 1.0, _("First page offset"),
                                       _("Usually 0 or 1. Use 1 to start a book with a single right-hand page."));
        registerSearch(page.id, page.title, "paired", secTitle, "", "paired-offset", _("First page offset"),
                       _("Usually 0 or 1. Use 1 to start a book with a single right-hand page."),
                       "two pages book layout pairs drawing area", rPairs);
    }

    // Resizing
    {
        const char* secTitle = _("Resizing");
        GtkBox* card = addSection(page, "resizing", secTitle);

        GtkWidget* rResize = addSwitchRow(card, "cbRestoreLineWidthEnabled", _("Keep line width when resizing"));
        registerSearch(page.id, page.title, "resizing", secTitle, "", "resize-width",
                       _("Keep line width when resizing"), "", "preserve line width scale drawing area", rResize);
    }

    // Snapping
    {
        const char* secTitle = _("Snapping");
        GtkBox* card = addSection(page, "snapping", secTitle);

        GtkWidget* rGrid = addSpinRow(card, "spSnapGridSize", "adjustmentSnapGridSize", 2, 0.01, _("Grid size"));
        registerSearch(page.id, page.title, "snapping", secTitle, "", "snap-grid", _("Grid size"), "",
                       "snap grid drawing area", rGrid);

        GtkWidget* rGridTol = addSpinRow(card, "spSnapGridTolerance", "adjustmentSnapGridTolerance", 2, 0.05,
                                         _("Grid snapping tolerance"));
        registerSearch(page.id, page.title, "snapping", secTitle, "", "snap-gridtol", _("Grid snapping tolerance"), "",
                       "drawing area", rGridTol);

        GtkWidget* rRotTol = addSpinRow(card, "spSnapRotationTolerance", "adjustmentSnapRotationTolerance", 2, 0.05,
                                        _("Rotation snapping tolerance"));
        registerSearch(page.id, page.title, "snapping", secTitle, "", "snap-rottol", _("Rotation snapping tolerance"),
                       "", "rotation snap angle drawing area", rRotTol);

        GtkWidget* rSnapShapes = addSwitchRow(card, "cbSnapRecognizedShapesEnabled", _("Snap recognized shapes"));
        registerSearch(page.id, page.title, "snapping", secTitle, "", "snap-shapes", _("Snap recognized shapes"), "",
                       "drawing area", rSnapShapes);
    }

    // Fullscreen
    {
        const char* secTitle = _("Fullscreen");
        GtkBox* card = addSection(page, "fullscreen", secTitle);

        GtkWidget* rMenu = addSwitchRow(card, "cbShowFullscreenMenubar", _("Show the menu bar"));
        registerSearch(page.id, page.title, "fullscreen", secTitle, "", "fs-menubar", _("Show the menu bar"), "",
                       "view", rMenu);

        GtkWidget* rBar = addSwitchRow(card, "cbShowFullscreenToolbar", _("Show the top bar"));
        registerSearch(page.id, page.title, "fullscreen", secTitle, "", "fs-toolbar", _("Show the top bar"), "", "view",
                       rBar);

        GtkWidget* rSide = addSwitchRow(card, "cbShowFullscreenSidebar", _("Show the page sidebar"));
        registerSearch(page.id, page.title, "fullscreen", secTitle, "", "fs-sidebar", _("Show the page sidebar"), "",
                       "view", rSide);

#ifdef __APPLE__
        setRowVisible("cbShowFullscreenMenubar", false);
#endif
    }

    // Presentation mode
    {
        const char* secTitle = _("Presentation mode");
        GtkBox* card = addSection(page, "presentation", secTitle);

        GtkWidget* rFs = addSwitchRow(card, "cbPresentationGoFullscreen", _("Go fullscreen"));
        registerSearch(page.id, page.title, "presentation", secTitle, "", "pres-fullscreen", _("Go fullscreen"), "",
                       "presentation slides view", rFs);

        GtkWidget* rMenu = addSwitchRow(card, "cbShowPresentationMenubar", _("Show the menu bar"));
        registerSearch(page.id, page.title, "presentation", secTitle, "", "pres-menubar", _("Show the menu bar"), "",
                       "view", rMenu);

        GtkWidget* rBar = addSwitchRow(card, "cbShowPresentationToolbar", _("Show the top bar"));
        registerSearch(page.id, page.title, "presentation", secTitle, "", "pres-toolbar", _("Show the top bar"), "",
                       "view", rBar);

        GtkWidget* rSide = addSwitchRow(card, "cbShowPresentationSidebar", _("Show the page sidebar"));
        registerSearch(page.id, page.title, "presentation", secTitle, "", "pres-sidebar", _("Show the page sidebar"),
                       "", "view", rSide);

#ifdef __APPLE__
        setRowVisible("cbShowPresentationMenubar", false);
#endif
    }
}

void SettingsDialog::buildPenPage() {
    PageInfo& page = addPage("pen", _("Pen"), "mx-prefs-pen-symbolic");

    // Pressure
    {
        const char* secTitle = _("Pressure");
        GtkBox* card = addSection(page, "pressure", secTitle);

        GtkWidget* rOn = addSwitchRow(card, "cbSettingPresureSensitivity", _("Pressure sensitivity"),
                                      _("Line width follows how hard you press."));
        registerSearch(page.id, page.title, "pressure", secTitle, "", "pressure-on", _("Pressure sensitivity"),
                       _("Line width follows how hard you press."), "pressure stylus width stylus", rOn);

        const char* pDep = _("Turn on “Pressure sensitivity” or “Guess pressure from speed” to change this.");
        GtkWidget* rMin = addScaleRow(card, "scaleMinimumPressure", "adjustmentMinimumPressure", 2,
                                      _("Minimum pressure"), nullptr, pDep);
        registerSearch(page.id, page.title, "pressure", secTitle, "", "pressure-min", _("Minimum pressure"), "",
                       "pressure sensitivity scale stylus", rMin);

        GtkWidget* rMult = addScaleRow(card, "scalePressureMultiplier", "adjustmentPressureMultiplier", 2,
                                       _("Pressure multiplier"), nullptr, pDep);
        registerSearch(page.id, page.title, "pressure", secTitle, "", "pressure-mult", _("Pressure multiplier"), "",
                       "stylus", rMult);

        GtkWidget* rInfer = addSwitchRow(card, "cbEnablePressureInference", _("Guess pressure from speed"),
                                         _("Used when the device reports no pressure, such as a mouse."));
        registerSearch(page.id, page.title, "pressure", secTitle, "", "pressure-infer", _("Guess pressure from speed"),
                       _("Used when the device reports no pressure, such as a mouse."),
                       "pressure inference speed stylus", rInfer);
    }

    // Pen buttons
    {
        const char* secTitle = _("Pen buttons");
        const char* secNote =
                _("The tool is used while the button is held. Releasing it brings back the previous tool.");
        GtkBox* card = addSection(page, "stylusbuttons", secTitle, secNote);

        GtkWidget* b1Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        widgets["hboxPenButton1"] = b1Box;
        GtkWidget* rB1 =
                addRowToCard(card, "stylus-b1", _("Side button 1"), _("Usually the one closer to the tip"), b1Box);
        registerSearch(page.id, page.title, "stylusbuttons", secTitle, secNote, "stylus-b1", _("Side button 1"),
                       _("Usually the one closer to the tip"), "stylus button 1 barrel lower first side button", rB1);

        GtkWidget* b2Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        widgets["hboxPenButton2"] = b2Box;
        GtkWidget* rB2 =
                addRowToCard(card, "stylus-b2", _("Side button 2"), _("Usually the one farther from the tip"), b2Box);
        registerSearch(page.id, page.title, "stylusbuttons", secTitle, secNote, "stylus-b2", _("Side button 2"),
                       _("Usually the one farther from the tip"), "stylus button 2 barrel upper second side button",
                       rB2);

        GtkWidget* erBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        widgets["hboxEraser"] = erBox;
        GtkWidget* rEr = addRowToCard(card, "stylus-eraser", _("Eraser end"), nullptr, erBox);
        registerSearch(page.id, page.title, "stylusbuttons", secTitle, secNote, "stylus-eraser", _("Eraser end"), "",
                       "stylus eraser tip flip", rEr);
    }

    // Eraser cursor
    {
        const char* secTitle = _("Eraser cursor");
        GtkBox* card = addSection(page, "eraservis", secTitle);

        GtkWidget* rVis = addComboTextRow(card, "cbEraserVisibility",
                                          {_("Never"), _("Always"), _("When floating"), _("When touching")},
                                          _("Show the pen's eraser on the page"));
        registerSearch(page.id, page.title, "eraservis", secTitle, "", "eraser-visible",
                       _("Show the pen's eraser on the page"), "", "eraser visibility cursor stylus", rVis);
    }

    // Stroke smoothing
    {
        const char* secTitle = _("Stroke smoothing");
        const char* secNote =
                _("Smoothing reduces shaky lines. Higher values add more lag between the pen and the line.");
        GtkBox* card = addSection(page, "stabilizer", secTitle, secNote);

        // Averaging combo backed by listStabilizerAveragingMethods
        auto* avgModel = GTK_TREE_MODEL(builder.getObject("listStabilizerAveragingMethods"));
        GtkWidget* cbAvg = gtk_combo_box_new_with_model(avgModel);
        GtkCellRenderer* avgRend = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cbAvg), avgRend, TRUE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(cbAvg), avgRend, "text", 0, nullptr);
        gtk_widget_set_name(cbAvg, "cbStabilizerAveragingMethods");
        widgets["cbStabilizerAveragingMethods"] = cbAvg;
        GtkWidget* rAvg = addRowToCard(card, "cbStabilizerAveragingMethods", _("Averaging"), nullptr, cbAvg);
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-method", _("Averaging"), "",
                       "stabilizer stabilization smoothing shaky jitter input system stabilization", rAvg);

        GtkWidget* rBuf =
                addSpinRow(card, "sbStabilizerBuffersize", "adjustmentStabilizingBuffersize", 0, 1.0, _("Buffer size"));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-buffer", _("Buffer size"), "",
                       "input system stabilization", rBuf);

        GtkWidget* rSigma = addSpinRow(card, "sbStabilizerSigma", "adjustmentStabilizerSigma", 2, 1.0, _("Gaussian σ"),
                                       _("Higher values smooth more and add more lag between cursor and tip."));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-sigma", _("Gaussian σ"),
                       _("Higher values smooth more and add more lag between cursor and tip."),
                       "gaussian sigma input system stabilization", rSigma);

        // Preprocessor combo backed by listStabilizerPreprocessors
        auto* preModel = GTK_TREE_MODEL(builder.getObject("listStabilizerPreprocessors"));
        GtkWidget* cbPre = gtk_combo_box_new_with_model(preModel);
        GtkCellRenderer* preRend = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cbPre), preRend, TRUE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(cbPre), preRend, "text", 0, nullptr);
        gtk_widget_set_name(cbPre, "cbStabilizerPreprocessors");
        widgets["cbStabilizerPreprocessors"] = cbPre;
        GtkWidget* rPre = addRowToCard(card, "cbStabilizerPreprocessors", _("Preprocessor"), nullptr, cbPre);
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-pre", _("Preprocessor"), "",
                       "input system stabilization", rPre);

        GtkWidget* rDead = addSpinRow(card, "sbStabilizerDeadzoneRadius", "adjustmentStabilizerDeadzoneRadius", 2, 2.0,
                                      _("Deadzone radius"));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-deadzone", _("Deadzone radius"), "",
                       "input system stabilization", rDead);

        GtkWidget* rCusp = addSwitchRow(card, "cbStabilizerEnableCuspDetection", _("Cusp detection"));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-cusp", _("Cusp detection"), "",
                       "input system stabilization", rCusp);

        GtkWidget* rDrag = addSpinRow(card, "sbStabilizerDrag", "adjustmentStabilizerDrag", 2, 2.0, _("Drag"),
                                      _("Too small causes oscillations; too high disables stabilization."));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-drag", _("Drag"),
                       _("Too small causes oscillations; too high disables stabilization."),
                       "inertia input system stabilization", rDrag);

        GtkWidget* rMass = addSpinRow(card, "sbStabilizerMass", "adjustmentStabilizerMass", 2, 2.0, _("Mass"),
                                      _("Higher mass adds more inertia and stabilization."));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-mass", _("Mass"),
                       _("Higher mass adds more inertia and stabilization."), "inertia input system stabilization",
                       rMass);

        GtkWidget* rFin =
                addSwitchRow(card, "cbStabilizerEnableFinalizeStroke", _("Finish the stroke at the pen position"),
                             _("Fills the gap smoothing can leave at the end of a stroke."), false,
                             _("Choose an averaging method or preprocessor to change this."));
        registerSearch(page.id, page.title, "stabilizer", secTitle, secNote, "stab-finalize",
                       _("Finish the stroke at the pen position"),
                       _("Fills the gap smoothing can leave at the end of a stroke."), "input system stabilization",
                       rFin);
    }

    // Stroke start fix
    {
        const char* secTitle = _("Stroke start fix");
        const char* secNote =
                _("Some pens draw a stray mark at the start of each stroke. Ignoring the first events removes it.");
        GtkBox* card = addSection(page, "artifact", secTitle, secNote);

        GtkWidget* rOn = addSwitchRow(card, "cbIgnoreFirstStylusEvents", _("Ignore the first events of each stroke"));
        registerSearch(page.id, page.title, "artifact", secTitle, secNote, "artifact-on",
                       _("Ignore the first events of each stroke"), "",
                       "artifact workaround glitch hook start stylus artifact workaround", rOn);

        GtkWidget* rNum = addSpinRow(card, "spNumIgnoredStylusEvents", "adjustmentIgnoreStylusEvents", 0, 1.0,
                                     _("Events to ignore"), nullptr, nullptr,
                                     _("Turn on “Ignore the first events of each stroke” to change this."));
        registerSearch(page.id, page.title, "artifact", secTitle, secNote, "artifact-n", _("Events to ignore"), "",
                       "stylus artifact workaround", rNum);
    }
}

void SettingsDialog::buildMousePage() {
    PageInfo& page = addPage("mouse", _("Mouse"), "mx-prefs-mouse-symbolic");

    const char* secTitle = _("Mouse buttons");
    const char* secNote = _("The tool is used while the button is held. Releasing it brings back the previous tool.");
    GtkBox* card = addSection(page, "mousebuttons", secTitle, secNote);

    auto addMouseBtnRow = [&](const char* boxId, const char* rowId, const char* label, const char* kw) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        widgets[boxId] = box;
        GtkWidget* row = addRowToCard(card, rowId, label, nullptr, box);
        registerSearch(page.id, page.title, "mousebuttons", secTitle, secNote, rowId, label, "",
                       joinKeywords(kw, "mouse").c_str(), row);
    };

    addMouseBtnRow("hboxLeftMouse", "mouse-left", _("Left button"), "mouse left click primary");
    addMouseBtnRow("hboxMiddleMouse", "mouse-middle", _("Middle button"), "mouse middle click wheel");
    addMouseBtnRow("hboxRightMouse", "mouse-right", _("Right button"), "mouse right click");
    addMouseBtnRow("hboxMouse4", "mouse-4", _("Back button"), "mouse 4th button");
    addMouseBtnRow("hboxMouse5", "mouse-5", _("Forward button"), "mouse 5th button");
}

void SettingsDialog::buildTouchPage() {
    PageInfo& page = addPage("touch", _("Touch"), "mx-prefs-touch-symbolic");

    // Palm rejection
    {
        const char* secTitle = _("Palm rejection");
        const char* secNote =
                _("If your hardware has no palm rejection, MoreXournal can turn off touch while the pen is near the "
                  "screen.");
        GtkBox* card = addSection(page, "handrecog", secTitle, secNote);

        GtkWidget* rOn = addSwitchRow(card, "cbDisableTouchOnPenNear", _("Turn off touch while the pen is near"));
        registerSearch(page.id, page.title, "handrecog", secTitle, secNote, "palm-on",
                       _("Turn off touch while the pen is near"), "",
                       "hand recognition palm rejection disable touch touchscreen", rOn);

        const char* palmDep = _("Turn on “Turn off touch while the pen is near” to change this.");
        GtkWidget* rMethod = addComboTextRow(card, "cbTouchDisableMethod", {_("Autodetect"), _("X11"), _("Custom")},
                                             _("Method"), nullptr, false, palmDep);
        registerSearch(page.id, page.title, "handrecog", secTitle, secNote, "palm-method", _("Method"), "",
                       "touchscreen", rMethod);

        GtkWidget* rTime = addSpinRow(card, "spTouchDisableTimeout", "adjustmentTouchTImeout", 1, 0.5,
                                      _("Turn touch back on after"), nullptr, _("s"), palmDep);
        registerSearch(page.id, page.title, "handrecog", secTitle, secNote, "palm-timeout",
                       _("Turn touch back on after"), "", "touchscreen", rTime);
    }

    // Finger drawing
    {
        const char* secTitle = _("Finger drawing");
        GtkBox* card = addSection(page, "touchdraw", secTitle);

        GtkWidget* rDraw = addSwitchRow(card, "cbTouchDrawing", _("Draw with your finger"),
                                        _("Otherwise one finger scrolls the page."));
        registerSearch(page.id, page.title, "touchdraw", secTitle, "", "touch-draw", _("Draw with your finger"),
                       _("Otherwise one finger scrolls the page."), "touch drawing finger touchscreen", rDraw);
    }

    // Pinch to zoom
    {
        const char* secTitle = _("Pinch to zoom");
        GtkBox* card = addSection(page, "pinch", secTitle);

        GtkWidget* rOn = addSwitchRow(card, "cbEnableZoomGestures", _("Pinch to zoom"),
                                      _("Takes effect after a restart."), true);
        registerSearch(page.id, page.title, "pinch", secTitle, "", "pinch-on", _("Pinch to zoom"),
                       _("Takes effect after a restart."), "zoom gestures pinch two fingers touchscreen zoom gestures",
                       rOn);

        GtkWidget* rThr = addSpinRow(card, "spTouchZoomStartThreshold", "adjustmentZoomStartThreshold", 1, 0.5,
                                     _("Start zooming after"), _("How much farther apart the two fingers must move."),
                                     "%", _("Turn on “Pinch to zoom” to change this."));
        registerSearch(page.id, page.title, "pinch", secTitle, "", "pinch-threshold", _("Start zooming after"),
                       _("How much farther apart the two fingers must move."), "touchscreen zoom gestures", rThr);
    }

    // Touch scrolling
    {
        const char* secTitle = _("Touch scrolling");
        GtkBox* card = addSection(page, "touchscroll", secTitle);

        GtkWidget* rInertial =
                addSwitchRow(card, "cbDisableGtkInertialScroll", _("Turn off GTK inertial scrolling"),
                             _("Works around scrolling bugs on some systems. Takes effect after a restart."), true);
        registerSearch(page.id, page.title, "touchscroll", secTitle, "", "touch-inertial",
                       _("Turn off GTK inertial scrolling"),
                       _("Works around scrolling bugs on some systems. Takes effect after a restart."),
                       "kinetic scroll touchscreen", rInertial);
    }

    // Touchscreen device override (legacy BUTTON_TOUCH with device selector)
    {
        const char* secTitle = _("Touchscreen device");
        const char* secNote =
                _("You can configure a device not identified by GTK as a touchscreen to behave as if it were one.");
        GtkBox* card = addSection(page, "touchdevice", secTitle, secNote);

        GtkWidget* touchBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        widgets["hboxTouch"] = touchBox;
        GtkWidget* rTouchDev =
                addRowToCard(card, "touch-device", _("Device action"), nullptr, touchBox, false, nullptr, true);
        registerSearch(page.id, page.title, "touchdevice", secTitle, secNote, "touch-device", _("Device action"), "",
                       "touchscreen device disable drawing", rTouchDev);
    }
}

void SettingsDialog::buildToolsPage() {
    PageInfo& page = addPage("tools", _("Tools"), "mx-prefs-tools-symbolic");

    // Automatic shapes (+ collapsed Shape recognition thresholds expander below)
    {
        const char* secTitle = _("Automatic shapes");
        const char* secNote =
                _("Hold the pen still at the end of a stroke to turn it into a line, triangle, rectangle or circle.");
        GtkBox* card = addSection(page, "autoshapes", secTitle, secNote);

        GtkWidget* rOn = addSwitchRow(card, "cbHoldToShapeEnabled", _("Turn strokes into shapes when you hold still"));
        registerSearch(page.id, page.title, "autoshapes", secTitle, secNote, "shape-on",
                       _("Turn strokes into shapes when you hold still"), "",
                       "hold to shape recognizer automatic shapes drawing area", rOn);

        GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        addCssClass(sep, "mx-prefs-row-sep");
        gtk_box_pack_start(card, sep, FALSE, FALSE, 0);
        gtk_box_pack_start(card, shapeSettingsPanel->getPanel(), FALSE, FALSE, 0);

        registerSearch(page.id, page.title, "autoshapes", secTitle, secNote, "shape-delay", _("Hold time"), "",
                       "delay hold shape drawing area", shapeSettingsPanel->getRowWidget("shape-delay"));
        registerSearch(page.id, page.title, "autoshapes", secTitle, secNote, "shape-jitter",
                       _("Allowed movement while holding"), "", "jitter drawing area",
                       shapeSettingsPanel->getRowWidget("shape-jitter"));
        registerSearch(page.id, page.title, "autoshapes", secTitle, secNote, "shape-sens", _("Recognition"), "",
                       "sensitivity drawing area", shapeSettingsPanel->getRowWidget("shape-sens"));
        registerSearch(page.id, page.title, "autoshapes", secTitle, secNote, "shape-min",
                       _("Smaller strokes stay freehand"), "", "minimum size drawing area",
                       shapeSettingsPanel->getRowWidget("shape-min"));
        registerSearch(page.id, page.title, "autoshapes", secTitle, secNote, "shape-pad", _("Try it"), "",
                       "test pad drawing area", shapeSettingsPanel->getRowWidget("shape-pad"));

        // Collapsed expander card directly underneath Automatic shapes
        GtkWidget* advCard = shapeSettingsPanel->getAdvancedCard();
        gtk_box_pack_start(page.contentBox, advCard, FALSE, FALSE, 0);

        const char* advTitle = _("Shape recognition thresholds");
        GtkExpander* exp = shapeSettingsPanel->getAdvancedExpander();
        registerSearch(page.id, page.title, "shapeadv", advTitle, "", "shapeadv-line", _("Line straightness"), "",
                       "threshold line", shapeSettingsPanel->getRowWidget("shapeadv-line"), exp);
        registerSearch(page.id, page.title, "shapeadv", advTitle, "", "shapeadv-circle", _("Circle roundness"), "",
                       "threshold circle", shapeSettingsPanel->getRowWidget("shapeadv-circle"), exp);
        registerSearch(page.id, page.title, "shapeadv", advTitle, "", "shapeadv-rect", _("Rectangle corner angle"), "",
                       "threshold rectangle", shapeSettingsPanel->getRowWidget("shapeadv-rect"), exp);
        registerSearch(page.id, page.title, "shapeadv", advTitle, "", "shapeadv-open", _("Open corners allowed"), "",
                       "triangle rectangle corners", shapeSettingsPanel->getRowWidget("shapeadv-open"), exp);
        registerSearch(page.id, page.title, "shapeadv", advTitle, "", "shapeadv-reset",
                       _("Restore the default thresholds"), "", "restore defaults",
                       shapeSettingsPanel->getRowWidget("shapeadv-reset"), exp);
    }

    // Tap action
    {
        const char* secTitle = _("Tap action");
        const char* secNote =
                _("A short tap with a drawing tool can select an object or open the floating toolbox instead of "
                  "drawing a dot.");
        GtkBox* card = addSection(page, "tapaction", secTitle, secNote);
        const char* tapDep = _("Turn on “Treat short taps as an action” to change this.");

        GtkWidget* rOn = addSwitchRow(card, "cbStrokeFilterEnabled", _("Treat short taps as an action"));
        registerSearch(page.id, page.title, "tapaction", secTitle, secNote, "tap-on",
                       _("Treat short taps as an action"), "",
                       "tap stroke filter floating toolbox dot drawing area action on tool tap", rOn);

        GtkWidget* rSel = addSwitchRow(card, "cbTrySelectOnStrokeFiltered", _("Try to select an object first"), nullptr,
                                       false, tapDep);
        registerSearch(page.id, page.title, "tapaction", secTitle, secNote, "tap-select",
                       _("Try to select an object first"), "", "drawing area action on tool tap", rSel);

        GtkWidget* rTb = addSwitchRow(card, "cbDoActionOnStrokeFiltered", _("Show the floating toolbox"), nullptr,
                                      false, tapDep);
        registerSearch(page.id, page.title, "tapaction", secTitle, secNote, "tap-toolbox",
                       _("Show the floating toolbox"), "", "floating toolbox drawing area action on tool tap", rTb);

        GtkWidget* rTime = addSpinRow(card, "spStrokeIgnoreTime", "adjustmentStrokeIgnoreTime", 0, 1.0, _("Time limit"),
                                      nullptr, _("ms"), tapDep);
        registerSearch(page.id, page.title, "tapaction", secTitle, secNote, "tap-time", _("Time limit"), "",
                       "drawing area action on tool tap", rTime);

        GtkWidget* rLen = addSpinRow(card, "spStrokeIgnoreLength", "adjustmentStrokeIgnoreLength", 2, 0.1,
                                     _("Length limit"), nullptr, _("mm"), tapDep);
        registerSearch(page.id, page.title, "tapaction", secTitle, secNote, "tap-length", _("Length limit"), "",
                       "drawing area action on tool tap", rLen);

        GtkWidget* rFreq = addSpinRow(card, "spStrokeSuccessiveTime", "adjustmentStrokeSuccessiveTime", 0, 1.0,
                                      _("Frequency limit"), nullptr, _("ms"), tapDep);
        registerSearch(page.id, page.title, "tapaction", secTitle, secNote, "tap-freq", _("Frequency limit"), "",
                       "drawing area action on tool tap", rFreq);
    }

    // Default tool
    {
        const char* secTitle = _("Default tool");
        const char* secNote = _("The tool and settings the Default tool button selects.");
        GtkBox* card = addSection(page, "defaulttool", secTitle, secNote);

        GtkWidget* defBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        widgets["hboxDefaultTool"] = defBox;
        GtkWidget* rDef = addRowToCard(card, "default-tool", _("Tool"), nullptr, defBox);
        registerSearch(page.id, page.title, "defaulttool", secTitle, secNote, "default-tool", _("Tool"), "",
                       "default button tool tools", rDef);
    }

    // Laser pointer
    {
        const char* secTitle = _("Laser pointer");
        GtkBox* card = addSection(page, "laser", secTitle);

        GtkWidget* rLaser = addSpinRow(card, "sbLaserFadeOutTime", "adjustmentLaserFadeOutTime", 0, 10.0,
                                       _("Fade out after"), nullptr, _("ms"));
        registerSearch(page.id, page.title, "laser", secTitle, "", "laser-fade", _("Fade out after"), "",
                       "laser pointer fade presentation tools", rLaser);
    }
}

#ifdef ENABLE_AUDIO
void SettingsDialog::buildAudioPage() {
    PageInfo& page = addPage("audio", _("Audio recording"), "mx-prefs-audio-symbolic");
    const char* audioDep = _("Turn on “Audio recording” to change this.");

    // Audio recording on/off (inverted polarity vs Settings::isAudioDisabled)
    {
        const char* secTitle = _("Audio recording");
        GtkBox* card = addSection(page, "audioon", secTitle);

        GtkWidget* rOn = addSwitchRow(card, "cbDisableAudio", _("Audio recording"),
                                      _("Record audio linked to your strokes. Takes effect after a restart."), true);
        registerSearch(page.id, page.title, "audioon", secTitle, "", "audio-on", _("Audio recording"),
                       _("Record audio linked to your strokes. Takes effect after a restart."),
                       "audio sound microphone disable audio recording", rOn);
    }

    // Recording
    {
        const char* secTitle = _("Recording");
        GtkBox* card = addSection(page, "audiorec", secTitle);

        GtkWidget* folderBtn = gtk_button_new_with_label(_("Select folder"));
        gtk_widget_set_name(folderBtn, "fcAudioPath");
        widgets["fcAudioPath"] = folderBtn;
        GtkWidget* rFolder =
                addRowToCard(card, "fcAudioPath", _("Save recordings in"), nullptr, folderBtn, false, audioDep);
        registerSearch(page.id, page.title, "audiorec", secTitle, "", "audio-folder", _("Save recordings in"), "",
                       "audio storage folder audio recording", rFolder);

        GtkWidget* rIn = addComboTextRow(card, "cbAudioInputDevice", {}, _("Microphone"), nullptr, false, audioDep);
        registerSearch(page.id, page.title, "audiorec", secTitle, "", "audio-in", _("Microphone"), "",
                       "input device microphone audio recording", rIn);

        GtkWidget* rRate = addComboTextRow(card, "cbAudioSampleRate",
                                           {_("16000 Hz"), _("44100 Hz"), _("96000 Hz"), _("192000 Hz")},
                                           _("Sample rate"), nullptr, false, audioDep);
        registerSearch(page.id, page.title, "audiorec", secTitle, "", "audio-rate", _("Sample rate"), "",
                       "quality audio recording", rRate);

        GtkWidget* rGain =
                addSpinRow(card, "spAudioGain", "adjustmentAudioGain", 2, 0.1, _("Gain"), nullptr, nullptr, audioDep);
        registerSearch(page.id, page.title, "audiorec", secTitle, "", "audio-gain", _("Gain"), "", "audio recording",
                       rGain);
    }

    // Playback
    {
        const char* secTitle = _("Playback");
        const char* secNote = _("Changes take effect on new recordings and playbacks.");
        GtkBox* card = addSection(page, "audioplay", secTitle, secNote);

        GtkWidget* rOut = addComboTextRow(card, "cbAudioOutputDevice", {}, _("Speakers"), nullptr, false, audioDep);
        registerSearch(page.id, page.title, "audioplay", secTitle, secNote, "audio-out", _("Speakers"), "",
                       "output device audio recording", rOut);

        GtkWidget* rSeek = addSpinRow(card, "spDefaultSeekTime", "adjustmentSeekTime", 2, 0.1,
                                      _("Skip forward and back by"), nullptr, _("s"), audioDep);
        registerSearch(page.id, page.title, "audioplay", secTitle, secNote, "audio-seek", _("Skip forward and back by"),
                       "", "seek time audio recording", rSeek);
    }
}
#endif

void SettingsDialog::buildLatexPage() {
    PageInfo& page = addPage("latex", _("LaTeX"), "mx-prefs-latex-symbolic");
    gtk_box_pack_start(page.contentBox, this->latexPanel.getPanel(), FALSE, FALSE, 0);

    const char* genTitle = _("LaTeX");
    registerSearch(page.id, page.title, "latexgen", genTitle, "", "latex-cmd", _("Generation command"), "",
                   "latex command pdflatex formula latex", latexPanel.getRowWidget("rowLatexGenCmd"));
    registerSearch(page.id, page.title, "latexgen", genTitle, "", "latex-check",
                   _("Check LaTeX is installed before running"), "", "latex", latexPanel.getRowWidget("rowLatexCheck"));
    registerSearch(page.id, page.title, "latexgen", genTitle, "", "latex-template", _("Template file"), "",
                   "latex template", latexPanel.getRowWidget("rowLatexTemplate"));
    registerSearch(page.id, page.title, "latexgen", genTitle, "", "latex-default", _("Default formula"), "", "latex",
                   latexPanel.getRowWidget("rowLatexDefault"));

    const char* edTitle = _("Formula editor");
    registerSearch(page.id, page.title, "latexeditor", edTitle, "", "latex-theme", _("Editor theme"), "",
                   "latex theme sourceview", latexPanel.getRowWidget("rowLatexTheme"));
    registerSearch(page.id, page.title, "latexeditor", edTitle, "", "latex-lines", _("Line numbers"), "", "latex",
                   latexPanel.getRowWidget("rowLatexLines"));
    registerSearch(page.id, page.title, "latexeditor", edTitle, "", "latex-highlight", _("Syntax highlighting"), "",
                   "latex", latexPanel.getRowWidget("rowLatexHighlight"));
    registerSearch(page.id, page.title, "latexeditor", edTitle, "", "latex-indent", _("Auto-indent"), "", "latex",
                   latexPanel.getRowWidget("rowLatexIndent"));
    registerSearch(page.id, page.title, "latexeditor", edTitle, "", "latex-wrap", _("Wrap long lines"), "", "latex",
                   latexPanel.getRowWidget("rowLatexWrap"));
    registerSearch(page.id, page.title, "latexeditor", edTitle, "", "latex-font", _("Font"), "", "editor font latex",
                   latexPanel.getRowWidget("boxCustomFontOptions"));

    const char* extTitle = _("External LaTeX editor");
    registerSearch(page.id, page.title, "latexext", extTitle, "", "latex-ext-on", _("Edit formulas in another program"),
                   "", "external editor latex", latexPanel.getRowWidget("rowLatexExtOn"));
    registerSearch(page.id, page.title, "latexext", extTitle, "", "latex-ext-cmd", _("Editor command"), "", "latex",
                   latexPanel.getRowWidget("rowLatexExtCmd"));
    registerSearch(page.id, page.title, "latexext", extTitle, "", "latex-ext-ext", _("Temporary file extension"), "",
                   "latex", latexPanel.getRowWidget("rowLatexExtExt"));
    registerSearch(page.id, page.title, "latexext", extTitle, "", "latex-ext-confirm",
                   _("Insert the formula when the editor closes"), "", "latex",
                   latexPanel.getRowWidget("rowLatexExtConfirm"));
}

void SettingsDialog::buildAiPage() {
    PageInfo& page = addPage("ai", _("AI assistants"), "mx-prefs-ai-symbolic");
    gtk_box_pack_start(page.contentBox, this->mcpSettingsPanel->getPanel(), FALSE, FALSE, 0);

    const char* aiNote =
            _("Let AI tools on this computer, such as Claude Code or Codex, see the open document through the Model "
              "Context Protocol (MCP). Nothing leaves this computer unless the AI tool itself sends it.");
    registerSearch(page.id, page.title, "aiconnect", _("AI assistants"), aiNote, "ai-on",
                   _("Allow AI assistants to connect"), "", "mcp claude codex ai assistant ai assistants",
                   mcpSettingsPanel->getTargetWidget("ai-on"));
    registerSearch(page.id, page.title, "aiconnect", _("AI assistants"), aiNote, "ai-edit", _("Allow edits"),
                   _("Assistants can add text, strokes, shapes and pages. Each change is one undo step, and assistants "
                     "can't delete what you wrote."),
                   "ai edit write ai assistants", mcpSettingsPanel->getTargetWidget("ai-edit"));
    registerSearch(page.id, page.title, "aiaccess", _("Connection"), "", "ai-address", _("Address"), "",
                   "mcp url address ai assistants", mcpSettingsPanel->getTargetWidget("ai-address"));
    registerSearch(page.id, page.title, "aiaccess", _("Connection"), "", "ai-token", _("Access token"),
                   _("A new token disconnects every AI tool until you paste the new setup command."),
                   "token bearer password ai assistants", mcpSettingsPanel->getTargetWidget("ai-token"));
    registerSearch(page.id, page.title, "aiaccess", _("Connection"), "", "ai-clients", _("Connected now"), "",
                   "mcp clients ai assistants", mcpSettingsPanel->getTargetWidget("ai-clients"));
    registerSearch(page.id, page.title, "aisetup", _("Set up an assistant"), "", "ai-setup", _("Set up an assistant"),
                   "", "claude code codex claude desktop setup command ai assistants",
                   mcpSettingsPanel->getTargetWidget("ai-setup"));
}

void SettingsDialog::buildAdvancedPage() {
    PageInfo& page = addPage(
            "advanced", _("Advanced"), "mx-prefs-advanced-symbolic",
            _("Settings most people never need to change. Use them to fix a device that is not detected correctly, "
              "tune performance, or try experimental behavior."));

    // Input devices
    {
        const char* secTitle = _("Input devices");
        const char* secNote =
                _("Choose how each device behaves. Use this when a pen or touchscreen is not detected correctly.");
        GtkBox* card = addSection(page, "devices", secTitle, secNote);

        GtkWidget* devBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_margin_start(devBox, 12);
        gtk_widget_set_margin_end(devBox, 12);
        gtk_widget_set_margin_top(devBox, 12);
        gtk_widget_set_margin_bottom(devBox, 12);
        widgets["hboxInputDeviceClasses"] = devBox;
        gtk_box_pack_start(card, devBox, FALSE, FALSE, 0);

        registerSearch(page.id, page.title, "devices", secTitle, secNote, "devices-panel",
                       _("Input devices and testing area"),
                       _("Change device classes, emulate tip contact, or inspect input logs."),
                       "device class tablet wacom stylus touchscreen mouse detected input system", devBox);
    }

    // Drawing input
    {
        const char* secTitle = _("Drawing input");
        GtkBox* card = addSection(page, "drawoutside", secTitle);

        GtkWidget* rOut = addSwitchRow(card, "cbInputSystemDrawOutsideWindow", _("Keep drawing past the window edge"));
        registerSearch(page.id, page.title, "drawoutside", secTitle, "", "draw-outside",
                       _("Keep drawing past the window edge"), "", "draw outside window input system options", rOut);
    }

    // Custom palm rejection commands
    {
        const char* secTitle = _("Custom palm rejection commands");
        const char* secNote =
                _("Used with the Custom method. They run on the interface thread, so they must return quickly.");
        GtkBox* card = addSection(page, "palmcmds", secTitle, secNote);
        const char* cmdDep =
                _("Turn on “Turn off touch while the pen is near” and select the Custom method to change this.");

        GtkWidget* rEn = addEntryRow(card, "txtEnableTouchCommand", _("Command to turn touch on"), nullptr, true,
                                     "btTestEnable", _("Test"), cmdDep);
        registerSearch(page.id, page.title, "palmcmds", secTitle, secNote, "palm-cmd-on", _("Command to turn touch on"),
                       "", "custom command enable touch touchscreen", rEn);

        GtkWidget* rDis = addEntryRow(card, "txtDisableTouchCommand", _("Command to turn touch off"), nullptr, true,
                                      "btTestDisable", _("Test"), cmdDep);
        registerSearch(page.id, page.title, "palmcmds", secTitle, secNote, "palm-cmd-off",
                       _("Command to turn touch off"), "", "custom command disable touch touchscreen", rDis);
    }

    // Text tool
    {
        const char* secTitle = _("Text tool");
        GtkBox* card = addSection(page, "texttab", secTitle);

        GtkWidget* rSp = addSwitchRow(card, "cbUseSpacesAsTab", _("Tab inserts spaces"));
        registerSearch(page.id, page.title, "texttab", secTitle, "", "text-spaces", _("Tab inserts spaces"), "",
                       "indent tab spaces text input system tab", rSp);

        GtkWidget* rNum =
                addSpinRow(card, "sbNumberOfSpacesForTab", "adjustmentNumberOfSpacesForTab", 0, 2.0,
                           _("Spaces per Tab"), nullptr, nullptr, _("Turn on “Tab inserts spaces” to change this."));
        registerSearch(page.id, page.title, "texttab", secTitle, "", "text-spaces-n", _("Spaces per Tab"), "",
                       "input system tab", rNum);
    }

    // Opening documents
    {
        const char* secTitle = _("Opening documents");
        GtkBox* card = addSection(page, "autoload", secTitle);

        GtkWidget* rRec = addSwitchRow(card, "cbAutoloadMostRecent", _("Reopen the last document at startup"));
        registerSearch(page.id, page.title, "autoload", secTitle, "", "autoload-recent",
                       _("Reopen the last document at startup"), "",
                       "startup recent restore session load save autoloading", rRec);

        GtkWidget* rXoj = addSwitchRow(card, "cbAutoloadXoj", _("Open the matching notes when opening a PDF"),
                                       _("If lecture.pdf has a lecture.xopp next to it, open the notes instead."));
        registerSearch(page.id, page.title, "autoload", secTitle, "", "autoload-xopp",
                       _("Open the matching notes when opening a PDF"),
                       _("If lecture.pdf has a lecture.xopp next to it, open the notes instead."),
                       "autoload journal xoj pdf load save autoloading", rXoj);
    }

    // Edge scrolling
    {
        const char* secTitle = _("Edge scrolling");
        const char* secNote = _("Scrolls the page when you drag a selection to the window edge.");
        GtkBox* card = addSection(page, "edgepan", secTitle, secNote);

        GtkWidget* rSpeed =
                addSpinRow(card, "edgePanSpeed", "adjustmentEdgePanSpeed", 0, 1.0, _("Base speed"), nullptr, "%");
        registerSearch(page.id, page.title, "edgepan", secTitle, secNote, "edge-speed", _("Base speed"), "",
                       "selection edge panning view selection edge panning", rSpeed);

        GtkWidget* rMult = addSpinRow(card, "edgePanMaxMult", "adjustmentEdgePanMaxMult", 1, 0.5, _("Maximum speed-up"),
                                      nullptr, "×");
        registerSearch(page.id, page.title, "edgepan", secTitle, secNote, "edge-mult", _("Maximum speed-up"), "",
                       "view selection edge panning", rMult);
    }

    // Page preloading
    {
        const char* secTitle = _("Page preloading");
        GtkBox* card = addSection(page, "preload", secTitle);

        GtkWidget* rBef = addSpinRow(card, "preloadPagesBefore", "adjustmentPreloadPagesBefore", 0, 1.0,
                                     _("Pages to preload before"));
        registerSearch(page.id, page.title, "preload", secTitle, "", "preload-before", _("Pages to preload before"), "",
                       "performance cache memory view performance", rBef);

        GtkWidget* rAft = addSpinRow(card, "preloadPagesAfter", "adjustmentPreloadPagesAfter", 0, 1.0,
                                     _("Pages to preload after"));
        registerSearch(page.id, page.title, "preload", secTitle, "", "preload-after", _("Pages to preload after"), "",
                       "performance view performance", rAft);

        GtkWidget* rClean = addSwitchRow(card, "cbEagerPageCleanup", _("Free hidden pages while scrolling"),
                                         _("Uses less memory on large documents."));
        registerSearch(page.id, page.title, "preload", secTitle, "", "preload-cleanup",
                       _("Free hidden pages while scrolling"), _("Uses less memory on large documents."),
                       "eager cleanup memory view performance", rClean);
    }

    // PDF rendering
    {
        const char* secTitle = _("PDF rendering");
        GtkBox* card = addSection(page, "pdfcache", secTitle);

        GtkWidget* rPdf = addSpinRow(card, "spReRenderThreshold", "adjustmentReRenderThreshold", 0, 2.0,
                                     _("Re-render PDFs when zoom changes by more than"),
                                     _("Sharper PDF backgrounds while zooming. Printing is not affected."), "%");
        registerSearch(page.id, page.title, "pdfcache", secTitle, "", "pdf-rerender",
                       _("Re-render PDFs when zoom changes by more than"),
                       _("Sharper PDF backgrounds while zooming. Printing is not affected."),
                       "pdf cache quality blurry drawing area pdf cache", rPdf);
    }

    // Screen calibration
    {
        const char* secTitle = _("Screen calibration");
        const char* secNote =
                // xgettext:no-c-format
                _("Makes 100% zoom show real size. Hold a ruler to the screen and move the slider until both match.");
        GtkBox* card = addSection(page, "dpi", secTitle, secNote);

        GtkWidget* rAuto = addSwitchRow(card, "zoomCallibAutomaticCb", _("Detect automatically"));
        registerSearch(page.id, page.title, "dpi", secTitle, secNote, "dpi-auto", _("Detect automatically"), "",
                       "dpi calibration ruler real size zoom display dpi", rAuto);

        GtkWidget* rulerBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        auto* zoomAdj = GTK_ADJUSTMENT(builder.getObject("adjustmentZoom"));
        GtkWidget* slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, zoomAdj);
        gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
        gtk_range_set_round_digits(GTK_RANGE(slider), 0);
        gtk_widget_set_hexpand(slider, TRUE);
        gtk_widget_set_name(slider, "zoomCallibSlider");
        widgets["zoomCallibSlider"] = slider;

        gtk_box_pack_start(GTK_BOX(rulerBox), slider, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(rulerBox), callib, FALSE, FALSE, 0);
        gtk_widget_show(callib);

        GtkWidget* rRuler = addRowToCard(card, "dpi-ruler", _("Screen ruler (cm)"), nullptr, rulerBox, false,
                                         _("Available when “Detect automatically” is off."), true);
        registerSearch(page.id, page.title, "dpi", secTitle, secNote, "dpi-ruler", _("Screen ruler (cm)"), "",
                       "zoom display dpi", rRuler);
    }

    // Modifiers by draw direction (Experimental)
    {
        const char* secTitle = _("Modifiers by draw direction");
        const char* secNote = _("Drawing a shape up or left acts as if Shift or Ctrl were held.");
        GtkBox* card = addSection(page, "drawdir", secTitle, secNote, _("Experimental"));

        GtkWidget* rOn = addSwitchRow(card, "cbDrawDirModsEnabled", _("Set modifiers by draw direction"));
        registerSearch(page.id, page.title, "drawdir", secTitle, secNote, "drawdir-on",
                       _("Set modifiers by draw direction"), "",
                       "experimental direction modifier shift ctrl drawing area", rOn);

        GtkWidget* rRad =
                addSpinRow(card, "spDrawDirModsRadius", "adjustmentDrawDirModRadius", 0, 1.0, _("Direction radius"),
                           nullptr, _("px"), _("Turn on “Set modifiers by draw direction” to change this."));
        registerSearch(page.id, page.title, "drawdir", secTitle, secNote, "drawdir-radius", _("Direction radius"), "",
                       "drawing area", rRad);
    }
}

void SettingsDialog::buildSearchResultsPage() {
    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(contentBox, 4);
    gtk_widget_set_margin_bottom(contentBox, 40);
    gtk_widget_set_margin_start(contentBox, 24);
    gtk_widget_set_margin_end(contentBox, 24);
    gtk_widget_set_valign(contentBox, GTK_ALIGN_START);

    gtk_container_add(GTK_CONTAINER(scroller), contentBox);
    g_signal_connect(scroller, "size-allocate", G_CALLBACK(updateCenteredMargins), contentBox);
    gtk_stack_add_named(contentStack, scroller, "__search__");
    searchResultsBox = GTK_BOX(contentBox);
}

void SettingsDialog::onNavRowSelected(GtkListBoxRow* row) {
    if (!row) {
        return;
    }
    const auto* pageId = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "mx-page-id"));
    if (!pageId) {
        return;
    }
    activePageId = pageId;

    // Clicking a page in the sidebar clears any active search
    const char* q = gtk_entry_get_text(GTK_ENTRY(searchEntry));
    if (q && *q) {
        gtk_entry_set_text(GTK_ENTRY(searchEntry), "");
    }

    gtk_stack_set_visible_child_name(contentStack, activePageId.c_str());
}

void SettingsDialog::onSearchChanged() {
    const char* rawQuery = gtk_entry_get_text(GTK_ENTRY(searchEntry));
    const std::string query = rawQuery ? rawQuery : "";
    const auto tokens = xoj::prefs::tokenizeSearchQuery(query);

    if (tokens.empty()) {
        // Restore sidebar badges and opacity
        for (const auto& p: pages) {
            removeCssClass(GTK_WIDGET(p.navRow), "mx-prefs-nav-dim");
            gtk_widget_hide(GTK_WIDGET(p.countLabel));
        }
        gtk_stack_set_visible_child_name(contentStack, activePageId.c_str());
        return;
    }

    const auto matchIndices = xoj::prefs::filterSearchEntries(searchEntries, query);

    // Group matching indices by page, preserving page order
    std::unordered_map<std::string, std::vector<size_t>> matchesByPage;
    for (size_t idx: matchIndices) {
        matchesByPage[searchTargets[idx].entry.pageId].push_back(idx);
    }

    // Update sidebar counts and dim non-matching pages
    for (const auto& p: pages) {
        auto it = matchesByPage.find(p.id);
        if (it != matchesByPage.end() && !it->second.empty()) {
            removeCssClass(GTK_WIDGET(p.navRow), "mx-prefs-nav-dim");
            gtk_label_set_text(p.countLabel, std::to_string(it->second.size()).c_str());
            gtk_widget_show(GTK_WIDGET(p.countLabel));
        } else {
            addCssClass(GTK_WIDGET(p.navRow), "mx-prefs-nav-dim");
            gtk_widget_hide(GTK_WIDGET(p.countLabel));
        }
    }

    // Clear previous search result widgets
    GList* children = gtk_container_get_children(GTK_CONTAINER(searchResultsBox));
    for (GList* it = children; it != nullptr; it = it->next) {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }
    g_list_free(children);

    if (matchIndices.empty()) {
        GtkWidget* emptyBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        addCssClass(emptyBox, "mx-prefs-empty");
        gtk_widget_set_halign(emptyBox, GTK_ALIGN_CENTER);

        GtkWidget* titleLbl = gtk_label_new(_("No settings match"));
        addCssClass(titleLbl, "mx-prefs-section-title");
        GtkWidget* hintLbl = gtk_label_new(_("Try another word, such as pen, zoom or backup."));
        addCssClass(hintLbl, "mx-prefs-row-sub");

        gtk_box_pack_start(GTK_BOX(emptyBox), titleLbl, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(emptyBox), hintLbl, FALSE, FALSE, 0);
        gtk_box_pack_start(searchResultsBox, emptyBox, FALSE, FALSE, 0);
    } else {
        GtkWidget* heading = gtk_label_new(_("Search results"));
        gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
        addCssClass(heading, "mx-prefs-page-title");
        gtk_box_pack_start(searchResultsBox, heading, FALSE, FALSE, 0);

        for (const auto& p: pages) {
            auto it = matchesByPage.find(p.id);
            if (it == matchesByPage.end() || it->second.empty()) {
                continue;
            }

            GtkWidget* groupBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            GtkWidget* pageLbl = gtk_label_new(p.title.c_str());
            gtk_label_set_xalign(GTK_LABEL(pageLbl), 0.0F);
            addCssClass(pageLbl, "mx-prefs-section-title");
            gtk_box_pack_start(GTK_BOX(groupBox), pageLbl, FALSE, FALSE, 0);

            GtkWidget* listBox = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(listBox), GTK_SELECTION_NONE);
            gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(listBox), TRUE);
            addCssClass(listBox, "mx-prefs-results-list");
            gtk_list_box_set_header_func(
                    GTK_LIST_BOX(listBox),
                    +[](GtkListBoxRow* row, GtkListBoxRow* before, gpointer) {
                        if (before) {
                            GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                            addCssClass(sep, "mx-prefs-row-sep");
                            gtk_widget_show(sep);
                            gtk_list_box_row_set_header(row, sep);
                        } else {
                            gtk_list_box_row_set_header(row, nullptr);
                        }
                    },
                    nullptr, nullptr);

            g_signal_connect(listBox, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
                                 auto* self = static_cast<SettingsDialog*>(data);
                                 auto idx = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(row), "mx-target-idx"));
                                 self->jumpToSearchResult(idx);
                             }),
                             this);

            for (size_t idx: it->second) {
                const auto& entry = searchTargets[idx].entry;
                GtkWidget* row = gtk_list_box_row_new();
                g_object_set_data(G_OBJECT(row), "mx-target-idx", GSIZE_TO_POINTER(idx));

                GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
                gtk_widget_set_hexpand(vbox, TRUE);

                GtkWidget* lbl = gtk_label_new(entry.label.c_str());
                gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
                gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
                addCssClass(lbl, "mx-prefs-row-title");
                gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

                std::string meta = entry.pageTitle + " › " + entry.sectionTitle;
                if (!entry.subtitle.empty()) {
                    meta += " · " + entry.subtitle;
                }
                GtkWidget* subLbl = gtk_label_new(meta.c_str());
                gtk_label_set_xalign(GTK_LABEL(subLbl), 0.0F);
                gtk_label_set_line_wrap(GTK_LABEL(subLbl), TRUE);
                addCssClass(subLbl, "mx-prefs-row-sub");
                gtk_box_pack_start(GTK_BOX(vbox), subLbl, FALSE, FALSE, 0);

                GtkWidget* chevron = gtk_label_new("›");
                gtk_widget_set_valign(chevron, GTK_ALIGN_CENTER);
                addCssClass(chevron, "mx-prefs-row-sub");

                gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
                gtk_box_pack_end(GTK_BOX(hbox), chevron, FALSE, FALSE, 0);
                gtk_container_add(GTK_CONTAINER(row), hbox);
                gtk_list_box_insert(GTK_LIST_BOX(listBox), row, -1);
            }

            gtk_box_pack_start(GTK_BOX(groupBox), listBox, FALSE, FALSE, 0);
            gtk_box_pack_start(searchResultsBox, groupBox, FALSE, FALSE, 0);
        }
    }

    gtk_widget_show_all(GTK_WIDGET(searchResultsBox));
    gtk_stack_set_visible_child_name(contentStack, "__search__");
}

void SettingsDialog::jumpToSearchResult(size_t targetIndex) {
    if (targetIndex >= searchTargets.size()) {
        return;
    }
    const auto& target = searchTargets[targetIndex];
    auto pageIt = pageIndexById.find(target.entry.pageId);
    if (pageIt == pageIndexById.end()) {
        return;
    }

    activePageId = target.entry.pageId;
    gtk_entry_set_text(GTK_ENTRY(searchEntry), "");
    gtk_list_box_select_row(navList, pages[pageIt->second].navRow);
    gtk_stack_set_visible_child_name(contentStack, activePageId.c_str());

    if (target.parentExpander) {
        gtk_expander_set_expanded(target.parentExpander, TRUE);
    }

    clearFlash();
    flashedWidget = target.targetWidget;
    // A row can be hidden by another setting (e.g. the deadzone radius when no preprocessor is chosen).
    // Point at the closest visible container instead, so the jump still lands on the right section.
    GtkWidget* pageContent = GTK_WIDGET(pages[pageIt->second].contentBox);
    while (flashedWidget && flashedWidget != pageContent && !gtk_widget_get_visible(flashedWidget)) {
        flashedWidget = gtk_widget_get_parent(flashedWidget);
    }
    if (flashedWidget == pageContent) {
        flashedWidget = nullptr;
    }
    GtkScrolledWindow* scroller = pages[pageIt->second].scroller;
    GtkWidget* contentBox = GTK_WIDGET(pages[pageIt->second].contentBox);

    if (flashedWidget) {
        addCssClass(flashedWidget, "mx-prefs-row-flash");
        flashTimer = g_timeout_add(
                1500,
                +[](gpointer data) -> gboolean {
                    auto* self = static_cast<SettingsDialog*>(data);
                    self->flashTimer.consume();
                    self->clearFlash();
                    return G_SOURCE_REMOVE;
                },
                this);

        struct ScrollJob {
            SettingsDialog* self;
            GtkScrolledWindow* scroller;
            GtkWidget* contentBox;
            GtkWidget* target;
        };
        scrollIdle = g_idle_add_full(
                G_PRIORITY_DEFAULT_IDLE,
                +[](gpointer data) -> gboolean {
                    auto* job = static_cast<ScrollJob*>(data);
                    job->self->scrollIdle.consume();
                    gint destX = 0;
                    gint destY = 0;
                    if (gtk_widget_translate_coordinates(job->target, job->contentBox, 0, 0, &destX, &destY)) {
                        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(job->scroller);
                        const double pageH = gtk_adjustment_get_page_size(vadj);
                        const double targetY = std::max(0.0, static_cast<double>(destY) - pageH * 0.25);
                        const double maxVal =
                                std::max(0.0, gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj));
                        gtk_adjustment_set_value(vadj, std::clamp(targetY, 0.0, maxVal));
                    }
                    return G_SOURCE_REMOVE;
                },
                new ScrollJob{this, scroller, contentBox, flashedWidget},
                +[](gpointer data) { delete static_cast<ScrollJob*>(data); });
    }
}

void SettingsDialog::clearFlash() {
    if (flashedWidget) {
        removeCssClass(flashedWidget, "mx-prefs-row-flash");
        flashedWidget = nullptr;
    }
}

void SettingsDialog::initMouseButtonEvents(GladeSearchpath* gladeSearchPath) {
    auto emplaceButton = [gladeSearchPath, this](const char* hbox, Button button, bool withDevice = false) {
        buttonConfigs.emplace_back(std::make_unique<ButtonConfigGui>(gladeSearchPath, GTK_BOX(getWidget(hbox)),
                                                                     settings, button, withDevice));
    };

    emplaceButton("hboxLeftMouse", BUTTON_MOUSE_LEFT);
    emplaceButton("hboxMiddleMouse", BUTTON_MOUSE_MIDDLE);
    emplaceButton("hboxRightMouse", BUTTON_MOUSE_RIGHT);
    emplaceButton("hboxMouse4", BUTTON_MOUSE_4);
    emplaceButton("hboxMouse5", BUTTON_MOUSE_5);
    emplaceButton("hboxEraser", BUTTON_ERASER);
    emplaceButton("hboxTouch", BUTTON_TOUCH, true);
    emplaceButton("hboxPenButton1", BUTTON_STYLUS_ONE);
    emplaceButton("hboxPenButton2", BUTTON_STYLUS_TWO);
    emplaceButton("hboxDefaultTool", BUTTON_DEFAULT);
}

void SettingsDialog::setDpi(int dpi) {
    if (this->dpi == dpi) {
        return;
    }
    this->dpi = dpi;
    zoomcallib_set_val(ZOOM_CALLIB(callib), dpi);
}

void SettingsDialog::loadSwitch(const char* name, bool value) {
    gtk_switch_set_active(GTK_SWITCH(getWidget(name)), value);
}

bool SettingsDialog::getSwitch(const char* name) { return gtk_switch_get_active(GTK_SWITCH(getWidget(name))) != FALSE; }

void SettingsDialog::loadSlider(const char* name, double value) {
    gtk_range_set_value(GTK_RANGE(getWidget(name)), value);
}

double SettingsDialog::getSlider(const char* name) { return gtk_range_get_value(GTK_RANGE(getWidget(name))); }

void SettingsDialog::setSegmentedActive(const char* name, int index) {
    auto it = segmentedControls.find(name);
    if (it == segmentedControls.end()) {
        return;
    }
    const auto idx = static_cast<size_t>(std::clamp(index, 0, static_cast<int>(it->second.buttons.size()) - 1));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(it->second.buttons[idx]), TRUE);
}

int SettingsDialog::getSegmentedActive(const char* name) const {
    auto it = segmentedControls.find(name);
    if (it == segmentedControls.end()) {
        return 0;
    }
    for (size_t i = 0; i < it->second.buttons.size(); ++i) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(it->second.buttons[i]))) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void SettingsDialog::updatePressureSensitivityOptions() {
    const bool havePressureInput = getSwitch("cbSettingPresureSensitivity") || getSwitch("cbEnablePressureInference");
    setRowDepState("scaleMinimumPressure", havePressureInput);
    setRowDepState("scalePressureMultiplier", havePressureInput);
}

void SettingsDialog::updatePressureValueLabels() {
    if (auto it = scaleValueLabels.find("scaleMinimumPressure"); it != scaleValueLabels.end()) {
        const int pct = round_cast<int>(getSlider("scaleMinimumPressure") * 100.0);
        gtk_label_set_text(it->second, (std::to_string(pct) + " %").c_str());
    }
    if (auto it = scaleValueLabels.find("scalePressureMultiplier"); it != scaleValueLabels.end()) {
        const int pct = round_cast<int>(getSlider("scalePressureMultiplier") * 100.0);
        gtk_label_set_text(it->second, (std::to_string(pct) + " %").c_str());
    }
}

void SettingsDialog::updatePageSpaceRows() {
    const bool unlimited = getSwitch("cbUnlimitedScrolling");
    const bool addH = !unlimited && getSwitch("cbAddHorizontalSpace");
    const bool addV = !unlimited && getSwitch("cbAddVerticalSpace");

    setRowDepState("cbAddHorizontalSpace", !unlimited);
    setRowDepState("cbAddVerticalSpace", !unlimited);
    setRowDepState("spAddHorizontalSpaceLeft", addH);
    setRowDepState("spAddHorizontalSpaceRight", addH);
    setRowDepState("spAddVerticalSpaceAbove", addV);
    setRowDepState("spAddVerticalSpaceBelow", addV);
}

void SettingsDialog::updateRecolorRows() {
    const bool on = getSwitch("cbRecolorDrawingArea");
    setRowDepState("cbRecolorPreviewSidebar", on);
    setRowDepState("recolorLight", on);
    setRowDepState("recolorDark", on);
}

void SettingsDialog::updateCursorHighlightRows() {
    const int stylusCursorType = gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbStylusCursorType")));
    const bool cursorAllowsHighlight =
            (stylusCursorType != STYLUS_CURSOR_NONE && stylusCursorType != STYLUS_CURSOR_ARROW);
    setRowDepState("cbHighlightPosition", cursorAllowsHighlight);

    const bool hlEnabled = cursorAllowsHighlight && getSwitch("cbHighlightPosition");
    setRowDepState("cursorHighlightColor", hlEnabled);
    setRowDepState("cursorHighlightRadius", hlEnabled);
    setRowDepState("cursorHighlightBorderWidth", hlEnabled);
    setRowDepState("cursorHighlightBorderColor", hlEnabled);
}

void SettingsDialog::updateCustomTouchCommandRows() {
    const bool palmOn = getSwitch("cbDisableTouchOnPenNear");
    setRowDepState("cbTouchDisableMethod", palmOn);
    setRowDepState("spTouchDisableTimeout", palmOn);

    const int touchMethod = gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbTouchDisableMethod")));
    const bool customOn = palmOn && touchMethod == 2;
    setRowDepState("txtEnableTouchCommand", customOn);
    setRowDepState("txtDisableTouchCommand", customOn);
}

void SettingsDialog::updateStrokeFilterRows() {
    const bool on = getSwitch("cbStrokeFilterEnabled");
    setRowDepState("cbTrySelectOnStrokeFiltered", on);
    setRowDepState("cbDoActionOnStrokeFiltered", on);
    setRowDepState("spStrokeIgnoreTime", on);
    setRowDepState("spStrokeIgnoreLength", on);
    setRowDepState("spStrokeSuccessiveTime", on);
}

#ifdef ENABLE_AUDIO
void SettingsDialog::updateAudioRows() {
    const bool audioEnabled = getSwitch("cbDisableAudio");
    setRowDepState("fcAudioPath", audioEnabled);
    setRowDepState("cbAudioInputDevice", audioEnabled);
    setRowDepState("cbAudioSampleRate", audioEnabled);
    setRowDepState("spAudioGain", audioEnabled);
    setRowDepState("cbAudioOutputDevice", audioEnabled);
    setRowDepState("spDefaultSeekTime", audioEnabled);
}
#endif

void SettingsDialog::showStabilizerAvMethodOptions(StrokeStabilizer::AveragingMethod method) {
    const bool showArithmetic = method == StrokeStabilizer::AveragingMethod::ARITHMETIC;
    setRowVisible("sbStabilizerBuffersize", showArithmetic);

    const bool showSigma = method == StrokeStabilizer::AveragingMethod::VELOCITY_GAUSSIAN;
    setRowVisible("sbStabilizerSigma", showSigma);

    const bool preprocessorOn =
            static_cast<StrokeStabilizer::Preprocessor>(gtk_combo_box_get_active(
                    GTK_COMBO_BOX(getWidget("cbStabilizerPreprocessors")))) != StrokeStabilizer::Preprocessor::NONE;
    setRowDepState("cbStabilizerEnableFinalizeStroke", showSigma || showArithmetic || preprocessorOn);
}

void SettingsDialog::showStabilizerPreprocessorOptions(StrokeStabilizer::Preprocessor preprocessor) {
    const bool showDeadzone = preprocessor == StrokeStabilizer::Preprocessor::DEADZONE;
    setRowVisible("sbStabilizerDeadzoneRadius", showDeadzone);
    setRowVisible("cbStabilizerEnableCuspDetection", showDeadzone);

    const bool showInertia = preprocessor == StrokeStabilizer::Preprocessor::INERTIA;
    setRowVisible("sbStabilizerDrag", showInertia);
    setRowVisible("sbStabilizerMass", showInertia);

    const bool averagingOn = static_cast<StrokeStabilizer::AveragingMethod>(gtk_combo_box_get_active(
                                     GTK_COMBO_BOX(getWidget("cbStabilizerAveragingMethods")))) !=
                             StrokeStabilizer::AveragingMethod::NONE;
    setRowDepState("cbStabilizerEnableFinalizeStroke", showDeadzone || showInertia || averagingOn);
}

void SettingsDialog::load() {
    loadSwitch("cbSettingPresureSensitivity", settings->isPressureSensitivity());
    loadSwitch("cbEnableZoomGestures", settings->isZoomGesturesEnabled());
    loadSwitch("cbForceZoomToFitOnLoad", settings->getForceZoomToFitOnLoad());
    loadSwitch("cbShowSidebarRight", settings->isSidebarOnRight());
    loadSwitch("cbShowScrollbarLeft", settings->isScrollbarOnLeft());
    loadSwitch("cbAutoloadMostRecent", settings->isAutoloadMostRecent());
    loadSwitch("cbAutoloadXoj", settings->isAutoloadPdfXoj());
    loadSwitch("cbAutosave", settings->isAutosaveEnabled());
    loadSwitch("cbAddVerticalSpace", settings->getAddVerticalSpace());
    loadSwitch("cbAddHorizontalSpace", settings->getAddHorizontalSpace());
    loadSwitch("cbUnlimitedScrolling", settings->getUnlimitedScrolling());
    loadSwitch("cbDrawDirModsEnabled", settings->getDrawDirModsEnabled());
    loadSwitch("cbStrokeFilterEnabled", settings->getStrokeFilterEnabled());
    loadSwitch("cbDoActionOnStrokeFiltered", settings->getDoActionOnStrokeFiltered());
    loadSwitch("cbTrySelectOnStrokeFiltered", settings->getTrySelectOnStrokeFiltered());
    loadSwitch("cbSnapRecognizedShapesEnabled", settings->getSnapRecognizedShapesEnabled());
    loadSwitch("cbHoldToShapeEnabled", settings->isHoldToShapeEnabled());
    loadSwitch("cbRestoreLineWidthEnabled", settings->getRestoreLineWidthEnabled());
    loadSwitch("cbStockIcons", settings->areStockIconsUsed());
    loadSwitch("cbShowPageShadow", settings->isShowPageShadow());
    loadSwitch("cbHideHorizontalScrollbar", (settings->getScrollbarHideType() & SCROLLBAR_HIDE_HORIZONTAL) != 0);
    loadSwitch("cbHideVerticalScrollbar", (settings->getScrollbarHideType() & SCROLLBAR_HIDE_VERTICAL) != 0);
    loadSwitch("cbDisableScrollbarFadeout", settings->isScrollbarFadeoutDisabled());
#ifdef ENABLE_AUDIO
    loadSwitch("cbDisableAudio", !settings->isAudioDisabled());
#endif
    loadSwitch("cbEnablePressureInference", settings->isPressureGuessingEnabled());
    loadSwitch("cbTouchDrawing", settings->getTouchDrawingEnabled());
    loadSwitch("cbDisableGtkInertialScroll", !settings->getGtkTouchInertialScrollingEnabled());
    const bool ignoreStylusEventsEnabled = settings->getIgnoredStylusEvents() != 0;
    loadSwitch("cbIgnoreFirstStylusEvents", ignoreStylusEventsEnabled);
    loadSwitch("cbInputSystemDrawOutsideWindow", settings->getInputSystemDrawOutsideWindowEnabled());

    // Tab size
    loadSwitch("cbUseSpacesAsTab", settings->getUseSpacesAsTab());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbNumberOfSpacesForTab")),
                              settings->getNumberOfSpacesForTab());
    setRowDepState("sbNumberOfSpacesForTab", settings->getUseSpacesAsTab());

    // Laser pointer
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbLaserFadeOutTime")), settings->getLaserPointerFadeOutTime());

    // Stabilizer
    loadSwitch("cbStabilizerEnableCuspDetection", settings->getStabilizerCuspDetection());
    loadSwitch("cbStabilizerEnableFinalizeStroke", settings->getStabilizerFinalizeStroke());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerBuffersize")),
                              static_cast<double>(settings->getStabilizerBuffersize()));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerDeadzoneRadius")),
                              settings->getStabilizerDeadzoneRadius());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerDrag")), settings->getStabilizerDrag());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerMass")), settings->getStabilizerMass());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerSigma")), settings->getStabilizerSigma());

    gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbStabilizerAveragingMethods")),
                             static_cast<int>(settings->getStabilizerAveragingMethod()));
    gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbStabilizerPreprocessors")),
                             static_cast<int>(settings->getStabilizerPreprocessor()));
    showStabilizerAvMethodOptions(settings->getStabilizerAveragingMethod());
    showStabilizerPreprocessorOptions(settings->getStabilizerPreprocessor());

    // Sidebar page numbering
    gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbSidebarPageNumberStyle")),
                             static_cast<int>(settings->getSidebarNumberingStyle()));

    // Default file names
    gtk_editable_set_text(GTK_EDITABLE(getWidget("txtDefaultSaveName")),
                          char_cast(settings->getDefaultSaveName().c_str()));
    gtk_editable_set_text(GTK_EDITABLE(getWidget("txtDefaultPdfName")),
                          char_cast(settings->getDefaultPdfExportName().c_str()));

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spAutosaveTimeout")), settings->getAutosaveTimeout());

    if (!ignoreStylusEventsEnabled) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spNumIgnoredStylusEvents")), 1);
    } else {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spNumIgnoredStylusEvents")),
                                  settings->getIgnoredStylusEvents());
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spPairsOffset")), settings->getPairsOffset());

    const EmptyLastPageAppendType append = settings->getEmptyLastPageAppend();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(getWidget("rdLastPageAppendOnDrawOfLastPage")),
                                 append == EmptyLastPageAppendType::OnDrawOfLastPage);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(getWidget("rdLastPageAppendOnScrollToEndOfLastPage")),
                                 append == EmptyLastPageAppendType::OnScrollToEndOfLastPage);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(getWidget("rdLastPageAppendDisabled")),
                                 append == EmptyLastPageAppendType::Disabled);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spSnapRotationTolerance")),
                              settings->getSnapRotationTolerance());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spSnapGridTolerance")), settings->getSnapGridTolerance());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spSnapGridSize")),
                              settings->getSnapGridSize() / DEFAULT_GRID_SIZE);

    shapeSettingsPanel->load(*settings);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("edgePanSpeed")), settings->getEdgePanSpeed());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("edgePanMaxMult")), settings->getEdgePanMaxMult());

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spZoomStep")), settings->getZoomStep());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spZoomStepScroll")), settings->getZoomStepScroll());

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spAddHorizontalSpaceRight")),
                              settings->getAddHorizontalSpaceAmountRight());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spAddVerticalSpaceAbove")),
                              settings->getAddVerticalSpaceAmountAbove());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spAddHorizontalSpaceLeft")),
                              settings->getAddHorizontalSpaceAmountLeft());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spAddVerticalSpaceBelow")),
                              settings->getAddVerticalSpaceAmountBelow());

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spDrawDirModsRadius")), settings->getDrawDirModsRadius());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spReRenderThreshold")),
                              settings->getPDFPageRerenderThreshold());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spTouchZoomStartThreshold")),
                              settings->getTouchZoomStartThreshold());

    {
        int time = 0;
        double length = 0;
        int successive = 0;
        settings->getStrokeFilter(&time, &length, &successive);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spStrokeIgnoreTime")), time);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spStrokeIgnoreLength")), length);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spStrokeSuccessiveTime")), successive);
    }

    auto* zoomCallibSlider = GTK_RANGE(getWidget("zoomCallibSlider"));
    if (auto storedDpi = settings->getDisplayDpi(); storedDpi == -1) {
        loadSwitch("zoomCallibAutomaticCb", true);
        g_signal_connect(zoomCallibSlider, "realize", G_CALLBACK(+[](GtkWidget* slider, gpointer) {
                             auto detected = xoj::util::gtk::getWidgetDPI(slider);
                             gtk_range_set_value(GTK_RANGE(slider), detected.value_or(Util::DPI_NORMALIZATION_FACTOR));
                         }),
                         nullptr);
        this->setDpi(72);
    } else {
        loadSwitch("zoomCallibAutomaticCb", false);
        this->setDpi(storedDpi);
    }
    gtk_range_set_value(zoomCallibSlider, this->dpi);
    setRowDepState("dpi-ruler", !getSwitch("zoomCallibAutomaticCb"));

    loadSlider("scaleMinimumPressure", settings->getMinimumPressure());
    loadSlider("scalePressureMultiplier", settings->getPressureMultiplier());
    updatePressureValueLabels();

    GdkRGBA color = Util::rgb_to_GdkRGBA(settings->getBorderColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("colorBorder")), &color);
    color = Util::rgb_to_GdkRGBA(settings->getBackgroundColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("colorBackground")), &color);
    color = Util::rgb_to_GdkRGBA(settings->getSelectionColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("colorSelection")), &color);
    color = Util::rgb_to_GdkRGBA(settings->getActiveSelectionColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("colorSelectionActive")), &color);

    {
        const auto& recolor = settings->getRecolorParameters();
        loadSwitch("cbRecolorDrawingArea", recolor.recolorizeMainView);
        loadSwitch("cbRecolorPreviewSidebar", recolor.recolorizeSidebarMiniatures);
        color = Util::argb_to_GdkRGBA(recolor.recolor.getLight());
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("recolorLight")), &color);
        color = Util::argb_to_GdkRGBA(recolor.recolor.getDark());
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("recolorDark")), &color);
    }

    loadSwitch("cbHighlightPosition", settings->isHighlightPosition());
    color = Util::argb_to_GdkRGBA(settings->getCursorHighlightColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("cursorHighlightColor")), &color);
    color = Util::argb_to_GdkRGBA(settings->getCursorHighlightBorderColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(getWidget("cursorHighlightBorderColor")), &color);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("cursorHighlightRadius")),
                              settings->getCursorHighlightRadius());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("cursorHighlightBorderWidth")),
                              settings->getCursorHighlightBorderWidth());

    switch (settings->getStylusCursorType()) {
        case STYLUS_CURSOR_NONE:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbStylusCursorType")), 0);
            break;
        case STYLUS_CURSOR_BIG:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbStylusCursorType")), 2);
            break;
        case STYLUS_CURSOR_ARROW:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbStylusCursorType")), 3);
            break;
        case STYLUS_CURSOR_DOT:
        default:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbStylusCursorType")), 1);
            break;
    }

    switch (settings->getEraserVisibility()) {
        case ERASER_VISIBILITY_NEVER:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbEraserVisibility")), 0);
            break;
        case ERASER_VISIBILITY_HOVER:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbEraserVisibility")), 2);
            break;
        case ERASER_VISIBILITY_TOUCH:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbEraserVisibility")), 3);
            break;
        case ERASER_VISIBILITY_ALWAYS:
        default:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbEraserVisibility")), 1);
            break;
    }

    switch (settings->getIconTheme()) {
        case ICON_THEME_LUCIDE:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbIconTheme")), 1);
            break;
        case ICON_THEME_COLOR:
        default:
            gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbIconTheme")), 0);
            break;
    }

    switch (settings->getThemeVariant()) {
        case THEME_VARIANT_FORCE_LIGHT:
            setSegmentedActive("cbThemeVariant", 1);
            break;
        case THEME_VARIANT_FORCE_DARK:
            setSegmentedActive("cbThemeVariant", 2);
            break;
        case THEME_VARIANT_USE_SYSTEM:
        default:
            setSegmentedActive("cbThemeVariant", 0);
            break;
    }

    switch (settings->getTouchMode()) {
        case TouchMode::OFF:
            setSegmentedActive("cbTouchMode", 1);
            break;
        case TouchMode::ON:
            setSegmentedActive("cbTouchMode", 2);
            break;
        case TouchMode::AUTO:
        default:
            setSegmentedActive("cbTouchMode", 0);
            break;
    }

    auto viewMode = settings->getViewModes().at(PresetViewModeIds::VIEW_MODE_FULLSCREEN);
    const bool showFullscreenToolbar = viewMode.showToolbar;
    const bool showFullscreenSidebar = viewMode.showSidebar;
#ifndef __APPLE__
    const bool showFullscreenMenubar = viewMode.showMenubar;
#endif
    viewMode = settings->getViewModes().at(PresetViewModeIds::VIEW_MODE_PRESENTATION);
    const bool showPresentationToolbar = viewMode.showToolbar;
    const bool showPresentationSidebar = viewMode.showSidebar;
    const bool goPresentationFullscreen = viewMode.goFullscreen;
#ifndef __APPLE__
    const bool showPresentationMenubar = viewMode.showMenubar;
    loadSwitch("cbShowFullscreenMenubar", showFullscreenMenubar);
    loadSwitch("cbShowPresentationMenubar", showPresentationMenubar);
    loadSwitch("cbHideMenubarStartup", settings->getViewModes().at(PresetViewModeIds::VIEW_MODE_DEFAULT).showMenubar);
    loadSwitch("cbUseCSD", settings->isUseClientSideDecorations());
#endif
    loadSwitch("cbShowFullscreenToolbar", showFullscreenToolbar);
    loadSwitch("cbShowFullscreenSidebar", showFullscreenSidebar);
    loadSwitch("cbShowPresentationToolbar", showPresentationToolbar);
    loadSwitch("cbShowPresentationSidebar", showPresentationSidebar);
    loadSwitch("cbPresentationGoFullscreen", goPresentationFullscreen);
    loadSwitch("cbShowFilepathInTitlebar", settings->isFilepathInTitlebarShown());
    loadSwitch("cbShowPageNumberInTitlebar", settings->isPageNumberInTitlebarShown());

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("preloadPagesBefore")),
                              static_cast<double>(settings->getPreloadPagesBefore()));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("preloadPagesAfter")),
                              static_cast<double>(settings->getPreloadPagesAfter()));
    loadSwitch("cbEagerPageCleanup", settings->isEagerPageCleanup());

    SElement& touch = settings->getCustomElement("touch");
    bool disablePen = false;
    touch.getBool("disableTouch", disablePen);
    loadSwitch("cbDisableTouchOnPenNear", disablePen);

    string disableMethod;
    touch.getString("method", disableMethod);
    int methodId = 0;
    if (disableMethod == "X11") {
        methodId = 1;
    } else if (disableMethod == "custom") {
        methodId = 2;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbTouchDisableMethod")), methodId);

    string cmd;
    touch.getString("cmdEnable", cmd);
    gtk_editable_set_text(GTK_EDITABLE(getWidget("txtEnableTouchCommand")), cmd.c_str());

    cmd = "";
    touch.getString("cmdDisable", cmd);
    gtk_editable_set_text(GTK_EDITABLE(getWidget("txtDisableTouchCommand")), cmd.c_str());

    int timeoutMs = 1000;
    touch.getInt("timeout", timeoutMs);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spTouchDisableTimeout")), timeoutMs / 1000.0);

    // Sync all dependency states after values are loaded
    setRowDepState("spAutosaveTimeout", getSwitch("cbAutosave"));
    setRowDepState("spNumIgnoredStylusEvents", getSwitch("cbIgnoreFirstStylusEvents"));
    setRowDepState("spDrawDirModsRadius", getSwitch("cbDrawDirModsEnabled"));
    setRowDepState("spTouchZoomStartThreshold", getSwitch("cbEnableZoomGestures"));
    shapeSettingsPanel->setHoldControlsSensitive(getSwitch("cbHoldToShapeEnabled"));
    updatePageSpaceRows();
    updateRecolorRows();
    updateStrokeFilterRows();
    updateCustomTouchCommandRows();
    updateCursorHighlightRows();
    updatePressureSensitivityOptions();

#ifdef ENABLE_AUDIO
    setAudioRecordingFolder(settings->getAudioFolder());
    if (this->control->getAudioController()) {
        this->audioInputDevices = this->control->getAudioController()->getInputDevices();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(getWidget("cbAudioInputDevice")), "", _("System default"));
        gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioInputDevice")), 0);
        for (auto& audioInputDevice: this->audioInputDevices) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(getWidget("cbAudioInputDevice")), "",
                                      audioInputDevice.getDeviceName().c_str());
        }
        for (size_t i = 0; i < this->audioInputDevices.size(); i++) {
            if (this->audioInputDevices[i].getSelected()) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioInputDevice")), static_cast<gint>(i + 1));
            }
        }

        this->audioOutputDevices = this->control->getAudioController()->getOutputDevices();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(getWidget("cbAudioOutputDevice")), "", _("System default"));
        gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioOutputDevice")), 0);
        for (auto& audioOutputDevice: this->audioOutputDevices) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(getWidget("cbAudioOutputDevice")), "",
                                      audioOutputDevice.getDeviceName().c_str());
        }
        for (size_t i = 0; i < this->audioOutputDevices.size(); i++) {
            if (this->audioOutputDevices[i].getSelected()) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioOutputDevice")), static_cast<gint>(i + 1));
            }
        }

        switch (static_cast<int>(settings->getAudioSampleRate())) {
            case 16000:
                gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioSampleRate")), 0);
                break;
            case 96000:
                gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioSampleRate")), 2);
                break;
            case 192000:
                gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioSampleRate")), 3);
                break;
            case 44100:
            default:
                gtk_combo_box_set_active(GTK_COMBO_BOX(getWidget("cbAudioSampleRate")), 1);
                break;
        }

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spAudioGain")), settings->getAudioGain());
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(getWidget("spDefaultSeekTime")), settings->getDefaultSeekTime());
    }
    updateAudioRows();
#endif

    this->latexPanel.load(settings->latexSettings);
    paletteTab.renderPaletteTab(this->control->getPalette().getFilePath());
    this->mcpSettingsPanel->load(*settings);
}

void SettingsDialog::save() {
    settings->transactionStart();

    settings->setPressureSensitivity(getSwitch("cbSettingPresureSensitivity"));
    settings->setMinimumPressure(getSlider("scaleMinimumPressure"));
    settings->setPressureMultiplier(getSlider("scalePressureMultiplier"));
    settings->setZoomGesturesEnabled(getSwitch("cbEnableZoomGestures"));
    settings->setSidebarOnRight(getSwitch("cbShowSidebarRight"));
    settings->setScrollbarOnLeft(getSwitch("cbShowScrollbarLeft"));
    settings->setAutoloadMostRecent(getSwitch("cbAutoloadMostRecent"));
    settings->setAutoloadPdfXoj(getSwitch("cbAutoloadXoj"));
    settings->setAutosaveEnabled(getSwitch("cbAutosave"));
    settings->setAddVerticalSpace(getSwitch("cbAddVerticalSpace"));
    settings->setAddHorizontalSpace(getSwitch("cbAddHorizontalSpace"));
    settings->setUnlimitedScrolling(getSwitch("cbUnlimitedScrolling"));
    settings->setDrawDirModsEnabled(getSwitch("cbDrawDirModsEnabled"));
    settings->setStrokeFilterEnabled(getSwitch("cbStrokeFilterEnabled"));
    settings->setDoActionOnStrokeFiltered(getSwitch("cbDoActionOnStrokeFiltered"));
    settings->setTrySelectOnStrokeFiltered(getSwitch("cbTrySelectOnStrokeFiltered"));
    settings->setSnapRecognizedShapesEnabled(getSwitch("cbSnapRecognizedShapesEnabled"));
    settings->setHoldToShapeEnabled(getSwitch("cbHoldToShapeEnabled"));
    settings->setRestoreLineWidthEnabled(getSwitch("cbRestoreLineWidthEnabled"));
    settings->setAreStockIconsUsed(getSwitch("cbStockIcons"));
    settings->setShowPageShadow(getSwitch("cbShowPageShadow"));
    settings->setPressureGuessingEnabled(getSwitch("cbEnablePressureInference"));
    settings->setTouchDrawingEnabled(getSwitch("cbTouchDrawing"));
    settings->setGtkTouchInertialScrollingEnabled(!getSwitch("cbDisableGtkInertialScroll"));
    settings->setInputSystemDrawOutsideWindowEnabled(getSwitch("cbInputSystemDrawOutsideWindow"));
    settings->setScrollbarFadeoutDisabled(getSwitch("cbDisableScrollbarFadeout"));
#ifdef ENABLE_AUDIO
    settings->setAudioDisabled(!getSwitch("cbDisableAudio"));
#endif

    // Tab size
    settings->setUseSpacesAsTab(getSwitch("cbUseSpacesAsTab"));
    settings->setNumberOfSpacesForTab(static_cast<unsigned int>(
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(getWidget("sbNumberOfSpacesForTab")))));

    settings->setLaserPointerFadeOutTime(static_cast<unsigned int>(
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(getWidget("sbLaserFadeOutTime")))));

    // Stabilizer
    settings->setStabilizerAveragingMethod(static_cast<StrokeStabilizer::AveragingMethod>(
            gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbStabilizerAveragingMethods")))));
    settings->setStabilizerPreprocessor(static_cast<StrokeStabilizer::Preprocessor>(
            gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbStabilizerPreprocessors")))));
    settings->setStabilizerBuffersize(static_cast<size_t>(
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(getWidget("sbStabilizerBuffersize")))));
    settings->setStabilizerDeadzoneRadius(
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerDeadzoneRadius"))));
    settings->setStabilizerDrag(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerDrag"))));
    settings->setStabilizerMass(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerMass"))));
    settings->setStabilizerSigma(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("sbStabilizerSigma"))));
    settings->setStabilizerCuspDetection(getSwitch("cbStabilizerEnableCuspDetection"));
    settings->setStabilizerFinalizeStroke(getSwitch("cbStabilizerEnableFinalizeStroke"));

    settings->setSidebarNumberingStyle(static_cast<SidebarNumberingStyle>(
            gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbSidebarPageNumberStyle")))));

    auto scrollbarHideType =
            static_cast<std::make_unsigned<std::underlying_type<ScrollbarHideType>::type>::type>(SCROLLBAR_HIDE_NONE);
    if (getSwitch("cbHideHorizontalScrollbar")) {
        scrollbarHideType |= SCROLLBAR_HIDE_HORIZONTAL;
    }
    if (getSwitch("cbHideVerticalScrollbar")) {
        scrollbarHideType |= SCROLLBAR_HIDE_VERTICAL;
    }
    settings->setScrollbarHideType(static_cast<ScrollbarHideType>(scrollbarHideType));

    GdkRGBA color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("colorBorder")), &color);
    settings->setBorderColor(Util::GdkRGBA_to_argb(color));

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("colorBackground")), &color);
    settings->setBackgroundColor(Util::GdkRGBA_to_argb(color));

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("colorSelection")), &color);
    settings->setSelectionColor(Util::GdkRGBA_to_argb(color));

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("colorSelectionActive")), &color);
    settings->setActiveSelectionColor(Util::GdkRGBA_to_argb(color));

    {
        GdkRGBA color2;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("recolorLight")), &color);
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("recolorDark")), &color2);

        settings->setRecolorParameters({getSwitch("cbRecolorDrawingArea"), getSwitch("cbRecolorPreviewSidebar"),
                                        Recolor(Util::GdkRGBA_to_argb(color), Util::GdkRGBA_to_argb(color2))});
    }

    settings->setHighlightPosition(getSwitch("cbHighlightPosition"));
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("cursorHighlightColor")), &color);
    settings->setCursorHighlightColor(Util::GdkRGBA_to_argb(color));
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(getWidget("cursorHighlightBorderColor")), &color);
    settings->setCursorHighlightBorderColor(Util::GdkRGBA_to_argb(color));
    settings->setCursorHighlightRadius(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("cursorHighlightRadius"))));
    settings->setCursorHighlightBorderWidth(
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("cursorHighlightBorderWidth"))));

    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbStylusCursorType")))) {
        case 0:
            settings->setStylusCursorType(STYLUS_CURSOR_NONE);
            break;
        case 2:
            settings->setStylusCursorType(STYLUS_CURSOR_BIG);
            break;
        case 3:
            settings->setStylusCursorType(STYLUS_CURSOR_ARROW);
            break;
        case 1:
        default:
            settings->setStylusCursorType(STYLUS_CURSOR_DOT);
            break;
    }

    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbEraserVisibility")))) {
        case 0:
            settings->setEraserVisibility(ERASER_VISIBILITY_NEVER);
            break;
        case 2:
            settings->setEraserVisibility(ERASER_VISIBILITY_HOVER);
            break;
        case 3:
            settings->setEraserVisibility(ERASER_VISIBILITY_TOUCH);
            break;
        case 1:
        default:
            settings->setEraserVisibility(ERASER_VISIBILITY_ALWAYS);
            break;
    }

    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbIconTheme")))) {
        case 1:
            settings->setIconTheme(ICON_THEME_LUCIDE);
            break;
        case 0:
        default:
            settings->setIconTheme(ICON_THEME_COLOR);
            break;
    }

    switch (getSegmentedActive("cbThemeVariant")) {
        case 1:
            settings->setThemeVariant(THEME_VARIANT_FORCE_LIGHT);
            break;
        case 2:
            settings->setThemeVariant(THEME_VARIANT_FORCE_DARK);
            break;
        case 0:
        default:
            settings->setThemeVariant(THEME_VARIANT_USE_SYSTEM);
            break;
    }

    switch (getSegmentedActive("cbTouchMode")) {
        case 1:
            settings->setTouchMode(TouchMode::OFF);
            break;
        case 2:
            settings->setTouchMode(TouchMode::ON);
            break;
        case 0:
        default:
            settings->setTouchMode(TouchMode::AUTO);
            break;
    }

    ViewMode viewModeFullscreen;
    viewModeFullscreen.goFullscreen = true;
#ifdef __APPLE__
    viewModeFullscreen.showMenubar = settings->getViewModes().at(PresetViewModeIds::VIEW_MODE_FULLSCREEN).showMenubar;
#else
    viewModeFullscreen.showMenubar = getSwitch("cbShowFullscreenMenubar");
#endif
    viewModeFullscreen.showToolbar = getSwitch("cbShowFullscreenToolbar");
    viewModeFullscreen.showSidebar = getSwitch("cbShowFullscreenSidebar");
    settings->setViewMode(PresetViewModeIds::VIEW_MODE_FULLSCREEN, viewModeFullscreen);

    ViewMode viewModePresentation;
#ifdef __APPLE__
    viewModePresentation.showMenubar =
            settings->getViewModes().at(PresetViewModeIds::VIEW_MODE_PRESENTATION).showMenubar;
#else
    viewModePresentation.showMenubar = getSwitch("cbShowPresentationMenubar");
#endif
    viewModePresentation.showToolbar = getSwitch("cbShowPresentationToolbar");
    viewModePresentation.showSidebar = getSwitch("cbShowPresentationSidebar");
    viewModePresentation.goFullscreen = getSwitch("cbPresentationGoFullscreen");
    settings->setViewMode(PresetViewModeIds::VIEW_MODE_PRESENTATION, viewModePresentation);

#ifndef __APPLE__
    const bool alwaysShowMenubar = getSwitch("cbHideMenubarStartup");
    bool menubarPreferenceChanged = false;
    const bool inDefaultViewMode = settings->getActiveViewMode() == PresetViewModeIds::VIEW_MODE_DEFAULT;
    {
        ViewMode viewModeDefault = settings->getViewModes()[PresetViewModeIds::VIEW_MODE_DEFAULT];
        menubarPreferenceChanged = alwaysShowMenubar != viewModeDefault.showMenubar;
        viewModeDefault.showMenubar = alwaysShowMenubar;
        settings->setViewMode(PresetViewModeIds::VIEW_MODE_DEFAULT, viewModeDefault);
    }
    if (inDefaultViewMode) {
        settings->setMenubarVisible(alwaysShowMenubar);
    }
    settings->setUseClientSideDecorations(getSwitch("cbUseCSD"));
#endif
    settings->setFilepathInTitlebarShown(getSwitch("cbShowFilepathInTitlebar"));
    settings->setPageNumberInTitlebarShown(getSwitch("cbShowPageNumberInTitlebar"));

    constexpr auto spinAsUint = [&](GtkSpinButton* btn) {
        int v = gtk_spin_button_get_value_as_int(btn);
        return v < 0 ? 0U : static_cast<unsigned int>(v);
    };
    settings->setPreloadPagesBefore(spinAsUint(GTK_SPIN_BUTTON(getWidget("preloadPagesBefore"))));
    settings->setPreloadPagesAfter(spinAsUint(GTK_SPIN_BUTTON(getWidget("preloadPagesAfter"))));
    settings->setEagerPageCleanup(getSwitch("cbEagerPageCleanup"));

    settings->setDefaultSaveName(
            xoj::util::utf8(gtk_editable_get_text(GTK_EDITABLE(getWidget("txtDefaultSaveName")))).str());
    settings->setDefaultPdfExportName(
            xoj::util::utf8(gtk_editable_get_text(GTK_EDITABLE(getWidget("txtDefaultPdfName")))).str());

    settings->setAutosaveTimeout(
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spAutosaveTimeout")))));

    if (getSwitch("cbIgnoreFirstStylusEvents")) {
        settings->setIgnoredStylusEvents(
                static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spNumIgnoredStylusEvents")))));
    } else {
        settings->setIgnoredStylusEvents(0);
    }

    settings->setPairsOffset(static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spPairsOffset")))));

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(getWidget("rdLastPageAppendDisabled")))) {
        settings->setEmptyLastPageAppend(EmptyLastPageAppendType::Disabled);
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(getWidget("rdLastPageAppendOnDrawOfLastPage")))) {
        settings->setEmptyLastPageAppend(EmptyLastPageAppendType::OnDrawOfLastPage);
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(getWidget("rdLastPageAppendOnScrollToEndOfLastPage")))) {
        settings->setEmptyLastPageAppend(EmptyLastPageAppendType::OnScrollToEndOfLastPage);
    } else {
        settings->setEmptyLastPageAppend(EmptyLastPageAppendType::Disabled);
    }

    settings->setEdgePanSpeed(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("edgePanSpeed"))));
    settings->setEdgePanMaxMult(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("edgePanMaxMult"))));

    settings->setZoomStep(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spZoomStep"))));
    settings->setZoomStepScroll(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spZoomStepScroll"))));
    settings->setForceZoomToFitOnLoad(getSwitch("cbForceZoomToFitOnLoad"));

    settings->setAddHorizontalSpaceAmountRight(
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spAddHorizontalSpaceRight")))));
    settings->setAddVerticalSpaceAmountAbove(
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spAddVerticalSpaceAbove")))));
    settings->setAddHorizontalSpaceAmountLeft(
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spAddHorizontalSpaceLeft")))));
    settings->setAddVerticalSpaceAmountBelow(
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spAddVerticalSpaceBelow")))));

    settings->setDrawDirModsRadius(
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spDrawDirModsRadius")))));

    {
        const int strokeIgnoreTime =
                static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spStrokeIgnoreTime"))));
        const double strokeIgnoreLength = gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spStrokeIgnoreLength")));
        const int strokeSuccessiveTime =
                static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spStrokeSuccessiveTime"))));
        settings->setStrokeFilter(strokeIgnoreTime, strokeIgnoreLength, strokeSuccessiveTime);
    }

    settings->setTouchZoomStartThreshold(
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spTouchZoomStartThreshold"))));
    settings->setPDFPageRerenderThreshold(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spReRenderThreshold"))));
    settings->setDisplayDpi(getSwitch("zoomCallibAutomaticCb") ? -1 : dpi);

    for (auto&& bcg: this->buttonConfigs) {
        bcg->saveSettings();
    }

    languageConfig->saveSettings();

    SElement& touch = settings->getCustomElement("touch");
    touch.setBool("disableTouch", getSwitch("cbDisableTouchOnPenNear"));
    const int touchMethod = gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbTouchDisableMethod")));
    switch (touchMethod) {
        case 1:
            touch.setString("method", "X11");
            break;
        case 2:
            touch.setString("method", "custom");
            break;
        case 0:
        default:
            touch.setString("method", "auto");
    }
    touch.setString("cmdEnable", gtk_editable_get_text(GTK_EDITABLE(getWidget("txtEnableTouchCommand"))));
    touch.setString("cmdDisable", gtk_editable_get_text(GTK_EDITABLE(getWidget("txtDisableTouchCommand"))));
    touch.setInt(
            "timeout",
            static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spTouchDisableTimeout"))) * 1000));

    settings->setSnapRotationTolerance(
            static_cast<double>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spSnapRotationTolerance")))));
    settings->setSnapGridTolerance(
            static_cast<double>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spSnapGridTolerance")))));
    settings->setSnapGridSize(static_cast<double>(
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spSnapGridSize"))) * DEFAULT_GRID_SIZE));

    shapeSettingsPanel->save(*settings);

#ifdef ENABLE_AUDIO
    settings->setAudioFolder(audioRecordingsFolder);

    const size_t selectedInputDeviceIndex =
            static_cast<size_t>(gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbAudioInputDevice"))));
    if (selectedInputDeviceIndex == 0) {
        settings->setAudioInputDevice(Settings::AUDIO_INPUT_SYSTEM_DEFAULT);
    } else if (selectedInputDeviceIndex - 1 < this->audioInputDevices.size()) {
        settings->setAudioInputDevice(this->audioInputDevices[selectedInputDeviceIndex - 1].getIndex());
    }

    const size_t selectedOutputDeviceIndex =
            static_cast<size_t>(gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbAudioOutputDevice"))));
    if (selectedOutputDeviceIndex == 0) {
        settings->setAudioOutputDevice(Settings::AUDIO_OUTPUT_SYSTEM_DEFAULT);
    } else if (selectedOutputDeviceIndex - 1 < this->audioOutputDevices.size()) {
        settings->setAudioOutputDevice(this->audioOutputDevices[selectedOutputDeviceIndex - 1].getIndex());
    }

    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(getWidget("cbAudioSampleRate")))) {
        case 0:
            settings->setAudioSampleRate(16000.0);
            break;
        case 2:
            settings->setAudioSampleRate(96000.0);
            break;
        case 3:
            settings->setAudioSampleRate(192000.0);
            break;
        case 1:
        default:
            settings->setAudioSampleRate(44100.0);
            break;
    }

    settings->setAudioGain(static_cast<double>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spAudioGain")))));
    settings->setDefaultSeekTime(
            static_cast<unsigned int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(getWidget("spDefaultSeekTime")))));
#endif

    const std::optional<std::filesystem::path> selectedPalette = paletteTab.getSelectedPalette();
    if (selectedPalette.has_value()) {
        settings->setColorPaletteSetting(selectedPalette.value());
    }

    this->deviceTestingArea->saveSettings();
    this->latexPanel.save(settings->latexSettings);
    this->mcpSettingsPanel->save(*settings);

    settings->transactionEnd();

#ifndef __APPLE__
    if (menubarPreferenceChanged && inDefaultViewMode) {
        this->control->setShowMenubar(alwaysShowMenubar);
    }
#endif

    this->control->getWindow()->setGtkTouchscreenScrollingForDeviceMapping();
    this->control->getToolHandler()->loadButtonSettings();
    this->control->getWindow()->getXournal()->onSettingsChanged();
}

#ifdef ENABLE_AUDIO
void SettingsDialog::setAudioRecordingFolder(fs::path folder) {
    GtkWidget* btn = getWidget("fcAudioPath");
    if (folder.empty()) {
        GtkWidget* lbl = gtk_label_new(nullptr);
        auto msg = _("Select folder");
        auto markup = xoj::util::OwnedCString::assumeOwnership(
                g_markup_printf_escaped("<span style=\"italic\">%s</span>", msg));
        gtk_label_set_markup(GTK_LABEL(lbl), markup.get());
        gtk_widget_show(lbl);
        gtk_button_set_child(GTK_BUTTON(btn), lbl);
        gtk_widget_set_tooltip_text(btn, _("Folder not set - Select a folder to enable recording"));
        audioRecordingsFolder.clear();
    } else {
        if (!fs::is_directory(folder)) {
            g_warning("SettingsDialog::setAudioRecordingFolder(): provided path is not a directory: %s",
                      char_cast(folder.u8string().c_str()));
            return;
        }
        auto name = folder.filename().u8string();
        gtk_button_set_label(GTK_BUTTON(btn),
                             name.empty() ? char_cast(folder.u8string().c_str()) : char_cast(name.c_str()));
        gtk_widget_set_tooltip_text(btn, char_cast(folder.u8string().c_str()));
        audioRecordingsFolder = std::move(folder);
    }
}
#endif
