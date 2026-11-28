/*
 * MoreXournal
 *
 * Self-contained Preferences panel for built-in MCP server ("AI assistants")
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gtk/gtk.h>

#include "util/raii/GSourceURef.h"

class Control;
class Settings;

class McpSettingsPanel {
public:
    explicit McpSettingsPanel(Control* control);
    ~McpSettingsPanel();

    McpSettingsPanel(const McpSettingsPanel&) = delete;
    McpSettingsPanel& operator=(const McpSettingsPanel&) = delete;

    GtkWidget* getPanel() const { return rootBox; }
    GtkWidget* getTargetWidget(const char* id) const;

    void load(const Settings& settings);
    void save(Settings& settings) const;

private:
    enum class SetupTab { CLAUDE_CODE, CODEX, CLAUDE_DESKTOP };

    GtkWidget* buildMainSwitchesGroup();
    GtkWidget* buildConnectionInfoGroup();
    GtkWidget* buildSetupCommandsGroup();

    void onEnabledSwitched();
    void onAllowEditsSwitched();
    void onToggleShowToken();
    void onGenerateNewToken();
    void onCopyCommand();
    void onSetupTabChanged();

    void updateUiState();
    void updateStatusLine();
    void updateTokenDisplay();
    void updateSetupCommand();
    void refreshConnectedClients();

    std::string buildCurrentSnippet() const;
    static std::string resolveBridgeExecutablePath();

    Control* control = nullptr;
    GtkWidget* rootBox = nullptr;

    GtkSwitch* enabledSwitch = nullptr;
    GtkWidget* statusDot = nullptr;
    GtkLabel* statusLabel = nullptr;

    GtkWidget* allowEditsRow = nullptr;
    GtkSwitch* allowEditsSwitch = nullptr;

    GtkWidget* connectionGroup = nullptr;
    GtkEntry* addressEntry = nullptr;
    GtkEntry* tokenEntry = nullptr;
    GtkButton* showTokenBtn = nullptr;
    GtkBox* clientsBox = nullptr;

    GtkWidget* setupGroup = nullptr;
    GtkStack* setupStack = nullptr;
    GtkLabel* setupInstructionLabel = nullptr;
    GtkLabel* copiedLabel = nullptr;
    GtkLabel* commandLabel = nullptr;

    bool mcpEnabled = false;
    bool mcpAllowEdits = false;
    int mcpPort = 47615;
    std::string pendingToken;
    bool tokenVisible = false;
    bool updatingWidgets = false;
    SetupTab activeTab = SetupTab::CLAUDE_CODE;

    xoj::util::GSourceURef clientRefreshTimer;
    xoj::util::GSourceURef copiedHideTimer;
};
