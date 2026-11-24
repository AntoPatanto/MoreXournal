#include "SearchResultView.h"

#include "control/SearchControl.h"
#include "util/Range.h"
#include "util/raii/CairoWrappers.h"
#include "view/Repaintable.h"

using namespace xoj::view;

SearchResultView::SearchResultView(const SearchControl* searchControl, Repaintable* parent, Color frameColor,
                                   Color frameHighlightColor):
        OverlayView(parent),
        searchControl(searchControl),
        frameColor(frameColor),
        frameHighlightColor(frameHighlightColor) {
    this->registerToPool(searchControl->getViewPool());
}

SearchResultView::~SearchResultView() noexcept { this->unregisterFromPool(); }

void SearchResultView::draw(cairo_t* cr) const {
    const XojPdfRectangle* highlightRect = this->searchControl->getHighlightRect();
    xoj::util::CairoSaveGuard saveGuard(cr);

    cairo_set_line_width(cr, CURRENT_BORDER_WIDTH_IN_PIXELS / this->parent->getZoom());

    for (const XojPdfRectangle& rect: this->searchControl->getResults()) {
        cairo_rectangle(cr, rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1);
        if (&rect == highlightRect) {
            Util::cairo_set_source_rgbi(cr, frameHighlightColor, CURRENT_BACKGROUND_OPACITY);
            cairo_fill_preserve(cr);
            Color borderColor = (frameHighlightColor == Colors::mx_search_current) ? Colors::mx_search_current_border :
                                                                                     frameHighlightColor;
            Util::cairo_set_source_rgbi(cr, borderColor);
            cairo_stroke(cr);
        } else {
            Util::cairo_set_source_rgbi(cr, frameColor, BACKGROUND_OPACITY);
            cairo_fill(cr);
        }
    }
}

bool SearchResultView::isViewOf(const OverlayBase* overlay) const { return overlay == this->searchControl; }

void SearchResultView::on(SearchResultView::SearchChangedNotification) {
    Range rg = this->parent->getVisiblePart();
    if (!rg.empty()) {
        this->parent->flagDirtyRegion(this->parent->getVisiblePart());
    }
}
