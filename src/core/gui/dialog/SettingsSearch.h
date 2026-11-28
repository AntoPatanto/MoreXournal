/*
 * MoreXournal
 *
 * Pure search normalization and matching for the Preferences dialog
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xoj::prefs {

struct SearchEntry {
    std::string rowId;
    std::string pageId;
    std::string pageTitle;
    std::string sectionId;
    std::string sectionTitle;
    std::string sectionNote;
    std::string label;
    std::string subtitle;
    std::string keywords;
};

/**
 * Normalize UTF-8 text for case- and accent-insensitive substring search.
 * Folds ASCII and common Latin accented characters (both NFC and NFD) to
 * lowercase ASCII equivalents and drops combining diacritical marks.
 */
std::string normalizeForSearch(std::string_view input);

/**
 * Split normalized text into whitespace-delimited search tokens.
 */
std::vector<std::string> tokenizeSearchQuery(std::string_view query);

/**
 * Return true if every query token matches somewhere in the entry's
 * page title, section title/note, label, subtitle, or keywords/synonyms.
 * Returns false for an empty or whitespace-only query.
 */
bool matchesSearchEntry(const SearchEntry& entry, std::string_view query);

/**
 * Return the indices of all entries matching the query, preserving order.
 */
std::vector<size_t> filterSearchEntries(const std::vector<SearchEntry>& entries, std::string_view query);

}  // namespace xoj::prefs
