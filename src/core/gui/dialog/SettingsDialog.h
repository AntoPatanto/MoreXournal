/*
 * Xournal++
 *
 * Settings Dialog
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtk/gtk.h>

#include "audio/DeviceInfo.h"
#include "control/tools/StrokeStabilizerEnum.h"
#include "gui/Builder.h"
#include "util/raii/GSourceURef.h"
#include "util/raii/GtkWindowUPtr.h"

#include "ButtonConfigGui.h"
#include "LanguageConfigGui.h"
#include "LatexSettingsPanel.h"
#include "SettingsDialogPaletteTab.h"
#include "SettingsSearch.h"
#include "config-features.h"
#include "filesystem.h"

class Control;
class Settings;
class DeviceTestingArea;
class McpSettingsPanel;
class ShapeSettingsPanel;

struct Palette;

class SettingsDialog {
public:
    SettingsDialog(GladeSearchpath* gladeSearchPath, Settings* settings, Control* control,
                   const std::vector<fs::path>& paletteDirectories, std::function<void()> callback);
    ~SettingsDialog();

    inline GtkWindow* getWindow() const { return window.get(); }

private:
    struct PageInfo {
        std::string id;
        std::string title;
        GtkListBoxRow* navRow = nullptr;
        GtkLabel* countLabel = nullptr;
        GtkScrolledWindow* scroller = nullptr;
        GtkBox* contentBox = nullptr;
    };

    struct RowWidgets {
        GtkWidget* rowBox = nullptr;
        GtkWidget* separator = nullptr;
        GtkWidget* depHint = nullptr;
    };

    struct SegmentedControl {
        GtkWidget* box = nullptr;
        std::vector<GtkRadioButton*> buttons;
    };

    struct SearchTarget {
        xoj::prefs::SearchEntry entry;
        GtkWidget* targetWidget = nullptr;
        GtkExpander* parentExpander = nullptr;
    };

    void buildPages(GladeSearchpath* gladeSearchPath);
    void buildGeneralPage();
    void buildAppearancePage();
    void buildPagesZoomPage();
    void buildPenPage();
    void buildMousePage();
    void buildTouchPage();
    void buildToolsPage();
#ifdef ENABLE_AUDIO
    void buildAudioPage();
#endif
    void buildLatexPage();
    void buildAiPage();
    void buildAdvancedPage();
    void buildSearchResultsPage();

    PageInfo& addPage(const char* id, const char* title, const char* iconName, const char* leadText = nullptr);
    GtkBox* addSection(PageInfo& page, const char* sectionId, const char* title, const char* note = nullptr,
                       const char* tag = nullptr);
    void registerSearch(const std::string& pageId, const std::string& pageTitle, const std::string& sectionId,
                        const std::string& sectionTitle, const std::string& sectionNote, const char* rowId,
                        const char* label, const char* subtitle, const char* keywords, GtkWidget* targetWidget,
                        GtkExpander* parentExpander = nullptr);

    GtkWidget* addRowToCard(GtkBox* card, const char* rowId, const char* label, const char* subtitle,
                            GtkWidget* control, bool restartNeeded = false, const char* depHintText = nullptr,
                            bool wide = false);
    GtkWidget* addSwitchRow(GtkBox* card, const char* id, const char* label, const char* subtitle = nullptr,
                            bool restartNeeded = false, const char* depHintText = nullptr);
    GtkWidget* addSpinRow(GtkBox* card, const char* id, const char* adjustmentId, int digits, double climbRate,
                          const char* label, const char* subtitle = nullptr, const char* unit = nullptr,
                          const char* depHintText = nullptr);
    GtkWidget* addComboTextRow(GtkBox* card, const char* id, const std::vector<const char*>& items, const char* label,
                               const char* subtitle = nullptr, bool restartNeeded = false,
                               const char* depHintText = nullptr);
    GtkWidget* addSegmentedRow(GtkBox* card, const char* id, const std::vector<const char*>& options, const char* label,
                               const char* subtitle = nullptr);
    GtkWidget* addColorRow(GtkBox* card, const char* id, const char* label, const char* subtitle = nullptr,
                           bool useAlpha = false, const char* depHintText = nullptr);
    GtkWidget* addEntryRow(GtkBox* card, const char* id, const char* label, const char* subtitle = nullptr,
                           bool monospace = false, const char* actionButtonId = nullptr,
                           const char* actionButtonLabel = nullptr, const char* depHintText = nullptr);
    GtkWidget* addScaleRow(GtkBox* card, const char* id, const char* adjustmentId, int roundDigits, const char* label,
                           const char* subtitle = nullptr, const char* depHintText = nullptr);

    GtkWidget* getWidget(const char* name);
    void setRowDepState(const char* rowId, bool met);
    void setRowVisible(const char* rowId, bool visible);

    void onNavRowSelected(GtkListBoxRow* row);
    void onSearchChanged();
    void jumpToSearchResult(size_t targetIndex);
    void clearFlash();

    void save();
    void setDpi(int dpi);
    void updatePageSpaceRows();
    void updateRecolorRows();
    void updateCursorHighlightRows();
    void updateCustomTouchCommandRows();
    void updatePressureSensitivityOptions();
    void updatePressureValueLabels();
    void updateStrokeFilterRows();
#ifdef ENABLE_AUDIO
    void updateAudioRows();
    void setAudioRecordingFolder(fs::path folder);
#endif
    void showStabilizerAvMethodOptions(StrokeStabilizer::AveragingMethod method);
    void showStabilizerPreprocessorOptions(StrokeStabilizer::Preprocessor preprocessor);

    void load();
    void loadSwitch(const char* name, bool value);
    bool getSwitch(const char* name);
    void loadSlider(const char* name, double value);
    double getSlider(const char* name);
    void setSegmentedActive(const char* name, int index);
    int getSegmentedActive(const char* name) const;

    void initMouseButtonEvents(GladeSearchpath* gladeSearchPath);

private:
    Settings* settings = nullptr;
    Control* control = nullptr;
    GtkWidget* callib = nullptr;
    int dpi = 72;

#ifdef ENABLE_AUDIO
    fs::path audioRecordingsFolder;
    std::vector<DeviceInfo> audioInputDevices;
    std::vector<DeviceInfo> audioOutputDevices;
#endif

    Builder builder;
    xoj::util::GtkWindowUPtr window;
    GtkSearchEntry* searchEntry = nullptr;
    GtkListBox* navList = nullptr;
    GtkStack* contentStack = nullptr;
    GtkBox* searchResultsBox = nullptr;

    std::vector<PageInfo> pages;
    std::unordered_map<std::string, size_t> pageIndexById;
    std::string activePageId = "general";

    std::unordered_map<std::string, GtkWidget*> widgets;
    std::unordered_map<std::string, RowWidgets> rows;
    std::unordered_map<std::string, SegmentedControl> segmentedControls;
    std::unordered_map<std::string, GtkLabel*> scaleValueLabels;

    std::vector<SearchTarget> searchTargets;
    std::vector<xoj::prefs::SearchEntry> searchEntries;

    GtkWidget* flashedWidget = nullptr;
    xoj::util::GSourceURef flashTimer;
    xoj::util::GSourceURef scrollIdle;

    std::unique_ptr<DeviceTestingArea> deviceTestingArea;
    std::unique_ptr<ShapeSettingsPanel> shapeSettingsPanel;
    std::unique_ptr<McpSettingsPanel> mcpSettingsPanel;
    std::unique_ptr<LanguageConfigGui> languageConfig;
    std::vector<std::unique_ptr<ButtonConfigGui>> buttonConfigs;

    LatexSettingsPanel latexPanel;
    SettingsDialogPaletteTab paletteTab;

    std::function<void()> callback;
};
