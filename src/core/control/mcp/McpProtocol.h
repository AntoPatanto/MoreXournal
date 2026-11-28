/*
 * MoreXournal
 *
 * MCP JSON-RPC 2.0 protocol dispatcher supporting both 2025-11-25 and 2026-07-28 specifications
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "McpHttp.h"

namespace xoj::mcp {

constexpr std::array<std::string_view, 3> SUPPORTED_PROTOCOL_VERSIONS = {"2025-06-18", "2025-11-25", "2026-07-28"};
constexpr std::string_view DEFAULT_INITIALIZE_PROTOCOL_VERSION = "2025-11-25";

struct ToolCallResult {
    bool isError = false;
    nlohmann::json content = nlohmann::json::array();

    static auto text(std::string message, bool error = false) -> ToolCallResult;
    static auto jsonText(const nlohmann::json& data) -> ToolCallResult;
    static auto pngImage(std::string base64Data) -> ToolCallResult;
};

using ToolCallHandler = std::function<ToolCallResult(const std::string& name, const nlohmann::json& arguments)>;

struct McpDispatchOutcome {
    HttpResponse response;
    /// Populated when initialize provides clientInfo.name
    std::optional<std::string> clientName;
};

/// Returns true if the protocol version string is in SUPPORTED_PROTOCOL_VERSIONS.
auto isSupportedProtocolVersion(std::string_view version) -> bool;

/// Returns the JSON array of tool definitions for tools/list.
auto buildToolListJson() -> nlohmann::json;

/// Dispatch a validated POST /mcp HTTP request through JSON-RPC 2.0 / MCP handling.
auto dispatchMcpRequest(const HttpRequest& req, const ToolCallHandler& toolHandler, std::string_view serverVersion)
        -> McpDispatchOutcome;

}  // namespace xoj::mcp
