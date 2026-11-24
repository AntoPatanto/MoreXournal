#include "VerticalScrollbarRevealController.h"

#include <utility>

namespace {

auto monotonicNowMs() -> int64_t { return g_get_monotonic_time() / 1000; }

#if GTK_MAJOR_VERSION == 3
auto getOverlayIndicatorWindow(GtkScrolledWindow* scrolledWindow, GtkWidget* scrollbar) -> GdkWindow* {
    if (!scrolledWindow || !scrollbar) {
        return nullptr;
    }
    GdkWindow* parentWin = gtk_widget_get_parent_window(scrollbar);
    GdkWindow* swWin = gtk_widget_get_window(GTK_WIDGET(scrolledWindow));
    if (parentWin != nullptr && parentWin != swWin) {
        return parentWin;
    }
    return nullptr;
}
#endif

}  // namespace

VerticalScrollbarRevealController::VerticalScrollbarRevealController(GtkScrolledWindow* scrolledWindow,
                                                                     std::function<bool()> isZoomInProgress):
        scrolledWindow(scrolledWindow),
        vscrollbar(gtk_scrolled_window_get_vscrollbar(scrolledWindow)),
        vadjustment(gtk_scrolled_window_get_vadjustment(scrolledWindow)),
        isZoomInProgress(std::move(isZoomInProgress)) {
    if (this->scrolledWindow != nullptr) {
        g_object_add_weak_pointer(G_OBJECT(this->scrolledWindow), reinterpret_cast<gpointer*>(&this->scrolledWindow));
        g_signal_connect(this->scrolledWindow, "destroy", G_CALLBACK(onScrolledWindowDestroy), this);
    }

    if (vscrollbar != nullptr) {
        g_object_add_weak_pointer(G_OBJECT(vscrollbar), reinterpret_cast<gpointer*>(&vscrollbar));
        // Keep gtk_widget_show_all() on ancestors from bypassing scroll-only visibility.
        gtk_widget_set_no_show_all(vscrollbar, TRUE);

        g_signal_connect(vscrollbar, "button-press-event", G_CALLBACK(onScrollbarButtonPress), this);
        g_signal_connect(vscrollbar, "button-release-event", G_CALLBACK(onScrollbarButtonRelease), this);
        g_signal_connect(vscrollbar, "touch-event", G_CALLBACK(onScrollbarTouchEvent), this);
        g_signal_connect(vscrollbar, "grab-notify", G_CALLBACK(onScrollbarGrabNotify), this);
        g_signal_connect(vscrollbar, "hide", G_CALLBACK(onScrollbarHide), this);
    }

    if (vadjustment != nullptr) {
        g_object_add_weak_pointer(G_OBJECT(vadjustment), reinterpret_cast<gpointer*>(&vadjustment));
        g_signal_connect(vadjustment, "changed", G_CALLBACK(onAdjustmentChanged), this);
        // Connect after GTK's internal overlay indicator handler so we can keep the
        // overlay indicator GdkWindow hidden on sub-threshold micro-movements.
        g_signal_connect_after(vadjustment, "value-changed", G_CALLBACK(onAdjustmentValueChanged), this);

        state.onRangeChanged(gtk_adjustment_get_value(vadjustment), gtk_adjustment_get_upper(vadjustment),
                             gtk_adjustment_get_page_size(vadjustment), monotonicNowMs());
    }
}

VerticalScrollbarRevealController::~VerticalScrollbarRevealController() { detach(); }

void VerticalScrollbarRevealController::detach() {
    cancelHideTimer();
    pointerPressed = false;
    activeTouchSequences.clear();

    if (scrolledWindow != nullptr) {
        g_signal_handlers_disconnect_by_data(scrolledWindow, this);
        g_object_remove_weak_pointer(G_OBJECT(scrolledWindow), reinterpret_cast<gpointer*>(&scrolledWindow));
        scrolledWindow = nullptr;
    }
    if (vscrollbar != nullptr) {
        g_signal_handlers_disconnect_by_data(vscrollbar, this);
        g_object_remove_weak_pointer(G_OBJECT(vscrollbar), reinterpret_cast<gpointer*>(&vscrollbar));
        vscrollbar = nullptr;
    }
    if (vadjustment != nullptr) {
        g_signal_handlers_disconnect_by_data(vadjustment, this);
        g_object_remove_weak_pointer(G_OBJECT(vadjustment), reinterpret_cast<gpointer*>(&vadjustment));
        vadjustment = nullptr;
    }
}

void VerticalScrollbarRevealController::configure(bool hiddenByPreference, bool fadeoutDisabled) {
    if (scrolledWindow == nullptr || vscrollbar == nullptr) {
        return;
    }

    pointerPressed = false;
    activeTouchSequences.clear();

    const auto action = state.configure(hiddenByPreference, fadeoutDisabled);
    gtk_scrolled_window_set_overlay_scrolling(scrolledWindow, !fadeoutDisabled);
    applyAction(action);
}

void VerticalScrollbarRevealController::applyAction(const xoj::scroll::VerticalScrollbarRevealAction& action) {
    if (scrolledWindow == nullptr || vscrollbar == nullptr) {
        return;
    }

    if (action.cancelHideTimer) {
        cancelHideTimer();
    }
    if (action.armHideTimer) {
        armHideTimer();
    }

    GtkPolicyType hpolicy = GTK_POLICY_AUTOMATIC;
    GtkPolicyType vpolicy = GTK_POLICY_AUTOMATIC;
    gtk_scrolled_window_get_policy(scrolledWindow, &hpolicy, &vpolicy);

    const GtkPolicyType desiredVPolicy = action.visible ? GTK_POLICY_AUTOMATIC : GTK_POLICY_EXTERNAL;

    if (action.visible) {
        if (vpolicy != desiredVPolicy) {
            gtk_scrolled_window_set_policy(scrolledWindow, hpolicy, desiredVPolicy);
        }
        if (!gtk_widget_get_visible(vscrollbar)) {
            gtk_widget_set_visible(vscrollbar, TRUE);
        }
#if GTK_MAJOR_VERSION == 3
        if (GdkWindow* indicatorWin = getOverlayIndicatorWindow(scrolledWindow, vscrollbar);
            indicatorWin != nullptr && !gdk_window_is_visible(indicatorWin)) {
            gdk_window_show(indicatorWin);
        }
        if (state.getMode() == xoj::scroll::VerticalScrollbarMode::ScrollOnly &&
            gtk_widget_get_opacity(vscrollbar) < 0.99) {
            gtk_widget_set_opacity(vscrollbar, 1.0);
        }
#endif
    } else {
        if (gtk_widget_get_visible(vscrollbar)) {
            gtk_widget_set_visible(vscrollbar, FALSE);
        }
        if (vpolicy != desiredVPolicy) {
            gtk_scrolled_window_set_policy(scrolledWindow, hpolicy, desiredVPolicy);
        }
#if GTK_MAJOR_VERSION == 3
        if (GdkWindow* indicatorWin = getOverlayIndicatorWindow(scrolledWindow, vscrollbar);
            indicatorWin != nullptr && gdk_window_is_visible(indicatorWin)) {
            gdk_window_hide(indicatorWin);
        }
#endif
    }
}

void VerticalScrollbarRevealController::updateInteractionState() {
    const bool interacting = pointerPressed || !activeTouchSequences.empty();
    applyAction(state.setScrollbarInteracting(interacting));
}

void VerticalScrollbarRevealController::clearInteractionState() {
    if (!pointerPressed && activeTouchSequences.empty()) {
        return;
    }
    pointerPressed = false;
    activeTouchSequences.clear();
    applyAction(state.setScrollbarInteracting(false));
}

void VerticalScrollbarRevealController::cancelHideTimer() {
    if (hideTimeoutId != 0) {
        g_source_remove(hideTimeoutId);
        hideTimeoutId = 0;
    }
}

void VerticalScrollbarRevealController::armHideTimer() {
    cancelHideTimer();
    hideTimeoutId = g_timeout_add(xoj::scroll::VerticalScrollbarRevealState::HIDE_DELAY_MS, onHideTimeout, this);
}

void VerticalScrollbarRevealController::onScrolledWindowDestroy(GtkWidget*, VerticalScrollbarRevealController* self) {
    self->detach();
}

void VerticalScrollbarRevealController::onAdjustmentChanged(GtkAdjustment* adjustment,
                                                            VerticalScrollbarRevealController* self) {
    const auto action =
            self->state.onRangeChanged(gtk_adjustment_get_value(adjustment), gtk_adjustment_get_upper(adjustment),
                                       gtk_adjustment_get_page_size(adjustment), monotonicNowMs());
    self->applyAction(action);
}

void VerticalScrollbarRevealController::onAdjustmentValueChanged(GtkAdjustment* adjustment,
                                                                 VerticalScrollbarRevealController* self) {
    const bool zooming = self->isZoomInProgress && self->isZoomInProgress();
    const auto action =
            self->state.onValueChanged(gtk_adjustment_get_value(adjustment), gtk_adjustment_get_upper(adjustment),
                                       gtk_adjustment_get_page_size(adjustment), zooming, monotonicNowMs());
    self->applyAction(action);
}

auto VerticalScrollbarRevealController::onScrollbarButtonPress(GtkWidget*, GdkEventButton*,
                                                               VerticalScrollbarRevealController* self) -> gboolean {
    self->pointerPressed = true;
    self->updateInteractionState();
    return GDK_EVENT_PROPAGATE;
}

auto VerticalScrollbarRevealController::onScrollbarButtonRelease(GtkWidget*, GdkEventButton*,
                                                                 VerticalScrollbarRevealController* self) -> gboolean {
    self->pointerPressed = false;
    self->updateInteractionState();
    return GDK_EVENT_PROPAGATE;
}

auto VerticalScrollbarRevealController::onScrollbarTouchEvent(GtkWidget*, GdkEventTouch* event,
                                                              VerticalScrollbarRevealController* self) -> gboolean {
    switch (event->type) {
        case GDK_TOUCH_BEGIN:
            self->activeTouchSequences.insert(event->sequence);
            self->updateInteractionState();
            break;
        case GDK_TOUCH_END:
        case GDK_TOUCH_CANCEL:
            self->activeTouchSequences.erase(event->sequence);
            self->updateInteractionState();
            break;
        default:
            break;
    }
    return GDK_EVENT_PROPAGATE;
}

void VerticalScrollbarRevealController::onScrollbarGrabNotify(GtkWidget*, gboolean wasGrabbed,
                                                              VerticalScrollbarRevealController* self) {
    if (!wasGrabbed) {
        self->clearInteractionState();
    }
}

void VerticalScrollbarRevealController::onScrollbarHide(GtkWidget*, VerticalScrollbarRevealController* self) {
    self->clearInteractionState();
}

auto VerticalScrollbarRevealController::onHideTimeout(gpointer data) -> gboolean {
    auto* self = static_cast<VerticalScrollbarRevealController*>(data);
    self->hideTimeoutId = 0;
    self->applyAction(self->state.onHideTimeout());
    return G_SOURCE_REMOVE;
}
