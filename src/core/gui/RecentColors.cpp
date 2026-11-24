#include "RecentColors.h"

#include <algorithm>  // for all_of, find
#include <cctype>     // for isxdigit
#include <cstdlib>    // for strtoul

namespace xoj::toolpill {

namespace {
Color opaque(Color c) {
    c.alpha = 0xff;
    return c;
}

bool contains(const std::vector<Color>& v, Color c) { return std::find(v.begin(), v.end(), c) != v.end(); }
}  // namespace

RecentColors::RecentColors(const std::vector<Color>& colors) {
    for (auto it = colors.rbegin(); it != colors.rend(); ++it) {
        push(*it);
    }
}

bool RecentColors::push(Color color) {
    color = opaque(color);
    if (color == PINNED_COLOR || (!colors.empty() && colors.front() == color)) {
        return false;
    }
    if (auto it = std::find(colors.begin(), colors.end(), color); it != colors.end()) {
        colors.erase(it);
    } else if (colors.size() >= RECENT_COLOR_COUNT) {
        colors.pop_back();
    }
    colors.insert(colors.begin(), color);
    return true;
}

std::string RecentColors::toString() const {
    std::string s;
    for (Color c: colors) {
        if (!s.empty()) {
            s += ',';
        }
        s += Util::rgb_to_hex_string(c);
    }
    return s;
}

RecentColors RecentColors::fromString(std::string_view s) {
    std::vector<Color> parsed;
    while (!s.empty()) {
        const size_t comma = s.find(',');
        const std::string entry(s.substr(0, comma));
        s = comma == std::string_view::npos ? std::string_view{} : s.substr(comma + 1);

        if (entry.size() != 7 || entry[0] != '#' ||
            !std::all_of(entry.begin() + 1, entry.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; })) {
            continue;
        }
        const unsigned long rgb = std::strtoul(entry.c_str() + 1, nullptr, 16);
        parsed.push_back(opaque(Color(static_cast<uint32_t>(rgb))));
    }
    return RecentColors(parsed);
}

std::vector<Color> pillColors(const RecentColors& recent, const std::vector<Color>& palette, size_t count,
                              bool includePinned) {
    std::vector<Color> shown;
    auto add = [&](Color c) {
        c = opaque(c);
        if (!includePinned && c == PINNED_COLOR) {
            return;
        }
        if (shown.size() < count && !contains(shown, c)) {
            shown.push_back(c);
        }
    };
    if (includePinned) {
        add(PINNED_COLOR);
    }
    for (Color c: recent.get()) {
        add(c);
    }
    for (Color c: palette) {
        add(c);
    }
    return shown;
}

}  // namespace xoj::toolpill
