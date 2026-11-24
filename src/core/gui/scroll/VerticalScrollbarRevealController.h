/*
 * MoreXournal
 *
 * GTK controller that keeps the main canvas vertical scrollbar disabled in
 * overlay mode until sustained page scrolling occurs, then hides it after 1 s.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <unordered_set>

#include <gtk/gtk.h>

#include "VerticalScrollbarRevealLogic.h"

class VerticalScrollbarRevealController final {
public:
    VerticalScrollbarRevealController(GtkScrolledWindow* scrolledWindow, std::function<bool()> isZoomInProgress);
    ~VerticalScrollbarRevealController();

    VerticalScrollbarRevealController(const VerticalScrollbarRevealController&) = delete;
    auto operator=(const VerticalScrollbarRevealController&) -> VerticalScrollbarRevealController& = delete;
    VerticalScrollbarRevealController(VerticalScrollbarRevealController&&) = delete;
    auto operator=(VerticalScrollbarRevealController&&) -> VerticalScrollbarRevealController& = delete;

    void configure(bool hiddenByPreference, bool fadeoutDisabled);

    [[nodiscard]] auto isScrollbarVisible() const -> bool { return state.isVisible(); }

private:
    void applyAction(const xoj::scroll::VerticalScrollbarRevealAction& action);
    void updateInteractionState();
    void clearInteractionState();
    void cancelHideTimer();
    void armHideTimer();

    static void onScrolledWindowDestroy(GtkWidget* widget, VerticalScrollbarRevealController* self);
    static void onAdjustmentChanged(GtkAdjustment* adjustment, VerticalScrollbarRevealController* self);
    static void onAdjustmentValueChanged(GtkAdjustment* adjustment, VerticalScrollbarRevealController* self);
    static auto onScrollbarButtonPress(GtkWidget* widget, GdkEventButton* event,
                                       VerticalScrollbarRevealController* self) -> gboolean;
    static auto onScrollbarButtonRelease(GtkWidget* widget, GdkEventButton* event,
                                         VerticalScrollbarRevealController* self) -> gboolean;
    static auto onScrollbarTouchEvent(GtkWidget* widget, GdkEventTouch* event, VerticalScrollbarRevealController* self)
            -> gboolean;
    static void onScrollbarGrabNotify(GtkWidget* widget, gboolean wasGrabbed, VerticalScrollbarRevealController* self);
    static void onScrollbarHide(GtkWidget* widget, VerticalScrollbarRevealController* self);
    static auto onHideTimeout(gpointer data) -> gboolean;

private:
    GtkScrolledWindow* scrolledWindow = nullptr;
    GtkWidget* vscrollbar = nullptr;
    GtkAdjustment* vadjustment = nullptr;
    std::function<bool()> isZoomInProgress;
    xoj::scroll::VerticalScrollbarRevealState state;
    bool pointerPressed = false;
    std::unordered_set<GdkEventSequence*> activeTouchSequences;
    guint hideTimeoutId = 0;
};
