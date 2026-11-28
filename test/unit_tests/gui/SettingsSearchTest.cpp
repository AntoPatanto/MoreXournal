/*
 * MoreXournal
 *
 * Unit tests for Preferences search normalization and matching
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/dialog/SettingsSearch.h"

using namespace xoj::prefs;

namespace {

std::vector<SearchEntry> sampleEntries() {
    return {
            SearchEntry{
                    .rowId = "language",
                    .pageId = "general",
                    .pageTitle = "General",
                    .sectionId = "language",
                    .sectionTitle = "Language",
                    .sectionNote = "",
                    .label = "Interface language",
                    .subtitle = "Prendi effetto al riavvio — modalità più semplice",
                    .keywords = "translation locale language",
            },
            SearchEntry{
                    .rowId = "palm-on",
                    .pageId = "touch",
                    .pageTitle = "Touch",
                    .sectionId = "handrecog",
                    .sectionTitle = "Palm rejection",
                    .sectionNote = "Turn off touch while the pen is near.",
                    .label = "Turn off touch while the pen is near",
                    .subtitle = "",
                    .keywords = "hand recognition palm rejection disable touch touchscreen",
            },
            SearchEntry{
                    .rowId = "stab-method",
                    .pageId = "pen",
                    .pageTitle = "Pen",
                    .sectionId = "stabilizer",
                    .sectionTitle = "Stroke smoothing",
                    .sectionNote = "Smoothing reduces shaky lines.",
                    .label = "Averaging",
                    .subtitle = "",
                    .keywords = "stabilizer stabilization smoothing shaky jitter input system",
            },
            SearchEntry{
                    .rowId = "savename-new",
                    .pageId = "general",
                    .pageTitle = "General",
                    .sectionId = "savename",
                    .sectionTitle = "Default file names",
                    .sectionNote = "",
                    .label = "New documents",
                    .subtitle = "Français — résumé café",
                    .keywords = "save name template placeholder date load save",
            },
    };
}

}  // namespace

TEST(SettingsSearchTest, caseInsensitiveMatch) {
    const auto entries = sampleEntries();

    EXPECT_TRUE(matchesSearchEntry(entries[0], "LANGUAGE"));
    EXPECT_TRUE(matchesSearchEntry(entries[0], "InTeRfAcE"));
    EXPECT_TRUE(matchesSearchEntry(entries[1], "PALM REJECTION"));

    const auto results = filterSearchEntries(entries, "STROKE SMOOTHING");
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], 2U);
}

TEST(SettingsSearchTest, accentInsensitiveMatch) {
    const auto entries = sampleEntries();

    // Precomposed (NFC) accented text matched with unaccented ASCII query
    EXPECT_TRUE(matchesSearchEntry(entries[0], "modalita"));
    EXPECT_TRUE(matchesSearchEntry(entries[0], "piu semplice"));
    EXPECT_TRUE(matchesSearchEntry(entries[3], "francais"));
    EXPECT_TRUE(matchesSearchEntry(entries[3], "resume cafe"));

    // Accented query matches unaccented label
    EXPECT_TRUE(matchesSearchEntry(entries[0], "languàge"));

    // Decomposed (NFD: 'e' + U+0301) normalizes the same as ASCII 'e'
    EXPECT_EQ(normalizeForSearch("re\xCC\x81sume\xCC\x81"), "resume");
    EXPECT_EQ(normalizeForSearch("Über Größe Æther"), "uber grosse aether");
}

TEST(SettingsSearchTest, matchByKeywordOrSynonymOnly) {
    const auto entries = sampleEntries();

    // "hand recognition" and "touchscreen" are only in keywords/synonyms, not in label/section/page
    const auto palmResults = filterSearchEntries(entries, "hand recognition");
    ASSERT_EQ(palmResults.size(), 1U);
    EXPECT_EQ(palmResults[0], 1U);

    // Old Xournal++ tab synonym "input system" + keyword "jitter"
    const auto stabResults = filterSearchEntries(entries, "input system jitter");
    ASSERT_EQ(stabResults.size(), 1U);
    EXPECT_EQ(stabResults[0], 2U);

    // Old tab synonym "load save"
    const auto saveResults = filterSearchEntries(entries, "load save");
    ASSERT_EQ(saveResults.size(), 1U);
    EXPECT_EQ(saveResults[0], 3U);
}

TEST(SettingsSearchTest, noMatchAndEmptyQuery) {
    const auto entries = sampleEntries();

    EXPECT_TRUE(filterSearchEntries(entries, "").empty());
    EXPECT_TRUE(filterSearchEntries(entries, "   \t\n  ").empty());
    EXPECT_TRUE(filterSearchEntries(entries, "nonexistent-setting-xyz").empty());
    // Multi-word AND: both words exist across different entries, not on the same entry
    EXPECT_TRUE(filterSearchEntries(entries, "palm jitter").empty());
}
