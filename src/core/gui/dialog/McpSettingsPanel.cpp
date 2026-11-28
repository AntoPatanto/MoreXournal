#include "McpSettingsPanel.h"

#include <array>
#include <chrono>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "control/Control.h"
#include "control/mcp/McpServer.h"
#include "control/mcp/McpToken.h"
#include "control/settings/Settings.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"

namespace {

constexpr const char* MASKED_TOKEN = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                                     "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                                     "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                                     "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";

GtkWidget* makeBoldLabel(const char* text) {
    GtkWidget* lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "mx-mcp-row-title");
    return lbl;
}

GtkWidget* makeSubLabel(const char* text) {
    GtkWidget* lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "mx-mcp-hint");
    return lbl;
}

GtkWidget* makeGroupBox() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-mcp-group");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-prefs-card");
    return box;
}

GtkWidget* makeRowBox(GtkOrientation orientation, int spacing) {
    GtkWidget* box = gtk_box_new(orientation, spacing);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-mcp-row");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "mx-prefs-row");
    return box;
}

GtkWidget* makeSectionCaption(const char* text) {
    GtkWidget* lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0F);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "mx-prefs-section-title");
    return lbl;
}

}  // namespace

McpSettingsPanel::McpSettingsPanel(Control* control): control(control) {
    rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);

    // Section 1: AI assistants
    GtkWidget* connectSection = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(connectSection), makeSectionCaption(_("AI assistants")));
    GtkWidget* intro = makeSubLabel(
            _("Let AI tools on this computer, such as Claude Code or Codex, see the open document through the Model "
              "Context Protocol (MCP). Nothing leaves this computer unless the AI tool itself sends it."));
    gtk_style_context_add_class(gtk_widget_get_style_context(intro), "mx-prefs-section-note");
    gtk_box_append(GTK_BOX(connectSection), intro);
    gtk_box_append(GTK_BOX(connectSection), buildMainSwitchesGroup());
    gtk_box_append(GTK_BOX(rootBox), connectSection);

    // Section 2: Connection
    GtkWidget* connSection = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(connSection), makeSectionCaption(_("Connection")));
    connectionGroup = buildConnectionInfoGroup();
    gtk_box_append(GTK_BOX(connSection), connectionGroup);
    gtk_box_append(GTK_BOX(rootBox), connSection);

    // Section 3: Set up an assistant
    GtkWidget* setupSection = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(setupSection), makeSectionCaption(_("Set up an assistant")));
    setupGroup = buildSetupCommandsGroup();
    gtk_box_append(GTK_BOX(setupSection), setupGroup);
    gtk_box_append(GTK_BOX(rootBox), setupSection);

    gtk_widget_show_all(rootBox);
    gtk_widget_set_visible(GTK_WIDGET(copiedLabel), FALSE);

    clientRefreshTimer = g_timeout_add_seconds(
            5,
            +[](gpointer userData) -> gboolean {
                auto* self = static_cast<McpSettingsPanel*>(userData);
                self->refreshConnectedClients();
                self->updateStatusLine();
                return G_SOURCE_CONTINUE;
            },
            this);
}

GtkWidget* McpSettingsPanel::getTargetWidget(const char* id) const {
    std::string_view key(id ? id : "");
    if (key == "ai-edit") {
        return allowEditsRow;
    }
    if (key == "ai-address" || key == "ai-token" || key == "ai-clients") {
        return connectionGroup;
    }
    if (key == "ai-setup") {
        return setupGroup;
    }
    return rootBox;
}

McpSettingsPanel::~McpSettingsPanel() {
    clientRefreshTimer.cancel();
    copiedHideTimer.cancel();
    // SettingsDialog destroys this panel before its window, and destroying the window can still emit notify
    // signals (a GtkStack changes its visible child while its children are removed). Disconnect every handler
    // bound to this object so none of them runs on a freed panel.
    for (gpointer widget: {static_cast<gpointer>(enabledSwitch), static_cast<gpointer>(allowEditsSwitch),
                           static_cast<gpointer>(showTokenBtn), static_cast<gpointer>(setupStack)}) {
        if (widget) {
            g_signal_handlers_disconnect_by_data(widget, this);
        }
    }
}

GtkWidget* McpSettingsPanel::buildMainSwitchesGroup() {
    GtkWidget* group = makeGroupBox();

    // Row 1: Allow AI assistants to connect
    GtkWidget* row1 = makeRowBox(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* row1Text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(row1Text, TRUE);
    gtk_box_append(GTK_BOX(row1Text), makeBoldLabel(_("Allow AI assistants to connect")));

    GtkWidget* statusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    statusDot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(statusDot, 8, 8);
    gtk_widget_set_valign(statusDot, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(statusDot), "mx-mcp-dot");
    statusLabel = GTK_LABEL(makeSubLabel(_("Off. No AI tool can connect.")));
    gtk_box_append(GTK_BOX(statusBox), statusDot);
    gtk_box_append(GTK_BOX(statusBox), GTK_WIDGET(statusLabel));
    gtk_box_append(GTK_BOX(row1Text), statusBox);

    enabledSwitch = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_valign(GTK_WIDGET(enabledSwitch), GTK_ALIGN_CENTER);
    g_signal_connect_swapped(enabledSwitch, "notify::active",
                             G_CALLBACK(+[](McpSettingsPanel* self) { self->onEnabledSwitched(); }), this);

    gtk_box_append(GTK_BOX(row1), row1Text);
    gtk_box_append(GTK_BOX(row1), GTK_WIDGET(enabledSwitch));
    gtk_box_append(GTK_BOX(group), row1);

    gtk_box_append(GTK_BOX(group), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Row 2: Allow edits
    allowEditsRow = makeRowBox(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* row2Text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(row2Text, TRUE);
    gtk_box_append(GTK_BOX(row2Text), makeBoldLabel(_("Allow edits")));
    gtk_box_append(GTK_BOX(row2Text),
                   makeSubLabel(_("Assistants can add text, strokes, shapes and pages. Each change is one undo step, "
                                  "and assistants can't delete what you wrote.")));

    allowEditsSwitch = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_valign(GTK_WIDGET(allowEditsSwitch), GTK_ALIGN_CENTER);
    g_signal_connect_swapped(allowEditsSwitch, "notify::active",
                             G_CALLBACK(+[](McpSettingsPanel* self) { self->onAllowEditsSwitched(); }), this);

    gtk_box_append(GTK_BOX(allowEditsRow), row2Text);
    gtk_box_append(GTK_BOX(allowEditsRow), GTK_WIDGET(allowEditsSwitch));
    gtk_box_append(GTK_BOX(group), allowEditsRow);

    return group;
}

GtkWidget* McpSettingsPanel::buildConnectionInfoGroup() {
    GtkWidget* group = makeGroupBox();

    // Row 1: Address
    GtkWidget* addrRow = makeRowBox(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(addrRow), makeBoldLabel(_("Address")));
    addressEntry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_editable(GTK_EDITABLE(addressEntry), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(addressEntry)), "mx-mcp-mono");
    gtk_box_append(GTK_BOX(addrRow), GTK_WIDGET(addressEntry));
    gtk_box_append(GTK_BOX(group), addrRow);

    gtk_box_append(GTK_BOX(group), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Row 2: Access token
    GtkWidget* tokRow = makeRowBox(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(tokRow), makeBoldLabel(_("Access token")));

    GtkWidget* tokControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    tokenEntry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_editable(GTK_EDITABLE(tokenEntry), FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(tokenEntry), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(tokenEntry)), "mx-mcp-mono");

    showTokenBtn = GTK_BUTTON(gtk_button_new_with_label(_("Show")));
    g_signal_connect_swapped(showTokenBtn, "clicked",
                             G_CALLBACK(+[](McpSettingsPanel* self) { self->onToggleShowToken(); }), this);

    GtkWidget* newTokenBtn = gtk_button_new_with_label(_("New token"));
    g_signal_connect_swapped(newTokenBtn, "clicked",
                             G_CALLBACK(+[](McpSettingsPanel* self) { self->onGenerateNewToken(); }), this);

    gtk_box_append(GTK_BOX(tokControls), GTK_WIDGET(tokenEntry));
    gtk_box_append(GTK_BOX(tokControls), GTK_WIDGET(showTokenBtn));
    gtk_box_append(GTK_BOX(tokControls), newTokenBtn);
    gtk_box_append(GTK_BOX(tokRow), tokControls);
    gtk_box_append(GTK_BOX(tokRow),
                   makeSubLabel(_("A new token disconnects every AI tool until you paste the new setup command.")));
    gtk_box_append(GTK_BOX(group), tokRow);

    gtk_box_append(GTK_BOX(group), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Row 3: Connected now
    GtkWidget* clientsRow = makeRowBox(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(clientsRow), makeBoldLabel(_("Connected now")));
    clientsBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
    gtk_box_append(GTK_BOX(clientsRow), GTK_WIDGET(clientsBox));
    gtk_box_append(GTK_BOX(group), clientsRow);

    return group;
}

GtkWidget* McpSettingsPanel::buildSetupCommandsGroup() {
    GtkWidget* group = makeGroupBox();

    setupStack = GTK_STACK(gtk_stack_new());
    // Empty visible pages so GtkStackSwitcher renders the 3 native tabs while one shared viewer shows the snippet.
    for (const auto& [id, title]: std::array<std::pair<const char*, const char*>, 3>{
                 {{"claude-code", _("Claude Code")}, {"codex", _("Codex")}, {"claude-desktop", _("Claude Desktop")}}}) {
        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_show(page);
        gtk_stack_add_titled(setupStack, page, id, title);
    }
    gtk_widget_set_no_show_all(GTK_WIDGET(setupStack), TRUE);
    gtk_widget_hide(GTK_WIDGET(setupStack));

    GtkWidget* switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), setupStack);
    gtk_widget_set_halign(switcher, GTK_ALIGN_START);
    gtk_widget_set_margin_start(switcher, 10);
    gtk_widget_set_margin_end(switcher, 10);
    gtk_widget_set_margin_top(switcher, 8);
    gtk_widget_set_margin_bottom(switcher, 8);

    g_signal_connect_swapped(setupStack, "notify::visible-child-name",
                             G_CALLBACK(+[](McpSettingsPanel* self) { self->onSetupTabChanged(); }), this);

    gtk_box_append(GTK_BOX(group), switcher);
    gtk_box_append(GTK_BOX(group), GTK_WIDGET(setupStack));
    gtk_box_append(GTK_BOX(group), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* headerBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(headerBar, 12);
    gtk_widget_set_margin_end(headerBar, 12);
    gtk_widget_set_margin_top(headerBar, 8);
    gtk_widget_set_margin_bottom(headerBar, 4);

    setupInstructionLabel = GTK_LABEL(makeSubLabel(_("Run once in a terminal:")));
    gtk_widget_set_hexpand(GTK_WIDGET(setupInstructionLabel), TRUE);

    copiedLabel = GTK_LABEL(gtk_label_new(_("Copied")));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(copiedLabel)), "mx-mcp-copied");
    gtk_widget_set_no_show_all(GTK_WIDGET(copiedLabel), TRUE);

    GtkWidget* copyBtn = gtk_button_new_with_label(_("Copy"));
    g_signal_connect_swapped(copyBtn, "clicked", G_CALLBACK(+[](McpSettingsPanel* self) { self->onCopyCommand(); }),
                             this);

    gtk_box_append(GTK_BOX(headerBar), GTK_WIDGET(setupInstructionLabel));
    gtk_box_append(GTK_BOX(headerBar), GTK_WIDGET(copiedLabel));
    gtk_box_append(GTK_BOX(headerBar), copyBtn);
    gtk_box_append(GTK_BOX(group), headerBar);

    // A selectable label instead of a text view: GTK 3 text views can't turn off the hyphens Pango inserts when
    // it breaks a long word, and a hyphen inside the token would be copied by hand as part of it.
    commandLabel = GTK_LABEL(gtk_label_new(nullptr));
    gtk_label_set_selectable(commandLabel, TRUE);
    gtk_label_set_line_wrap(commandLabel, TRUE);
    gtk_label_set_line_wrap_mode(commandLabel, PANGO_WRAP_CHAR);
    gtk_label_set_xalign(commandLabel, 0.0);
#if PANGO_VERSION_CHECK(1, 44, 0)
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_insert_hyphens_new(FALSE));
    gtk_label_set_attributes(commandLabel, attrs);
    pango_attr_list_unref(attrs);
#endif
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(commandLabel)), "mx-mcp-code");
    gtk_box_append(GTK_BOX(group), GTK_WIDGET(commandLabel));

    return group;
}

void McpSettingsPanel::load(const Settings& settings) {
    updatingWidgets = true;
    mcpEnabled = settings.isMcpEnabled();
    mcpAllowEdits = settings.isMcpAllowEdits();
    mcpPort = settings.getMcpPort();
    pendingToken = settings.getMcpToken();
    tokenVisible = false;

    gtk_switch_set_active(enabledSwitch, mcpEnabled);
    gtk_switch_set_active(allowEditsSwitch, mcpEnabled && mcpAllowEdits);

    std::string addr = "http://127.0.0.1:" + std::to_string(mcpPort) + "/mcp";
    gtk_editable_set_text(GTK_EDITABLE(addressEntry), addr.c_str());

    updatingWidgets = false;
    updateUiState();
    refreshConnectedClients();
}

void McpSettingsPanel::save(Settings& settings) const {
    settings.setMcpPort(mcpPort);
    if (!pendingToken.empty()) {
        settings.setMcpToken(pendingToken);
    }
    settings.setMcpEnabled(mcpEnabled);
    settings.setMcpAllowEdits(mcpEnabled && mcpAllowEdits);
}

void McpSettingsPanel::onEnabledSwitched() {
    if (updatingWidgets) {
        return;
    }
    mcpEnabled = gtk_switch_get_active(enabledSwitch) != FALSE;
    if (mcpEnabled && pendingToken.empty()) {
        pendingToken = xoj::mcp::generateToken();
    }
    if (!mcpEnabled) {
        updatingWidgets = true;
        mcpAllowEdits = false;
        gtk_switch_set_active(allowEditsSwitch, FALSE);
        updatingWidgets = false;
    }
    updateUiState();
    refreshConnectedClients();
}

void McpSettingsPanel::onAllowEditsSwitched() {
    if (updatingWidgets) {
        return;
    }
    mcpAllowEdits = gtk_switch_get_active(allowEditsSwitch) != FALSE;
}

void McpSettingsPanel::onToggleShowToken() {
    tokenVisible = !tokenVisible;
    updateTokenDisplay();
}

void McpSettingsPanel::onGenerateNewToken() {
    pendingToken = xoj::mcp::generateToken();
    updateTokenDisplay();
    updateSetupCommand();
}

void McpSettingsPanel::onSetupTabChanged() {
    const char* name = gtk_stack_get_visible_child_name(setupStack);
    if (!name) {
        return;
    }
    std::string_view sv(name);
    if (sv == "codex") {
        activeTab = SetupTab::CODEX;
    } else if (sv == "claude-desktop") {
        activeTab = SetupTab::CLAUDE_DESKTOP;
    } else {
        activeTab = SetupTab::CLAUDE_CODE;
    }
    updateSetupCommand();
}

void McpSettingsPanel::onCopyCommand() {
    std::string snippet = buildCurrentSnippet();
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clipboard) {
        gtk_clipboard_set_text(clipboard, snippet.c_str(), static_cast<gint>(snippet.size()));
    }
    gtk_widget_set_visible(GTK_WIDGET(copiedLabel), TRUE);
    copiedHideTimer = g_timeout_add(
            1500,
            +[](gpointer userData) -> gboolean {
                auto* self = static_cast<McpSettingsPanel*>(userData);
                self->copiedHideTimer.consume();
                gtk_widget_set_visible(GTK_WIDGET(self->copiedLabel), FALSE);
                return G_SOURCE_REMOVE;
            },
            this);
}

void McpSettingsPanel::updateUiState() {
    gtk_widget_set_sensitive(allowEditsRow, mcpEnabled);
    gtk_widget_set_sensitive(connectionGroup, mcpEnabled);
    gtk_widget_set_sensitive(setupGroup, mcpEnabled);
    updateStatusLine();
    updateTokenDisplay();
    updateSetupCommand();
}

void McpSettingsPanel::updateStatusLine() {
    GtkStyleContext* dotCtx = gtk_widget_get_style_context(statusDot);
    GtkStyleContext* lblCtx = gtk_widget_get_style_context(GTK_WIDGET(statusLabel));
    gtk_style_context_remove_class(dotCtx, "mx-mcp-dot-ok");
    gtk_style_context_remove_class(dotCtx, "mx-mcp-dot-err");
    gtk_style_context_remove_class(lblCtx, "mx-mcp-error");

    if (!mcpEnabled) {
        gtk_label_set_text(statusLabel, _("Off. No AI tool can connect."));
        return;
    }

    xoj::mcp::McpServer* server = control ? control->getMcpServer() : nullptr;
    if (server) {
        std::string err = server->getLastError();
        if (!err.empty() && !server->isRunning()) {
            gtk_style_context_add_class(dotCtx, "mx-mcp-dot-err");
            gtk_style_context_add_class(lblCtx, "mx-mcp-error");
            gtk_label_set_text(statusLabel, err.c_str());
            return;
        }
        if (!server->isRunning()) {
            // The server only starts when the dialog is saved, so don't claim it is listening yet.
            gtk_label_set_text(statusLabel, _("On after you press Save. No AI tool can connect yet."));
            return;
        }
    }

    gtk_style_context_add_class(dotCtx, "mx-mcp-dot-ok");
    gtk_label_set_text(statusLabel, _("On. Listening on this computer only (127.0.0.1)."));
}

void McpSettingsPanel::updateTokenDisplay() {
    if (tokenVisible && !pendingToken.empty()) {
        gtk_editable_set_text(GTK_EDITABLE(tokenEntry), pendingToken.c_str());
        gtk_button_set_label(showTokenBtn, _("Hide"));
    } else {
        gtk_editable_set_text(GTK_EDITABLE(tokenEntry), MASKED_TOKEN);
        gtk_button_set_label(showTokenBtn, _("Show"));
    }
}

void McpSettingsPanel::updateSetupCommand() {
    switch (activeTab) {
        case SetupTab::CLAUDE_CODE:
            gtk_label_set_text(setupInstructionLabel, _("Run once in a terminal:"));
            break;
        case SetupTab::CODEX:
            gtk_label_set_text(setupInstructionLabel, _("Add to ~/.codex/config.toml:"));
            break;
        case SetupTab::CLAUDE_DESKTOP:
            gtk_label_set_text(setupInstructionLabel,
                               _("Add to claude_desktop_config.json (uses the bundled stdio bridge):"));
            break;
    }

    std::string snippet = buildCurrentSnippet();
    gtk_label_set_text(commandLabel, snippet.c_str());
}

void McpSettingsPanel::refreshConnectedClients() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(clientsBox));
    for (GList* it = children; it != nullptr; it = it->next) {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }
    g_list_free(children);

    std::vector<xoj::mcp::ConnectedClient> clients;
    if (mcpEnabled && control && control->getMcpServer()) {
        clients = control->getMcpServer()->getRecentClients();
    }

    if (clients.empty()) {
        GtkWidget* noneLbl = makeSubLabel(_("None yet"));
        gtk_box_append(clientsBox, noneLbl);
        gtk_widget_show(noneLbl);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (const auto& client: clients) {
        auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - client.lastSeen).count();
        long mins = elapsedSec / 60;
        std::string line;
        if (mins <= 0) {
            line = client.name + ", " + _("connected just now");
        } else {
            line = client.name + ", " + FS(_F("connected {1} min ago") % mins);
        }
        GtkWidget* lbl = makeSubLabel(line.c_str());
        gtk_box_append(clientsBox, lbl);
        gtk_widget_show(lbl);
    }
}

std::string McpSettingsPanel::resolveBridgeExecutablePath() {
#ifdef _WIN32
    fs::path bridgePath = Util::getExePath() / "morexournal-mcp.exe";
#else
    fs::path bridgePath = Util::getExePath() / "morexournal-mcp";
#endif
    return std::string(char_cast(bridgePath.u8string().c_str()));
}

std::string McpSettingsPanel::buildCurrentSnippet() const {
    const std::string portStr = std::to_string(mcpPort);
    const std::string tokenStr = pendingToken.empty() ? "<token>" : pendingToken;
    const std::string url = "http://127.0.0.1:" + portStr + "/mcp";

    switch (activeTab) {
        case SetupTab::CLAUDE_CODE:
            return "claude mcp add --transport http morexournal " + url + " --header \"Authorization: Bearer " +
                   tokenStr + "\"";
        case SetupTab::CODEX:
            // Codex uses http_headers for static inline bearer tokens (bearer_token_env_var is env-var only).
            return "[mcp_servers.morexournal]\nurl = \"" + url + "\"\nhttp_headers = { Authorization = \"Bearer " +
                   tokenStr + "\" }";
        case SetupTab::CLAUDE_DESKTOP: {
            const std::string bridge = resolveBridgeExecutablePath();
            // nlohmann::json::dump() handles JSON string escaping (including backslashes on Windows).
            const std::string escapedCmd = nlohmann::json(bridge).dump();
            const std::string escapedToken = nlohmann::json(tokenStr).dump();
            return "\"morexournal\": {\n  \"command\": " + escapedCmd + ",\n  \"args\": [\"--port\", \"" + portStr +
                   "\", \"--token\", " + escapedToken + "]\n}";
        }
    }
    return {};
}
