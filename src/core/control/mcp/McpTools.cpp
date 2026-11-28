#include "McpTools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <glib.h>

#include "control/Control.h"
#include "control/DocumentMutationHelpers.h"
#include "control/ScrollHandler.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/jobs/ImageExport.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "control/zoom/ZoomControl.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/LineStyle.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/StrokeStyle.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/StringUtils.h"

namespace xoj::mcp {

namespace {

auto isStrictInt(const nlohmann::json& j) -> bool {
    return (j.is_number_integer() || j.is_number_unsigned()) && !j.is_boolean();
}

auto isFiniteNumber(const nlohmann::json& j, double& out) -> bool {
    if (!j.is_number() || j.is_boolean()) {
        return false;
    }
    double val = j.get<double>();
    if (!std::isfinite(val)) {
        return false;
    }
    out = val;
    return true;
}

auto validatePageNumber1Based(const nlohmann::json& j, size_t pageCount, size_t& outZeroBased,
                              std::string& err) -> bool {
    if (pageCount == 0) {
        err = "The document has no pages";
        return false;
    }
    if (!isStrictInt(j)) {
        err = "Parameter 'page' must be an integer";
        return false;
    }
    int64_t p = j.get<int64_t>();
    if (p < 1 || static_cast<uint64_t>(p) > pageCount) {
        err = "Page " + std::to_string(p) + " is out of range (1.." + std::to_string(pageCount) + ")";
        return false;
    }
    outZeroBased = static_cast<size_t>(p - 1);
    return true;
}

auto isWithinPageBounds(double x, double y, double pageWidth, double pageHeight) -> bool {
    return x >= -PAGE_COORD_MARGIN_PT && x <= pageWidth + PAGE_COORD_MARGIN_PT && y >= -PAGE_COORD_MARGIN_PT &&
           y <= pageHeight + PAGE_COORD_MARGIN_PT;
}

auto shapeTypeToString(ShapeType type) -> const char* {
    switch (type) {
        case ShapeType::LINE:
            return "line";
        case ShapeType::POLYGON:
            return "polygon";
        case ShapeType::RECTANGLE:
            return "rectangle";
        case ShapeType::ELLIPSE:
            return "ellipse";
    }
    return "unknown";
}

auto lineStyleToApiString(const LineStyle& style) -> std::string {
    if (!style.hasDashes()) {
        return "solid";
    }
    std::string formatted = StrokeStyle::formatStyle(style);
    if (formatted == "plain") {
        return "solid";
    }
    return formatted;
}

auto rectToJson(const xoj::util::Rectangle<double>& r) -> nlohmann::json {
    return {{"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}};
}

void collectElementIntoLayerJson(const Element* elem, bool includePoints, size_t& strokesListed,
                                 size_t& pointsReturned, bool& strokesTruncated, bool& pointsTruncated,
                                 nlohmann::json& textsArr, nlohmann::json& shapesArr, nlohmann::json& strokesItems,
                                 size_t& plainStrokeCount, nlohmann::json& imagesArr, nlohmann::json& latexArr) {
    if (!elem) {
        return;
    }
    switch (elem->getType()) {
        case ELEMENT_TEXT: {
            const auto* text = static_cast<const Text*>(elem);
            const auto& origin = text->getOrigin();
            const auto& bbox = text->getBoundingBox();
            textsArr.push_back({
                    {"text", text->getText()},
                    {"x", origin.x},
                    {"y", origin.y},
                    {"width", bbox.width},
                    {"height", bbox.height},
                    {"font", {{"name", text->getFontName()}, {"size", text->getFontSize()}}},
                    {"color", formatColorHex(text->getColor())},
            });
            break;
        }
        case ELEMENT_STROKE: {
            const auto* stroke = static_cast<const Stroke*>(elem);
            const auto& bbox = stroke->getBoundingBox();
            if (const auto& shape = stroke->getShape(); shape.has_value()) {
                nlohmann::json ctrlPts = nlohmann::json::array();
                for (const auto& pt: shape->pts) {
                    ctrlPts.push_back(nlohmann::json::array({pt.x, pt.y}));
                }
                nlohmann::json shapeObj = {
                        {"type", shapeTypeToString(shape->type)},
                        {"control_points", std::move(ctrlPts)},
                        {"bounds", rectToJson(bbox)},
                        {"stroke_color", formatColorHex(stroke->getColor())},
                        {"width", stroke->getWidth()},
                        {"line_style", lineStyleToApiString(stroke->getLineStyle())},
                        {"fill_opacity", stroke->getFill()},
                };
                if (const auto& fc = stroke->getFillColor(); fc.has_value()) {
                    shapeObj["fill_color"] = formatColorHex(*fc);
                } else {
                    shapeObj["fill_color"] = nullptr;
                }
                shapesArr.push_back(std::move(shapeObj));
            } else {
                ++plainStrokeCount;
                if (strokesListed < MAX_CONTENT_STROKES_LISTED) {
                    ++strokesListed;
                    nlohmann::json sObj = {
                            {"bounds", rectToJson(bbox)},
                            {"color", formatColorHex(stroke->getColor())},
                            {"width", stroke->getWidth()},
                            {"point_count", stroke->getPointCount()},
                    };
                    if (includePoints) {
                        nlohmann::json pts = nlohmann::json::array();
                        for (const auto& p: stroke->getPointVector()) {
                            if (pointsReturned >= MAX_CONTENT_POINTS_RETURNED) {
                                pointsTruncated = true;
                                break;
                            }
                            pts.push_back(nlohmann::json::array({p.x, p.y}));
                            ++pointsReturned;
                        }
                        sObj["points"] = std::move(pts);
                    }
                    strokesItems.push_back(std::move(sObj));
                } else {
                    strokesTruncated = true;
                }
            }
            break;
        }
        case ELEMENT_IMAGE:
            imagesArr.push_back(rectToJson(elem->getBoundingBox()));
            break;
        case ELEMENT_TEXIMAGE:
            latexArr.push_back(rectToJson(elem->getBoundingBox()));
            break;
        default:
            break;
    }
}

auto handleGetDocumentInfo(Control* control) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    nlohmann::json out;
    {
        std::shared_lock<Document> lock(*doc);
        fs::path fp = doc->getFilepath();
        out["file_path"] = fp.empty() ? std::string("unsaved") : std::string(char_cast(fp.u8string().c_str()));

        fs::path pdfFp = doc->getPdfFilepath();
        if (!pdfFp.empty()) {
            out["pdf_background_path"] = std::string(char_cast(pdfFp.u8string().c_str()));
        }

        const size_t pageCount = doc->getPageCount();
        const size_t curPageIdx = control->getCurrentPageNo();
        out["page_count"] = pageCount;
        out["current_page"] = pageCount > 0 ? curPageIdx + 1 : 0;

        nlohmann::json pagesArr = nlohmann::json::array();
        for (size_t i = 0; i < pageCount; ++i) {
            PageRef p = doc->getPage(i);
            pagesArr.push_back({
                    {"page", i + 1},
                    {"width", p->getWidth()},
                    {"height", p->getHeight()},
                    {"layer_count", p->getLayerCount()},
                    {"current_layer", p->getSelectedLayerId()},
                    {"is_annotated", p->isAnnotated()},
            });
        }
        out["pages"] = std::move(pagesArr);

        if (pageCount > 0 && curPageIdx < pageCount) {
            out["current_layer"] = doc->getPage(curPageIdx)->getSelectedLayerId();
        } else {
            out["current_layer"] = 0;
        }
    }

    out["zoom"] = control->getZoomControl() ? control->getZoomControl()->getZoom() : 1.0;

    if (ToolHandler* th = control->getToolHandler()) {
        out["active_tool"] = std::string(toolTypeToString(th->getToolType()));
        out["color"] = formatColorHex(th->getColor());
        out["size"] = std::string(toolSizeToString(th->getSize()));
    }

    nlohmann::json selJson = nullptr;
    if (MainWindow* win = control->getWindow()) {
        if (XournalView* xv = win->getXournal()) {
            if (EditSelection* sel = xv->getSelection()) {
                size_t textCount = 0;
                size_t shapeCount = 0;
                size_t strokeCount = 0;
                size_t imageCount = 0;
                size_t latexCount = 0;
                size_t total = 0;
                bool hasBounds = false;
                double minX = 0.0;
                double minY = 0.0;
                double maxX = 0.0;
                double maxY = 0.0;

                for (const Element* e: sel->getElementsView()) {
                    ++total;
                    const auto& bb = e->getBoundingBox();
                    if (!hasBounds) {
                        minX = bb.x;
                        minY = bb.y;
                        maxX = bb.x + bb.width;
                        maxY = bb.y + bb.height;
                        hasBounds = true;
                    } else {
                        minX = std::min(minX, bb.x);
                        minY = std::min(minY, bb.y);
                        maxX = std::max(maxX, bb.x + bb.width);
                        maxY = std::max(maxY, bb.y + bb.height);
                    }
                    switch (e->getType()) {
                        case ELEMENT_TEXT:
                            ++textCount;
                            break;
                        case ELEMENT_STROKE:
                            if (static_cast<const Stroke*>(e)->getShape().has_value()) {
                                ++shapeCount;
                            } else {
                                ++strokeCount;
                            }
                            break;
                        case ELEMENT_IMAGE:
                            ++imageCount;
                            break;
                        case ELEMENT_TEXIMAGE:
                            ++latexCount;
                            break;
                        default:
                            break;
                    }
                }

                std::optional<size_t> selPage;
                {
                    std::shared_lock<Document> lock(*doc);
                    size_t idx = doc->indexOf(sel->getSourcePage());
                    if (idx != npos) {
                        selPage = idx + 1;
                    }
                }

                selJson = {
                        {"count", total},
                        {"page", selPage.has_value() ? nlohmann::json(*selPage) : nlohmann::json(nullptr)},
                        {"types",
                         {{"text", textCount},
                          {"shape", shapeCount},
                          {"stroke", strokeCount},
                          {"image", imageCount},
                          {"latex", latexCount}}},
                };
                if (hasBounds) {
                    selJson["bounds"] = {{"x", minX}, {"y", minY}, {"width", maxX - minX}, {"height", maxY - minY}};
                }
            }
        }
    }
    out["selection"] = std::move(selJson);

    return ToolCallResult::jsonText(out);
}

auto handleGetPageImage(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }
    auto validated = validatePageImageArgs(args, pageCount, control->getCurrentPageNo());
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<PageImageArgs>(validated);

    std::vector<uint8_t> pngBytes;
    std::string err;
    if (!ImageExport::renderPageToPngBytes(doc, params.pageIndex, params.maxSizePx, EXPORT_BACKGROUND_ALL, pngBytes,
                                           err)) {
        return ToolCallResult::text(err.empty() ? "Failed to render page image" : err, true);
    }

    gchar* b64 = g_base64_encode(pngBytes.data(), pngBytes.size());
    std::string encoded(b64 ? b64 : "");
    g_free(b64);
    return ToolCallResult::pngImage(std::move(encoded));
}

auto handleGetPageContent(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }
    auto validated = validatePageContentArgs(args, pageCount, control->getCurrentPageNo());
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<PageContentArgs>(validated);

    nlohmann::json out;
    size_t strokesListed = 0;
    size_t pointsReturned = 0;
    bool strokesTruncated = false;
    bool pointsTruncated = false;

    PageRef page;
    {
        std::shared_lock<Document> lock(*doc);
        if (params.pageIndex >= doc->getPageCount()) {
            return ToolCallResult::text("Page index out of range", true);
        }
        page = doc->getPage(params.pageIndex);
        out["page"] = params.pageIndex + 1;
        out["width"] = page->getWidth();
        out["height"] = page->getHeight();

        const size_t selectedLayerId = page->getSelectedLayerId();
        nlohmann::json layersArr = nlohmann::json::array();
        size_t layerNo = 1;
        for (const Layer* l: page->getLayersView()) {
            nlohmann::json textsArr = nlohmann::json::array();
            nlohmann::json shapesArr = nlohmann::json::array();
            nlohmann::json strokesItems = nlohmann::json::array();
            nlohmann::json imagesArr = nlohmann::json::array();
            nlohmann::json latexArr = nlohmann::json::array();
            size_t plainStrokeCount = 0;

            for (const Element* elem: l->getElementsView()) {
                collectElementIntoLayerJson(elem, params.includePoints, strokesListed, pointsReturned, strokesTruncated,
                                            pointsTruncated, textsArr, shapesArr, strokesItems, plainStrokeCount,
                                            imagesArr, latexArr);
            }

            layersArr.push_back({
                    {"layer", layerNo},
                    {"name", l->getName()},
                    {"visible", l->isVisible()},
                    {"selected", layerNo == selectedLayerId},
                    {"texts", std::move(textsArr)},
                    {"shapes", std::move(shapesArr)},
                    {"strokes", {{"count", plainStrokeCount}, {"items", std::move(strokesItems)}}},
                    {"images", std::move(imagesArr)},
                    {"latex", std::move(latexArr)},
            });
            ++layerNo;
        }
        out["layers"] = std::move(layersArr);
    }

    // Include elements currently held in an active selection on this page (read-only, without clearing selection)
    if (MainWindow* win = control->getWindow()) {
        if (XournalView* xv = win->getXournal()) {
            if (EditSelection* sel = xv->getSelection(); sel && sel->getSourcePage() == page) {
                nlohmann::json textsArr = nlohmann::json::array();
                nlohmann::json shapesArr = nlohmann::json::array();
                nlohmann::json strokesItems = nlohmann::json::array();
                nlohmann::json imagesArr = nlohmann::json::array();
                nlohmann::json latexArr = nlohmann::json::array();
                size_t plainStrokeCount = 0;
                for (const Element* elem: sel->getElementsView()) {
                    collectElementIntoLayerJson(elem, params.includePoints, strokesListed, pointsReturned,
                                                strokesTruncated, pointsTruncated, textsArr, shapesArr, strokesItems,
                                                plainStrokeCount, imagesArr, latexArr);
                }
                out["selected_elements"] = {
                        {"texts", std::move(textsArr)},
                        {"shapes", std::move(shapesArr)},
                        {"strokes", {{"count", plainStrokeCount}, {"items", std::move(strokesItems)}}},
                        {"images", std::move(imagesArr)},
                        {"latex", std::move(latexArr)},
                };
            }
        }
    }

    if (strokesTruncated) {
        out["strokes_truncated"] = true;
    }
    if (pointsTruncated) {
        out["points_truncated"] = true;
    }

    return ToolCallResult::jsonText(out);
}

auto makePageSizeLookup(Document* doc) -> std::function<std::pair<double, double>(size_t)> {
    return [doc](size_t pageIdx) -> std::pair<double, double> {
        std::shared_lock<Document> lock(*doc);
        PageRef p = doc->getPage(pageIdx);
        return {p->getWidth(), p->getHeight()};
    };
}

auto handleAddText(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }

    auto validated = validateAddTextArgs(args, pageCount, makePageSizeLookup(doc));
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<AddTextArgs>(validated);

    PageRef page;
    Layer* layer = nullptr;
    {
        std::shared_lock<Document> lock(*doc);
        page = doc->getPage(params.pageIndex);
        layer = page->getSelectedLayer();
    }
    if (!page || !layer) {
        return ToolCallResult::text("Target page or layer is unavailable", true);
    }

    Settings* settings = control->getSettings();
    XojFont font = settings ? settings->getFont() : XojFont{};
    if (params.fontSize.has_value()) {
        font.setSize(*params.fontSize);
    }

    Color color = Colors::black;
    if (params.color.has_value()) {
        color = *params.color;
    } else if (ToolHandler* th = control->getToolHandler()) {
        color = th->getTool(TOOL_TEXT).getColor();
    }

    auto textElem = xoj::control::createTextElement(params.text, font, color, params.x, params.y);
    const auto& bbox = textElem->getBoundingBox();
    const double width = bbox.width;
    const double height = bbox.height;

    std::vector<ElementPtr> batch;
    batch.push_back(std::move(textElem));
    xoj::control::insertElementsWithGroupedUndo(control, page, layer, std::move(batch), true);

    return ToolCallResult::jsonText({
            {"ok", true},
            {"page", params.pageIndex + 1},
            {"x", params.x},
            {"y", params.y},
            {"width", width},
            {"height", height},
            {"color", formatColorHex(color)},
            {"font_size", font.getSize()},
    });
}

auto handleAddStrokes(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }

    auto validated = validateAddStrokesArgs(args, pageCount, makePageSizeLookup(doc));
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<AddStrokesArgs>(validated);

    PageRef page;
    Layer* layer = nullptr;
    {
        std::shared_lock<Document> lock(*doc);
        page = doc->getPage(params.pageIndex);
        layer = page->getSelectedLayer();
    }
    if (!page || !layer) {
        return ToolCallResult::text("Target page or layer is unavailable", true);
    }

    Color defaultColor = Colors::black;
    double defaultWidth = 1.41;
    if (ToolHandler* th = control->getToolHandler()) {
        defaultColor = th->getTool(TOOL_PEN).getColor();
        defaultWidth = th->getToolThickness(TOOL_PEN)[th->getPenSize()];
    }

    std::vector<ElementPtr> elements;
    elements.reserve(params.strokes.size());
    size_t totalPoints = 0;
    for (const auto& spec: params.strokes) {
        auto stroke = std::make_unique<Stroke>();
        stroke->setToolType(StrokeTool::PEN);
        stroke->setColor(spec.color.value_or(defaultColor));
        stroke->setWidth(spec.width.value_or(defaultWidth));
        stroke->setFill(-1);
        for (const auto& pt: spec.points) {
            stroke->addPoint(Point(pt.x, pt.y, Point::NO_PRESSURE));
        }
        totalPoints += spec.points.size();
        elements.push_back(std::move(stroke));
    }

    xoj::control::insertElementsWithGroupedUndo(control, page, layer, std::move(elements), true);

    return ToolCallResult::jsonText({
            {"ok", true},
            {"page", params.pageIndex + 1},
            {"strokes_added", params.strokes.size()},
            {"total_points", totalPoints},
    });
}

auto handleAddShape(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }

    auto validated = validateAddShapeArgs(args, pageCount, makePageSizeLookup(doc));
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<AddShapeArgs>(validated);

    PageRef page;
    Layer* layer = nullptr;
    {
        std::shared_lock<Document> lock(*doc);
        page = doc->getPage(params.pageIndex);
        layer = page->getSelectedLayer();
    }
    if (!page || !layer) {
        return ToolCallResult::text("Target page or layer is unavailable", true);
    }

    Color defaultColor = Colors::black;
    double defaultWidth = 1.41;
    if (ToolHandler* th = control->getToolHandler()) {
        defaultColor = th->getTool(TOOL_PEN).getColor();
        defaultWidth = th->getToolThickness(TOOL_PEN)[th->getPenSize()];
    }

    auto stroke = std::make_unique<Stroke>();
    stroke->setToolType(StrokeTool::PEN);
    stroke->setColor(params.strokeColor.value_or(defaultColor));
    stroke->setWidth(params.width.value_or(defaultWidth));

    if (params.lineStyle == "solid") {
        stroke->setLineStyle(LineStyle{});
    } else {
        stroke->setLineStyle(StrokeStyle::parseStyle(params.lineStyle));
    }

    if (params.shapeInfo.type != ShapeType::LINE) {
        int opacity = -1;
        if (params.fillOpacity.has_value()) {
            opacity = *params.fillOpacity == 0 ? -1 : *params.fillOpacity;
        } else if (params.fillColor.has_value()) {
            opacity = 128;
        }
        stroke->setFill(opacity);
        if (opacity > 0 && params.fillColor.has_value()) {
            stroke->setFillColor(*params.fillColor);
        }
    } else {
        stroke->setFill(-1);
    }

    stroke->setShape(params.shapeInfo);
    const auto bbox = stroke->getBoundingBox();

    std::vector<ElementPtr> elements;
    elements.push_back(std::move(stroke));
    xoj::control::insertElementsWithGroupedUndo(control, page, layer, std::move(elements), true);

    return ToolCallResult::jsonText({
            {"ok", true},
            {"page", params.pageIndex + 1},
            {"type", params.typeName},
            {"bounds", rectToJson(bbox)},
    });
}

auto handleAddPage(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }

    auto validated = validateAddPageArgs(args, pageCount, control->getCurrentPageNo());
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<AddPageArgs>(validated);

    const auto& tmpl = control->getSettings()->getPageTemplateSettings();
    double width = tmpl.getPageWidth();
    double height = tmpl.getPageHeight();
    PageType bgType = tmpl.getBackgroundType();
    if (bgType.isPdfPage() || bgType.isImagePage()) {
        // Avoid spawning a modal file/PDF chooser dialog from an MCP tool call
        bgType = PageType(PageTypeFormat::Plain);
    }

    auto newPage = std::make_shared<XojPage>(width, height);
    newPage->setBackgroundColor(tmpl.getBackgroundColor());
    newPage->setBackgroundType(bgType);

    control->insertPage(newPage, params.insertPosition, false);

    return ToolCallResult::jsonText({
            {"ok", true},
            {"page", params.insertPosition + 1},
            {"page_count", pageCount + 1},
            {"width", width},
            {"height", height},
    });
}

auto handleGoToPage(Control* control, const nlohmann::json& args) -> ToolCallResult {
    Document* doc = control->getDocument();
    if (!doc) {
        return ToolCallResult::text("No document is open", true);
    }

    size_t pageCount = 0;
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
    }

    auto validated = validateGoToPageArgs(args, pageCount);
    if (std::holds_alternative<std::string>(validated)) {
        return ToolCallResult::text(std::get<std::string>(validated), true);
    }
    const auto& params = std::get<GoToPageArgs>(validated);

    if (ScrollHandler* sh = control->getScrollHandler()) {
        sh->scrollToPage(params.pageIndex);
    }
    control->firePageSelected(params.pageIndex);

    return ToolCallResult::jsonText({{"ok", true}, {"page", params.pageIndex + 1}});
}

}  // namespace

auto parseHexColor(std::string_view s) -> std::optional<Color> {
    if (s.size() != 7 || s[0] != '#') {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (size_t i = 1; i < 7; ++i) {
        char c = s[i];
        uint32_t nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<uint32_t>(10 + (c - 'a'));
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<uint32_t>(10 + (c - 'A'));
        } else {
            return std::nullopt;
        }
        value = (value << 4U) | nibble;
    }
    return Color(value | 0xff000000U);
}

auto formatColorHex(Color c) -> std::string {
    char buf[8];
    uint32_t rgb = static_cast<uint32_t>(c) & 0x00ffffffU;
    std::snprintf(buf, sizeof(buf), "#%06x", rgb);
    return std::string(buf);
}

auto validatePageImageArgs(const nlohmann::json& args, size_t pageCount, size_t currentPageIndex)
        -> ValidationResult<PageImageArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }
    if (pageCount == 0) {
        return std::string("The document has no pages");
    }

    PageImageArgs out;
    out.pageIndex = std::min(currentPageIndex, pageCount - 1);

    if (args.contains("page") && !args["page"].is_null()) {
        std::string err;
        if (!validatePageNumber1Based(args["page"], pageCount, out.pageIndex, err)) {
            return err;
        }
    }

    if (args.contains("max_size_px") && !args["max_size_px"].is_null()) {
        if (!isStrictInt(args["max_size_px"])) {
            return std::string("Parameter 'max_size_px' must be an integer");
        }
        int64_t val = args["max_size_px"].get<int64_t>();
        out.maxSizePx = static_cast<int>(
                std::clamp<int64_t>(val, MIN_PAGE_IMAGE_MAX_PX, MAX_PAGE_IMAGE_MAX_PX));
    }

    return out;
}

auto validatePageContentArgs(const nlohmann::json& args, size_t pageCount, size_t currentPageIndex)
        -> ValidationResult<PageContentArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }
    if (pageCount == 0) {
        return std::string("The document has no pages");
    }

    PageContentArgs out;
    out.pageIndex = std::min(currentPageIndex, pageCount - 1);

    if (args.contains("page") && !args["page"].is_null()) {
        std::string err;
        if (!validatePageNumber1Based(args["page"], pageCount, out.pageIndex, err)) {
            return err;
        }
    }

    if (args.contains("include_points") && !args["include_points"].is_null()) {
        if (!args["include_points"].is_boolean()) {
            return std::string("Parameter 'include_points' must be a boolean");
        }
        out.includePoints = args["include_points"].get<bool>();
    }

    return out;
}

auto validateAddTextArgs(const nlohmann::json& args, size_t pageCount,
                         const std::function<std::pair<double, double>(size_t)>& pageSizeLookup)
        -> ValidationResult<AddTextArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }

    AddTextArgs out;
    if (!args.contains("page")) {
        return std::string("Missing required parameter 'page'");
    }
    std::string err;
    if (!validatePageNumber1Based(args["page"], pageCount, out.pageIndex, err)) {
        return err;
    }

    if (!args.contains("x") || !isFiniteNumber(args["x"], out.x)) {
        return std::string("Parameter 'x' must be a finite number");
    }
    if (!args.contains("y") || !isFiniteNumber(args["y"], out.y)) {
        return std::string("Parameter 'y' must be a finite number");
    }

    auto [pageW, pageH] = pageSizeLookup(out.pageIndex);
    if (!isWithinPageBounds(out.x, out.y, pageW, pageH)) {
        return std::string("Coordinates (x, y) are outside the page bounds");
    }

    if (!args.contains("text") || !args["text"].is_string()) {
        return std::string("Parameter 'text' must be a string");
    }
    out.text = args["text"].get<std::string>();
    if (out.text.empty()) {
        return std::string("Parameter 'text' must not be empty");
    }
    if (out.text.size() > MAX_TEXT_BYTES) {
        return std::string("Parameter 'text' exceeds maximum length of 65536 bytes");
    }
    if (!g_utf8_validate(out.text.data(), static_cast<gssize>(out.text.size()), nullptr)) {
        return std::string("Parameter 'text' must be valid UTF-8");
    }

    if (args.contains("color") && !args["color"].is_null()) {
        if (!args["color"].is_string()) {
            return std::string("Parameter 'color' must be a #rrggbb hex string");
        }
        auto parsed = parseHexColor(args["color"].get<std::string>());
        if (!parsed.has_value()) {
            return std::string("Invalid color format; expected #rrggbb");
        }
        out.color = *parsed;
    }

    if (args.contains("font_size") && !args["font_size"].is_null()) {
        double fs = 0.0;
        if (!isFiniteNumber(args["font_size"], fs) || fs < MIN_FONT_SIZE_PT || fs > MAX_FONT_SIZE_PT) {
            return std::string("Parameter 'font_size' must be a finite number between 4 and 200");
        }
        out.fontSize = fs;
    }

    return out;
}

auto validateAddStrokesArgs(const nlohmann::json& args, size_t pageCount,
                            const std::function<std::pair<double, double>(size_t)>& pageSizeLookup)
        -> ValidationResult<AddStrokesArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }

    AddStrokesArgs out;
    if (!args.contains("page")) {
        return std::string("Missing required parameter 'page'");
    }
    std::string err;
    if (!validatePageNumber1Based(args["page"], pageCount, out.pageIndex, err)) {
        return err;
    }

    if (!args.contains("strokes") || !args["strokes"].is_array() || args["strokes"].empty()) {
        return std::string("Parameter 'strokes' must be a non-empty array");
    }
    if (args["strokes"].size() > MAX_STROKES_PER_CALL) {
        return std::string("Too many strokes in one call (maximum " + std::to_string(MAX_STROKES_PER_CALL) + ")");
    }

    auto [pageW, pageH] = pageSizeLookup(out.pageIndex);
    size_t totalPoints = 0;
    out.strokes.reserve(args["strokes"].size());

    for (size_t sIdx = 0; sIdx < args["strokes"].size(); ++sIdx) {
        const auto& sObj = args["strokes"][sIdx];
        if (!sObj.is_object()) {
            return std::string("Each entry in 'strokes' must be an object");
        }
        if (!sObj.contains("points") || !sObj["points"].is_array() || sObj["points"].size() < 2) {
            return std::string("Each stroke must have a 'points' array with at least 2 [x, y] pairs");
        }

        totalPoints += sObj["points"].size();
        if (totalPoints > MAX_STROKE_POINTS_PER_CALL) {
            return std::string("Total stroke points exceed the limit of " +
                               std::to_string(MAX_STROKE_POINTS_PER_CALL));
        }

        StrokeSpec spec;
        spec.points.reserve(sObj["points"].size());
        for (const auto& ptJson: sObj["points"]) {
            if (!ptJson.is_array() || ptJson.size() != 2) {
                return std::string("Each point must be a [x, y] pair");
            }
            double px = 0.0;
            double py = 0.0;
            if (!isFiniteNumber(ptJson[0], px) || !isFiniteNumber(ptJson[1], py)) {
                return std::string("Stroke point coordinates must be finite numbers");
            }
            if (!isWithinPageBounds(px, py, pageW, pageH)) {
                return std::string("Stroke point coordinates are outside the page bounds");
            }
            spec.points.emplace_back(px, py);
        }

        if (sObj.contains("color") && !sObj["color"].is_null()) {
            if (!sObj["color"].is_string()) {
                return std::string("Stroke 'color' must be a #rrggbb hex string");
            }
            auto parsed = parseHexColor(sObj["color"].get<std::string>());
            if (!parsed.has_value()) {
                return std::string("Invalid stroke color format; expected #rrggbb");
            }
            spec.color = *parsed;
        }

        if (sObj.contains("width") && !sObj["width"].is_null()) {
            double w = 0.0;
            if (!isFiniteNumber(sObj["width"], w) || w < MIN_STROKE_WIDTH_PT || w > MAX_STROKE_WIDTH_PT) {
                return std::string("Stroke 'width' must be a finite number between 0.1 and 100");
            }
            spec.width = w;
        }

        out.strokes.push_back(std::move(spec));
    }

    return out;
}

auto validateAddShapeArgs(const nlohmann::json& args, size_t pageCount,
                          const std::function<std::pair<double, double>(size_t)>& pageSizeLookup)
        -> ValidationResult<AddShapeArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }

    AddShapeArgs out;
    if (!args.contains("page")) {
        return std::string("Missing required parameter 'page'");
    }
    std::string err;
    if (!validatePageNumber1Based(args["page"], pageCount, out.pageIndex, err)) {
        return err;
    }

    if (!args.contains("type") || !args["type"].is_string()) {
        return std::string("Parameter 'type' must be one of: line, rectangle, ellipse");
    }
    out.typeName = args["type"].get<std::string>();

    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    if (!args.contains("x1") || !isFiniteNumber(args["x1"], x1) || !args.contains("y1") ||
        !isFiniteNumber(args["y1"], y1) || !args.contains("x2") || !isFiniteNumber(args["x2"], x2) ||
        !args.contains("y2") || !isFiniteNumber(args["y2"], y2)) {
        return std::string("Parameters 'x1', 'y1', 'x2', 'y2' must be finite numbers");
    }

    auto [pageW, pageH] = pageSizeLookup(out.pageIndex);
    if (!isWithinPageBounds(x1, y1, pageW, pageH) || !isWithinPageBounds(x2, y2, pageW, pageH)) {
        return std::string("Shape coordinates are outside the page bounds");
    }

    if (out.typeName == "line") {
        out.shapeInfo = ShapeInfo{ShapeType::LINE, {{x1, y1}, {x2, y2}}};
    } else if (out.typeName == "rectangle") {
        double minX = std::min(x1, x2);
        double maxX = std::max(x1, x2);
        double minY = std::min(y1, y2);
        double maxY = std::max(y1, y2);
        out.shapeInfo = ShapeInfo{ShapeType::RECTANGLE, {{minX, minY}, {minX, maxY}, {maxX, minY}}};
    } else if (out.typeName == "ellipse") {
        double cx = 0.5 * (x1 + x2);
        double cy = 0.5 * (y1 + y2);
        double rx = 0.5 * std::abs(x2 - x1);
        double ry = 0.5 * std::abs(y2 - y1);
        out.shapeInfo = ShapeInfo{ShapeType::ELLIPSE, {{cx, cy}, {cx + rx, cy}, {cx, cy + ry}}};
    } else {
        return std::string("Invalid shape type '" + out.typeName + "'; expected line, rectangle, or ellipse");
    }

    if (!out.shapeInfo.isValid()) {
        return std::string("Degenerate shape geometry (endpoints coincide or zero width/height)");
    }

    if (args.contains("stroke_color") && !args["stroke_color"].is_null()) {
        if (!args["stroke_color"].is_string()) {
            return std::string("Parameter 'stroke_color' must be a #rrggbb hex string");
        }
        auto parsed = parseHexColor(args["stroke_color"].get<std::string>());
        if (!parsed.has_value()) {
            return std::string("Invalid stroke_color format; expected #rrggbb");
        }
        out.strokeColor = *parsed;
    }

    if (args.contains("width") && !args["width"].is_null()) {
        double w = 0.0;
        if (!isFiniteNumber(args["width"], w) || w < MIN_STROKE_WIDTH_PT || w > MAX_STROKE_WIDTH_PT) {
            return std::string("Parameter 'width' must be a finite number between 0.1 and 100");
        }
        out.width = w;
    }

    if (args.contains("fill_color") && !args["fill_color"].is_null()) {
        if (!args["fill_color"].is_string()) {
            return std::string("Parameter 'fill_color' must be a #rrggbb hex string");
        }
        auto parsed = parseHexColor(args["fill_color"].get<std::string>());
        if (!parsed.has_value()) {
            return std::string("Invalid fill_color format; expected #rrggbb");
        }
        out.fillColor = *parsed;
    }

    if (args.contains("fill_opacity") && !args["fill_opacity"].is_null()) {
        if (!isStrictInt(args["fill_opacity"])) {
            return std::string("Parameter 'fill_opacity' must be an integer between 0 and 255");
        }
        int64_t op = args["fill_opacity"].get<int64_t>();
        if (op < 0 || op > 255) {
            return std::string("Parameter 'fill_opacity' must be between 0 and 255");
        }
        out.fillOpacity = static_cast<int>(op);
    }

    if (args.contains("line_style") && !args["line_style"].is_null()) {
        if (!args["line_style"].is_string()) {
            return std::string("Parameter 'line_style' must be a string");
        }
        std::string ls = args["line_style"].get<std::string>();
        if (ls != "solid" && ls != "dash" && ls != "dashdot" && ls != "dot") {
            return std::string("Invalid line_style '" + ls + "'; expected solid, dash, dashdot, or dot");
        }
        out.lineStyle = std::move(ls);
    }

    return out;
}

auto validateAddPageArgs(const nlohmann::json& args, size_t pageCount, size_t currentPageIndex)
        -> ValidationResult<AddPageArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }

    AddPageArgs out;
    if (args.contains("after") && !args["after"].is_null()) {
        if (!isStrictInt(args["after"])) {
            return std::string("Parameter 'after' must be an integer");
        }
        int64_t after = args["after"].get<int64_t>();
        if (after < 0 || static_cast<uint64_t>(after) > pageCount) {
            return std::string("Parameter 'after' (" + std::to_string(after) + ") is out of range (0.." +
                               std::to_string(pageCount) + ")");
        }
        out.insertPosition = static_cast<size_t>(after);
    } else {
        out.insertPosition = std::min(currentPageIndex + 1, pageCount);
    }

    return out;
}

auto validateGoToPageArgs(const nlohmann::json& args, size_t pageCount) -> ValidationResult<GoToPageArgs> {
    if (!args.is_object()) {
        return std::string("Arguments must be a JSON object");
    }
    if (!args.contains("page")) {
        return std::string("Missing required parameter 'page'");
    }
    GoToPageArgs out;
    std::string err;
    if (!validatePageNumber1Based(args["page"], pageCount, out.pageIndex, err)) {
        return err;
    }
    return out;
}

auto isWriteTool(std::string_view toolName) -> bool {
    return toolName == "add_text" || toolName == "add_strokes" || toolName == "add_shape" || toolName == "add_page" ||
           toolName == "go_to_page";
}

auto executeMcpToolOnMainThread(Control* control, const std::string& toolName, const nlohmann::json& args,
                                bool allowEdits) -> ToolCallResult {
    if (!control) {
        return ToolCallResult::text("MoreXournal controller is unavailable", true);
    }

    if (isWriteTool(toolName) && !allowEdits) {
        return ToolCallResult::text(std::string(EDITS_DISABLED_ERROR), true);
    }

    if (toolName == "get_document_info") {
        return handleGetDocumentInfo(control);
    }
    if (toolName == "get_page_image") {
        return handleGetPageImage(control, args);
    }
    if (toolName == "get_page_content") {
        return handleGetPageContent(control, args);
    }
    if (toolName == "add_text") {
        return handleAddText(control, args);
    }
    if (toolName == "add_strokes") {
        return handleAddStrokes(control, args);
    }
    if (toolName == "add_shape") {
        return handleAddShape(control, args);
    }
    if (toolName == "add_page") {
        return handleAddPage(control, args);
    }
    if (toolName == "go_to_page") {
        return handleGoToPage(control, args);
    }

    return ToolCallResult::text("Unknown tool: " + toolName, true);
}

}  // namespace xoj::mcp
