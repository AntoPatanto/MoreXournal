#include "SettingsSearch.h"

#include <cstdint>

namespace xoj::prefs {

namespace {

bool isAsciiSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Decode one UTF-8 codepoint starting at pos; advances pos by at least 1.
uint32_t decodeUtf8(std::string_view input, size_t& pos) {
    const auto b0 = static_cast<unsigned char>(input[pos]);
    if (b0 < 0x80) {
        pos += 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && pos + 1 < input.size()) {
        const auto b1 = static_cast<unsigned char>(input[pos + 1]);
        if ((b1 & 0xC0) == 0x80) {
            pos += 2;
            return (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(b1 & 0x3F);
        }
    }
    if ((b0 & 0xF0) == 0xE0 && pos + 2 < input.size()) {
        const auto b1 = static_cast<unsigned char>(input[pos + 1]);
        const auto b2 = static_cast<unsigned char>(input[pos + 2]);
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
            pos += 3;
            return (static_cast<uint32_t>(b0 & 0x0F) << 12) | (static_cast<uint32_t>(b1 & 0x3F) << 6) |
                   static_cast<uint32_t>(b2 & 0x3F);
        }
    }
    if ((b0 & 0xF8) == 0xF0 && pos + 3 < input.size()) {
        const auto b1 = static_cast<unsigned char>(input[pos + 1]);
        const auto b2 = static_cast<unsigned char>(input[pos + 2]);
        const auto b3 = static_cast<unsigned char>(input[pos + 3]);
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
            pos += 4;
            return (static_cast<uint32_t>(b0 & 0x07) << 18) | (static_cast<uint32_t>(b1 & 0x3F) << 12) |
                   (static_cast<uint32_t>(b2 & 0x3F) << 6) | static_cast<uint32_t>(b3 & 0x3F);
        }
    }
    pos += 1;
    return b0;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Map common Latin accented/special characters to lowercase ASCII base letters.
// Returns nullptr if no ASCII folding applies.
const char* foldLatinCodepoint(uint32_t cp) {
    switch (cp) {
        // Latin-1 Supplement
        case 0x00C0:
        case 0x00C1:
        case 0x00C2:
        case 0x00C3:
        case 0x00C4:
        case 0x00C5:
        case 0x00E0:
        case 0x00E1:
        case 0x00E2:
        case 0x00E3:
        case 0x00E4:
        case 0x00E5:
        case 0x0100:
        case 0x0101:
        case 0x0102:
        case 0x0103:
        case 0x0104:
        case 0x0105:
            return "a";
        case 0x00C6:
        case 0x00E6:
            return "ae";
        case 0x00C7:
        case 0x00E7:
        case 0x0106:
        case 0x0107:
        case 0x0108:
        case 0x0109:
        case 0x010A:
        case 0x010B:
        case 0x010C:
        case 0x010D:
            return "c";
        case 0x010E:
        case 0x010F:
        case 0x0110:
        case 0x0111:
            return "d";
        case 0x00C8:
        case 0x00C9:
        case 0x00CA:
        case 0x00CB:
        case 0x00E8:
        case 0x00E9:
        case 0x00EA:
        case 0x00EB:
        case 0x0112:
        case 0x0113:
        case 0x0114:
        case 0x0115:
        case 0x0116:
        case 0x0117:
        case 0x0118:
        case 0x0119:
        case 0x011A:
        case 0x011B:
            return "e";
        case 0x011C:
        case 0x011D:
        case 0x011E:
        case 0x011F:
        case 0x0120:
        case 0x0121:
        case 0x0122:
        case 0x0123:
            return "g";
        case 0x0124:
        case 0x0125:
        case 0x0126:
        case 0x0127:
            return "h";
        case 0x00CC:
        case 0x00CD:
        case 0x00CE:
        case 0x00CF:
        case 0x00EC:
        case 0x00ED:
        case 0x00EE:
        case 0x00EF:
        case 0x0128:
        case 0x0129:
        case 0x012A:
        case 0x012B:
        case 0x012C:
        case 0x012D:
        case 0x012E:
        case 0x012F:
        case 0x0130:
        case 0x0131:
            return "i";
        case 0x0134:
        case 0x0135:
            return "j";
        case 0x0136:
        case 0x0137:
            return "k";
        case 0x0139:
        case 0x013A:
        case 0x013B:
        case 0x013C:
        case 0x013D:
        case 0x013E:
        case 0x013F:
        case 0x0140:
        case 0x0141:
        case 0x0142:
            return "l";
        case 0x00D1:
        case 0x00F1:
        case 0x0143:
        case 0x0144:
        case 0x0145:
        case 0x0146:
        case 0x0147:
        case 0x0148:
            return "n";
        case 0x00D2:
        case 0x00D3:
        case 0x00D4:
        case 0x00D5:
        case 0x00D6:
        case 0x00D8:
        case 0x00F2:
        case 0x00F3:
        case 0x00F4:
        case 0x00F5:
        case 0x00F6:
        case 0x00F8:
        case 0x014C:
        case 0x014D:
        case 0x014E:
        case 0x014F:
        case 0x0150:
        case 0x0151:
            return "o";
        case 0x0152:
        case 0x0153:
            return "oe";
        case 0x0154:
        case 0x0155:
        case 0x0156:
        case 0x0157:
        case 0x0158:
        case 0x0159:
            return "r";
        case 0x00DF:
        case 0x1E9E:
            return "ss";
        case 0x015A:
        case 0x015B:
        case 0x015C:
        case 0x015D:
        case 0x015E:
        case 0x015F:
        case 0x0160:
        case 0x0161:
            return "s";
        case 0x0162:
        case 0x0163:
        case 0x0164:
        case 0x0165:
        case 0x0166:
        case 0x0167:
            return "t";
        case 0x00D9:
        case 0x00DA:
        case 0x00DB:
        case 0x00DC:
        case 0x00F9:
        case 0x00FA:
        case 0x00FB:
        case 0x00FC:
        case 0x0168:
        case 0x0169:
        case 0x016A:
        case 0x016B:
        case 0x016C:
        case 0x016D:
        case 0x016E:
        case 0x016F:
        case 0x0170:
        case 0x0171:
        case 0x0172:
        case 0x0173:
            return "u";
        case 0x0174:
        case 0x0175:
            return "w";
        case 0x00DD:
        case 0x00FD:
        case 0x00FF:
        case 0x0176:
        case 0x0177:
        case 0x0178:
            return "y";
        case 0x0179:
        case 0x017A:
        case 0x017B:
        case 0x017C:
        case 0x017D:
        case 0x017E:
            return "z";
        // Greek sigma used in Gaussian stabilizer label
        case 0x03A3:
        case 0x03C3:
            return "sigma";
        default:
            return nullptr;
    }
}

std::string buildHaystack(const SearchEntry& entry) {
    std::string combined;
    combined.reserve(entry.pageTitle.size() + entry.sectionTitle.size() + entry.sectionNote.size() +
                     entry.label.size() + entry.subtitle.size() + entry.keywords.size() + 6);
    combined.append(entry.pageTitle);
    combined.push_back(' ');
    combined.append(entry.sectionTitle);
    combined.push_back(' ');
    combined.append(entry.sectionNote);
    combined.push_back(' ');
    combined.append(entry.label);
    combined.push_back(' ');
    combined.append(entry.subtitle);
    combined.push_back(' ');
    combined.append(entry.keywords);
    return normalizeForSearch(combined);
}

bool matchesTokens(const SearchEntry& entry, const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return false;
    }
    const std::string haystack = buildHaystack(entry);
    for (const auto& token: tokens) {
        if (haystack.find(token) == std::string::npos) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string normalizeForSearch(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    size_t pos = 0;
    while (pos < input.size()) {
        const uint32_t cp = decodeUtf8(input, pos);

        // Skip combining diacritical marks (supports NFD decomposed accents)
        if (cp >= 0x0300 && cp <= 0x036F) {
            continue;
        }

        if (cp >= 'A' && cp <= 'Z') {
            out.push_back(static_cast<char>(cp - 'A' + 'a'));
            continue;
        }

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
            continue;
        }

        if (const char* folded = foldLatinCodepoint(cp)) {
            out.append(folded);
            continue;
        }

        // Fold basic Cyrillic uppercase (А-Я -> а-я) and Greek uppercase (Α-Ω -> α-ω)
        if (cp >= 0x0410 && cp <= 0x042F) {
            appendUtf8(out, cp + 0x20);
        } else if (cp >= 0x0391 && cp <= 0x03A9) {
            appendUtf8(out, cp + 0x20);
        } else {
            appendUtf8(out, cp);
        }
    }

    return out;
}

std::vector<std::string> tokenizeSearchQuery(std::string_view query) {
    const std::string norm = normalizeForSearch(query);
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < norm.size()) {
        while (i < norm.size() && isAsciiSpace(norm[i])) {
            ++i;
        }
        const size_t start = i;
        while (i < norm.size() && !isAsciiSpace(norm[i])) {
            ++i;
        }
        if (i > start) {
            tokens.emplace_back(norm.substr(start, i - start));
        }
    }
    return tokens;
}

bool matchesSearchEntry(const SearchEntry& entry, std::string_view query) {
    return matchesTokens(entry, tokenizeSearchQuery(query));
}

std::vector<size_t> filterSearchEntries(const std::vector<SearchEntry>& entries, std::string_view query) {
    const auto tokens = tokenizeSearchQuery(query);
    std::vector<size_t> matches;
    if (tokens.empty()) {
        return matches;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (matchesTokens(entries[i], tokens)) {
            matches.push_back(i);
        }
    }
    return matches;
}

}  // namespace xoj::prefs
