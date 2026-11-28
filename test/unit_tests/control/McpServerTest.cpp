/*
 * MoreXournal
 *
 * Unit tests for the built-in MCP server (token, HTTP, security, JSON-RPC, tool validation, loopback socket)
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include <gio/gio.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "control/mcp/McpHttp.h"
#include "control/mcp/McpProtocol.h"
#include "control/mcp/McpServer.h"
#include "control/mcp/McpToken.h"
#include "control/mcp/McpTools.h"
#include "control/settings/Settings.h"
#include "filesystem.h"

namespace {

using namespace xoj::mcp;

auto makeRawPost(const std::string& path, const std::string& body, const std::string& extraHeaders = "",
                 const std::string& host = "127.0.0.1:47615") -> std::string {
    std::ostringstream os;
    os << "POST " << path << " HTTP/1.1\r\n"
       << "Host: " << host << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << extraHeaders << "\r\n"
       << body;
    return os.str();
}

auto sendRawHttp(uint16_t port, const std::string& rawRequest) -> std::string {
    auto fut = std::async(std::launch::async, [port, rawRequest]() -> std::string {
        GSocketClient* client = g_socket_client_new();
        g_socket_client_set_timeout(client, 5);
        GError* err = nullptr;
        GSocketConnection* conn = g_socket_client_connect_to_host(client, "127.0.0.1", port, nullptr, &err);
        g_object_unref(client);
        if (!conn) {
            if (err) {
                g_error_free(err);
            }
            g_main_context_wakeup(nullptr);
            return {};
        }

        GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
        gsize written = 0;
        g_output_stream_write_all(out, rawRequest.data(), rawRequest.size(), &written, nullptr, nullptr);
        g_output_stream_flush(out, nullptr, nullptr);

        GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
        std::string response;
        char buf[4096];
        for (;;) {
            gssize n = g_input_stream_read(in, buf, sizeof(buf), nullptr, nullptr);
            if (n <= 0) {
                break;
            }
            response.append(buf, static_cast<size_t>(n));
        }
        g_object_unref(conn);
        g_main_context_wakeup(nullptr);
        return response;
    });

    while (fut.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
    }
    while (g_main_context_iteration(nullptr, FALSE)) {}
    return fut.get();
}

auto extractHttpStatus(const std::string& rawResponse) -> int {
    size_t sp1 = rawResponse.find(' ');
    if (sp1 == std::string::npos || sp1 + 4 > rawResponse.size()) {
        return 0;
    }
    return std::stoi(rawResponse.substr(sp1 + 1, 3));
}

auto extractHttpBody(const std::string& rawResponse) -> std::string {
    size_t pos = rawResponse.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return {};
    }
    return rawResponse.substr(pos + 4);
}

auto fixedPageSize(double w = 595.0, double h = 842.0) -> std::function<std::pair<double, double>(size_t)> {
    return [w, h](size_t) { return std::make_pair(w, h); };
}

}  // namespace

TEST(McpTokenTest, formatAndUniqueness) {
    std::string t1 = generateToken();
    std::string t2 = generateToken();

    EXPECT_TRUE(isValidTokenFormat(t1));
    EXPECT_TRUE(isValidTokenFormat(t2));
    EXPECT_EQ(t1.size(), TOKEN_LENGTH);
    EXPECT_EQ(t1.substr(0, 3), "mx_");
    EXPECT_NE(t1, t2);
}

TEST(McpTokenTest, constantTimeEquals) {
    EXPECT_TRUE(constantTimeEquals("mx_abcdef", "mx_abcdef"));
    EXPECT_FALSE(constantTimeEquals("mx_abcdef", "mx_abcdeg"));
    EXPECT_FALSE(constantTimeEquals("mx_abcdef", "mx_abcde"));
    EXPECT_FALSE(constantTimeEquals("", "a"));
    EXPECT_TRUE(constantTimeEquals("", ""));
}

TEST(McpHttpTest, parseValidPostHeaders) {
    std::string raw = makeRawPost("/mcp", "{\"jsonrpc\":\"2.0\"}", "Authorization: Bearer mx_test\r\n");
    auto res = parseHttpHeaders(raw);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.request.method, "POST");
    EXPECT_EQ(res.request.path, "/mcp");
    EXPECT_EQ(res.contentLength, 17U);
    EXPECT_EQ(res.request.header("authorization"), "Bearer mx_test");
}

TEST(McpHttpTest, rejectMalformedRequestLine) {
    auto res1 = parseHttpHeaders("INVALID\r\nHost: 127.0.0.1\r\n\r\n");
    EXPECT_FALSE(res1.ok);
    EXPECT_EQ(res1.errorStatus, 400);

    auto res2 = parseHttpHeaders("POST /mcp HTTP/2.0\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n");
    EXPECT_FALSE(res2.ok);
    EXPECT_EQ(res2.errorStatus, 400);

    auto res3 = parseHttpHeaders("POST relative HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n");
    EXPECT_FALSE(res3.ok);
    EXPECT_EQ(res3.errorStatus, 400);
}

TEST(McpHttpTest, rejectOversizeHeadersAndBody) {
    std::string hugeHeader = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Pad: " +
                             std::string(MAX_HTTP_HEADER_BYTES + 100, 'a') + "\r\nContent-Length: 0\r\n\r\n";
    auto resHdr = parseHttpHeaders(hugeHeader);
    EXPECT_FALSE(resHdr.ok);
    EXPECT_EQ(resHdr.errorStatus, 431);

    std::string hugeBody = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " +
                           std::to_string(MAX_HTTP_BODY_BYTES + 1) + "\r\n\r\n";
    auto resBody = parseHttpHeaders(hugeBody);
    EXPECT_FALSE(resBody.ok);
    EXPECT_EQ(resBody.errorStatus, 413);
}

TEST(McpHttpTest, rejectMissingContentLengthAndChunked) {
    auto missingCl = parseHttpHeaders("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    EXPECT_FALSE(missingCl.ok);
    EXPECT_EQ(missingCl.errorStatus, 411);

    auto chunked = parseHttpHeaders(
            "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    EXPECT_FALSE(chunked.ok);
    EXPECT_TRUE(chunked.errorStatus == 400 || chunked.errorStatus == 411);
}

TEST(McpHttpTest, authOriginHostAndMethodChecks) {
    const std::string token = "mx_0123456789abcdef";

    auto makeReq = [](const std::string& method, const std::string& path, const std::string& host,
                      const std::string& origin, const std::string& auth) {
        HttpRequest req;
        req.method = method;
        req.path = path;
        req.target = path;
        req.httpVersion = "HTTP/1.1";
        if (!host.empty()) {
            req.headers["host"] = host;
        }
        if (!origin.empty()) {
            req.headers["origin"] = origin;
        }
        if (!auth.empty()) {
            req.headers["authorization"] = auth;
        }
        return req;
    };

    // Valid request passes
    EXPECT_FALSE(validateHttpRequest(makeReq("POST", "/mcp", "127.0.0.1:47615", "", "Bearer " + token), token)
                         .has_value());
    EXPECT_FALSE(validateHttpRequest(makeReq("POST", "/mcp", "localhost:47615", "http://localhost:3000",
                                             "Bearer " + token),
                                     token)
                         .has_value());
    EXPECT_FALSE(
            validateHttpRequest(makeReq("POST", "/mcp", "[::1]:47615", "http://127.0.0.1", "Bearer " + token), token)
                    .has_value());

    // Missing or wrong auth -> 401
    auto noAuth = validateHttpRequest(makeReq("POST", "/mcp", "127.0.0.1:47615", "", ""), token);
    ASSERT_TRUE(noAuth.has_value());
    EXPECT_EQ(noAuth->status, 401);

    auto wrongAuth = validateHttpRequest(makeReq("POST", "/mcp", "127.0.0.1:47615", "", "Bearer mx_wrong"), token);
    ASSERT_TRUE(wrongAuth.has_value());
    EXPECT_EQ(wrongAuth->status, 401);

    auto basicAuth = validateHttpRequest(makeReq("POST", "/mcp", "127.0.0.1:47615", "", "Basic dXNlcjpwYXNz"), token);
    ASSERT_TRUE(basicAuth.has_value());
    EXPECT_EQ(basicAuth->status, 401);

    // Bad Origin -> 403
    for (const char* badOrigin: {"https://evil.example", "http://evil.example", "http://localhost.evil.example",
                                 "http://127.0.0.1@evil.example", "null", "http://127.0.0.1/mcp"}) {
        auto r = validateHttpRequest(makeReq("POST", "/mcp", "127.0.0.1:47615", badOrigin, "Bearer " + token), token);
        ASSERT_TRUE(r.has_value()) << "Expected 403 for Origin: " << badOrigin;
        EXPECT_EQ(r->status, 403) << "Origin: " << badOrigin;
    }

    // Bad Host -> 403
    for (const char* badHost: {"evil.example", "127.0.0.1.evil.example", "127.0.0.1:99999", "0.0.0.0:47615"}) {
        auto r = validateHttpRequest(makeReq("POST", "/mcp", badHost, "", "Bearer " + token), token);
        ASSERT_TRUE(r.has_value()) << "Expected 403 for Host: " << badHost;
        EXPECT_EQ(r->status, 403) << "Host: " << badHost;
    }

    // GET on /mcp -> 405
    auto getResp = validateHttpRequest(makeReq("GET", "/mcp", "127.0.0.1:47615", "", "Bearer " + token), token);
    ASSERT_TRUE(getResp.has_value());
    EXPECT_EQ(getResp->status, 405);

    // Wrong path -> 404
    auto wrongPath = validateHttpRequest(makeReq("POST", "/other", "127.0.0.1:47615", "", "Bearer " + token), token);
    ASSERT_TRUE(wrongPath.has_value());
    EXPECT_EQ(wrongPath->status, 404);
}

TEST(McpProtocolTest, initializeNegotiationAndStatelessRequests) {
    ToolCallHandler dummy = [](const std::string& name, const nlohmann::json&) {
        return ToolCallResult::text("called:" + name, false);
    };

    for (const char* ver: {"2025-06-18", "2025-11-25", "2026-07-28"}) {
        HttpRequest req;
        req.body = nlohmann::json{{"jsonrpc", "2.0"},
                                  {"id", 1},
                                  {"method", "initialize"},
                                  {"params", {{"protocolVersion", ver}, {"clientInfo", {{"name", "TestClient"}}}}}}
                           .dump();
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 200);
        EXPECT_EQ(out.clientName, "TestClient");
        auto body = nlohmann::json::parse(out.response.body);
        EXPECT_EQ(body["result"]["protocolVersion"], ver);
        EXPECT_EQ(body["result"]["serverInfo"]["name"], "morexournal");
    }

    // No-initialize stateless call works directly (2026-07-28 style)
    HttpRequest stateless;
    stateless.headers["mcp-protocol-version"] = "2026-07-28";
    stateless.headers["mcp-method"] = "tools/list";
    stateless.body = nlohmann::json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}}.dump();
    auto listOut = dispatchMcpRequest(stateless, dummy, "0.1.2");
    EXPECT_EQ(listOut.response.status, 200);
    auto listBody = nlohmann::json::parse(listOut.response.body);
    ASSERT_TRUE(listBody["result"]["tools"].is_array());
    EXPECT_EQ(listBody["result"]["tools"].size(), 8U);

    // server/discover works
    HttpRequest discover;
    discover.body = nlohmann::json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "server/discover"}}.dump();
    auto discOut = dispatchMcpRequest(discover, dummy, "0.1.2");
    EXPECT_EQ(discOut.response.status, 200);
    auto discBody = nlohmann::json::parse(discOut.response.body);
    EXPECT_TRUE(discBody["result"]["protocolVersions"].is_array());
}

TEST(McpProtocolTest, jsonRpcErrorCodesBatchAndNotifications) {
    ToolCallHandler dummy = [](const std::string&, const nlohmann::json&) {
        return ToolCallResult::text("tool failed", true);
    };

    // Unknown method -> -32601
    {
        HttpRequest req;
        req.body = nlohmann::json{{"jsonrpc", "2.0"}, {"id", 10}, {"method", "no/such/method"}}.dump();
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 200);
        auto j = nlohmann::json::parse(out.response.body);
        EXPECT_EQ(j["error"]["code"], -32601);
        EXPECT_EQ(j["id"], 10);
    }

    // Bad JSON -> -32700
    {
        HttpRequest req;
        req.body = "{not valid json";
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 400);
        auto j = nlohmann::json::parse(out.response.body);
        EXPECT_EQ(j["error"]["code"], -32700);
    }

    // Batch array rejected -> -32600
    {
        HttpRequest req;
        req.body = "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]";
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 400);
        auto j = nlohmann::json::parse(out.response.body);
        EXPECT_EQ(j["error"]["code"], -32600);
    }

    // Notification -> 202 empty body
    {
        HttpRequest req;
        req.body = nlohmann::json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}.dump();
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 202);
        EXPECT_TRUE(out.response.body.empty());
    }

    // Mismatched Mcp-Method header -> 400
    {
        HttpRequest req;
        req.headers["mcp-method"] = "tools/call";
        req.body = nlohmann::json{{"jsonrpc", "2.0"}, {"id", 11}, {"method", "ping"}}.dump();
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 400);
    }

    // Tool failure returns isError: true inside result (not protocol error)
    {
        HttpRequest req;
        req.body = nlohmann::json{{"jsonrpc", "2.0"},
                                  {"id", 12},
                                  {"method", "tools/call"},
                                  {"params", {{"name", "add_text"}, {"arguments", nlohmann::json::object()}}}}
                           .dump();
        auto out = dispatchMcpRequest(req, dummy, "0.1.2");
        EXPECT_EQ(out.response.status, 200);
        auto j = nlohmann::json::parse(out.response.body);
        EXPECT_FALSE(j.contains("error"));
        EXPECT_TRUE(j["result"]["isError"].get<bool>());
    }
}

TEST(McpToolsValidationTest, validatesInputsAndRejectsBadValues) {
    // Hex colors
    EXPECT_TRUE(parseHexColor("#3563e9").has_value());
    EXPECT_TRUE(parseHexColor("#ABCDEF").has_value());
    EXPECT_FALSE(parseHexColor("3563e9").has_value());
    EXPECT_FALSE(parseHexColor("#123").has_value());
    EXPECT_FALSE(parseHexColor("#12345g").has_value());
    EXPECT_FALSE(parseHexColor("#1234567").has_value());

    // get_page_image clamping and bad page
    {
        auto ok = validatePageImageArgs({{"page", 1}, {"max_size_px", 99999}}, 3, 0);
        ASSERT_TRUE(std::holds_alternative<PageImageArgs>(ok));
        EXPECT_EQ(std::get<PageImageArgs>(ok).maxSizePx, MAX_PAGE_IMAGE_MAX_PX);

        auto low = validatePageImageArgs({{"max_size_px", 10}}, 3, 1);
        ASSERT_TRUE(std::holds_alternative<PageImageArgs>(low));
        EXPECT_EQ(std::get<PageImageArgs>(low).maxSizePx, MIN_PAGE_IMAGE_MAX_PX);
        EXPECT_EQ(std::get<PageImageArgs>(low).pageIndex, 1U);

        auto badPage = validatePageImageArgs({{"page", 4}}, 3, 0);
        EXPECT_TRUE(std::holds_alternative<std::string>(badPage));

        auto zeroPage = validatePageImageArgs({{"page", 0}}, 3, 0);
        EXPECT_TRUE(std::holds_alternative<std::string>(zeroPage));
    }

    // add_text validation: NaN, bad color, out of bounds, empty text
    {
        auto nanJson = nlohmann::json{{"page", 1}, {"x", std::numeric_limits<double>::quiet_NaN()}, {"y", 10}, {"text", "hi"}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddTextArgs(nanJson, 2, fixedPageSize())));

        auto badColor = nlohmann::json{{"page", 1}, {"x", 10}, {"y", 10}, {"text", "hi"}, {"color", "red"}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddTextArgs(badColor, 2, fixedPageSize())));

        auto outOfBounds = nlohmann::json{{"page", 1}, {"x", 9999}, {"y", 10}, {"text", "hi"}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddTextArgs(outOfBounds, 2, fixedPageSize())));

        auto emptyText = nlohmann::json{{"page", 1}, {"x", 10}, {"y", 10}, {"text", ""}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddTextArgs(emptyText, 2, fixedPageSize())));

        auto valid = nlohmann::json{{"page", 2}, {"x", 50}, {"y", 60}, {"text", "Hello"}, {"color", "#112233"}, {"font_size", 14}};
        auto res = validateAddTextArgs(valid, 2, fixedPageSize());
        ASSERT_TRUE(std::holds_alternative<AddTextArgs>(res));
        EXPECT_EQ(std::get<AddTextArgs>(res).pageIndex, 1U);
    }

    // add_strokes validation: too many points and <2 points
    {
        nlohmann::json tooShort = {{"page", 1}, {"strokes", {{{"points", {{10, 10}}}}}}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddStrokesArgs(tooShort, 1, fixedPageSize())));

        nlohmann::json hugePts = nlohmann::json::array();
        for (size_t i = 0; i <= MAX_STROKE_POINTS_PER_CALL; ++i) {
            hugePts.push_back(nlohmann::json::array({10.0, 20.0}));
        }
        nlohmann::json tooMany = {{"page", 1}, {"strokes", {{{"points", std::move(hugePts)}}}}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddStrokesArgs(tooMany, 1, fixedPageSize())));
    }

    // add_shape validation: degenerate geometry, bad type, bad line_style
    {
        nlohmann::json flatRect = {{"page", 1}, {"type", "rectangle"}, {"x1", 50}, {"y1", 50}, {"x2", 50}, {"y2", 100}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddShapeArgs(flatRect, 1, fixedPageSize())));

        nlohmann::json badType = {{"page", 1}, {"type", "triangle"}, {"x1", 10}, {"y1", 10}, {"x2", 50}, {"y2", 50}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddShapeArgs(badType, 1, fixedPageSize())));

        nlohmann::json badStyle = {{"page", 1}, {"type", "line"}, {"x1", 10}, {"y1", 10}, {"x2", 50}, {"y2", 50}, {"line_style", "wavy"}};
        EXPECT_TRUE(std::holds_alternative<std::string>(validateAddShapeArgs(badStyle, 1, fixedPageSize())));

        nlohmann::json goodEllipse = {{"page", 1},          {"type", "ellipse"},     {"x1", 20},
                                      {"y1", 30},           {"x2", 120},             {"y2", 90},
                                      {"stroke_color", "#3563e9"}, {"fill_color", "#e3eaff"},
                                      {"fill_opacity", 100}, {"line_style", "dash"}};
        auto res = validateAddShapeArgs(goodEllipse, 1, fixedPageSize());
        ASSERT_TRUE(std::holds_alternative<AddShapeArgs>(res));
        EXPECT_EQ(std::get<AddShapeArgs>(res).shapeInfo.type, ShapeType::ELLIPSE);
    }

    // Write tool gating check
    EXPECT_TRUE(isWriteTool("add_text"));
    EXPECT_TRUE(isWriteTool("go_to_page"));
    EXPECT_FALSE(isWriteTool("get_document_info"));
}

TEST(McpServerLiveTest, loopbackSocketRequestAuthAndTokenRotation) {
    const std::string token1 = generateToken();
    const std::string token2 = generateToken();

    McpServer server(
            [](const std::string& name, const nlohmann::json& args) -> ToolCallResult {
                return ToolCallResult::jsonText({{"echo_tool", name}, {"args", args}});
            },
            "0.1.2-test");

    ASSERT_TRUE(server.start(0, token1, false)) << server.getLastError();
    const uint16_t port = server.getBoundPort();
    ASSERT_GT(port, 0U);
    const std::string host = "127.0.0.1:" + std::to_string(port);

    // 1. Unauthenticated request -> 401
    {
        std::string pingBody = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
        std::string resp = sendRawHttp(port, makeRawPost("/mcp", pingBody, "", host));
        EXPECT_EQ(extractHttpStatus(resp), 401);
    }

    // 2. Bad Origin -> 403
    {
        std::string pingBody = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
        std::string resp = sendRawHttp(
                port,
                makeRawPost("/mcp", pingBody,
                            "Authorization: Bearer " + token1 + "\r\nOrigin: https://attacker.example\r\n", host));
        EXPECT_EQ(extractHttpStatus(resp), 403);
    }

    // 3. Authenticated initialize + tools/call
    {
        std::string initBody =
                R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","clientInfo":{"name":"UnitTestClient"}}})";
        std::string resp = sendRawHttp(
                port,
                makeRawPost("/mcp", initBody, "Authorization: Bearer " + token1 + "\r\nUser-Agent: unit-test/1.0\r\n",
                            host));
        EXPECT_EQ(extractHttpStatus(resp), 200);
        auto j = nlohmann::json::parse(extractHttpBody(resp));
        EXPECT_EQ(j["result"]["protocolVersion"], "2025-11-25");

        auto clients = server.getRecentClients();
        ASSERT_EQ(clients.size(), 1U);
        EXPECT_EQ(clients[0].name, "UnitTestClient");
    }

    {
        std::string callBody =
                R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_document_info","arguments":{}}})";
        std::string resp = sendRawHttp(
                port, makeRawPost("/mcp", callBody, "Authorization: Bearer " + token1 + "\r\n", host));
        EXPECT_EQ(extractHttpStatus(resp), 200);
        auto j = nlohmann::json::parse(extractHttpBody(resp));
        EXPECT_EQ(j["id"], 2);
        ASSERT_TRUE(j["result"]["content"].is_array());
        auto textJson = nlohmann::json::parse(j["result"]["content"][0]["text"].get<std::string>());
        EXPECT_EQ(textJson["echo_tool"], "get_document_info");
    }

    // 4. Rotate token -> old token rejected immediately, new token accepted
    server.updateRuntimeConfig(token2, true);
    {
        std::string pingBody = R"({"jsonrpc":"2.0","id":3,"method":"ping"})";
        std::string oldResp = sendRawHttp(
                port, makeRawPost("/mcp", pingBody, "Authorization: Bearer " + token1 + "\r\n", host));
        EXPECT_EQ(extractHttpStatus(oldResp), 401);

        std::string newResp = sendRawHttp(
                port, makeRawPost("/mcp", pingBody, "Authorization: Bearer " + token2 + "\r\n", host));
        EXPECT_EQ(extractHttpStatus(newResp), 200);
    }

    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST(McpSettingsTest, saveAndLoadRoundTrip) {
    fs::path tmpDir = fs::temp_directory_path() / ("morexournal-mcp-settings-test-" + generateToken().substr(3, 12));
    fs::create_directories(tmpDir);
    fs::path settingsFile = tmpDir / "settings.xml";

    std::string generatedToken;
    {
        Settings s(settingsFile);
        EXPECT_FALSE(s.isMcpEnabled());
        EXPECT_FALSE(s.isMcpAllowEdits());
        EXPECT_EQ(s.getMcpPort(), Settings::DEFAULT_MCP_PORT);
        EXPECT_TRUE(s.getMcpToken().empty());

        s.setMcpEnabled(true);
        generatedToken = s.getMcpToken();
        EXPECT_TRUE(isValidTokenFormat(generatedToken));
        s.setMcpAllowEdits(true);
        s.setMcpPort(47777);
    }

    {
        Settings loaded(settingsFile);
        ASSERT_TRUE(loaded.load());
        EXPECT_TRUE(loaded.isMcpEnabled());
        EXPECT_TRUE(loaded.isMcpAllowEdits());
        EXPECT_EQ(loaded.getMcpPort(), 47777);
        EXPECT_EQ(loaded.getMcpToken(), generatedToken);
    }

    std::error_code ec;
    fs::remove_all(tmpDir, ec);
}
