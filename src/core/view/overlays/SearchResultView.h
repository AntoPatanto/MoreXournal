/*
 * Xournal++
 *
 * View highlighting search results
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cairo.h>  // for cairo_t

#include "util/Color.h"
#include "util/DispatchPool.h"  // for Listener
#include "view/overlays/OverlayView.h"

class OverlayBase;
class SearchControl;

namespace xoj::view {
class Repaintable;

class SearchResultView final: public OverlayView, public xoj::util::Listener<SearchResultView> {

public:
    SearchResultView(const SearchControl* searchControl, Repaintable* parent, Color frameColor,
                     Color frameHighlightColor);
    ~SearchResultView() noexcept override;

    /**
     * @brief Draws the overlay to the given context
     */
    void draw(cairo_t* cr) const override;

    bool isViewOf(const OverlayBase* overlay) const override;

    /**
     * Listener interface
     */
    static constexpr struct SearchChangedNotification {
    } SEARCH_CHANGED_NOTIFICATION = {};
    void on(SearchChangedNotification);

private:
    const SearchControl* searchControl;
    const Color frameColor;
    const Color frameHighlightColor;

public:
    // Width of the frame around the active search match in screen-space pixels
    static constexpr double CURRENT_BORDER_WIDTH_IN_PIXELS = 1.5;
    // Fill opacity of the active search match
    static constexpr double CURRENT_BACKGROUND_OPACITY = 0.35;
    // Fill opacity of non-active search matches
    static constexpr double BACKGROUND_OPACITY = 0.15;
};
};  // namespace xoj::view
