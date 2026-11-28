/*
 * MoreXournal
 *
 * MCP tool input validation and execution against the open document
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "control/mcp/McpProtocol.h"
#include "model/ShapeInfo.h"
#include "util/Color.h"
#include "util/Point.h"

class Control;

namespace xoj::mcp {

constexpr std::string_view EDITS_DISABLED_ERROR = "Edits are off. Turn on Allow edits in Preferences > AI assistants.";

constexpr double PAGE_COORD_MARGIN_PT = 100.0;
constexpr int DEFAULT_PAGE_IMAGE_MAX_PX = 1600;
constexpr int MIN_PAGE_IMAGE_MAX_PX = 256;
constexpr int MAX_PAGE_IMAGE_MAX_PX = 4096;
constexpr size_t MAX_TEXT_BYTES = 64 * 1024;
constexpr double MIN_FONT_SIZE_PT = 4.0;
constexpr double MAX_FONT_SIZE_PT = 200.0;
constexpr double MIN_STROKE_WIDTH_PT = 0.1;
constexpr double MAX_STROKE_WIDTH_PT = 100.0;
constexpr size_t MAX_STROKES_PER_CALL = 500;
constexpr size_t MAX_STROKE_POINTS_PER_CALL = 20000;
constexpr size_t MAX_CONTENT_STROKES_LISTED = 500;
constexpr size_t MAX_CONTENT_POINTS_RETURNED = 10000;

struct PageImageArgs {
    size_t pageIndex = 0;  ///< 0-based
    int maxSizePx = DEFAULT_PAGE_IMAGE_MAX_PX;
};

struct PageContentArgs {
    size_t pageIndex = 0;  ///< 0-based
    bool includePoints = false;
};

struct AddTextArgs {
    size_t pageIndex = 0;  ///< 0-based
    double x = 0.0;
    double y = 0.0;
    std::string text;
    std::optional<Color> color;
    std::optional<double> fontSize;
};

struct StrokeSpec {
    std::vector<xoj::util::Point<double>> points;
    std::optional<Color> color;
    std::optional<double> width;
};

struct AddStrokesArgs {
    size_t pageIndex = 0;  ///< 0-based
    std::vector<StrokeSpec> strokes;
};

struct AddShapeArgs {
    size_t pageIndex = 0;  ///< 0-based
    ShapeInfo shapeInfo;
    std::string typeName;
    std::optional<Color> strokeColor;
    std::optional<double> width;
    std::optional<Color> fillColor;
    std::optional<int> fillOpacity;  ///< 0..255 (0 = unfilled)
    std::string lineStyle = "solid";
};

struct AddPageArgs {
    size_t insertPosition = 0;  ///< 0-based insertion index in [0, pageCount]
};

struct GoToPageArgs {
    size_t pageIndex = 0;  ///< 0-based
};

template <typename T>
using ValidationResult = std::variant<T, std::string>;

/// Parse "#rrggbb" hex color into opaque ARGB Color (0xffRRGGBB).
auto parseHexColor(std::string_view s) -> std::optional<Color>;

/// Format Color as lowercase "#rrggbb".
auto formatColorHex(Color c) -> std::string;

/// Pure input validators (unit-testable without GTK or Control).
auto validatePageImageArgs(const nlohmann::json& args, size_t pageCount, size_t currentPageIndex)
        -> ValidationResult<PageImageArgs>;

auto validatePageContentArgs(const nlohmann::json& args, size_t pageCount, size_t currentPageIndex)
        -> ValidationResult<PageContentArgs>;

auto validateAddTextArgs(const nlohmann::json& args, size_t pageCount,
                         const std::function<std::pair<double, double>(size_t)>& pageSizeLookup)
        -> ValidationResult<AddTextArgs>;

auto validateAddStrokesArgs(const nlohmann::json& args, size_t pageCount,
                            const std::function<std::pair<double, double>(size_t)>& pageSizeLookup)
        -> ValidationResult<AddStrokesArgs>;

auto validateAddShapeArgs(const nlohmann::json& args, size_t pageCount,
                          const std::function<std::pair<double, double>(size_t)>& pageSizeLookup)
        -> ValidationResult<AddShapeArgs>;

auto validateAddPageArgs(const nlohmann::json& args, size_t pageCount, size_t currentPageIndex)
        -> ValidationResult<AddPageArgs>;

auto validateGoToPageArgs(const nlohmann::json& args, size_t pageCount) -> ValidationResult<GoToPageArgs>;

/// Returns true if the tool name is a write tool gated by Allow edits.
auto isWriteTool(std::string_view toolName) -> bool;

/// Execute a tool call on the GTK main thread against the active Control.
auto executeMcpToolOnMainThread(Control* control, const std::string& toolName, const nlohmann::json& args,
                                bool allowEdits) -> ToolCallResult;

}  // namespace xoj::mcp
