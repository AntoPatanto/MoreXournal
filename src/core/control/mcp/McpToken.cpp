#include "McpToken.h"

#include <array>
#include <cstdint>
#include <stdexcept>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>  // arc4random_buf
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace xoj::mcp {

namespace {

void fillRandomBytes(uint8_t* buf, size_t len) {
#if defined(_WIN32)
    NTSTATUS status = BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(buf, len);
#else
    size_t filled = 0;
#if defined(__linux__)
    while (filled < len) {
        ssize_t n = getrandom(buf + filled, len - filled, 0);
        if (n > 0) {
            filled += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno == ENOSYS) {
            break;  // Fall back to /dev/urandom on older kernels
        }
        throw std::runtime_error("getrandom failed");
    }
    if (filled == len) {
        return;
    }
#endif
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("Failed to open /dev/urandom");
    }
    while (filled < len) {
        ssize_t n = read(fd, buf + filled, len - filled);
        if (n > 0) {
            filled += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            close(fd);
            throw std::runtime_error("Failed to read from /dev/urandom");
        }
    }
    close(fd);
#endif
}

}  // namespace

auto generateToken() -> std::string {
    std::array<uint8_t, TOKEN_RANDOM_BYTES> bytes{};
    fillRandomBytes(bytes.data(), bytes.size());

    static constexpr char HEX[] = "0123456789abcdef";
    std::string token;
    token.reserve(TOKEN_LENGTH);
    token.append(TOKEN_PREFIX);
    for (uint8_t b: bytes) {
        token.push_back(HEX[(b >> 4U) & 0x0FU]);
        token.push_back(HEX[b & 0x0FU]);
    }
    return token;
}

auto constantTimeEquals(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<uint8_t>(diff | (static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i])));
    }
    return diff == 0;
}

auto isValidTokenFormat(std::string_view token) -> bool {
    if (token.size() != TOKEN_LENGTH || token.substr(0, TOKEN_PREFIX.size()) != TOKEN_PREFIX) {
        return false;
    }
    for (size_t i = TOKEN_PREFIX.size(); i < token.size(); ++i) {
        char c = token[i];
        bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!isHex) {
            return false;
        }
    }
    return true;
}

}  // namespace xoj::mcp
