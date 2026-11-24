#include "LinkHighlightView.h"

#include <algorithm>  // for max

#include "control/tools/LinkHandler.h"
#include "model/Link.h"
#include "util/Range.h"
#include "util/raii/CairoWrappers.h"
#include "view/Repaintable.h"  // for Repaintable

using namespace xoj::view;

constexpr double LINE_WIDTH_PX = 1.5;  // screen-space pixels
constexpr Color LINE_COLOR{Colors::mx_accent};

LinkHighlightView::LinkHighlightView(const LinkHandler* handler, Repaintable* parent):
        OverlayView(parent), handler(handler) {
    this->registerToPool(handler->getViewPool());
}

LinkHighlightView::~LinkHighlightView() { this->unregisterFromPool(); }

void LinkHighlightView::on(FlagDirtyRegionRequest, const Range& rg) {
    Range padded = rg;
    padded.addPadding(LINE_WIDTH_PX / this->parent->getZoom());
    this->parent->flagDirtyRegion(padded);
}

void LinkHighlightView::deleteOn(FinalizationRequest, const Range& rg) {
    Range padded = rg;
    padded.addPadding(LINE_WIDTH_PX / this->parent->getZoom());
    this->parent->deleteOverlayView(this, padded);
}

void LinkHighlightView::draw(cairo_t* cr) const {
    xoj::util::CairoSaveGuard saveGuard(cr);
    const double lineWidth = LINE_WIDTH_PX / this->parent->getZoom();
    for (const auto& optRect: {handler->getHighlightRect(), handler->getSelectRect()}) {
        if (optRect.has_value()) {
            auto rect = optRect.value();
            Util::cairo_set_source_rgbi(cr, LINE_COLOR);
            cairo_rectangle(cr, rect.x + lineWidth / 2.0, rect.y + lineWidth / 2.0,
                            std::max(0.0, rect.width - lineWidth), std::max(0.0, rect.height - lineWidth));
            cairo_set_line_width(cr, lineWidth);
            cairo_stroke(cr);
        }
    }
}

bool LinkHighlightView::isViewOf(const OverlayBase* overlay) const { return overlay == this->handler; }
