/*
 * MoreXournal
 *
 * Cryptographic token generation and constant-time comparison for the MCP server
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <string_view>

namespace xoj::mcp {

constexpr std::string_view TOKEN_PREFIX = "mx_";
constexpr size_t TOKEN_RANDOM_BYTES = 32;
constexpr size_t TOKEN_LENGTH = TOKEN_PREFIX.size() + TOKEN_RANDOM_BYTES * 2;

/// Generate a new bearer token ("mx_" + 64 lowercase hex chars from 32 CSPRNG bytes).
/// Throws std::runtime_error if the OS CSPRNG fails.
auto generateToken() -> std::string;

/// Constant-time comparison of two strings (returns false on length mismatch without leaking content timing).
auto constantTimeEquals(std::string_view a, std::string_view b) -> bool;

/// Validate that a token has the expected prefix and hex format.
auto isValidTokenFormat(std::string_view token) -> bool;

}  // namespace xoj::mcp
