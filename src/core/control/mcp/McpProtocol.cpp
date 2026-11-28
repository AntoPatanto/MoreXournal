#include "McpProtocol.h"

#include <algorithm>
#include <utility>

namespace xoj::mcp {

namespace {

auto makeJsonRpcError(const nlohmann::json& id, int code, std::string_view message,
                      const nlohmann::json* data = nullptr) -> nlohmann::json {
    nlohmann::json err = {{"code", code}, {"message", message}};
    if (data != nullptr) {
        err["data"] = *data;
    }
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(err)}};
}

auto makeJsonRpcResult(const nlohmann::json& id, nlohmann::json result) -> nlohmann::json {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

auto makeJsonHttpResponse(int status, const nlohmann::json& payload,
                          std::string_view protocolVersionHeader = {}) -> HttpResponse {
    HttpResponse resp;
    resp.status = status;
    resp.reason = status == 200 ? "OK" : "Bad Request";
    resp.contentType = "application/json";
    resp.body = payload.dump();
    if (!protocolVersionHeader.empty()) {
        resp.extraHeaders["MCP-Protocol-Version"] = std::string(protocolVersionHeader);
    }
    return resp;
}

auto readOnlyAnnotations() -> nlohmann::json {
    return {
            {"readOnlyHint", true},
            {"destructiveHint", false},
            {"idempotentHint", true},
            {"openWorldHint", false},
    };
}

auto writeAnnotations(bool idempotent = false) -> nlohmann::json {
    return {
            {"readOnlyHint", false},
            {"destructiveHint", false},
            {"idempotentHint", idempotent},
            {"openWorldHint", false},
    };
}

}  // namespace

auto ToolCallResult::text(std::string message, bool error) -> ToolCallResult {
    ToolCallResult res;
    res.isError = error;
    res.content = nlohmann::json::array({{{"type", "text"}, {"text", std::move(message)}}});
    return res;
}

auto ToolCallResult::jsonText(const nlohmann::json& data) -> ToolCallResult { return text(data.dump(2), false); }

auto ToolCallResult::pngImage(std::string base64Data) -> ToolCallResult {
    ToolCallResult res;
    res.isError = false;
    res.content = nlohmann::json::array({{{"type", "image"}, {"data", std::move(base64Data)}, {"mimeType", "image/png"}}});
    return res;
}

auto isSupportedProtocolVersion(std::string_view version) -> bool {
    return std::find(SUPPORTED_PROTOCOL_VERSIONS.begin(), SUPPORTED_PROTOCOL_VERSIONS.end(), version) !=
           SUPPORTED_PROTOCOL_VERSIONS.end();
}

auto buildToolListJson() -> nlohmann::json {
    return nlohmann::json::array({
            {
                    {"name", "get_document_info"},
                    {"description",
                     "Return metadata about the document currently open in MoreXournal: file path (or \"unsaved\"), "
                     "page count, 1-based current page, per-page width and height in page points (1/72 inch, origin "
                     "top-left), current layer, zoom level, active tool, color, size, and a summary of the current "
                     "selection."},
                    {"inputSchema", {{"type", "object"}, {"properties", nlohmann::json::object()}, {"additionalProperties", false}}},
                    {"annotations", readOnlyAnnotations()},
            },
            {
                    {"name", "get_page_image"},
                    {"description",
                     "Render a page of the open document (background and all visible layers) as a PNG image. Pages are "
                     "1-based and default to the current page. max_size_px sets the longest edge in pixels (default "
                     "1600, clamped to 256..4096)."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"properties",
                              {
                                      {"page",
                                       {{"type", "integer"},
                                        {"minimum", 1},
                                        {"description", "1-based page number (defaults to the current page)."}}},
                                      {"max_size_px",
                                       {{"type", "integer"},
                                        {"minimum", 256},
                                        {"maximum", 4096},
                                        {"default", 1600},
                                        {"description",
                                         "Maximum width or height of the rendered PNG in pixels (default 1600, "
                                         "clamped to 256..4096)."}}},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", readOnlyAnnotations()},
            },
            {
                    {"name", "get_page_content"},
                    {"description",
                     "Return the structured elements on a page of the open document grouped by layer: text boxes, "
                     "editable shapes (line, polygon, rectangle, ellipse), freehand strokes (count and bounding boxes, "
                     "plus points only when include_points is true), and image/LaTeX bounding boxes. Coordinates are "
                     "in page points (1/72 inch) with origin at the top-left of the page. Pages are 1-based."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"properties",
                              {
                                      {"page",
                                       {{"type", "integer"},
                                        {"minimum", 1},
                                        {"description", "1-based page number (defaults to the current page)."}}},
                                      {"include_points",
                                       {{"type", "boolean"},
                                        {"default", false},
                                        {"description",
                                         "When true, include polyline coordinates for plain strokes (capped for "
                                         "response size)."}}},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", readOnlyAnnotations()},
            },
            {
                    {"name", "add_text"},
                    {"description",
                     "Add a text box to the selected layer of a page (one undo step). Requires Allow edits to be "
                     "enabled in Preferences > AI assistants. Coordinates (x, y) are the top-left corner in page "
                     "points (1/72 inch, origin top-left). Pages are 1-based."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"required", nlohmann::json::array({"page", "x", "y", "text"})},
                             {"properties",
                              {
                                      {"page", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based page number."}}},
                                      {"x", {{"type", "number"}, {"description", "Left coordinate in page points (origin top-left)."}}},
                                      {"y", {{"type", "number"}, {"description", "Top coordinate in page points (origin top-left)."}}},
                                      {"text", {{"type", "string"}, {"minLength", 1}, {"description", "UTF-8 text content."}}},
                                      {"color",
                                       {{"type", "string"},
                                        {"pattern", "^#[0-9a-fA-F]{6}$"},
                                        {"description", "Optional text color as #rrggbb hex (defaults to text tool color)."}}},
                                      {"font_size",
                                       {{"type", "number"},
                                        {"minimum", 4.0},
                                        {"maximum", 200.0},
                                        {"description", "Optional font size in points (defaults to current font size)."}}},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", writeAnnotations()},
            },
            {
                    {"name", "add_strokes"},
                    {"description",
                     "Add one or more freehand pen strokes to the selected layer of a page as a single undo step. "
                     "Requires Allow edits to be enabled in Preferences > AI assistants. Coordinates are [x, y] pairs "
                     "in page points (1/72 inch, origin top-left), capped at 20000 total points. Pages are 1-based."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"required", nlohmann::json::array({"page", "strokes"})},
                             {"properties",
                              {
                                      {"page", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based page number."}}},
                                      {"strokes",
                                       {
                                               {"type", "array"},
                                               {"minItems", 1},
                                               {"maxItems", 500},
                                               {"items",
                                                {
                                                        {"type", "object"},
                                                        {"required", nlohmann::json::array({"points"})},
                                                        {"properties",
                                                         {
                                                                 {"points",
                                                                  {{"type", "array"},
                                                                   {"minItems", 2},
                                                                   {"items",
                                                                    {{"type", "array"},
                                                                     {"prefixItems",
                                                                      nlohmann::json::array({{{"type", "number"}}, {{"type", "number"}}})},
                                                                     {"minItems", 2},
                                                                     {"maxItems", 2}}},
                                                                   {"description",
                                                                    "Sequence of [x, y] points in page points (origin top-left)."}}},
                                                                 {"color",
                                                                  {{"type", "string"},
                                                                   {"pattern", "^#[0-9a-fA-F]{6}$"},
                                                                   {"description", "Optional stroke color as #rrggbb hex."}}},
                                                                 {"width",
                                                                  {{"type", "number"},
                                                                   {"minimum", 0.1},
                                                                   {"maximum", 100.0},
                                                                   {"description", "Optional stroke width in page points."}}},
                                                         }},
                                                        {"additionalProperties", false},
                                                }},
                                       }},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", writeAnnotations()},
            },
            {
                    {"name", "add_shape"},
                    {"description",
                     "Add an editable shape (line, rectangle, or ellipse) to the selected layer of a page as a single "
                     "undo step, matching objects created by MoreXournal's shape tools. Requires Allow edits to be "
                     "enabled in Preferences > AI assistants. Coordinates are in page points (1/72 inch, origin "
                     "top-left): (x1, y1) and (x2, y2) are endpoints for a line or opposite bounding-box corners for "
                     "a rectangle or ellipse. Pages are 1-based."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"required", nlohmann::json::array({"page", "type", "x1", "y1", "x2", "y2"})},
                             {"properties",
                              {
                                      {"page", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based page number."}}},
                                      {"type",
                                       {{"type", "string"},
                                        {"enum", nlohmann::json::array({"line", "rectangle", "ellipse"})},
                                        {"description", "Shape type."}}},
                                      {"x1", {{"type", "number"}, {"description", "First X coordinate in page points."}}},
                                      {"y1", {{"type", "number"}, {"description", "First Y coordinate in page points."}}},
                                      {"x2", {{"type", "number"}, {"description", "Second X coordinate in page points."}}},
                                      {"y2", {{"type", "number"}, {"description", "Second Y coordinate in page points."}}},
                                      {"stroke_color",
                                       {{"type", "string"},
                                        {"pattern", "^#[0-9a-fA-F]{6}$"},
                                        {"description", "Optional outline color as #rrggbb hex."}}},
                                      {"width",
                                       {{"type", "number"},
                                        {"minimum", 0.1},
                                        {"maximum", 100.0},
                                        {"description", "Optional stroke width in page points."}}},
                                      {"fill_color",
                                       {{"type", "string"},
                                        {"pattern", "^#[0-9a-fA-F]{6}$"},
                                        {"description",
                                         "Optional fill color as #rrggbb hex for rectangle or ellipse (sets fill "
                                         "opacity to 128 if fill_opacity is omitted)."}}},
                                      {"fill_opacity",
                                       {{"type", "integer"},
                                        {"minimum", 0},
                                        {"maximum", 255},
                                        {"description",
                                         "Optional fill opacity (0 = unfilled, 1..255 = alpha) for rectangle or ellipse."}}},
                                      {"line_style",
                                       {{"type", "string"},
                                        {"enum", nlohmann::json::array({"solid", "dash", "dashdot", "dot"})},
                                        {"default", "solid"},
                                        {"description", "Optional stroke dash pattern."}}},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", writeAnnotations()},
            },
            {
                    {"name", "add_page"},
                    {"description",
                     "Insert a new page using the current page template (one undo step). Requires Allow edits to be "
                     "enabled in Preferences > AI assistants. Pages are 1-based; omit after to insert after the "
                     "current page, or pass after=0 to insert before page 1."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"properties",
                              {
                                      {"after",
                                       {{"type", "integer"},
                                        {"minimum", 0},
                                        {"description",
                                         "1-based page number after which to insert the new page (0 inserts as page 1; "
                                         "defaults to current page)."}}},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", writeAnnotations()},
            },
            {
                    {"name", "go_to_page"},
                    {"description",
                     "Scroll the viewport to a 1-based page number. Requires Allow edits to be enabled in "
                     "Preferences > AI assistants."},
                    {"inputSchema",
                     {
                             {"type", "object"},
                             {"required", nlohmann::json::array({"page"})},
                             {"properties",
                              {
                                      {"page", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based page number."}}},
                              }},
                             {"additionalProperties", false},
                     }},
                    {"annotations", writeAnnotations(true)},
            },
    });
}

auto dispatchMcpRequest(const HttpRequest& req, const ToolCallHandler& toolHandler,
                        std::string_view serverVersion) -> McpDispatchOutcome {
    McpDispatchOutcome outcome;

    std::string responseProtoHeader;
    auto protoHeader = req.header("mcp-protocol-version");
    if (protoHeader.has_value()) {
        if (!isSupportedProtocolVersion(*protoHeader)) {
            nlohmann::json supported = nlohmann::json::array();
            for (auto v: SUPPORTED_PROTOCOL_VERSIONS) {
                supported.push_back(v);
            }
            nlohmann::json data = {{"error", "UnsupportedProtocolVersionError"}, {"supportedVersions", supported}};
            outcome.response = makeJsonHttpResponse(
                    400, makeJsonRpcError(nullptr, -32600, "Unsupported MCP-Protocol-Version", &data));
            return outcome;
        }
        responseProtoHeader = std::string(*protoHeader);
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error&) {
        outcome.response = makeJsonHttpResponse(400, makeJsonRpcError(nullptr, -32700, "Parse error"));
        return outcome;
    }

    if (payload.is_array()) {
        outcome.response = makeJsonHttpResponse(
                400, makeJsonRpcError(nullptr, -32600, "JSON-RPC batch requests are not supported"));
        return outcome;
    }

    if (!payload.is_object()) {
        outcome.response = makeJsonHttpResponse(400, makeJsonRpcError(nullptr, -32600, "Invalid Request"));
        return outcome;
    }

    const bool hasId = payload.contains("id");
    const nlohmann::json id = hasId ? payload["id"] : nlohmann::json(nullptr);
    if (hasId && !(id.is_string() || id.is_number_integer() || id.is_number_unsigned() || id.is_null())) {
        outcome.response = makeJsonHttpResponse(400, makeJsonRpcError(nullptr, -32600, "Invalid Request id"));
        return outcome;
    }

    if (!payload.contains("jsonrpc") || !payload["jsonrpc"].is_string() || payload["jsonrpc"] != "2.0" ||
        !payload.contains("method") || !payload["method"].is_string()) {
        outcome.response = makeJsonHttpResponse(400, makeJsonRpcError(id, -32600, "Invalid Request"));
        return outcome;
    }

    const std::string method = payload["method"].get<std::string>();
    const nlohmann::json params = payload.contains("params") ? payload["params"] : nlohmann::json::object();

    // 2026-07-28 header/body consistency checks
    if (auto mcpMethod = req.header("mcp-method"); mcpMethod.has_value() && *mcpMethod != method) {
        nlohmann::json data = {{"error", "HeaderMismatchError"}, {"header", "Mcp-Method"}};
        outcome.response = makeJsonHttpResponse(
                400, makeJsonRpcError(id, -32600, "Mcp-Method header does not match JSON-RPC method", &data));
        return outcome;
    }

    if (protoHeader.has_value() && params.is_object() && params.contains("_meta") && params["_meta"].is_object()) {
        const auto& meta = params["_meta"];
        if (meta.contains("io.modelcontextprotocol/protocolVersion") &&
            meta["io.modelcontextprotocol/protocolVersion"].is_string() &&
            meta["io.modelcontextprotocol/protocolVersion"].get<std::string>() != *protoHeader) {
            nlohmann::json data = {{"error", "HeaderMismatchError"}, {"header", "MCP-Protocol-Version"}};
            outcome.response = makeJsonHttpResponse(
                    400,
                    makeJsonRpcError(id, -32600,
                                     "MCP-Protocol-Version header does not match params._meta protocolVersion", &data));
            return outcome;
        }
    }

    // Notifications return 202 Accepted with an empty body
    if (!hasId || method.rfind("notifications/", 0) == 0) {
        HttpResponse resp;
        resp.status = 202;
        resp.reason = "Accepted";
        if (!responseProtoHeader.empty()) {
            resp.extraHeaders["MCP-Protocol-Version"] = responseProtoHeader;
        }
        outcome.response = std::move(resp);
        return outcome;
    }

    if (method == "initialize") {
        std::string negotiated = std::string(DEFAULT_INITIALIZE_PROTOCOL_VERSION);
        if (params.is_object()) {
            if (params.contains("protocolVersion") && params["protocolVersion"].is_string()) {
                std::string requested = params["protocolVersion"].get<std::string>();
                if (isSupportedProtocolVersion(requested)) {
                    negotiated = std::move(requested);
                }
            }
            if (params.contains("clientInfo") && params["clientInfo"].is_object()) {
                const auto& ci = params["clientInfo"];
                if (ci.contains("name") && ci["name"].is_string()) {
                    std::string name = ci["name"].get<std::string>();
                    if (!name.empty()) {
                        outcome.clientName = std::move(name);
                    }
                }
            }
        }

        nlohmann::json result = {
                {"protocolVersion", negotiated},
                {"capabilities", {{"tools", {{"listChanged", false}}}}},
                {"serverInfo", {{"name", "morexournal"}, {"version", serverVersion}}},
        };
        outcome.response = makeJsonHttpResponse(200, makeJsonRpcResult(id, std::move(result)), negotiated);
        return outcome;
    }

    if (method == "server/discover") {
        nlohmann::json versions = nlohmann::json::array();
        for (auto it = SUPPORTED_PROTOCOL_VERSIONS.rbegin(); it != SUPPORTED_PROTOCOL_VERSIONS.rend(); ++it) {
            versions.push_back(*it);
        }
        nlohmann::json result = {
                {"protocolVersions", std::move(versions)},
                {"capabilities", {{"tools", {{"listChanged", false}}}}},
                {"serverInfo", {{"name", "morexournal"}, {"version", serverVersion}}},
        };
        outcome.response = makeJsonHttpResponse(200, makeJsonRpcResult(id, std::move(result)), responseProtoHeader);
        return outcome;
    }

    if (method == "ping") {
        outcome.response =
                makeJsonHttpResponse(200, makeJsonRpcResult(id, nlohmann::json::object()), responseProtoHeader);
        return outcome;
    }

    if (method == "tools/list") {
        nlohmann::json result = {{"tools", buildToolListJson()}};
        outcome.response = makeJsonHttpResponse(200, makeJsonRpcResult(id, std::move(result)), responseProtoHeader);
        return outcome;
    }

    if (method == "tools/call") {
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
            ToolCallResult err = ToolCallResult::text("Missing or invalid tool name", true);
            nlohmann::json result = {{"content", err.content}, {"isError", true}};
            outcome.response = makeJsonHttpResponse(200, makeJsonRpcResult(id, std::move(result)), responseProtoHeader);
            return outcome;
        }

        const std::string toolName = params["name"].get<std::string>();
        if (auto mcpName = req.header("mcp-name"); mcpName.has_value() && *mcpName != toolName) {
            nlohmann::json data = {{"error", "HeaderMismatchError"}, {"header", "Mcp-Name"}};
            outcome.response = makeJsonHttpResponse(
                    400, makeJsonRpcError(id, -32600, "Mcp-Name header does not match tool name", &data));
            return outcome;
        }

        nlohmann::json arguments = nlohmann::json::object();
        if (params.contains("arguments") && !params["arguments"].is_null()) {
            if (!params["arguments"].is_object()) {
                ToolCallResult err = ToolCallResult::text("Tool arguments must be a JSON object", true);
                nlohmann::json result = {{"content", err.content}, {"isError", true}};
                outcome.response =
                        makeJsonHttpResponse(200, makeJsonRpcResult(id, std::move(result)), responseProtoHeader);
                return outcome;
            }
            arguments = params["arguments"];
        }

        ToolCallResult callRes = toolHandler ? toolHandler(toolName, arguments) :
                                               ToolCallResult::text("Tool handler is unavailable", true);
        nlohmann::json result = {{"content", std::move(callRes.content)}};
        if (callRes.isError) {
            result["isError"] = true;
        }
        outcome.response = makeJsonHttpResponse(200, makeJsonRpcResult(id, std::move(result)), responseProtoHeader);
        return outcome;
    }

    outcome.response =
            makeJsonHttpResponse(200, makeJsonRpcError(id, -32601, "Method not found: " + method), responseProtoHeader);
    return outcome;
}

}  // namespace xoj::mcp
