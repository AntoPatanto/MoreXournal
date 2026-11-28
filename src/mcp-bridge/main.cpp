/*
 * MoreXournal
 *
 * Stdio-to-Streamable-HTTP bridge for MCP clients such as Claude Desktop
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <gio/gio.h>
#include <nlohmann/json.hpp>

#include "config.h"

namespace {

constexpr uint16_t DEFAULT_PORT = 47615;
constexpr unsigned int IO_TIMEOUT_SECONDS = 30;
constexpr size_t MAX_LINE_BYTES = 8 * 1024 * 1024;
constexpr const char* NOT_RUNNING_MSG = "MoreXournal is not running or AI assistants are off in Preferences";

auto toLowerAscii(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c: s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

auto trimAscii(std::string_view sv) -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

struct BridgeHttpResponse {
    bool connected = false;
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string errorDetail;
};

auto postJsonToServer(uint16_t port, const std::string& token, const std::string& jsonBody,
                      const std::string& protocolVersion, const std::string& mcpMethod,
                      const std::string& mcpName) -> BridgeHttpResponse {
    BridgeHttpResponse res;

    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_timeout(client, IO_TIMEOUT_SECONDS);

    GError* err = nullptr;
    GSocketConnection* conn = g_socket_client_connect_to_host(client, "127.0.0.1", port, nullptr, &err);
    g_object_unref(client);

    if (!conn) {
        if (err) {
            res.errorDetail = err->message;
            g_error_free(err);
        }
        return res;
    }

    res.connected = true;

    std::ostringstream req;
    req.imbue(std::locale::classic());
    req << "POST /mcp HTTP/1.1\r\n";
    req << "Host: 127.0.0.1:" << port << "\r\n";
    req << "Authorization: Bearer " << token << "\r\n";
    req << "Content-Type: application/json\r\n";
    req << "Accept: application/json\r\n";
    req << "User-Agent: morexournal-mcp/" << PROJECT_VERSION << "\r\n";
    if (!protocolVersion.empty()) {
        req << "MCP-Protocol-Version: " << protocolVersion << "\r\n";
    }
    if (!mcpMethod.empty()) {
        req << "Mcp-Method: " << mcpMethod << "\r\n";
    }
    if (!mcpName.empty()) {
        req << "Mcp-Name: " << mcpName << "\r\n";
    }
    req << "Content-Length: " << jsonBody.size() << "\r\n";
    req << "Connection: close\r\n\r\n";
    req << jsonBody;

    const std::string wire = req.str();
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    gsize written = 0;
    if (!g_output_stream_write_all(out, wire.data(), wire.size(), &written, nullptr, &err)) {
        res.connected = false;
        if (err) {
            res.errorDetail = err->message;
            g_error_free(err);
        }
        g_object_unref(conn);
        return res;
    }
    g_output_stream_flush(out, nullptr, nullptr);

    GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
    std::string raw;
    raw.reserve(4096);
    char buf[8192];
    for (;;) {
        gssize n = g_input_stream_read(in, buf, sizeof(buf), nullptr, &err);
        if (n < 0) {
            res.connected = false;
            if (err) {
                res.errorDetail = err->message;
                g_error_free(err);
            }
            g_object_unref(conn);
            return res;
        }
        if (n == 0) {
            break;
        }
        if (raw.size() + static_cast<size_t>(n) > MAX_LINE_BYTES + 65536) {
            res.errorDetail = "Response from MoreXournal exceeded size limit";
            g_object_unref(conn);
            return res;
        }
        raw.append(buf, static_cast<size_t>(n));
    }
    g_object_unref(conn);

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        res.errorDetail = "Malformed HTTP response from MoreXournal";
        return res;
    }

    std::string_view headerBlock(raw.data(), headerEnd);
    size_t firstLineEnd = headerBlock.find("\r\n");
    std::string_view statusLine =
            firstLineEnd == std::string_view::npos ? headerBlock : headerBlock.substr(0, firstLineEnd);

    size_t sp1 = statusLine.find(' ');
    if (sp1 == std::string_view::npos) {
        res.errorDetail = "Malformed HTTP status line";
        return res;
    }
    size_t sp2 = statusLine.find(' ', sp1 + 1);
    std::string_view codeStr =
            sp2 == std::string_view::npos ? statusLine.substr(sp1 + 1) : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);
    int code = 0;
    auto [ptr, ec] = std::from_chars(codeStr.data(), codeStr.data() + codeStr.size(), code);
    if (ec != std::errc{} || ptr != codeStr.data() + codeStr.size()) {
        res.errorDetail = "Invalid HTTP status code";
        return res;
    }
    res.status = code;

    if (firstLineEnd != std::string_view::npos) {
        size_t pos = firstLineEnd + 2;
        while (pos < headerBlock.size()) {
            size_t next = headerBlock.find("\r\n", pos);
            std::string_view line =
                    next == std::string_view::npos ? headerBlock.substr(pos) : headerBlock.substr(pos, next - pos);
            pos = next == std::string_view::npos ? headerBlock.size() : next + 2;
            size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                res.headers[toLowerAscii(line.substr(0, colon))] = std::string(trimAscii(line.substr(colon + 1)));
            }
        }
    }

    res.body = raw.substr(headerEnd + 4);
    return res;
}

void writeJsonLine(const nlohmann::json& j) {
    std::cout << j.dump() << '\n' << std::flush;
}

void writeJsonRpcError(const nlohmann::json& id, int code, std::string_view message) {
    writeJsonLine({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [--port <port>] [--token <token>]\n"
              << "Bridges MCP stdio JSON-RPC to MoreXournal's local HTTP server on 127.0.0.1.\n"
              << "The token may also be supplied via the MOREXOURNAL_MCP_TOKEN environment variable.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = DEFAULT_PORT;
    std::string token;
    if (const char* envToken = std::getenv("MOREXOURNAL_MCP_TOKEN"); envToken != nullptr) {
        token = envToken;
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            std::string_view val(argv[++i]);
            unsigned int parsed = 0;
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), parsed);
            if (ec != std::errc{} || ptr != val.data() + val.size() || parsed < 1 || parsed > 65535) {
                std::cerr << "morexournal-mcp: invalid --port value\n";
                return 2;
            }
            port = static_cast<uint16_t>(parsed);
        } else if (arg == "--token" && i + 1 < argc) {
            token = argv[++i];
        } else {
            std::cerr << "morexournal-mcp: unknown or incomplete argument: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    if (token.empty()) {
        std::cerr << "morexournal-mcp: missing access token (pass --token or set MOREXOURNAL_MCP_TOKEN)\n";
        return 2;
    }

    std::string negotiatedProtocolVersion;
    std::string line;

    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (trimAscii(line).empty()) {
            continue;
        }
        if (line.size() > MAX_LINE_BYTES) {
            writeJsonRpcError(nullptr, -32600, "Message exceeds 8 MiB limit");
            continue;
        }

        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(line);
        } catch (const nlohmann::json::parse_error&) {
            writeJsonRpcError(nullptr, -32700, "Parse error");
            continue;
        }

        const bool isObject = msg.is_object();
        const bool hasId = isObject && msg.contains("id");
        const nlohmann::json id = hasId ? msg["id"] : nlohmann::json(nullptr);
        const std::string method = (isObject && msg.contains("method") && msg["method"].is_string()) ?
                                           msg["method"].get<std::string>() :
                                           std::string();
        std::string mcpName;
        std::string requestProto = negotiatedProtocolVersion;
        if (isObject && msg.contains("params") && msg["params"].is_object()) {
            const auto& params = msg["params"];
            if (method == "tools/call" && params.contains("name") && params["name"].is_string()) {
                mcpName = params["name"].get<std::string>();
            }
            if (params.contains("_meta") && params["_meta"].is_object()) {
                const auto& meta = params["_meta"];
                if (meta.contains("io.modelcontextprotocol/protocolVersion") &&
                    meta["io.modelcontextprotocol/protocolVersion"].is_string()) {
                    requestProto = meta["io.modelcontextprotocol/protocolVersion"].get<std::string>();
                }
            }
        }

        BridgeHttpResponse resp = postJsonToServer(port, token, line, requestProto, method, mcpName);
        if (!resp.connected) {
            std::cerr << "morexournal-mcp: unable to reach 127.0.0.1:" << port << "\n";
            if (hasId) {
                writeJsonRpcError(id, -32000, NOT_RUNNING_MSG);
            }
            continue;
        }

        if (resp.status == 202) {
            // Notification accepted: emit nothing on stdout
            continue;
        }

        if (resp.status == 401) {
            std::cerr << "morexournal-mcp: server rejected token (HTTP 401)\n";
            if (hasId) {
                writeJsonRpcError(id, -32001,
                                  "Authentication failed. Copy the current token from MoreXournal Preferences > AI "
                                  "assistants.");
            }
            continue;
        }

        if (resp.status == 0) {
            std::cerr << "morexournal-mcp: " << resp.errorDetail << "\n";
            if (hasId) {
                writeJsonRpcError(id, -32000, NOT_RUNNING_MSG);
            }
            continue;
        }

        // Parse response body so stdout always gets clean single-line JSON (no embedded newlines)
        try {
            nlohmann::json respJson = nlohmann::json::parse(resp.body);
            if (method == "initialize" && respJson.is_object() && respJson.contains("result") &&
                respJson["result"].is_object() && respJson["result"].contains("protocolVersion") &&
                respJson["result"]["protocolVersion"].is_string()) {
                negotiatedProtocolVersion = respJson["result"]["protocolVersion"].get<std::string>();
            }
            if (respJson.is_object() && respJson.contains("jsonrpc")) {
                writeJsonLine(respJson);
            } else if (hasId) {
                std::string msgText = "MoreXournal MCP server returned HTTP " + std::to_string(resp.status);
                if (respJson.is_object() && respJson.contains("error") && respJson["error"].is_string()) {
                    msgText = respJson["error"].get<std::string>();
                }
                writeJsonRpcError(id, -32000, msgText);
            }
        } catch (const nlohmann::json::parse_error&) {
            std::cerr << "morexournal-mcp: invalid JSON response (HTTP " << resp.status << ")\n";
            if (hasId) {
                writeJsonRpcError(id, -32000,
                                  "MoreXournal MCP server returned an unexpected response (HTTP " +
                                          std::to_string(resp.status) + ")");
            }
        }
    }

    return 0;
}
