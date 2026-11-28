#include "McpServer.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "control/Control.h"
#include "control/mcp/McpHttp.h"
#include "control/mcp/McpTools.h"
#include "control/settings/Settings.h"

#include "config.h"

namespace xoj::mcp {

namespace {

constexpr auto CLIENT_ACTIVE_WINDOW = std::chrono::minutes(2);

struct PendingMainCall {
    std::promise<ToolCallResult> promise;
    std::atomic<bool> completed{false};
};

// Reads up to the end of the headers and parses them. The body is read separately, after the request has
// passed the Host, Origin and token checks, so an unauthenticated client can't make the server allocate or wait
// for a body of up to MAX_HTTP_BODY_BYTES.
auto readHttpHeadersFromStream(GInputStream* in, GCancellable* cancellable, std::string& raw,
                               HttpHeaderParseResult& outParsed, HttpResponse& outError) -> bool {
    raw.reserve(2048);
    char chunk[4096];
    GError* err = nullptr;

    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        gssize n = g_input_stream_read(in, chunk, sizeof(chunk), cancellable, &err);
        if (n <= 0) {
            if (err) {
                g_error_free(err);
            }
            return false;
        }
        raw.append(chunk, static_cast<size_t>(n));
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd == std::string::npos && raw.size() > MAX_HTTP_HEADER_BYTES) {
            outError = makeJsonErrorResponse(431, "Request Header Fields Too Large",
                                             "Request headers exceed 16 KiB limit");
            return false;
        }
    }

    outParsed = parseHttpHeaders(raw);
    if (!outParsed.ok) {
        outError = makeJsonErrorResponse(outParsed.errorStatus, "", outParsed.errorMessage);
        return false;
    }
    return true;
}

// Reads the body announced by Content-Length, starting with the bytes that arrived together with the headers.
auto readHttpBodyFromStream(GInputStream* in, GCancellable* cancellable, const std::string& raw,
                            const HttpHeaderParseResult& parsed, HttpRequest& request) -> bool {
    char chunk[4096];
    GError* err = nullptr;
    request.body.reserve(parsed.contentLength);

    if (raw.size() > parsed.headerEndOffset) {
        size_t alreadyRead = raw.size() - parsed.headerEndOffset;
        size_t toCopy = std::min(alreadyRead, parsed.contentLength);
        request.body.append(raw.data() + parsed.headerEndOffset, toCopy);
    }

    while (request.body.size() < parsed.contentLength) {
        size_t remaining = parsed.contentLength - request.body.size();
        size_t want = std::min(remaining, sizeof(chunk));
        gssize n = g_input_stream_read(in, chunk, want, cancellable, &err);
        if (n <= 0) {
            if (err) {
                g_error_free(err);
            }
            return false;
        }
        request.body.append(chunk, static_cast<size_t>(n));
    }
    return true;
}

void writeHttpResponseToStream(GOutputStream* out, GCancellable* cancellable, const HttpResponse& resp) {
    std::string wire = serializeHttpResponse(resp);
    gsize written = 0;
    GError* err = nullptr;
    g_output_stream_write_all(out, wire.data(), wire.size(), &written, cancellable, &err);
    if (err) {
        g_error_free(err);
    }
    g_output_stream_flush(out, cancellable, nullptr);
}

}  // namespace

struct McpServer::SharedState {
    Control* control = nullptr;
    ToolCallHandler directToolHandler;
    std::string serverVersion;

    mutable std::mutex mutex;
    std::string token;
    bool allowEdits = false;
    bool running = false;
    uint16_t boundPort = 0;
    std::string lastError;

    // Connected client tracking (pruned to last 2 minutes)
    std::unordered_map<std::string, ConnectedClient> clients;
    std::unordered_map<std::string, std::string> uaToClientName;

    // Shutdown / cancellation coordination
    std::atomic<bool> stopped{false};
    GCancellable* cancellable = nullptr;
    std::mutex pendingMutex;
    std::vector<std::shared_ptr<PendingMainCall>> pendingCalls;

    SharedState(): cancellable(g_cancellable_new()) {}
    ~SharedState() {
        if (cancellable) {
            g_object_unref(cancellable);
            cancellable = nullptr;
        }
    }

    void recordClientActivity(const std::optional<std::string>& initClientName,
                              const std::optional<std::string_view>& userAgent) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex);

        // Prune expired entries first
        for (auto it = clients.begin(); it != clients.end();) {
            if (now - it->second.lastSeen > CLIENT_ACTIVE_WINDOW) {
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        std::string ua = userAgent.has_value() ? std::string(*userAgent) : std::string();
        std::string displayName;
        if (initClientName.has_value() && !initClientName->empty()) {
            displayName = *initClientName;
            if (!ua.empty()) {
                uaToClientName[ua] = displayName;
            }
        } else if (!ua.empty()) {
            auto mapped = uaToClientName.find(ua);
            displayName = (mapped != uaToClientName.end()) ? mapped->second : ua;
        } else {
            displayName = "MCP client";
        }

        clients[displayName] = ConnectedClient{displayName, now};
    }

    void cancelAllPendingMainCalls() {
        std::vector<std::shared_ptr<PendingMainCall>> toCancel;
        {
            std::lock_guard lock(pendingMutex);
            toCancel.swap(pendingCalls);
        }
        for (auto& call: toCancel) {
            if (!call->completed.exchange(true)) {
                call->promise.set_value(ToolCallResult::text("MCP server is stopping", true));
            }
        }
    }
};

McpServer::McpServer(Control* control): state(std::make_shared<SharedState>()) {
    state->control = control;
    state->serverVersion = PROJECT_VERSION;
}

McpServer::McpServer(ToolCallHandler directToolHandler, std::string serverVersion):
        state(std::make_shared<SharedState>()) {
    state->directToolHandler = std::move(directToolHandler);
    state->serverVersion = std::move(serverVersion);
}

McpServer::~McpServer() { stop(); }

auto McpServer::start(uint16_t port, const std::string& token, bool allowEdits) -> bool {
    stop();

    auto nextState = std::make_shared<SharedState>();
    nextState->control = state->control;
    nextState->directToolHandler = state->directToolHandler;
    nextState->serverVersion = state->serverVersion;
    nextState->token = token;
    nextState->allowEdits = allowEdits;
    state = nextState;

    GSocketService* threaded = g_threaded_socket_service_new(MAX_CONCURRENT_CONNECTIONS);
    if (!threaded) {
        std::lock_guard lock(state->mutex);
        state->lastError = "Could not create local socket service.";
        return false;
    }

    GInetAddress* loopback = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* bindAddr = g_inet_socket_address_new(loopback, port);
    g_object_unref(loopback);

    GSocketAddress* effectiveAddr = nullptr;
    GError* err = nullptr;
    gboolean ok = g_socket_listener_add_address(G_SOCKET_LISTENER(threaded), bindAddr, G_SOCKET_TYPE_STREAM,
                                                G_SOCKET_PROTOCOL_TCP, nullptr, &effectiveAddr, &err);
    g_object_unref(bindAddr);

    if (!ok) {
        std::string msg = "Could not listen on port " + std::to_string(port) +
                          ". Another program may be using it.";
        if (err) {
            g_warning("MCP server failed to bind 127.0.0.1:%u: %s", static_cast<unsigned>(port), err->message);
            g_error_free(err);
        }
        g_socket_listener_close(G_SOCKET_LISTENER(threaded));
        g_object_unref(threaded);
        std::lock_guard lock(state->mutex);
        state->lastError = std::move(msg);
        return false;
    }

    uint16_t actualPort = port;
    if (effectiveAddr && G_IS_INET_SOCKET_ADDRESS(effectiveAddr)) {
        actualPort = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effectiveAddr));
    }
    if (effectiveAddr) {
        g_object_unref(effectiveAddr);
    }

    // Hold a shared_ptr copy for the signal handler, released via destroy notify when the service is disposed.
    auto* statePtr = new std::shared_ptr<SharedState>(state);
    g_signal_connect_data(
            threaded, "run", G_CALLBACK(&McpServer::onIncomingConnection), statePtr,
            +[](gpointer data, GClosure*) { delete static_cast<std::shared_ptr<SharedState>*>(data); },
            static_cast<GConnectFlags>(0));

    service = threaded;
    {
        std::lock_guard lock(state->mutex);
        state->running = true;
        state->boundPort = actualPort;
        state->lastError.clear();
    }

    g_socket_service_start(service);
    return true;
}

void McpServer::stop() {
    if (state) {
        state->stopped.store(true);
        if (state->cancellable) {
            g_cancellable_cancel(state->cancellable);
        }
        state->cancelAllPendingMainCalls();
        std::lock_guard lock(state->mutex);
        state->running = false;
        state->boundPort = 0;
        state->clients.clear();
        state->uaToClientName.clear();
    }

    if (service) {
        g_socket_service_stop(service);
        g_socket_listener_close(G_SOCKET_LISTENER(service));
        g_object_unref(service);
        service = nullptr;
    }
}

void McpServer::updateRuntimeConfig(const std::string& token, bool allowEdits) {
    std::lock_guard lock(state->mutex);
    state->token = token;
    state->allowEdits = allowEdits;
}

void McpServer::applySettings(Settings& settings) {
    if (!settings.isMcpEnabled()) {
        stop();
        std::lock_guard lock(state->mutex);
        state->lastError.clear();
        return;
    }

    const std::string& token = settings.ensureMcpToken();
    const uint16_t desiredPort = static_cast<uint16_t>(settings.getMcpPort());
    const bool allowEdits = settings.isMcpAllowEdits();

    if (isRunning() && getBoundPort() == desiredPort) {
        updateRuntimeConfig(token, allowEdits);
        return;
    }

    start(desiredPort, token, allowEdits);
}

auto McpServer::isRunning() const -> bool {
    std::lock_guard lock(state->mutex);
    return state->running;
}

auto McpServer::getBoundPort() const -> uint16_t {
    std::lock_guard lock(state->mutex);
    return state->boundPort;
}

auto McpServer::getLastError() const -> std::string {
    std::lock_guard lock(state->mutex);
    return state->lastError;
}

auto McpServer::getRecentClients() const -> std::vector<ConnectedClient> {
    const auto now = std::chrono::steady_clock::now();
    std::vector<ConnectedClient> active;
    std::lock_guard lock(state->mutex);
    for (const auto& [key, entry]: state->clients) {
        if (now - entry.lastSeen <= CLIENT_ACTIVE_WINDOW) {
            active.push_back(entry);
        }
    }
    std::sort(active.begin(), active.end(),
              [](const ConnectedClient& a, const ConnectedClient& b) { return a.lastSeen > b.lastSeen; });
    return active;
}

gboolean McpServer::onIncomingConnection(GThreadedSocketService*, GSocketConnection* connection, GObject*,
                                         gpointer userData) {
    auto stateCopy = *static_cast<std::shared_ptr<SharedState>*>(userData);
    handleConnection(stateCopy, connection);
    return TRUE;
}

void McpServer::handleConnection(const std::shared_ptr<SharedState>& state, GSocketConnection* connection) {
    if (!state || state->stopped.load()) {
        return;
    }

    GSocket* sock = g_socket_connection_get_socket(connection);
    if (sock) {
        g_socket_set_timeout(sock, HTTP_READ_TIMEOUT_SECONDS);
    }

    GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(connection));

    std::string raw;
    HttpHeaderParseResult parsed;
    HttpResponse parseErr;
    parseErr.status = 0;

    if (!readHttpHeadersFromStream(in, state->cancellable, raw, parsed, parseErr)) {
        if (parseErr.status != 0 && !state->stopped.load()) {
            writeHttpResponseToStream(out, state->cancellable, parseErr);
        }
        return;
    }

    std::string currentToken;
    bool currentAllowEdits = false;
    {
        std::lock_guard lock(state->mutex);
        currentToken = state->token;
        currentAllowEdits = state->allowEdits;
    }

    HttpRequest req = std::move(parsed.request);
    if (auto errResp = validateHttpRequest(req, currentToken); errResp.has_value()) {
        writeHttpResponseToStream(out, state->cancellable, *errResp);
        return;
    }

    if (!readHttpBodyFromStream(in, state->cancellable, raw, parsed, req)) {
        return;
    }

    ToolCallHandler toolHandler = [state, currentAllowEdits](const std::string& name,
                                                             const nlohmann::json& arguments) -> ToolCallResult {
        if (state->directToolHandler) {
            return state->directToolHandler(name, arguments);
        }
        if (state->stopped.load() || !state->control) {
            return ToolCallResult::text("MCP server is unavailable", true);
        }

        auto call = std::make_shared<PendingMainCall>();
        auto future = call->promise.get_future();
        {
            std::lock_guard lock(state->pendingMutex);
            if (state->stopped.load()) {
                return ToolCallResult::text("MCP server is stopping", true);
            }
            state->pendingCalls.push_back(call);
        }

        struct InvokeData {
            std::shared_ptr<SharedState> state;
            std::shared_ptr<PendingMainCall> call;
            std::string name;
            nlohmann::json arguments;
            bool allowEdits;
        };

        auto* invoke = new InvokeData{state, call, name, arguments, currentAllowEdits};
        g_main_context_invoke_full(
                nullptr, G_PRIORITY_DEFAULT,
                +[](gpointer data) -> gboolean {
                    auto* d = static_cast<InvokeData*>(data);
                    ToolCallResult res;
                    if (d->state->stopped.load() || !d->state->control) {
                        res = ToolCallResult::text("MCP server is stopping", true);
                    } else {
                        res = executeMcpToolOnMainThread(d->state->control, d->name, d->arguments, d->allowEdits);
                    }
                    if (!d->call->completed.exchange(true)) {
                        d->call->promise.set_value(std::move(res));
                    }
                    {
                        std::lock_guard lock(d->state->pendingMutex);
                        auto& list = d->state->pendingCalls;
                        list.erase(std::remove(list.begin(), list.end(), d->call), list.end());
                    }
                    return G_SOURCE_REMOVE;
                },
                invoke, +[](gpointer data) { delete static_cast<InvokeData*>(data); });

        return future.get();
    };

    McpDispatchOutcome outcome = dispatchMcpRequest(req, toolHandler, state->serverVersion);
    state->recordClientActivity(outcome.clientName, req.header("user-agent"));

    if (!state->stopped.load()) {
        writeHttpResponseToStream(out, state->cancellable, outcome.response);
    }
}

}  // namespace xoj::mcp
