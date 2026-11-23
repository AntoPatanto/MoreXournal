/*
 * MoreXournal
 *
 * The colors the floating tool pill shows: black, then the recently used colors
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>      // for size_t
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include "util/Color.h"  // for Color

namespace xoj::toolpill {

/// Always the first swatch of the pill, never stored among the recent colors
constexpr Color PINNED_COLOR = Colors::black;
/// How many recent colors are kept: the pill shows them after the pinned color
constexpr size_t RECENT_COLOR_COUNT = 5;

/**
 * The colors the user finished a stroke or an element with, most recent first, without duplicates and without
 * PINNED_COLOR. The alpha is ignored: every color is stored opaque, like the TOOL_COLOR action state.
 */
class RecentColors {
public:
    RecentColors() = default;
    /// Keeps the valid colors of `colors`, in order, as if they had been used from the last to the first
    explicit RecentColors(const std::vector<Color>& colors);

    /// Moves `color` to the front, dropping the least recent color when full. Returns whether the list changed.
    bool push(Color color);

    const std::vector<Color>& get() const { return colors; }

    /// "#rrggbb,#rrggbb,...", for the settings file
    std::string toString() const;
    /// Parses toString(), skipping invalid entries
    static RecentColors fromString(std::string_view s);

private:
    std::vector<Color> colors;
};

/**
 * The `count` colors of the pill: PINNED_COLOR, the recent colors, then palette colors in palette order (skipping
 * the ones already shown) when there are not enough recent colors. All colors are opaque.
 */
std::vector<Color> pillColors(const RecentColors& recent, const std::vector<Color>& palette, size_t count);

}  // namespace xoj::toolpill
