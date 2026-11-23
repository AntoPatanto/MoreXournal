/*
 * MoreXournal
 *
 * Tests for the colors the floating tool pill shows: black, then the recently used colors
 *
 * @license GNU GPLv2 or later
 */

#include <vector>

#include <gtest/gtest.h>

#include "gui/RecentColors.h"
#include "util/Color.h"

using namespace xoj::toolpill;

namespace {
const Color black = Colors::black;
const Color red{0xffff0000U};
const Color yellow{0xffffff00U};
const Color blue{0xff0000ffU};
const Color green{0xff00ff00U};
const Color cyan{0xff00ffffU};
const Color magenta{0xffff00ffU};
const Color gray{0xff808080U};
const Color white{0xffffffffU};

using Colors_ = std::vector<Color>;
}  // namespace

TEST(RecentColorsTest, testUserExample) {
    RecentColors r;
    for (Color c: {black, red, yellow, red}) {
        r.push(c);
    }
    EXPECT_EQ(pillColors(r, {}, 6), (Colors_{black, red, yellow}));
    r.push(blue);
    EXPECT_EQ(pillColors(r, {}, 6), (Colors_{black, blue, red, yellow}));
}

TEST(RecentColorsTest, testPushMovesToFrontWithoutDuplicates) {
    RecentColors r;
    EXPECT_TRUE(r.push(red));
    EXPECT_TRUE(r.push(blue));
    EXPECT_FALSE(r.push(blue));  // Already the most recent
    EXPECT_TRUE(r.push(red));
    EXPECT_EQ(r.get(), (Colors_{red, blue}));
}

TEST(RecentColorsTest, testBlackIsNeverRecent) {
    RecentColors r;
    EXPECT_FALSE(r.push(black));
    r.push(red);
    EXPECT_FALSE(r.push(black));
    EXPECT_FALSE(r.push(Color(0x80000000U)));  // Black with another alpha
    EXPECT_EQ(r.get(), (Colors_{red}));
    EXPECT_EQ(RecentColors(Colors_{black, red}).get(), (Colors_{red}));
}

TEST(RecentColorsTest, testAlphaIsIgnored) {
    RecentColors r;
    r.push(Color(0x80ff0000U));  // Red with highlighter-like alpha
    EXPECT_FALSE(r.push(red));
    EXPECT_EQ(r.get(), (Colors_{red}));
}

TEST(RecentColorsTest, testLeastRecentIsEvictedWhenFull) {
    RecentColors r;
    for (Color c: {red, yellow, blue, green, cyan}) {
        r.push(c);
    }
    EXPECT_EQ(r.get().size(), RECENT_COLOR_COUNT);
    r.push(yellow);  // Reused: nothing drops out
    EXPECT_EQ(r.get(), (Colors_{yellow, cyan, green, blue, red}));
    r.push(magenta);
    EXPECT_EQ(r.get(), (Colors_{magenta, yellow, cyan, green, blue}));
}

TEST(RecentColorsTest, testPaletteFillsTheFreeSlots) {
    RecentColors r;
    r.push(blue);
    r.push(red);
    const Colors_ palette{black, red, gray, Color(0x00808080U), blue, white, green, cyan};
    // Black and the recent colors are skipped, gray only once even with another alpha
    EXPECT_EQ(pillColors(r, palette, 6), (Colors_{black, red, blue, gray, white, green}));
    EXPECT_EQ(pillColors(RecentColors{}, Colors_{black, white}, 6), (Colors_{black, white}));
    EXPECT_EQ(pillColors(RecentColors{}, {}, 6), (Colors_{black}));
}

TEST(RecentColorsTest, testPillShowsAtMostCountColors) {
    RecentColors r;
    for (Color c: {red, yellow, blue, green, cyan}) {
        r.push(c);
    }
    EXPECT_EQ(pillColors(r, {gray, white}, 6), (Colors_{black, cyan, green, blue, yellow, red}));
}

TEST(RecentColorsTest, testStringRoundTrip) {
    RecentColors r;
    r.push(Color(0xff123456U));
    r.push(red);
    EXPECT_EQ(r.toString(), "#ff0000,#123456");
    EXPECT_EQ(RecentColors::fromString(r.toString()).get(), r.get());
    EXPECT_TRUE(RecentColors::fromString("").get().empty());
    // Invalid entries, black and duplicates are skipped; too many entries keep the first ones
    EXPECT_EQ(RecentColors::fromString("#ff0000,junk,#00000,#000000,#ff0000,#0000ff").get(), (Colors_{red, blue}));
    EXPECT_EQ(RecentColors::fromString("#ff0000,#ffff00,#0000ff,#00ff00,#00ffff,#ff00ff").get(),
              (Colors_{red, yellow, blue, green, cyan}));
}
