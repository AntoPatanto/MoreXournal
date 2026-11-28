/*
 * MoreXournal
 *
 * Local-only Streamable HTTP MCP server built on GIO GThreadedSocketService
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gio/gio.h>

#include "McpProtocol.h"

class Control;
class Settings;

namespace xoj::mcp {

struct ConnectedClient {
    std::string name;
    std::chrono::steady_clock::time_point lastSeen;
};

class McpServer {
public:
    /// Construct a server that executes tools against Control on the GTK main thread.
    explicit McpServer(Control* control);

    /// Construct a server with a custom tool handler (used in unit tests without a GTK main loop).
    McpServer(ToolCallHandler directToolHandler, std::string serverVersion);

    ~McpServer();

    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    /// Start listening on 127.0.0.1:<port> (port == 0 binds an ephemeral port).
    /// Returns true on success; on failure sets lastError().
    auto start(uint16_t port, const std::string& token, bool allowEdits) -> bool;

    /// Immediately stop listening, cancel active connections, and release the port.
    void stop();

    /// Update authentication token and allow-edits flag without restarting the listener.
    void updateRuntimeConfig(const std::string& token, bool allowEdits);

    /// Reconcile running state, port, token, and allowEdits against Settings.
    void applySettings(Settings& settings);

    auto isRunning() const -> bool;
    auto getBoundPort() const -> uint16_t;
    auto getLastError() const -> std::string;

    /// Returns clients seen within the last 2 minutes, sorted by most recently seen.
    auto getRecentClients() const -> std::vector<ConnectedClient>;

private:
    struct SharedState;

    static gboolean onIncomingConnection(GThreadedSocketService* service, GSocketConnection* connection,
                                         GObject* sourceObject, gpointer userData);
    static void handleConnection(const std::shared_ptr<SharedState>& state, GSocketConnection* connection);

    std::shared_ptr<SharedState> state;
    GSocketService* service = nullptr;
};

}  // namespace xoj::mcp
