#include "McpHttp.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <sstream>

#include <nlohmann/json.hpp>

#include "McpToken.h"

namespace xoj::mcp {

namespace {

auto trimAsciiWhitespace(std::string_view sv) -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) {
        sv.remove_suffix(1);
    }
    return sv;
}

auto isValidHeaderName(std::string_view name) -> bool {
    if (name.empty()) {
        return false;
    }
    for (unsigned char c: name) {
        if (c <= 32 || c >= 127 || c == ':') {
            return false;
        }
    }
    return true;
}

auto isValidPortStr(std::string_view portStr) -> bool {
    if (portStr.empty() || portStr.size() > 5) {
        return false;
    }
    for (char c: portStr) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    unsigned int port = 0;
    auto [ptr, ec] = std::from_chars(portStr.data(), portStr.data() + portStr.size(), port);
    return ec == std::errc{} && ptr == portStr.data() + portStr.size() && port >= 1 && port <= 65535;
}

auto isLoopbackHostPort(std::string_view hostPort) -> bool {
    if (hostPort.empty()) {
        return false;
    }
    std::string lower = toLowerAscii(hostPort);
    std::string_view host;
    std::string_view port;

    if (lower.front() == '[') {
        size_t closeBracket = lower.find(']');
        if (closeBracket == std::string::npos) {
            return false;
        }
        host = std::string_view(lower).substr(0, closeBracket + 1);
        std::string_view rest = std::string_view(lower).substr(closeBracket + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') {
                return false;
            }
            port = rest.substr(1);
            if (!isValidPortStr(port)) {
                return false;
            }
        }
    } else {
        size_t colon = lower.find(':');
        if (colon == std::string::npos) {
            host = lower;
        } else {
            // Reject multiple colons (e.g. unbracketed IPv6 or malformed host)
            if (lower.find(':', colon + 1) != std::string::npos) {
                return false;
            }
            host = std::string_view(lower).substr(0, colon);
            port = std::string_view(lower).substr(colon + 1);
            if (!isValidPortStr(port)) {
                return false;
            }
        }
    }

    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

auto defaultReasonForStatus(int status) -> std::string_view {
    switch (status) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 411:
            return "Length Required";
        case 413:
            return "Payload Too Large";
        case 431:
            return "Request Header Fields Too Large";
        case 500:
            return "Internal Server Error";
        case 503:
            return "Service Unavailable";
        default:
            return "Error";
    }
}

}  // namespace

auto toLowerAscii(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c: s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

auto HttpRequest::header(std::string_view lowerName) const -> std::optional<std::string_view> {
    auto it = headers.find(std::string(lowerName));
    if (it == headers.end()) {
        return std::nullopt;
    }
    return std::string_view(it->second);
}

auto parseHttpHeaders(std::string_view rawHeaders) -> HttpHeaderParseResult {
    HttpHeaderParseResult res;

    size_t headerEnd = rawHeaders.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        if (rawHeaders.size() > MAX_HTTP_HEADER_BYTES) {
            res.errorStatus = 431;
            res.errorMessage = "Request headers exceed 16 KiB limit";
            return res;
        }
        res.errorStatus = 400;
        res.errorMessage = "Incomplete HTTP headers";
        return res;
    }

    size_t totalHeaderBytes = headerEnd + 4;
    if (totalHeaderBytes > MAX_HTTP_HEADER_BYTES) {
        res.errorStatus = 431;
        res.errorMessage = "Request headers exceed 16 KiB limit";
        return res;
    }
    res.headerEndOffset = totalHeaderBytes;

    std::string_view block = rawHeaders.substr(0, headerEnd);
    size_t firstCrlf = block.find("\r\n");
    std::string_view requestLine = firstCrlf == std::string_view::npos ? block : block.substr(0, firstCrlf);

    // Parse "METHOD SP TARGET SP HTTP/1.x"
    size_t sp1 = requestLine.find(' ');
    if (sp1 == std::string_view::npos || sp1 == 0) {
        res.errorStatus = 400;
        res.errorMessage = "Malformed HTTP request line";
        return res;
    }
    size_t sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || sp2 == sp1 + 1) {
        res.errorStatus = 400;
        res.errorMessage = "Malformed HTTP request line";
        return res;
    }
    if (requestLine.find(' ', sp2 + 1) != std::string_view::npos) {
        res.errorStatus = 400;
        res.errorMessage = "Malformed HTTP request line";
        return res;
    }

    std::string_view method = requestLine.substr(0, sp1);
    std::string_view target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string_view version = requestLine.substr(sp2 + 1);

    for (unsigned char c: method) {
        if (c < 'A' || c > 'Z') {
            res.errorStatus = 400;
            res.errorMessage = "Invalid HTTP method";
            return res;
        }
    }
    if (target.empty() || target.front() != '/') {
        res.errorStatus = 400;
        res.errorMessage = "Invalid HTTP request target";
        return res;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        res.errorStatus = 400;
        res.errorMessage = "Unsupported HTTP version";
        return res;
    }

    res.request.method = std::string(method);
    res.request.target = std::string(target);
    size_t queryPos = target.find('?');
    res.request.path = std::string(queryPos == std::string_view::npos ? target : target.substr(0, queryPos));
    res.request.httpVersion = std::string(version);

    bool sawContentLength = false;
    size_t parsedContentLength = 0;

    if (firstCrlf != std::string_view::npos) {
        size_t pos = firstCrlf + 2;
        while (pos < block.size()) {
            size_t nextCrlf = block.find("\r\n", pos);
            std::string_view line =
                    nextCrlf == std::string_view::npos ? block.substr(pos) : block.substr(pos, nextCrlf - pos);
            pos = nextCrlf == std::string_view::npos ? block.size() : nextCrlf + 2;

            size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                res.errorStatus = 400;
                res.errorMessage = "Malformed HTTP header";
                return res;
            }
            std::string_view rawName = line.substr(0, colon);
            if (!isValidHeaderName(rawName)) {
                res.errorStatus = 400;
                res.errorMessage = "Invalid HTTP header name";
                return res;
            }
            std::string name = toLowerAscii(rawName);
            std::string value = std::string(trimAsciiWhitespace(line.substr(colon + 1)));

            if (name == "host") {
                if (res.request.headers.contains("host")) {
                    res.errorStatus = 400;
                    res.errorMessage = "Duplicate Host header";
                    return res;
                }
                res.request.headers.emplace(std::move(name), std::move(value));
            } else if (name == "content-length") {
                if (value.empty()) {
                    res.errorStatus = 400;
                    res.errorMessage = "Invalid Content-Length header";
                    return res;
                }
                for (char c: value) {
                    if (c < '0' || c > '9') {
                        res.errorStatus = 400;
                        res.errorMessage = "Invalid Content-Length header";
                        return res;
                    }
                }
                uint64_t cl = 0;
                auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), cl);
                if (ec != std::errc{} || ptr != value.data() + value.size()) {
                    res.errorStatus = 413;
                    res.errorMessage = "Content-Length exceeds limit";
                    return res;
                }
                if (cl > MAX_HTTP_BODY_BYTES) {
                    res.errorStatus = 413;
                    res.errorMessage = "Request body exceeds 8 MiB limit";
                    return res;
                }
                if (sawContentLength && parsedContentLength != static_cast<size_t>(cl)) {
                    res.errorStatus = 400;
                    res.errorMessage = "Conflicting Content-Length headers";
                    return res;
                }
                sawContentLength = true;
                parsedContentLength = static_cast<size_t>(cl);
                res.request.headers["content-length"] = std::move(value);
            } else {
                auto [it, inserted] = res.request.headers.emplace(name, value);
                if (!inserted) {
                    it->second += ", ";
                    it->second += value;
                }
            }
        }
    }

    if (res.request.httpVersion == "HTTP/1.1" && !res.request.headers.contains("host")) {
        res.errorStatus = 400;
        res.errorMessage = "Missing Host header";
        return res;
    }

    if (auto te = res.request.header("transfer-encoding"); te.has_value()) {
        std::string lowerTe = toLowerAscii(*te);
        if (lowerTe.find("chunked") != std::string::npos || !lowerTe.empty()) {
            res.errorStatus = sawContentLength ? 400 : 411;
            res.errorMessage = "Transfer-Encoding is not supported; send a fixed Content-Length";
            return res;
        }
    }

    if (res.request.method == "POST" && !sawContentLength) {
        res.errorStatus = 411;
        res.errorMessage = "Content-Length header is required for POST requests";
        return res;
    }

    res.contentLength = parsedContentLength;
    res.ok = true;
    return res;
}

auto isAllowedHost(std::string_view host) -> bool { return isLoopbackHostPort(host); }

auto isAllowedOrigin(std::string_view origin) -> bool {
    constexpr std::string_view HTTP_SCHEME = "http://";
    if (origin.size() <= HTTP_SCHEME.size()) {
        return false;
    }
    if (toLowerAscii(origin.substr(0, HTTP_SCHEME.size())) != HTTP_SCHEME) {
        return false;
    }
    std::string_view authority = origin.substr(HTTP_SCHEME.size());
    // Reject path, query, fragment, userinfo or whitespace
    if (authority.find_first_of("/?#@ \t\r\n") != std::string_view::npos) {
        return false;
    }
    return isLoopbackHostPort(authority);
}

auto isAuthorizedBearer(std::string_view authHeader, std::string_view expectedToken) -> bool {
    if (expectedToken.empty()) {
        return false;
    }
    constexpr std::string_view BEARER = "Bearer ";
    if (authHeader.size() <= BEARER.size()) {
        return false;
    }
    // RFC 6750 / 7235 auth scheme is case-insensitive
    if (toLowerAscii(authHeader.substr(0, BEARER.size())) != "bearer ") {
        return false;
    }
    std::string_view supplied = authHeader.substr(BEARER.size());
    return constantTimeEquals(supplied, expectedToken);
}

auto makeJsonErrorResponse(int status, std::string_view reason, std::string_view message,
                           std::map<std::string, std::string> extraHeaders) -> HttpResponse {
    HttpResponse resp;
    resp.status = status;
    resp.reason = reason.empty() ? std::string(defaultReasonForStatus(status)) : std::string(reason);
    resp.contentType = "application/json";
    nlohmann::json errObj = {{"error", message}};
    resp.body = errObj.dump();
    resp.extraHeaders = std::move(extraHeaders);
    return resp;
}

auto validateHttpRequest(const HttpRequest& req, std::string_view expectedToken) -> std::optional<HttpResponse> {
    if (auto host = req.header("host"); host.has_value()) {
        if (!isAllowedHost(*host)) {
            return makeJsonErrorResponse(403, "Forbidden", "Forbidden Host header");
        }
    } else if (req.httpVersion == "HTTP/1.1") {
        return makeJsonErrorResponse(400, "Bad Request", "Missing Host header");
    }

    if (auto origin = req.header("origin"); origin.has_value()) {
        if (!isAllowedOrigin(*origin)) {
            return makeJsonErrorResponse(403, "Forbidden", "Forbidden Origin header");
        }
    }

    auto auth = req.header("authorization");
    if (!auth.has_value() || !isAuthorizedBearer(*auth, expectedToken)) {
        return makeJsonErrorResponse(401, "Unauthorized", "Unauthorized", {{"WWW-Authenticate", "Bearer"}});
    }

    if (req.path != "/mcp") {
        return makeJsonErrorResponse(404, "Not Found", "Not Found");
    }

    if (req.method != "POST") {
        return makeJsonErrorResponse(405, "Method Not Allowed", "Only POST is supported on /mcp", {{"Allow", "POST"}});
    }

    return std::nullopt;
}

auto serializeHttpResponse(const HttpResponse& resp) -> std::string {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    std::string_view reason = resp.reason.empty() ? defaultReasonForStatus(resp.status) : std::string_view(resp.reason);
    out << "HTTP/1.1 " << resp.status << ' ' << reason << "\r\n";
    if (!resp.contentType.empty()) {
        out << "Content-Type: " << resp.contentType << "\r\n";
    }
    out << "Content-Length: " << resp.body.size() << "\r\n";
    out << "Cache-Control: no-store\r\n";
    out << "Connection: close\r\n";
    for (const auto& [k, v]: resp.extraHeaders) {
        out << k << ": " << v << "\r\n";
    }
    out << "\r\n";
    out << resp.body;
    return out.str();
}

}  // namespace xoj::mcp
