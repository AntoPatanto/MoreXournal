/*
 * MoreXournal
 *
 * Minimal HTTP/1.1 request parser, security checks and response serializer for the MCP server
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace xoj::mcp {

constexpr size_t MAX_HTTP_HEADER_BYTES = 16 * 1024;
constexpr size_t MAX_HTTP_BODY_BYTES = 8 * 1024 * 1024;
constexpr unsigned int HTTP_READ_TIMEOUT_SECONDS = 30;
constexpr int MAX_CONCURRENT_CONNECTIONS = 8;

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string httpVersion;
    /// Lowercase header name -> value
    std::map<std::string, std::string> headers;
    std::string body;

    auto header(std::string_view lowerName) const -> std::optional<std::string_view>;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string contentType;
    std::string body;
    std::map<std::string, std::string> extraHeaders;
};

struct HttpHeaderParseResult {
    bool ok = false;
    int errorStatus = 400;
    std::string errorMessage;
    HttpRequest request;
    size_t contentLength = 0;
    size_t headerEndOffset = 0;
};

/// Convert an ASCII string to lowercase.
auto toLowerAscii(std::string_view s) -> std::string;

/// Parse the HTTP/1.1 request line and headers from raw bytes up to "\r\n\r\n".
/// Enforces MAX_HTTP_HEADER_BYTES, MAX_HTTP_BODY_BYTES, Content-Length on POST, and rejects chunked transfer encoding.
auto parseHttpHeaders(std::string_view rawHeaders) -> HttpHeaderParseResult;

/// Validate the Host header value (must be 127.0.0.1, localhost, or [::1], with optional valid port).
auto isAllowedHost(std::string_view host) -> bool;

/// Validate the Origin header value (must be http://127.0.0.1[:port], http://localhost[:port], or http://[::1][:port]).
auto isAllowedOrigin(std::string_view origin) -> bool;

/// Validate the Authorization header ("Bearer <token>") using constant-time comparison.
auto isAuthorizedBearer(std::string_view authHeader, std::string_view expectedToken) -> bool;

/// Run Host, Origin, Authorization, path, and method checks on a parsed request.
/// Returns std::nullopt if the request is allowed to proceed to JSON-RPC handling, or an error HttpResponse otherwise.
auto validateHttpRequest(const HttpRequest& req, std::string_view expectedToken) -> std::optional<HttpResponse>;

/// Serialize an HttpResponse into HTTP/1.1 wire bytes.
auto serializeHttpResponse(const HttpResponse& resp) -> std::string;

/// Helper to build a JSON error HTTP response.
auto makeJsonErrorResponse(int status, std::string_view reason, std::string_view message,
                           std::map<std::string, std::string> extraHeaders = {}) -> HttpResponse;

}  // namespace xoj::mcp
